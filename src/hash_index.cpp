#include "hash_index.h"
#include "errors.h"
namespace flashforge {
bool HashIndex::contains(const std::string& h)const{return entries_.count(h)!=0;}
const ChunkRecord* HashIndex::find(const std::string& h)const{auto i=entries_.find(h);return i==entries_.end()?nullptr:&i->second;}
ChunkRecord* HashIndex::find(const std::string& h){auto i=entries_.find(h);return i==entries_.end()?nullptr:&i->second;}
void HashIndex::set(const std::string& h,ChunkRecord r){entries_[h]=std::move(r);}
void HashIndex::increment(const std::string& h,std::uint64_t n){auto* r=find(h);if(!r)throw StorageException("missing chunk index entry: "+h);r->reference_count+=n;}
void HashIndex::decrement(const std::string& h,std::uint64_t n){auto* r=find(h);if(!r||r->reference_count<n)throw StorageException("chunk reference underflow: "+h);r->reference_count-=n;}
void HashIndex::erase(const std::string& h){entries_.erase(h);}void HashIndex::clear(){entries_.clear();}
}
