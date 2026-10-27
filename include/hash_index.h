#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
namespace flashforge {
struct ChunkRecord { std::filesystem::path physical_path; std::uint64_t size{}; std::uint64_t reference_count{}; };
class HashIndex {
 public:
  bool contains(const std::string& hash) const;
  const ChunkRecord* find(const std::string& hash) const;
  ChunkRecord* find(const std::string& hash);
  void set(const std::string& hash, ChunkRecord record);
  void increment(const std::string& hash, std::uint64_t amount = 1);
  void decrement(const std::string& hash, std::uint64_t amount = 1);
  void erase(const std::string& hash);
  void clear();
  const auto& entries() const noexcept { return entries_; }
 private: std::unordered_map<std::string, ChunkRecord> entries_;
};
}

