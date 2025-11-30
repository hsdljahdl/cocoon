#pragma once

#include "SimpleJsonSerializer.hpp"
#include "td/utils/port/Clocks.h"
#include "td/utils/Status.h"

#include <cmath>

namespace cocoon {

class AmortCounter {
 public:
  static double cur_time() {
    return td::Clocks::monotonic();
  }
  AmortCounter(double period) : value_(0), ts_(cur_time()), inv_period_(1.0 / period) {
  }

  void relax() {
    auto t = cur_time();
    value_ *= exp(-(t - ts_) * inv_period_);
    ts_ = t;
  }

  AmortCounter &operator+=(double value) {
    relax();
    value_ += value;
    return *this;
  }

  double operator()() {
    relax();
    return value_;
  }

 private:
  double value_;
  double ts_;
  double inv_period_;
};

struct AmortCounterList {
  double value;
  AmortCounter value_1s{1.0};
  AmortCounter value_10s{10.0};
  AmortCounter value_1m{60.0};
  AmortCounter value_10m{600.0};
  AmortCounter value_1h{3600.0};
  AmortCounter value_1d{86400.0};
  std::mutex mutex_;

  AmortCounterList &operator+=(double incr_value) {
    std::lock_guard lock(mutex_);
    value += incr_value;
    value_1s += incr_value;
    value_10s += incr_value;
    value_1m += incr_value;
    value_10m += incr_value;
    value_1h += incr_value;
    value_1d += incr_value;
    return *this;
  }
  AmortCounterList &operator++(int) {
    return *this += 1;
  }

  static const std::string &header() {
    static const std::string r = "<td>total</td><td>1s</td><td>10s</td><td>1m</td><td>10m</td><td>1h</td><td>1d</td>";
    return r;
  }

  std::string to_html_row() {
    std::lock_guard lock(mutex_);
    return PSTRING() << "<td>" << value << "</td>" << "<td>" << value_1s() << "</td>" << "<td>" << value_10s()
                     << "</td>"
                     << "<td>" << value_1m() << "</td>" << "<td>" << value_10m() << "</td>" << "<td>" << value_1h()
                     << "</td>" << "<td>" << value_1d() << "</td>";
  }

  void to_jb(SimpleJsonSerializer &jb, td::Slice name) {
    std::lock_guard lock(mutex_);
    jb.start_array(name);
    jb.add_element(value);
    jb.add_element(value_1s());
    jb.add_element(value_10s());
    jb.add_element(value_1m());
    jb.add_element(value_10m());
    jb.add_element(value_1h());
    jb.add_element(value_1d());
    jb.stop_array();
  }
};

}  // namespace cocoon
