/*
 * health-stats.cpp
 * 
 * Implementation of rate tracking.
 */

#include "health-stats.h"
#include "td/utils/logging.h"
#include <algorithm>

namespace cocoon {

// ============================================================================
// RateTracker Implementation
// ============================================================================

void RateTracker::add(uint64_t value) {
  auto now = td::Time::now();
  history_[write_idx_ % HISTORY] = {value, now};
  write_idx_++;
  count_ = std::min(count_ + 1, HISTORY);
}

double RateTracker::get_rate(double seconds) const {
  if (count_ == 0)
    return 0.0;

  const auto& latest = history_[(write_idx_ - 1) % HISTORY];
  const Sample* target = nullptr;

  // Search from newest to oldest, find first sample old enough
  for (size_t i = 1; i <= count_; i++) {
    const auto& s = history_[(write_idx_ - i) % HISTORY];
    double age = latest.timestamp - s.timestamp;

    if (age >= seconds - 0.5) {  // Found sample old enough
      target = &s;
      break;
    }
  }

  // If not found (all samples too recent), use oldest available
  if (!target) {
    target = &history_[(write_idx_ - count_) % HISTORY];
  }

  double time_delta = latest.timestamp - target->timestamp;
  uint64_t value_delta = latest.value >= target->value ? latest.value - target->value : 0;

  return safe_divide(value_delta, time_delta);
}

std::array<double, 3> RateTracker::get_rates() const {
  if (count_ == 0) {
    return {0.0, 0.0, 0.0};
  }

  auto rate_10s = get_rate(WINDOW_10S);
  auto rate_1m = get_rate(WINDOW_1M);
  auto rate_5m = get_rate(WINDOW_5M);

  return {rate_10s, rate_1m, rate_5m};
}

double RateTracker::safe_divide(uint64_t value_delta, double time_delta) {
  return time_delta >= 0.1 && value_delta > 0 ? static_cast<double>(value_delta) / time_delta : 0.0;
}

// ============================================================================
// IoRateTracker Implementation
// ============================================================================

void IoRateTracker::update(uint64_t read_bytes, uint64_t write_bytes) {
  read_.add(read_bytes);
  write_.add(write_bytes);
}

std::array<IoRateTracker::Rates, 3> IoRateTracker::get_rates() const {
  auto read_rates = read_.get_rates();
  auto write_rates = write_.get_rates();

  return {
      Rates{read_rates[0], write_rates[0]},  // 10s
      Rates{read_rates[1], write_rates[1]},  // 10m
      Rates{read_rates[2], write_rates[2]}   // total
  };
}

// ============================================================================
// CpuRateTracker Implementation
// ============================================================================

void CpuRateTracker::update(uint64_t total_ticks, uint64_t idle_ticks) {
  total_ticks_.add(total_ticks);
  idle_ticks_.add(idle_ticks);
}

std::array<double, 3> CpuRateTracker::get_utilization() const {
  auto total = total_ticks_.get_rates();
  auto idle = idle_ticks_.get_rates();

  auto calc = [](double total_rate, double idle_rate) {
    return total_rate > 0 ? std::clamp((total_rate - idle_rate) / total_rate * 100.0, 0.0, 100.0) : 0.0;
  };

  return {calc(total[0], idle[0]), calc(total[1], idle[1]), calc(total[2], idle[2])};
}

// ============================================================================
// ServiceRateTracker Implementation
// ============================================================================

void ServiceRateTracker::update(uint64_t cpu_nsec, uint64_t io_read, uint64_t io_write, int pid) {
  if (pid != last_pid_ && last_pid_ != 0) {
    // Service restarted - reset all trackers
    *this = ServiceRateTracker();
  }
  last_pid_ = pid;
  cpu_nsec_.add(cpu_nsec);
  io_.update(io_read, io_write);
}

std::array<double, 3> ServiceRateTracker::get_cpu_percent() const {
  auto rates = cpu_nsec_.get_rates();  // nsec/sec
  // Convert to percentage: (nsec/sec) / 1e9 * 100 = rate / 1e7
  return {(rates[0] / 1e7), (rates[1] / 1e7), (rates[2] / 1e7)};
}

std::array<IoRateTracker::Rates, 3> ServiceRateTracker::get_io_rates() const {
  return io_.get_rates();
}

// ============================================================================
// StatsCollector Implementation
// ============================================================================

void StatsCollector::update_cpu(uint64_t total_ticks, uint64_t idle_ticks) {
  cpu_.update(total_ticks, idle_ticks);
}

void StatsCollector::update_disk(const std::string& device, uint64_t read_bytes, uint64_t write_bytes) {
  disk_[device].update(read_bytes, write_bytes);
}

void StatsCollector::update_network(const std::string& iface, uint64_t rx_bytes, uint64_t tx_bytes) {
  net_[iface].update(rx_bytes, tx_bytes);
}

void StatsCollector::update_service(const std::string& service, uint64_t cpu_nsec, uint64_t io_read, uint64_t io_write,
                                    int pid) {
  services_[service].update(cpu_nsec, io_read, io_write, pid);
}

std::array<double, 3> StatsCollector::get_cpu_utilization() const {
  return cpu_.get_utilization();
}

std::array<IoRateTracker::Rates, 3> StatsCollector::get_disk_rates(const std::string& device) const {
  auto it = disk_.find(device);
  return it != disk_.end() ? it->second.get_rates() : std::array<IoRateTracker::Rates, 3>{};
}

std::array<IoRateTracker::Rates, 3> StatsCollector::get_net_rates(const std::string& iface) const {
  auto it = net_.find(iface);
  return it != net_.end() ? it->second.get_rates() : std::array<IoRateTracker::Rates, 3>{};
}

bool StatsCollector::has_disk(const std::string& device) const {
  return disk_.count(device) > 0;
}

bool StatsCollector::has_network(const std::string& iface) const {
  return net_.count(iface) > 0;
}

std::array<double, 3> StatsCollector::get_service_cpu(const std::string& service) const {
  auto it = services_.find(service);
  return it != services_.end() ? it->second.get_cpu_percent() : std::array<double, 3>{};
}

std::array<IoRateTracker::Rates, 3> StatsCollector::get_service_io(const std::string& service) const {
  auto it = services_.find(service);
  return it != services_.end() ? it->second.get_io_rates() : std::array<IoRateTracker::Rates, 3>{};
}

bool StatsCollector::has_service(const std::string& service) const {
  return services_.count(service) > 0;
}

}  // namespace cocoon
