#include "WorkerUplinkMonitor.h"
#include "WorkerRunner.h"
#include "http/http.h"
#include "td/actor/actor.h"
#include "td/utils/Time.h"
#include "runners/helpers/HttpSender.hpp"
#include "td/utils/buffer.h"

namespace cocoon {

void WorkerUplinkMonitor::send_request() {
  auto req = ton::http::HttpRequest::create("GET", "/v1/models", "HTTP/1.0").move_as_ok();
  req->complete_parse_header().ensure();
  auto payload = req->create_empty_payload().move_as_ok();
  payload->complete_parse();

  td::actor::send_closure(
      runner_, &WorkerRunner::send_http_request, std::move(req), std::move(payload), td::Timestamp::in(10.0),
      [self_id = actor_id(this)](
          td::Result<std::pair<std::unique_ptr<ton::http::HttpResponse>, std::shared_ptr<ton::http::HttpPayload>>> R) {
        if (R.is_error()) {
          td::actor::send_closure(self_id, &WorkerUplinkMonitor::requests_completed, false);
        } else {
          td::actor::send_closure(self_id, &WorkerUplinkMonitor::got_http_answer, R.move_as_ok());
        }
      });
}

void WorkerUplinkMonitor::got_http_answer(
    std::pair<std::unique_ptr<ton::http::HttpResponse>, std::shared_ptr<ton::http::HttpPayload>> res) {
  if (res.first->code() != ton::http::HttpStatusCode::status_ok) {
    requests_completed(false);
    return;
  }

  td::actor::create_actor<HttpPayloadReceiver>(
      "payloadreceiver", std::move(res.second),
      [self_id = actor_id(this)](td::Result<td::BufferSlice> R) {
        if (R.is_error()) {
          td::actor::send_closure(self_id, &WorkerUplinkMonitor::requests_completed, false);
        } else {
          td::actor::send_closure(self_id, &WorkerUplinkMonitor::requests_completed, true);
        }
      },
      td::Timestamp::in(30.0))
      .release();
}

void WorkerUplinkMonitor::requests_completed(bool is_success) {
  if (is_success != cur_state_) {
    cur_state_ = is_success;
    td::actor::send_closure(runner_, &WorkerRunner::set_uplink_is_ok, is_success);
  }
  next_check_at_ = td::Timestamp::in(td::Random::fast(1.0, 2.0));
  alarm_timestamp().relax(next_check_at_);
}

}  // namespace cocoon
