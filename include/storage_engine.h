#pragma once
#include "chunker.h"
#include "garbage_collector.h"
#include "hash_index.h"
#include "metadata_store.h"
#include "thread_pool.h"
#include "wal.h"
#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <vector>
namespace flashforge {
struct StorageStats {
 std::uint64_t logical_size{}, physical_size{}, unique_chunks{}, total_chunk_references{}, file_count{};
 double dedup_ratio{}; std::uint64_t space_saved{};
};
class StorageEngine {
 public:
  explicit StorageEngine(std::filesystem::path root, std::size_t chunk_size = 4096,
                         std::size_t workers = 0);
  std::string put(const std::filesystem::path& source);
  void get(const std::string& file_id, const std::filesystem::path& destination) const;
  void deleteFile(const std::string& file_id);
  void update(const std::string& file_id, const std::filesystem::path& source);
  std::vector<FileMetadata> listFiles() const;
  StorageStats stats() const;
  GCStats collectGarbage();
  std::uint64_t chunkRefCount(const std::string& hash) const;
 private:
  std::filesystem::path chunkPath(const std::string& hash) const;
  void persistChunk(const Chunk& chunk);
  std::vector<Chunk> prepareChunks(const std::filesystem::path& source);
  void rebuildIndex(); void commitState();
  std::filesystem::path root_, chunks_root_; FixedSizeChunker chunker_;
  mutable std::shared_mutex state_mutex_; MetadataStore metadata_; HashIndex index_;
  WriteAheadLog wal_; ThreadPool pool_;
};
}
