#pragma once

#include "runners/helpers/AmortCounter.h"

namespace cocoon {

struct ClientStats {
  AmortCounterList requests_received;
  AmortCounterList requests_failed;
  AmortCounterList requests_success;
  AmortCounterList total_requests_time;
  AmortCounterList request_bytes_received;
  AmortCounterList answer_bytes_sent;

  const std::string &header() {
    return AmortCounterList::header();
  }
};

}  // namespace cocoon
