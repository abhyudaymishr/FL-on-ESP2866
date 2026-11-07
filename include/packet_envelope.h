#pragma once
#include <cstddef>
#include <cstdint>
namespace pifl {
constexpr size_t kPacketChecksumSize = 4;
// Development integrity checksum only. This is not cryptographic authentication.
size_t wrapPacket(const uint8_t* input, size_t length, uint8_t* output, size_t capacity, const char* key);
bool unwrapPacket(const uint8_t* input, size_t length, uint8_t* output, size_t capacity,
                  size_t& outputLength, const char* key);
}
