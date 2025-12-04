#pragma once

#include "td/actor/ActorId.h"
#include "td/actor/actor.h"
#include "td/utils/Time.h"
#include "td/utils/buffer.h"
#include "ton/http/http.h"
#include <memory>

namespace cocoon {

class HttpPayloadReceiver : public td::actor::Actor {
 public:
  HttpPayloadReceiver(std::shared_ptr<ton::http::HttpPayload> payload, td::Promise<td::BufferSlice> promise,
                      td::Timestamp timeout)
      : payload_(std::move(payload)), promise_(std::move(promise)), timeout_(timeout) {
  }

  void start_up() override;

  void try_answer_query(bool from_timer = false) {
    auto r = payload_->store_tl(std::numeric_limits<size_t>::max());
    answer_.push_back(std::move(r->data_));
    if (r->last_) {
      answer_query();
    } else {
      return;
    }
  }

  void alarm() override {
    stop();
  }

  void answer_query() {
    size_t size = 0;
    for (auto &a : answer_) {
      size += a.size();
    }
    td::BufferSlice buf(size);
    auto M = buf.as_slice();
    for (auto &a : answer_) {
      M.copy_from(a.as_slice());
      M.remove_prefix(a.size());
    }
    CHECK(!M.size());
    promise_.set_value(std::move(buf));
    stop();
  }

  void abort_query(td::Status error) {
    LOG(INFO) << "aborting http payload downloading: " << error;
    stop();
  }

 private:
  static constexpr size_t watermark() {
    return (1 << 21) - (1 << 11);
  }

  std::shared_ptr<ton::http::HttpPayload> payload_;
  std::vector<td::BufferSlice> answer_;

  td::Promise<td::BufferSlice> promise_;
  td::Timestamp timeout_;
};

class HttpPayloadCbReceiver : public td::actor::Actor {
 public:
  class Cb {
   public:
    virtual ~Cb() = default;
    virtual void data_chunk(td::BufferSlice buffer, bool is_finished) = 0;
    virtual void error(td::Status error) = 0;
  };
  HttpPayloadCbReceiver(std::shared_ptr<ton::http::HttpPayload> payload, std::unique_ptr<Cb> callback,
                        td::Timestamp timeout)
      : payload_(std::move(payload)), callback_(std::move(callback)), timeout_(timeout) {
  }

  void start_up() override;

  void try_answer_query(bool from_timer = false) {
    auto r = payload_->store_tl(std::numeric_limits<size_t>::max());
    if (r->data_.size() > 0 || r->last_) {
      callback_->data_chunk(std::move(r->data_), r->last_);
    }
    if (r->last_) {
      stop();
    } else {
      return;
    }
  }

  void alarm() override {
    stop();
  }
  void abort_query(td::Status error) {
    LOG(INFO) << "aborting http payload downloading: " << error;
    callback_->error(std::move(error));
    stop();
  }

 private:
  static constexpr size_t watermark() {
    return (1 << 21) - (1 << 11);
  }

  std::shared_ptr<ton::http::HttpPayload> payload_;
  std::unique_ptr<Cb> callback_;
  td::Timestamp timeout_;
};

}  // namespace cocoon
