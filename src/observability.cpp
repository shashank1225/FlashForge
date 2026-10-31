#include "observability.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <system_error>
#if defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#endif

namespace flashforge { namespace {
std::uint64_t nowMs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}
std::string clean(std::string value) {
  for (char& c : value) if (c == '\t' || c == '\n' || c == '\r') c = '_';
  return value;
}
std::string escapeJson(const std::string& value) {
  std::ostringstream out;
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c == '\n') out << "\\n";
    else if (c >= 0x20) out << c;
  }
  return out.str();
}
std::uint64_t percentile(std::vector<std::uint64_t> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::ceil(p * values.size()) - 1);
  return values[std::min(index, values.size() - 1)];
}
#if defined(__linux__)
double pressureAverage(const std::filesystem::path& path) {
  std::ifstream input(path); std::string label, avg10;
  if (!(input >> label >> avg10) || avg10.rfind("avg10=", 0) != 0) return -1.0;
  try { return std::stod(avg10.substr(6)); } catch (...) { return -1.0; }
}
#endif
SystemSignals collectSystem(const std::filesystem::path& root) {
  SystemSignals signals;
  std::error_code ec;
  auto space = std::filesystem::space(root, ec);
  if (!ec) { signals.filesystem_free_bytes = space.available; signals.filesystem_capacity_bytes = space.capacity; }
#if defined(__linux__)
  signals.linux_signals_available = true;
  std::ifstream status("/proc/self/status"); std::string key;
  while (status >> key) {
    if (key == "VmRSS:") { std::uint64_t kb; status >> kb; signals.resident_bytes = kb * 1024; }
    std::string rest; std::getline(status, rest);
  }
  std::ifstream io("/proc/self/io");
  while (io >> key) {
    std::uint64_t value{}; io >> value;
    if (key == "read_bytes:") signals.read_bytes = value;
    else if (key == "write_bytes:") signals.write_bytes = value;
  }
  signals.io_pressure_pct = pressureAverage("/proc/pressure/io");
  signals.memory_pressure_pct = pressureAverage("/proc/pressure/memory");
  if (DIR* dir = ::opendir("/proc/self/fd")) { while (::readdir(dir)) ++signals.open_fds; ::closedir(dir); if (signals.open_fds >= 2) signals.open_fds -= 2; }
#endif
  return signals;
}
}

MetricsStore::MetricsStore(std::filesystem::path root, std::uint64_t retention)
    : root_(std::move(root)), directory_(root_ / "observability"),
      samples_path_(directory_ / "metrics.tsv"), retention_samples_(retention) {
  const char* setting = std::getenv("FLASHFORGE_OBSERVABILITY");
  enabled_ = !setting || std::string(setting) != "0";
  if (enabled_) writer_ = std::thread([this]{ writerLoop(); });
}

MetricsStore::~MetricsStore() {
  if (!writer_.joinable()) return;
  { std::lock_guard lock(mutex_); stopping_ = true; }
  cv_.notify_one(); writer_.join();
}

void MetricsStore::record(const OperationSample& sample) noexcept {
  if (!enabled_) return;
  try { std::lock_guard lock(mutex_); if (queue_.size() >= queue_capacity_) { ++dropped_; return; } queue_.push_back(sample); cv_.notify_one(); }
  catch (...) { ++dropped_; }
}

std::vector<OperationSample> MetricsStore::load() const {
  std::vector<OperationSample> samples;
  std::ifstream in(samples_path_); std::string line;
  while (std::getline(in,line)) { try { std::vector<std::string> fields; std::size_t start=0; for(;;){auto pos=line.find('\t',start);fields.push_back(line.substr(start,pos-start));if(pos==std::string::npos)break;start=pos+1;}if(fields.size()!=11&&fields.size()<18)continue;OperationSample s;s.timestamp_ms=std::stoull(fields[0]);s.operation=fields[1];s.success=std::stoull(fields[2])!=0;s.latency_us=std::stoull(fields[3]);s.bytes=std::stoull(fields[4]);s.hash_us=std::stoull(fields[5]);s.durability_us=std::stoull(fields[6]);if(fields.size()==11){s.dedup_hits=std::stoull(fields[7]);s.dedup_misses=std::stoull(fields[8]);s.checksum_failures=std::stoull(fields[9]);s.gc_reclaimed=std::stoull(fields[10]);}else{s.wal_write_us=std::stoull(fields[7]);s.wal_fsync_us=std::stoull(fields[8]);s.metadata_write_us=std::stoull(fields[9]);s.metadata_fsync_us=std::stoull(fields[10]);s.metadata_rename_us=std::stoull(fields[11]);s.directory_fsync_us=std::stoull(fields[12]);s.fault_delay_us=std::stoull(fields[13]);s.dedup_hits=std::stoull(fields[14]);s.dedup_misses=std::stoull(fields[15]);s.checksum_failures=std::stoull(fields[16]);s.gc_reclaimed=std::stoull(fields[17]);}samples.push_back(s);}catch(...){}}
  { std::lock_guard lock(mutex_); samples.insert(samples.end(),in_flight_.begin(),in_flight_.end());samples.insert(samples.end(),queue_.begin(),queue_.end()); }
  return samples;
}

