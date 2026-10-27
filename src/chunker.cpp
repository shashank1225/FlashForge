#include "chunker.h"
#include "errors.h"
#include <fstream>
namespace flashforge {
FixedSizeChunker::FixedSizeChunker(std::size_t size):chunk_size_(size){if(size==0)throw StorageException("chunk size must be positive");}
std::vector<Chunk> FixedSizeChunker::chunkFile(const std::filesystem::path& path) const {
 std::ifstream in(path,std::ios::binary);if(!in)throw FileNotFoundException("cannot open input: "+path.string());
 std::vector<Chunk> chunks;std::size_t index=0;
 while(in){Chunk c;c.bytes.resize(chunk_size_);in.read(reinterpret_cast<char*>(c.bytes.data()),static_cast<std::streamsize>(c.bytes.size()));auto n=in.gcount();if(n<=0)break;c.bytes.resize(static_cast<std::size_t>(n));c.index=index++;c.size=c.bytes.size();chunks.push_back(std::move(c));}
 if(!in.eof()&&in.fail())throw StorageException("failed reading input: "+path.string());return chunks;
}
}

