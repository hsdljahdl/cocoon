/*
 * health-metrics.h
 * 
 * System metrics collection from procfs/sysfs.
 * Pure functions for reading and parsing system information.
 */

#pragma once

#include "td/utils/Status.h"
#include <string>
#include <map>
#include <cstdint>

namespace cocoon {

// Service metrics
namespace service {
struct Metrics {
  std::string name;
  std::string state;
  std::string sub_state;
  int pid = 0;
  uint32_t restart_count = 0;
  uint64_t memory_bytes = 0;
  uint64_t memory_max = 0;  // MemoryMax/MemoryHigh limit
  uint64_t cpu_usage_nsec = 0;
  int num_tasks = 0;

  // Per-process metrics from /proc/<pid>/
  int open_fds = 0;
  int tcp_connections = 0;
  uint64_t io_read_bytes = 0;
  uint64_t io_write_bytes = 0;
};
}  // namespace service

// System metrics snapshot
struct SystemMetrics {
  // CPU
  double load_1m = 0.0;
  double load_5m = 0.0;
  double load_15m = 0.0;
  int cpu_cores = 0;
  uint64_t cpu_total_ticks = 0;
  uint64_t cpu_idle_ticks = 0;

  // Memory
  uint64_t mem_total = 0;
  uint64_t mem_available = 0;
  uint64_t swap_total = 0;
  uint64_t swap_free = 0;

  // System
  uint64_t uptime_seconds = 0;

  // I/O (cumulative counters since boot)
  std::map<std::string, std::pair<uint64_t, uint64_t>> disk_io;  // device -> (read_bytes, write_bytes)
  std::map<std::string, std::pair<uint64_t, uint64_t>> net_io;   // iface -> (rx_bytes, tx_bytes)
};

namespace metrics {

// Read procfs/sysfs file (these report size=0, so we use direct read)
td::Result<std::string> read_proc_file(td::CSlice path, size_t buffer_size = 65536);

// Parsers (pure functions, modify metrics struct)
td::Status parse_loadavg(SystemMetrics& m);
td::Status parse_meminfo(SystemMetrics& m);
td::Status parse_uptime(SystemMetrics& m);
td::Status parse_cpuinfo(SystemMetrics& m);
td::Status parse_cpu_stat(SystemMetrics& m);
td::Status parse_net_dev(SystemMetrics& m);
td::Status parse_diskstats(SystemMetrics& m);

// Collect all metrics
SystemMetrics collect_all();

// Cache for static values (read once)
int get_cpu_cores();  // Cached CPU core count

// Per-process metrics (from /proc/<pid>/)
int count_open_fds(int pid);
int count_tcp_connections(int pid);
std::pair<uint64_t, uint64_t> get_process_io(int pid);  // Returns (read_bytes, write_bytes)

// Service metrics (from systemctl + cgroup)
service::Metrics parse_service_metrics(const std::string& service_name, const std::string& systemctl_output);

// Get CPU usage from cgroup (includes all descendants)
uint64_t get_cgroup_cpu_usage(const std::string& service_name);

// Get I/O stats from cgroup (includes all descendants)
std::pair<uint64_t, uint64_t> get_cgroup_io(const std::string& service_name);

// Get memory usage from cgroup (includes all descendants)
uint64_t get_cgroup_memory(const std::string& service_name);

// Get FD and socket counts from cgroup (includes all descendants)
std::pair<int, int> count_cgroup_fds_and_sockets(const std::string& service_name);

}  // namespace metrics
}  // namespace cocoon