void MetricsStore::writerLoop() noexcept {
  for (;;) {
    std::vector<OperationSample> batch;
    { std::unique_lock lock(mutex_); cv_.wait_for(lock,std::chrono::milliseconds(200),[this]{return stopping_||!queue_.empty();}); if(queue_.empty()&&stopping_)break;while(!queue_.empty()&&batch.size()<128){batch.push_back(std::move(queue_.front()));queue_.pop_front();}in_flight_.assign(batch.begin(),batch.end()); }
    writeBatch(batch);
    { std::lock_guard lock(mutex_); in_flight_.clear(); }
  }
}

void MetricsStore::writeBatch(const std::vector<OperationSample>& batch) noexcept {
  if(batch.empty())return;
  try { std::filesystem::create_directories(directory_);std::ofstream out(samples_path_,std::ios::app);if(!out){dropped_+=batch.size();return;}for(const auto&s:batch)out<<s.timestamp_ms<<'\t'<<clean(s.operation)<<'\t'<<s.success<<'\t'<<s.latency_us<<'\t'<<s.bytes<<'\t'<<s.hash_us<<'\t'<<s.durability_us<<'\t'<<s.wal_write_us<<'\t'<<s.wal_fsync_us<<'\t'<<s.metadata_write_us<<'\t'<<s.metadata_fsync_us<<'\t'<<s.metadata_rename_us<<'\t'<<s.directory_fsync_us<<'\t'<<s.fault_delay_us<<'\t'<<s.dedup_hits<<'\t'<<s.dedup_misses<<'\t'<<s.checksum_failures<<'\t'<<s.gc_reclaimed<<'\n';out.close();compactIfNeeded();}catch(...){dropped_+=batch.size();}
}

void MetricsStore::compactIfNeeded() noexcept {
  try {
    std::ifstream in(samples_path_); std::vector<std::string> lines; std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    if (lines.size() <= retention_samples_ * 11 / 10) return;
    auto temp = samples_path_; temp += ".tmp"; std::ofstream out(temp, std::ios::trunc);
    const auto first = lines.size() - static_cast<std::size_t>(retention_samples_);
    for (std::size_t i = first; i < lines.size(); ++i) out << lines[i] << '\n';
    out.close(); std::filesystem::rename(temp, samples_path_);
  } catch (...) {}
}

ObservabilityReport MetricsStore::report() const {
  auto samples = load(); ObservabilityReport result; result.system = collectSystem(root_);result.telemetry_samples_dropped=dropped_.load();
  std::map<std::string, std::vector<OperationSample>> grouped;
  std::uint64_t hits{}, misses{};
  for (const auto& s : samples) { grouped[s.operation].push_back(s); result.checksum_failures += s.checksum_failures; result.gc_reclaimed += s.gc_reclaimed; hits += s.dedup_hits; misses += s.dedup_misses; }
  result.dedup_hit_ratio = hits + misses ? static_cast<double>(hits) / static_cast<double>(hits + misses) : 0.0;
  for (const auto& [name, values] : grouped) {
    OperationSummary summary; summary.operation = name; std::vector<std::uint64_t> latencies;
    for (const auto& s : values) { ++summary.count; summary.failures += !s.success; summary.bytes += s.bytes; summary.hash_us += s.hash_us; summary.durability_us += s.durability_us;summary.wal_write_us+=s.wal_write_us;summary.wal_fsync_us+=s.wal_fsync_us;summary.metadata_write_us+=s.metadata_write_us;summary.metadata_fsync_us+=s.metadata_fsync_us;summary.metadata_rename_us+=s.metadata_rename_us;summary.directory_fsync_us+=s.directory_fsync_us;summary.fault_delay_us+=s.fault_delay_us; summary.dedup_hits += s.dedup_hits; summary.dedup_misses += s.dedup_misses; latencies.push_back(s.latency_us); }
    summary.p50_us = percentile(latencies, .50); summary.p95_us = percentile(latencies, .95); summary.p99_us = percentile(latencies, .99); result.operations.push_back(summary);
  }
  return result;
}

