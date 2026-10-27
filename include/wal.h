#pragma once
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>
namespace flashforge {
class WriteAheadLog {
 public:
  explicit WriteAheadLog(std::filesystem::path path);
  std::uint64_t begin(const std::vector<std::uint8_t>& snapshot);
  void commit(std::uint64_t txid);
  std::optional<std::vector<std::uint8_t>> latestCommittedSnapshot();
  void checkpoint();
 private:
  void append(std::uint8_t type, std::uint64_t txid, const std::vector<std::uint8_t>& payload);
  std::filesystem::path path_; std::mutex mutex_; std::uint64_t next_txid_{1};
};
}

