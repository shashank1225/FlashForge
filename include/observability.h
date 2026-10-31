#pragma once
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace flashforge {

struct OperationSample {
  std::uint64_t timestamp_ms{};
  std::string operation;
  bool success{};
  std::uint64_t latency_us{}, bytes{}, hash_us{}, durability_us{};
  std::uint64_t wal_write_us{}, wal_fsync_us{}, metadata_write_us{}, metadata_fsync_us{};
  std::uint64_t metadata_rename_us{}, directory_fsync_us{}, fault_delay_us{};
  std::uint64_t dedup_hits{}, dedup_misses{}, checksum_failures{}, gc_reclaimed{};
};

struct OperationSummary {
  std::string operation;
  std::uint64_t count{}, failures{}, bytes{}, p50_us{}, p95_us{}, p99_us{};
  std::uint64_t hash_us{}, durability_us{}, dedup_hits{}, dedup_misses{};
  std::uint64_t wal_write_us{}, wal_fsync_us{}, metadata_write_us{}, metadata_fsync_us{};
  std::uint64_t metadata_rename_us{}, directory_fsync_us{}, fault_delay_us{};
};

struct SystemSignals {
  bool linux_signals_available{};
  std::uint64_t resident_bytes{}, read_bytes{}, write_bytes{}, open_fds{};
  double io_pressure_pct{-1.0}, memory_pressure_pct{-1.0};
  std::uint64_t filesystem_free_bytes{}, filesystem_capacity_bytes{};
};

struct ObservabilityReport {
  std::vector<OperationSummary> operations;
  SystemSignals system;
  std::uint64_t checksum_failures{}, gc_reclaimed{};
  std::uint64_t telemetry_samples_dropped{};
  double dedup_hit_ratio{};
};

struct EngineeringDecision {
  std::string severity, diagnosis, evidence, recommendation;
  double confidence{};
};

class MetricsStore {
 public:
  MetricsStore(std::filesystem::path root, std::uint64_t retention_samples = 10000);
  ~MetricsStore();
  MetricsStore(const MetricsStore&) = delete;
  MetricsStore& operator=(const MetricsStore&) = delete;
  void record(const OperationSample& sample) noexcept;
  std::vector<OperationSample> load() const;
  ObservabilityReport report() const;
  std::vector<EngineeringDecision> diagnose() const;
  std::string json() const;
  std::string prometheus() const;

 private:
  void writerLoop() noexcept;
  void writeBatch(const std::vector<OperationSample>& batch) noexcept;
  void compactIfNeeded() noexcept;
  std::filesystem::path root_, directory_, samples_path_;
  std::uint64_t retention_samples_;
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  std::deque<OperationSample> queue_, in_flight_;
  std::thread writer_;
  std::atomic<std::uint64_t> dropped_{0};
  bool stopping_{false}, enabled_{true};
  static constexpr std::size_t queue_capacity_ = 1024;
};

class OperationTimer {
 public:
  OperationTimer(MetricsStore& store, std::string operation);
  ~OperationTimer();
  OperationTimer(const OperationTimer&) = delete;
  OperationTimer& operator=(const OperationTimer&) = delete;
  void bytes(std::uint64_t value) noexcept { sample_.bytes = value; }
  void hashTime(std::uint64_t value) noexcept { sample_.hash_us = value; }
  void durabilityTime(std::uint64_t value) noexcept { sample_.durability_us = value; }
  void durabilityBreakdown(std::uint64_t wal_write, std::uint64_t wal_fsync,
      std::uint64_t metadata_write, std::uint64_t metadata_fsync,
      std::uint64_t metadata_rename, std::uint64_t directory_fsync,
      std::uint64_t fault_delay) noexcept {
    sample_.wal_write_us=wal_write; sample_.wal_fsync_us=wal_fsync;
    sample_.metadata_write_us=metadata_write; sample_.metadata_fsync_us=metadata_fsync;
    sample_.metadata_rename_us=metadata_rename; sample_.directory_fsync_us=directory_fsync;
    sample_.fault_delay_us=fault_delay;
  }
  void dedup(std::uint64_t hits, std::uint64_t misses) noexcept { sample_.dedup_hits = hits; sample_.dedup_misses = misses; }
  void checksumFailure() noexcept { ++sample_.checksum_failures; }
  void reclaimed(std::uint64_t value) noexcept { sample_.gc_reclaimed = value; }
  void success() noexcept;

 private:
  void finish(bool successful) noexcept;
  MetricsStore* store_;
  OperationSample sample_;
  std::chrono::steady_clock::time_point started_;
  bool finished_{false};
};

}
