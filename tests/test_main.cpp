#include "storage_engine.h"
#include "checksum.h"
#include "errors.h"
#include "metadata_store.h"
#include "wal.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <thread>
using namespace flashforge;
namespace {
struct TempDir { std::filesystem::path path; TempDir(){path=std::filesystem::temp_directory_path()/("flashforge-test-"+std::to_string(std::random_device{}()));std::filesystem::create_directories(path);}~TempDir(){std::error_code ec;std::filesystem::remove_all(path,ec);} };
void write(const std::filesystem::path&p,const std::vector<std::uint8_t>&b){std::ofstream o(p,std::ios::binary);o.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));}
std::vector<std::uint8_t> read(const std::filesystem::path&p){std::ifstream i(p,std::ios::binary);return {(std::istreambuf_iterator<char>(i)),{}};}
std::vector<std::uint8_t> bytes(std::size_t n,std::uint32_t seed=7){std::vector<std::uint8_t>b(n);std::mt19937 r(seed);for(auto&v:b)v=static_cast<std::uint8_t>(r());return b;}
void check(bool v,const char*m){if(!v)throw std::runtime_error(m);}
template<class E,class F>void throws(F&&f){try{f();}catch(const E&){return;}throw std::runtime_error("expected exception");}
void roundTrip(std::size_t n){TempDir t;auto input=t.path/"in.bin",out=t.path/"out.bin";auto b=bytes(n);write(input,b);StorageEngine e(t.path/"store",4096,4);auto id=e.put(input);e.get(id,out);check(read(out)==b,"round trip mismatch");}
}
int main(){std::vector<std::pair<std::string,std::function<void()>>> tests;
 tests.push_back({"put/get round trip",[]{roundTrip(13000);}});
 tests.push_back({"empty file",[]{roundTrip(0);}});tests.push_back({"tiny file",[]{roundTrip(3);}});tests.push_back({"large binary file",[]{roundTrip(2*1024*1024+13);}});
 tests.push_back({"duplicate files and reference counts",[]{TempDir t;auto p=t.path/"x";auto b=bytes(8192);write(p,b);StorageEngine e(t.path/"s");auto a=e.put(p),c=e.put(p);(void)a;(void)c;auto s=e.stats();check(s.logical_size==16384&&s.physical_size==8192&&s.unique_chunks==2,"duplicate not deduplicated");check(e.chunkRefCount(sha256({b.begin(),b.begin()+4096}))==2,"bad refcount");}});
 tests.push_back({"duplicate chunks within file",[]{TempDir t;std::vector<std::uint8_t>b(8192,42);auto p=t.path/"x";write(p,b);StorageEngine e(t.path/"s");e.put(p);auto s=e.stats();check(s.unique_chunks==1&&s.total_chunk_references==2,"intra-file dedup failed");}});
 tests.push_back({"partial deduplication",[]{TempDir t;std::vector<std::uint8_t>a(8192,1),b;std::fill(a.begin()+4096,a.end(),2);b=a;std::fill(b.begin()+4096,b.end(),3);write(t.path/"a",a);write(t.path/"b",b);StorageEngine e(t.path/"s");e.put(t.path/"a");e.put(t.path/"b");check(e.stats().unique_chunks==3,"partial dedup failed");}});
 tests.push_back({"delete and garbage collection",[]{TempDir t;auto p=t.path/"x";write(p,bytes(5000));StorageEngine e(t.path/"s");auto id=e.put(p);e.deleteFile(id);check(e.stats().file_count==0&&e.stats().physical_size==0,"delete failed");auto g=e.collectGarbage();check(g.chunks_deleted==2&&g.bytes_reclaimed==5000,"gc failed");}});
 tests.push_back({"live data survives GC",[]{TempDir t;auto p=t.path/"x",o=t.path/"o";write(p,bytes(5000));StorageEngine e(t.path/"s");auto id=e.put(p);check(e.collectGarbage().chunks_deleted==0,"gc deleted live chunk");e.get(id,o);}});
 tests.push_back({"copy-on-write update",[]{TempDir t;std::vector<std::uint8_t>a(8192,3),b;std::fill(a.begin()+4096,a.end(),4);b=a;std::fill(b.begin()+4096,b.end(),5);write(t.path/"a",a);write(t.path/"b",b);StorageEngine e(t.path/"s");auto id=e.put(t.path/"a");e.update(id,t.path/"b");e.get(id,t.path/"o");check(read(t.path/"o")==b,"update mismatch");check(e.listFiles()[0].version==2&&e.stats().unique_chunks==2,"update metadata failed");check(e.collectGarbage().chunks_deleted==1,"old chunk not collectible");}});
 tests.push_back({"chunk corruption detection",[]{TempDir t;std::vector<std::uint8_t>b(4096,9);write(t.path/"x",b);StorageEngine e(t.path/"s");auto id=e.put(t.path/"x"),h=sha256(b);auto p=t.path/"s"/"chunks"/h.substr(0,2)/h.substr(2,2)/h;std::fstream f(p,std::ios::binary|std::ios::in|std::ios::out);char x=0;f.write(&x,1);f.close();throws<ChunkCorruptionException>([&]{e.get(id,t.path/"o");});}});
 tests.push_back({"restart persistence",[]{TempDir t;write(t.path/"x",bytes(9000));std::string id;{StorageEngine e(t.path/"s");id=e.put(t.path/"x");}{StorageEngine e(t.path/"s");e.get(id,t.path/"o");check(read(t.path/"o")==read(t.path/"x"),"restart failed");}}});
 tests.push_back({"WAL committed replay",[]{TempDir t;auto root=t.path/"s";std::filesystem::create_directories(root);MetadataStore m(root);FileMetadata f{"ff-9","empty",0,1,1,{}};m.upsert(f);auto snap=m.serialize();WriteAheadLog w(root/"wal.log");auto tx=w.begin(snap);w.commit(tx);StorageEngine e(root);check(e.listFiles().size()==1&&e.listFiles()[0].file_id=="ff-9","WAL replay failed");}});
 tests.push_back({"WAL ignores uncommitted",[]{TempDir t;auto root=t.path/"s";std::filesystem::create_directories(root);MetadataStore m(root);m.upsert({"ff-1","ghost",0,1,1,{}});WriteAheadLog w(root/"wal.log");w.begin(m.serialize());StorageEngine e(root);check(e.listFiles().empty(),"uncommitted transaction replayed");}});
 tests.push_back({"concurrent PUT",[]{TempDir t;auto p=t.path/"x";write(p,bytes(12000));StorageEngine e(t.path/"s",4096,4);std::vector<std::thread>v;for(int i=0;i<8;++i)v.emplace_back([&]{e.put(p);});for(auto&x:v)x.join();check(e.stats().file_count==8&&e.stats().physical_size==12000,"concurrent PUT failed");}});
 tests.push_back({"concurrent GET",[]{TempDir t;auto p=t.path/"x";write(p,bytes(12000));StorageEngine e(t.path/"s");auto id=e.put(p);std::vector<std::thread>v;for(int i=0;i<8;++i)v.emplace_back([&,i]{e.get(id,t.path/("o"+std::to_string(i)));});for(auto&x:v)x.join();for(int i=0;i<8;++i)check(read(t.path/("o"+std::to_string(i)))==read(p),"concurrent GET failed");}});
 tests.push_back({"PUT plus DELETE race",[]{TempDir t;auto p=t.path/"x";write(p,bytes(12000));StorageEngine e(t.path/"s");auto old=e.put(p);std::thread putter([&]{e.put(p);});std::thread deleter([&]{e.deleteFile(old);});putter.join();deleter.join();check(e.stats().file_count==1&&e.stats().physical_size==12000,"PUT/delete race lost data");}});
 tests.push_back({"UPDATE plus GET race",[]{TempDir t;auto a=bytes(1024*1024,1),b=bytes(1024*1024,2);write(t.path/"a",a);write(t.path/"b",b);StorageEngine e(t.path/"s");auto id=e.put(t.path/"a");std::thread updater([&]{e.update(id,t.path/"b");});std::thread reader([&]{e.get(id,t.path/"o");});updater.join();reader.join();auto result=read(t.path/"o");check(result==a||result==b,"GET observed torn update");check(e.listFiles()[0].version==2,"update race failed");}});
 tests.push_back({"GET plus DELETE serialization",[]{TempDir t;auto p=t.path/"x";write(p,bytes(1024*1024));StorageEngine e(t.path/"s");auto id=e.put(p);std::atomic<bool>ok=false;std::thread reader([&]{try{e.get(id,t.path/"o");ok=true;}catch(const FileNotFoundException&){}});std::thread deleter([&]{e.deleteFile(id);});reader.join();deleter.join();if(ok)check(read(t.path/"o")==read(p),"raced GET was partial");check(e.stats().file_count==0,"delete lost race");}});
 tests.push_back({"observability persists operation metrics",[]{TempDir t;auto p=t.path/"x";write(p,bytes(9000));{StorageEngine e(t.path/"s");auto id=e.put(p);e.get(id,t.path/"o");e.deleteFile(id);e.collectGarbage();}{StorageEngine e(t.path/"s");auto r=e.observabilityReport();check(r.operations.size()>=4,"operation metrics were not persisted");check(e.metricsJson().find("\"p99_us\"")!=std::string::npos,"JSON metrics missing latency");check(e.metricsPrometheus().find("flashforge_operations_total")!=std::string::npos,"Prometheus export missing counter");}}});
 tests.push_back({"decision engine turns signals into action",[]{TempDir t;MetricsStore metrics(t.path/"s");OperationSample sample;sample.timestamp_ms=1;sample.operation="put";sample.success=true;sample.latency_us=200000;sample.durability_us=180000;for(int i=0;i<3;++i)metrics.record(sample);auto decisions=metrics.diagnose();check(!decisions.empty(),"slow durable writes produced no diagnosis");check(decisions[0].recommendation.find("hash workers")!=std::string::npos,"diagnosis was not actionable");}});
 int failed=0;for(auto&[name,test]:tests){try{test();std::cout<<"[PASS] "<<name<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"[FAIL] "<<name<<": "<<e.what()<<'\n';}}std::cout<<(tests.size()-failed)<<"/"<<tests.size()<<" tests passed\n";return failed?1:0;}
