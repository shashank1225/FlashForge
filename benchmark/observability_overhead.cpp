#include "storage_engine.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace {
double run(const std::filesystem::path& base, const std::filesystem::path& input,
           bool enabled, int trial) {
  if (enabled) ::unsetenv("FLASHFORGE_OBSERVABILITY");
  else ::setenv("FLASHFORGE_OBSERVABILITY", "0", 1);
  auto root = base / ((enabled ? "enabled-" : "disabled-") + std::to_string(trial));
  std::filesystem::remove_all(root);
  auto started = std::chrono::steady_clock::now();
  { flashforge::StorageEngine engine(root, 4096, 4); engine.put(input); }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}
double median(std::vector<double> values) {
  std::sort(values.begin(), values.end()); return values[values.size()/2];
}
}

int main(int argc, char** argv) {
  const std::size_t mib = argc > 1 ? std::stoull(argv[1]) : 16;
  const auto base = std::filesystem::temp_directory_path() / "flashforge-observability-overhead";
  std::filesystem::remove_all(base); std::filesystem::create_directories(base);
  const auto input = base / "workload.bin"; std::ofstream out(input, std::ios::binary);
  std::mt19937 rng(42); std::vector<unsigned char> block(4096);
  for (std::size_t i=0;i<mib*256;++i) { for(auto& b:block)b=static_cast<unsigned char>(rng()); out.write(reinterpret_cast<const char*>(block.data()),block.size()); }
  out.close(); std::vector<double> enabled,disabled;
  std::cout << "mode,trial,elapsed_ms,throughput_mb_s\n";
  for(int trial=1;trial<=3;++trial) {
    auto off=run(base,input,false,trial); disabled.push_back(off); std::cout<<"disabled,"<<trial<<','<<std::fixed<<std::setprecision(2)<<off<<','<<mib/(off/1000.0)<<'\n';
    auto on=run(base,input,true,trial); enabled.push_back(on); std::cout<<"enabled,"<<trial<<','<<on<<','<<mib/(on/1000.0)<<'\n';
  }
  ::unsetenv("FLASHFORGE_OBSERVABILITY"); auto off=median(disabled),on=median(enabled);
  std::cout<<"median_disabled_ms,"<<off<<"\nmedian_enabled_ms,"<<on<<"\nobservability_overhead_pct,"<<((on-off)/off*100.0)<<'\n';
  std::filesystem::remove_all(base);
}
