#include "wal.h"
#include "checksum.h"
#include "errors.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <unordered_map>
namespace flashforge { namespace {
constexpr std::uint32_t MAGIC=0x314c5746; // FWL1 little endian
template<class T>void appendNum(std::vector<std::uint8_t>&o,T v){for(std::size_t i=0;i<sizeof(T);++i)o.push_back(static_cast<std::uint8_t>(v>>(i*8)));}
template<class T>bool readNum(const std::vector<std::uint8_t>&b,std::size_t&p,T&v){if(p+sizeof(T)>b.size())return false;v=0;for(std::size_t i=0;i<sizeof(T);++i)v|=static_cast<T>(b[p++])<<(i*8);return true;}
}
WriteAheadLog::WriteAheadLog(std::filesystem::path p):path_(std::move(p)){std::filesystem::create_directories(path_.parent_path());auto snap=latestCommittedSnapshot();(void)snap;}
void WriteAheadLog::append(std::uint8_t type,std::uint64_t tx,const std::vector<std::uint8_t>&payload){std::vector<std::uint8_t>b;appendNum(b,MAGIC);b.push_back(type);appendNum(b,tx);appendNum<std::uint64_t>(b,payload.size());appendNum(b,crc32(payload));b.insert(b.end(),payload.begin(),payload.end());int fd=::open(path_.c_str(),O_WRONLY|O_CREAT|O_APPEND,0644);if(fd<0)throw WALException("cannot open WAL");std::size_t d=0;while(d<b.size()){auto n=::write(fd,b.data()+d,b.size()-d);if(n<0){::close(fd);throw WALException("WAL write failed");}d+=static_cast<std::size_t>(n);}if(::fsync(fd)!=0){::close(fd);throw WALException("WAL fsync failed");}::close(fd);}
std::uint64_t WriteAheadLog::begin(const std::vector<std::uint8_t>&s){std::lock_guard lock(mutex_);auto id=next_txid_++;append(1,id,s);return id;}
void WriteAheadLog::commit(std::uint64_t id){std::lock_guard lock(mutex_);append(2,id,{});}
std::optional<std::vector<std::uint8_t>> WriteAheadLog::latestCommittedSnapshot(){std::lock_guard lock(mutex_);if(!std::filesystem::exists(path_))return std::nullopt;std::ifstream in(path_,std::ios::binary);std::vector<std::uint8_t>b((std::istreambuf_iterator<char>(in)),{});std::size_t p=0;std::unordered_map<std::uint64_t,std::vector<std::uint8_t>> pending;std::optional<std::vector<std::uint8_t>> latest;std::uint64_t maxid=0;while(p<b.size()){auto start=p;std::uint32_t magic,crc;std::uint8_t type;std::uint64_t id,n;if(!readNum(b,p,magic)||magic!=MAGIC||p>=b.size())break;type=b[p++];if(!readNum(b,p,id)||!readNum(b,p,n)||!readNum(b,p,crc)||n>b.size()-p){p=start;break;}std::vector<std::uint8_t>payload(b.begin()+static_cast<std::ptrdiff_t>(p),b.begin()+static_cast<std::ptrdiff_t>(p+n));p+=n;if(crc32(payload)!=crc)break;maxid=std::max(maxid,id);if(type==1)pending[id]=std::move(payload);else if(type==2){auto it=pending.find(id);if(it!=pending.end()){latest=it->second;pending.erase(it);}}}next_txid_=maxid+1;return latest;}
void WriteAheadLog::checkpoint(){std::lock_guard lock(mutex_);int fd=::open(path_.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd<0)throw WALException("cannot checkpoint WAL");::fsync(fd);::close(fd);}
}
