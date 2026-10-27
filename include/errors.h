#pragma once
#include <stdexcept>
#include <string>

namespace flashforge {
class StorageException : public std::runtime_error { public: using runtime_error::runtime_error; };
class FileNotFoundException : public StorageException { public: using StorageException::StorageException; };
class ChunkCorruptionException : public StorageException { public: using StorageException::StorageException; };
class WALException : public StorageException { public: using StorageException::StorageException; };
class MetadataException : public StorageException { public: using StorageException::StorageException; };
}