std::vector<EngineeringDecision> MetricsStore::diagnose() const {
  const auto r = report(); std::vector<EngineeringDecision> decisions;
  auto find = [&](const std::string& name) -> const OperationSummary* { for (const auto& op : r.operations) if (op.operation == name) return &op; return nullptr; };
  const auto* put = find("put"); const auto* gc = find("gc");
  if (r.checksum_failures) decisions.push_back({"CRITICAL", "Stored chunk corruption detected", std::to_string(r.checksum_failures) + " checksum verification failure(s)", "Stop serving the affected object, preserve evidence, and run a full integrity scrub. Do not automatically move or delete the only copy.", .99});
  if (put && put->count) {
    const auto avg_durability = put->durability_us / put->count;
    const auto avg_hash = put->hash_us / put->count;
    const auto avg_wal_fsync = put->wal_fsync_us / put->count;
    if (put->p99_us > 50000 && avg_wal_fsync * 2 > put->p99_us)
      decisions.push_back({"WARNING", "WAL synchronization is the likely dominant PUT-latency contributor", "PUT p99=" + std::to_string(put->p99_us / 1000) + "ms, average WAL fsync=" + std::to_string(avg_wal_fsync / 1000) + "ms" + (put->fault_delay_us ? ", test fault injection is active" : ""), "Inspect the synchronization and block-device path; adding hash workers is unlikely to help. This correlation does not by itself prove disk saturation.", put->fault_delay_us ? .99 : .90});
    else if (put->p99_us > 50000 && avg_durability * 2 > put->p99_us)
      decisions.push_back({"WARNING", "The durable commit path is the likely dominant PUT-latency contributor", "PUT p99=" + std::to_string(put->p99_us / 1000) + "ms, average full commit=" + std::to_string(avg_durability / 1000) + "ms", "Inspect WAL and metadata timing stages; adding hash workers is unlikely to help. The available evidence does not prove disk saturation.", .76});
    else if (put->p99_us > 50000 && avg_hash * 2 > put->p99_us)
      decisions.push_back({"WARNING", "Chunk hashing dominates PUT latency", "PUT p99=" + std::to_string(put->p99_us / 1000) + "ms, average hashing=" + std::to_string(avg_hash / 1000) + "ms", "Increase hashing workers while CPU headroom exists, then benchmark again.", .86});
    if (put->failures * 20 > put->count)
      decisions.push_back({"CRITICAL", "PUT failure rate exceeds five percent", std::to_string(put->failures) + " of " + std::to_string(put->count) + " PUT operations failed", "Inspect storage capacity, permissions, WAL errors, and recent incident logs before retrying traffic.", .98});
  }
  if (put && put->dedup_hits + put->dedup_misses >= 20 && r.dedup_hit_ratio < .10)
    decisions.push_back({"INFO", "The workload has low deduplication locality", "Dedup hit ratio=" + std::to_string(r.dedup_hit_ratio * 100.0) + "%", "Validate whether fixed 4 KiB boundaries match the workload; evaluate content-defined chunking before adding capacity.", .80});
  if (r.system.filesystem_capacity_bytes && r.system.filesystem_free_bytes * 10 < r.system.filesystem_capacity_bytes)
    decisions.push_back({"CRITICAL", "Storage capacity is below ten percent free", std::to_string(r.system.filesystem_free_bytes) + " bytes available", "Run garbage collection, inspect retention, and add capacity before writes begin failing.", .99});
  if (r.system.io_pressure_pct >= 20.0)
    decisions.push_back({"WARNING", "Linux reports sustained I/O pressure", "I/O pressure avg10=" + std::to_string(r.system.io_pressure_pct) + "%", "Inspect block-device latency and queue depth; rate-limit background GC if foreground latency is elevated.", .88});
  if (gc && put && gc->p99_us > 0 && put->p99_us > 100000)
    decisions.push_back({"INFO", "Foreground PUT latency is elevated while GC activity is present", "PUT p99=" + std::to_string(put->p99_us / 1000) + "ms and GC samples=" + std::to_string(gc->count), "Schedule GC during lower load or introduce an I/O rate limit.", .70});
  return decisions;
}

