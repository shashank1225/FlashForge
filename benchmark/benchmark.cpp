#include "storage_engine.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
int main(int argc,char**argv){using namespace flashforge;std::size_t mib=argc>1?std::stoull(argv[1]):64,files=argc>2?std::stoull(argv[2]):8;auto base=std::filesystem::temp_directory_path()/"flashforge-benchmark";std::filesystem::remove_all(base);std::filesystem::create_directories(base/"inputs");std::vector<std::uint8_t>common(4096);std::mt19937 rng(42);for(auto&b:common)b=static_cast<std::uint8_t>(rng());for(std::size_t f=0;f<files;++f){std::ofstream o(base/"inputs"/std::to_string(f),std::ios::binary);for(std::size_t n=0;n<mib*256/files;++n){if(n%2==0)o.write(reinterpret_cast<char*>(common.data()),common.size());else{auto unique=common;unique[0]=static_cast<std::uint8_t>(f);unique[1]=static_cast<std::uint8_t>(n);o.write(reinterpret_cast<char*>(unique.data()),unique.size());}}}std::cout<<"threads,throughput_mb_s,latency_ms,files_per_sec,dedup_ratio,physical_bytes\n";for(auto threads:{1u,2u,4u,8u,16u,32u}){auto root=base/("store-"+std::to_string(threads));StorageEngine e(root,4096,threads);auto start=std::chrono::steady_clock::now();for(std::size_t f=0;f<files;++f)e.put(base/"inputs"/std::to_string(f));auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();auto s=e.stats();std::cout<<threads<<','<<std::fixed<<std::setprecision(2)<<(mib/seconds)<<','<<(seconds*1000/files)<<','<<(files/seconds)<<','<<s.dedup_ratio<<','<<s.physical_size<<'\n';}std::filesystem::remove_all(base);}

