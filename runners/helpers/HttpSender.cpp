#include "HttpSender.hpp"

namespace cocoon {

void HttpPayloadReceiver::start_up() {
  class Cb : public ton::http::HttpPayload::Callback {
   public:
    Cb(td::actor::ActorId<HttpPayloadReceiver> id, size_t watermark) : self_id_(id), watermark_(watermark) {
    }
    void run(size_t ready_bytes) override {
      td::actor::send_closure(self_id_, &HttpPayloadReceiver::try_answer_query, false);
    }
    void completed() override {
      td::actor::send_closure(self_id_, &HttpPayloadReceiver::try_answer_query, false);
    }

   private:
    bool reached_ = false;
    td::actor::ActorId<HttpPayloadReceiver> self_id_;
    size_t watermark_;
  };

  payload_->add_callback(std::make_unique<Cb>(actor_id(this), ton::http::HttpRequest::low_watermark()));

  alarm_timestamp() = timeout_;

  try_answer_query(false);
}

void HttpPayloadCbReceiver::start_up() {
  class Cb : public ton::http::HttpPayload::Callback {
   public:
    Cb(td::actor::ActorId<HttpPayloadCbReceiver> id, size_t watermark) : self_id_(id), watermark_(watermark) {
    }
    void run(size_t ready_bytes) override {
      td::actor::send_closure(self_id_, &HttpPayloadCbReceiver::try_answer_query, false);
    }
    void completed() override {
      td::actor::send_closure(self_id_, &HttpPayloadCbReceiver::try_answer_query, false);
    }

   private:
    bool reached_ = false;
    td::actor::ActorId<HttpPayloadCbReceiver> self_id_;
    size_t watermark_;
  };

  payload_->add_callback(std::make_unique<Cb>(actor_id(this), ton::http::HttpRequest::low_watermark()));

  alarm_timestamp() = timeout_;

  try_answer_query(false);
}

}  // namespace cocoon
