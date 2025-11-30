#include "TonlibWrapper.h"

#include "auto/tl/tonlib_api.hpp"
#include "tonlib/tonlib/TonlibCallback.h"
#include "tonlib/tonlib/TonlibClient.h"
#include "td/actor/actor.h"
#include "td/utils/filesystem.h"
#include "td/utils/date.h"
#include "td/utils/overloaded.h"
#include "td/utils/port/path.h"
#include <map>

namespace cocoon {

class TonlibClientWrapperActor : public td::actor::Actor {
 public:
  void start_up() override;
  void on_result(td::int64 id, td::Result<ton::tl_object_ptr<ton::tonlib_api::Object>> result);
  void request(ton::tl_object_ptr<ton::tonlib_api::Function> req,
               td::actor::StartedTask<ton::tl_object_ptr<ton::tonlib_api::Object>>::ExternalPromise promise);

 private:
  td::actor::ActorOwn<tonlib::TonlibClient> tonlib_client_;
  td::int64 last_request_id_{0};
  std::map<td::int64, td::actor::StartedTask<ton::tl_object_ptr<ton::tonlib_api::Object>>::ExternalPromise>
      pending_requests_;
};

struct TonlibClientImpl {
  td::actor::ActorOwn<TonlibClientWrapperActor> actor_;
};

void TonlibClientWrapperActor::start_up() {
  class Cb : public tonlib::TonlibCallback {
   public:
    Cb(td::actor::ActorId<TonlibClientWrapperActor> self) : self_(self) {
    }
    void on_result(std::uint64_t id, ton::tonlib_api::object_ptr<ton::tonlib_api::Object> result) override {
      CHECK(result->get_id() != ton::tonlib_api::error::ID);
      td::actor::send_closure(self_, &TonlibClientWrapperActor::on_result, id, std::move(result));
    }
    void on_error(std::uint64_t id, ton::tonlib_api::object_ptr<ton::tonlib_api::error> error) override {
      td::actor::send_closure(self_, &TonlibClientWrapperActor::on_result, id,
                              td::Status::Error(error->code_, error->message_));
    }

   private:
    td::actor::ActorId<TonlibClientWrapperActor> self_;
  };

  tonlib_client_ = td::actor::create_actor<tonlib::TonlibClient>("tonlib", td::make_unique<Cb>(actor_id(this)));
}

void TonlibClientWrapperActor::on_result(td::int64 id, td::Result<ton::tl_object_ptr<ton::tonlib_api::Object>> result) {
  if (id == 0 && result.is_ok()) {
    auto update = ton::move_tl_object_as<ton::tonlib_api::Update>(result.move_as_ok());
    CHECK(update);
    tonlib_api::downcast_call(
        *update,
        td::overloaded([&](tonlib_api::updateSendLiteServerQuery &) {},
                       [&](tonlib_api::updateSyncState &u) {
                         ton::tonlib_api::downcast_call(
                             *u.sync_state_,
                             td::overloaded([&](tonlib_api::syncStateDone &s) { LOG(INFO) << "TonLib is synced"; },
                                            [&](tonlib_api::syncStateInProgress &s) {
                                              LOG(INFO)
                                                  << "TonLib is syncing: " << s.current_seqno_ << "/" << s.to_seqno_;
                                            }));
                       }));
  }
  auto it = pending_requests_.find(id);
  if (it != pending_requests_.end()) {
    it->second.set_result(std::move(result));
    pending_requests_.erase(it);
  }
}

void TonlibClientWrapperActor::request(
    ton::tl_object_ptr<ton::tonlib_api::Function> req,
    td::actor::StartedTask<ton::tl_object_ptr<ton::tonlib_api::Object>>::ExternalPromise promise) {
  auto id = ++last_request_id_;
  pending_requests_[id] = std::move(promise);
  td::actor::send_closure(tonlib_client_, &tonlib::TonlibClient::request, id, std::move(req));
}

TonlibWrapper::TonlibWrapper() {
}

void TonlibWrapper::init_actor() {
  if (!impl_) {
    impl_ = std::make_shared<TonlibClientImpl>();
    impl_->actor_ = td::actor::create_actor<TonlibClientWrapperActor>("TonlibClientWrapper");
  }
}
td::StringBuilder &operator<<(td::StringBuilder &sb, const TonlibWrapper::SyncInfo &info) {
  sb << "{blockchain_ts=" << info.last_synced_ts << " ("
     << date::format("%F %T", std::chrono::system_clock::time_point{std::chrono::seconds{info.last_synced_ts}})
     << "), blockchain_seqno=" << info.last_synced_seqno << "}";
  return sb;
}

td::actor::StartedTask<ton::tl_object_ptr<ton::tonlib_api::Object>> TonlibWrapper::request_raw(
    ton::tl_object_ptr<ton::tonlib_api::Function> req) {
  CHECK(impl_);
  auto [task, promise] = td::actor::StartedTask<ton::tl_object_ptr<ton::tonlib_api::Object>>::make_bridge();
  td::actor::send_closure(impl_->actor_, &TonlibClientWrapperActor::request, std::move(req), std::move(promise));
  return std::move(task);
}

td::actor::Task<td::Unit> TonlibWrapper::initialize(std::string ton_config_filename, bool is_testnet) {
  init_actor();

  LOG(INFO) << "Initializing tonlib...";
  auto ton_config_data = co_await td::read_file_str(ton_config_filename);
  auto tonlib_config =
      ton::create_tl_object<ton::tonlib_api::config>(ton_config_data, is_testnet ? "testnet" : "mainnet", false, false);
  td::mkdir("/tmp/tonlib.cache/", 0700).ignore();
  auto tonlib_options = ton::create_tl_object<ton::tonlib_api::options>(
      std::move(tonlib_config), ton::create_tl_object<ton::tonlib_api::keyStoreTypeDirectory>("/tmp/tonlib.cache/"));

  co_await request<ton::tonlib_api::init>(std::move(tonlib_options));
  LOG(INFO) << "Tonlib initialized";
  co_return td::Unit();
}
td::actor::Task<TonlibWrapper::SyncInfo> TonlibWrapper::sync_once() {
  auto block = co_await request<tonlib_api::sync>();
  LOG(DEBUG) << "tonlib: synced up to " << block->seqno_;
  auto seqno = block->seqno_;
  auto block_header = co_await request<tonlib_api::blocks_getBlockHeader>(std::move(block));
  auto blockchain_time = td::narrow_cast<td::int32>(block_header->gen_utime_);
  auto local_time = static_cast<td::int32>(td::Clocks::system());
  auto time_diff = std::abs(blockchain_time - local_time);
  if (time_diff > 30) {
    co_return td::Status::Error(PSLICE() << "Time is not synced: " << time_diff
                                         << "s blockchain_time=" << blockchain_time << " local_time=" << local_time);
  }
  co_return SyncInfo{.last_synced_seqno = static_cast<td::uint32>(seqno),
                     .last_synced_ts = static_cast<td::uint32>(blockchain_time)};
}
td::actor::Task<TonlibWrapper::SyncInfo> TonlibWrapper::sync() {
  while (true) {
    LOG(INFO) << "Syncing...";
    auto r = co_await sync_once().wrap();
    if (r.is_ok()) {
      LOG(INFO) << "TONLIB SYNCED!";
      co_return r.move_as_ok();
    }
    LOG(ERROR) << "Sync error: " << r.error();
  }
}

}  // namespace cocoon