std::string MetricsStore::json() const {
  const auto r = report(); std::ostringstream out; out << "{\"operations\":[";
  for (std::size_t i = 0; i < r.operations.size(); ++i) { const auto& op = r.operations[i]; if (i) out << ','; out << "{\"name\":\"" << escapeJson(op.operation) << "\",\"count\":" << op.count << ",\"failures\":" << op.failures << ",\"bytes\":" << op.bytes << ",\"p50_us\":" << op.p50_us << ",\"p95_us\":" << op.p95_us << ",\"p99_us\":" << op.p99_us << ",\"wal_write_us\":" << op.wal_write_us << ",\"wal_fsync_us\":" << op.wal_fsync_us << ",\"metadata_write_us\":" << op.metadata_write_us << ",\"metadata_fsync_us\":" << op.metadata_fsync_us << ",\"metadata_rename_us\":" << op.metadata_rename_us << ",\"directory_fsync_us\":" << op.directory_fsync_us << ",\"fault_delay_us\":" << op.fault_delay_us << '}'; }
  out << "],\"dedup_hit_ratio\":" << std::fixed << std::setprecision(4) << r.dedup_hit_ratio << ",\"checksum_failures\":" << r.checksum_failures << ",\"gc_reclaimed_bytes\":" << r.gc_reclaimed << ",\"telemetry_samples_dropped\":" << r.telemetry_samples_dropped << ",\"system\":{\"linux_signals_available\":" << (r.system.linux_signals_available ? "true" : "false") << ",\"resident_bytes\":" << r.system.resident_bytes << ",\"read_bytes\":" << r.system.read_bytes << ",\"write_bytes\":" << r.system.write_bytes << ",\"open_fds\":" << r.system.open_fds << ",\"io_pressure_pct\":" << r.system.io_pressure_pct << ",\"memory_pressure_pct\":" << r.system.memory_pressure_pct << ",\"filesystem_free_bytes\":" << r.system.filesystem_free_bytes << "}}"; return out.str();
}

std::string MetricsStore::prometheus() const {
  const auto r = report(); std::ostringstream out;
  for (const auto& op : r.operations) { out << "flashforge_operations_total{operation=\"" << op.operation << "\"} " << op.count << '\n'; out << "flashforge_operation_failures_total{operation=\"" << op.operation << "\"} " << op.failures << '\n'; out << "flashforge_operation_latency_p99_seconds{operation=\"" << op.operation << "\"} " << std::fixed << std::setprecision(6) << op.p99_us / 1000000.0 << '\n';out<<"flashforge_wal_fsync_seconds_total{operation=\""<<op.operation<<"\"} "<<op.wal_fsync_us/1000000.0<<'\n';out<<"flashforge_metadata_fsync_seconds_total{operation=\""<<op.operation<<"\"} "<<op.metadata_fsync_us/1000000.0<<'\n'; }
  out << "flashforge_dedup_hit_ratio " << r.dedup_hit_ratio << '\n' << "flashforge_checksum_failures_total " << r.checksum_failures << '\n' << "flashforge_gc_reclaimed_bytes_total " << r.gc_reclaimed << '\n' << "flashforge_telemetry_samples_dropped_total "<<r.telemetry_samples_dropped<<'\n'<< "flashforge_filesystem_free_bytes " << r.system.filesystem_free_bytes << '\n'; return out.str();
}

OperationTimer::OperationTimer(MetricsStore& store, std::string operation)
    : store_(&store), started_(std::chrono::steady_clock::now()) { sample_.timestamp_ms = nowMs(); sample_.operation = std::move(operation); }
OperationTimer::~OperationTimer() { finish(false); }
void OperationTimer::success() noexcept { finish(true); }
void OperationTimer::finish(bool successful) noexcept { if (finished_) return; finished_ = true; sample_.success = successful; sample_.latency_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_).count()); store_->record(sample_); }
}
