#pragma once
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>
namespace flashforge {
struct WALTiming {
  std::uint64_t write_us{}, fsync_us{}, fault_delay_us{};
};
class WriteAheadLog {
 public:
  explicit WriteAheadLog(std::filesystem::path path);
  std::uint64_t begin(const std::vector<std::uint8_t>& snapshot, WALTiming* timing = nullptr);
  void commit(std::uint64_t txid, WALTiming* timing = nullptr);
  std::optional<std::vector<std::uint8_t>> latestCommittedSnapshot();
  void checkpoint(WALTiming* timing = nullptr);
 private:
  void append(std::uint8_t type, std::uint64_t txid, const std::vector<std::uint8_t>& payload, WALTiming* timing);
  std::filesystem::path path_; std::mutex mutex_; std::uint64_t next_txid_{1};
};
}

