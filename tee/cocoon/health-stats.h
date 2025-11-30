/*
 * health-stats.h
 * 
 * Simple history-based rate tracking for health monitoring.
 * Stores timestamped samples and calculates rates over different time windows.
 */

#pragma once

#include "td/utils/Time.h"
#include <array>
#include <map>
#include <string>
#include <algorithm>

namespace cocoon {

// Simple rate tracker using circular buffer with exact timestamps
class RateTracker {
 public:
  struct Sample {
    uint64_t value = 0;
    double timestamp = 0.0;
  };

  static constexpr size_t HISTORY = 600;  // 10 minutes at 1s intervals
  static constexpr double WINDOW_10S = 10.0;
  static constexpr double WINDOW_1M = 60.0;
  static constexpr double WINDOW_5M = 300.0;

  RateTracker() = default;

  void add(uint64_t value);
  double get_rate(double seconds) const;
  std::array<double, 3> get_rates() const;

 private:
  static double safe_divide(uint64_t value_delta, double time_delta);

  std::array<Sample, HISTORY> history_;
  size_t write_idx_ = 0;
  size_t count_ = 0;
};

// I/O rate tracker (tracks read and write separately)
struct IoRateTracker {
  struct Rates {
    double read = 0.0;
    double write = 0.0;
  };

  void update(uint64_t read_bytes, uint64_t write_bytes);
  std::array<Rates, 3> get_rates() const;

 private:
  RateTracker read_;
  RateTracker write_;
};

// CPU utilization tracker
struct CpuRateTracker {
  void update(uint64_t total_ticks, uint64_t idle_ticks);
  std::array<double, 3> get_utilization() const;

 private:
  RateTracker total_ticks_;
  RateTracker idle_ticks_;
};

// Per-service rate tracker (handles PID changes by resetting on restart)
struct ServiceRateTracker {
  void update(uint64_t cpu_nsec, uint64_t io_read, uint64_t io_write, int pid);
  std::array<double, 3> get_cpu_percent() const;
  std::array<IoRateTracker::Rates, 3> get_io_rates() const;

 private:
  RateTracker cpu_nsec_;
  IoRateTracker io_;
  int last_pid_ = 0;
};

// Stats collector (owns all trackers)
class StatsCollector {
 public:
  void update_cpu(uint64_t total_ticks, uint64_t idle_ticks);
  void update_disk(const std::string& device, uint64_t read_bytes, uint64_t write_bytes);
  void update_network(const std::string& iface, uint64_t rx_bytes, uint64_t tx_bytes);
  void update_service(const std::string& service, uint64_t cpu_nsec, uint64_t io_read, uint64_t io_write, int pid);

  std::array<double, 3> get_cpu_utilization() const;
  std::array<IoRateTracker::Rates, 3> get_disk_rates(const std::string& device) const;
  std::array<IoRateTracker::Rates, 3> get_net_rates(const std::string& iface) const;
  std::array<double, 3> get_service_cpu(const std::string& service) const;
  std::array<IoRateTracker::Rates, 3> get_service_io(const std::string& service) const;

  bool has_disk(const std::string& device) const;
  bool has_network(const std::string& iface) const;
  bool has_service(const std::string& service) const;

 private:
  CpuRateTracker cpu_;
  std::map<std::string, IoRateTracker> disk_;
  std::map<std::string, IoRateTracker> net_;
  std::map<std::string, ServiceRateTracker> services_;
};

}  // namespace cocoon
