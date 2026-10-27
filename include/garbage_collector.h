#pragma once
#include <cstdint>
namespace flashforge { struct GCStats { std::uint64_t chunks_scanned{}, chunks_deleted{}, bytes_reclaimed{}; }; }

