#pragma once

#include "auto/tl/tonlib_api.h"
#include "td/actor/coro_task.h"
#include <memory>

namespace cocoon {

struct TonlibClientImpl;

class TonlibWrapper {
 public:
  TonlibWrapper();
  td::actor::Task<td::Unit> initialize(std::string ton_config_filename, bool is_testnet);
  struct SyncInfo {
    td::uint32 last_synced_seqno;
    td::uint32 last_synced_ts;
  };
  td::actor::Task<SyncInfo> sync_once();
  td::actor::Task<SyncInfo> sync();

  template <typename T, typename... Args>
  auto request(Args &&...args) -> td::actor::Task<typename T::ReturnType> {
    auto res = co_await request_raw(ton::create_tl_object<T>(std::forward<Args>(args)...));
    using RetType = typename T::ReturnType::element_type;
    co_return ton::move_tl_object_as<RetType>(std::move(res));
  }
  td::actor::StartedTask<ton::tl_object_ptr<ton::tonlib_api::Object>> request_raw(
      ton::tl_object_ptr<ton::tonlib_api::Function> req);

 private:
  void init_actor();
  std::shared_ptr<TonlibClientImpl> impl_;
};
td::StringBuilder &operator<<(td::StringBuilder &sb, const TonlibWrapper::SyncInfo &info);

}  // namespace cocoon
