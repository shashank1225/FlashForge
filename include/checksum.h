#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace flashforge {
std::string sha256(const std::vector<std::uint8_t>& data);
std::uint32_t crc32(const std::vector<std::uint8_t>& data);
}

