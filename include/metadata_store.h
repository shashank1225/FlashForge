#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
namespace flashforge {
struct MetadataPersistTiming {
  std::uint64_t write_us{}, fsync_us{}, rename_us{}, directory_fsync_us{};
};
struct FileMetadata {
  std::string file_id, original_filename; std::uint64_t logical_size{}, creation_timestamp{}, version{1};
  std::vector<std::string> chunk_hashes;
};
class MetadataStore {
 public:
  explicit MetadataStore(std::filesystem::path root);
  void load();
  std::vector<std::uint8_t> serialize() const;
  void deserialize(const std::vector<std::uint8_t>& bytes);
  void persistAtomic(const std::vector<std::uint8_t>& bytes, MetadataPersistTiming* timing = nullptr) const;
  std::string allocateId();
  FileMetadata* find(const std::string& id); const FileMetadata* find(const std::string& id) const;
  void upsert(FileMetadata metadata); void erase(const std::string& id);
  const auto& files() const noexcept { return files_; }
 private:
  std::filesystem::path root_, path_; std::uint64_t next_id_{1};
  std::unordered_map<std::string, FileMetadata> files_;
};
}

