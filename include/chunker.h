#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace flashforge {
struct Chunk { std::vector<std::uint8_t> bytes; std::size_t index{}; std::size_t size{}; std::string hash; };
class FixedSizeChunker {
 public:
  explicit FixedSizeChunker(std::size_t chunk_size = 4096);
  std::vector<Chunk> chunkFile(const std::filesystem::path& path) const;
  std::size_t chunkSize() const noexcept { return chunk_size_; }
 private: std::size_t chunk_size_;
};
}

