#pragma once

#include "td/actor/ActorId.h"
#include "td/actor/common.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "ton/http/http.h"

namespace cocoon {

class WorkerRunner;

class WorkerUplinkMonitor : public td::actor::Actor {
 public:
  WorkerUplinkMonitor(td::actor::ActorId<WorkerRunner> runner) : runner_(runner) {
  }
  void alarm() override {
    td::actor::Actor::alarm();
    if (next_check_at_.is_in_past()) {
      next_check_at_ = td::Timestamp::never();
      send_request();
    }
    alarm_timestamp().relax(next_check_at_);
  }
  void start_up() override {
    send_request();
  }

  void send_request();
  void got_http_answer(
      std::pair<std::unique_ptr<ton::http::HttpResponse>, std::shared_ptr<ton::http::HttpPayload>> res);
  void requests_completed(bool is_success);

 private:
  td::actor::ActorId<WorkerRunner> runner_;
  td::Timestamp next_check_at_;
  bool cur_state_{false};
};

}  // namespace cocoon
