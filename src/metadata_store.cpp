#include "metadata_store.h"
#include "errors.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <limits>
namespace flashforge { namespace {
template<class T>void put(std::vector<std::uint8_t>&o,T v){for(std::size_t i=0;i<sizeof(T);++i)o.push_back(static_cast<std::uint8_t>(v>>(i*8)));}
void putString(std::vector<std::uint8_t>&o,const std::string&s){put<std::uint64_t>(o,s.size());o.insert(o.end(),s.begin(),s.end());}
template<class T>T get(const std::vector<std::uint8_t>&b,std::size_t&p){if(p+sizeof(T)>b.size())throw MetadataException("truncated metadata");T v{};for(std::size_t i=0;i<sizeof(T);++i)v|=static_cast<T>(b[p++])<<(i*8);return v;}
std::string getString(const std::vector<std::uint8_t>&b,std::size_t&p){auto n=get<std::uint64_t>(b,p);if(n>b.size()-p)throw MetadataException("invalid metadata string");std::string s(reinterpret_cast<const char*>(b.data()+p),static_cast<std::size_t>(n));p+=n;return s;}
void syncDir(const std::filesystem::path&p){int fd=::open(p.c_str(),O_RDONLY);if(fd>=0){::fsync(fd);::close(fd);}}
}
MetadataStore::MetadataStore(std::filesystem::path root):root_(std::move(root)),path_(root_/"metadata.bin"){}
void MetadataStore::load(){files_.clear();next_id_=1;if(!std::filesystem::exists(path_))return;std::ifstream in(path_,std::ios::binary);if(!in)throw MetadataException("cannot open metadata");std::vector<std::uint8_t>b((std::istreambuf_iterator<char>(in)),{});deserialize(b);}
std::vector<std::uint8_t> MetadataStore::serialize()const{std::vector<std::uint8_t>o={'F','F','M','1'};put(o,next_id_);put<std::uint64_t>(o,files_.size());for(const auto&[id,f]:files_){putString(o,id);putString(o,f.original_filename);put(o,f.logical_size);put(o,f.creation_timestamp);put(o,f.version);put<std::uint64_t>(o,f.chunk_hashes.size());for(const auto&h:f.chunk_hashes)putString(o,h);}return o;}
void MetadataStore::deserialize(const std::vector<std::uint8_t>&b){if(b.size()<4||std::memcmp(b.data(),"FFM1",4)!=0)throw MetadataException("invalid metadata magic");std::size_t p=4;auto next=get<std::uint64_t>(b,p),count=get<std::uint64_t>(b,p);std::unordered_map<std::string,FileMetadata> parsed;for(std::uint64_t i=0;i<count;++i){FileMetadata f;f.file_id=getString(b,p);f.original_filename=getString(b,p);f.logical_size=get<std::uint64_t>(b,p);f.creation_timestamp=get<std::uint64_t>(b,p);f.version=get<std::uint64_t>(b,p);auto n=get<std::uint64_t>(b,p);if(n>(b.size()-p)/8+1)throw MetadataException("invalid chunk count");for(std::uint64_t j=0;j<n;++j)f.chunk_hashes.push_back(getString(b,p));parsed.emplace(f.file_id,std::move(f));}if(p!=b.size())throw MetadataException("trailing metadata bytes");next_id_=next;files_=std::move(parsed);}
void MetadataStore::persistAtomic(const std::vector<std::uint8_t>&b)const{std::filesystem::create_directories(root_);auto tmp=path_;tmp+=".tmp";int fd=::open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd<0)throw MetadataException("cannot create metadata temporary file");std::size_t done=0;while(done<b.size()){auto n=::write(fd,b.data()+done,b.size()-done);if(n<0){::close(fd);throw MetadataException("metadata write failed");}done+=static_cast<std::size_t>(n);}if(::fsync(fd)!=0){::close(fd);throw MetadataException("metadata fsync failed");}::close(fd);std::error_code ec;std::filesystem::rename(tmp,path_,ec);if(ec)throw MetadataException("metadata rename failed: "+ec.message());syncDir(root_);}
std::string MetadataStore::allocateId(){return "ff-"+std::to_string(next_id_++);}
FileMetadata* MetadataStore::find(const std::string&id){auto i=files_.find(id);return i==files_.end()?nullptr:&i->second;}const FileMetadata* MetadataStore::find(const std::string&id)const{auto i=files_.find(id);return i==files_.end()?nullptr:&i->second;}
void MetadataStore::upsert(FileMetadata f){files_[f.file_id]=std::move(f);}void MetadataStore::erase(const std::string&id){files_.erase(id);}
}

