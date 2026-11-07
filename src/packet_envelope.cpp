#include "packet_envelope.h"
#include "protocol.h"
#include <string.h>
namespace pifl {
namespace {
uint32_t checksum(const uint8_t* b, size_t n, const char* key) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
  for (size_t i = 0; key[i]; ++i) h = (h ^ static_cast<uint8_t>(key[i])) * 16777619u;
  return h;
}
}
size_t wrapPacket(const uint8_t* in, size_t n, uint8_t* out, size_t cap, const char* key) {
  if (!in || !out || !key || n < kPacketHeaderSize || n > kMaxPacketSize || cap < n+kPacketChecksumSize) return 0;
  const uint32_t tag = checksum(in,n,key);
  memcpy(out,in,n);
  for (unsigned i=0; i<4; ++i) out[n+i] = static_cast<uint8_t>(tag >> (8*i));
  return n+kPacketChecksumSize;
}
bool unwrapPacket(const uint8_t* in, size_t n, uint8_t* out, size_t cap, size_t& outLength, const char* key) {
  outLength = 0;
  if (!in || !out || !key || n < kPacketHeaderSize+kPacketChecksumSize || n > kMaxPacketSize+kPacketChecksumSize) return false;
  const size_t payloadLength = n-kPacketChecksumSize;
  if (payloadLength > cap) return false;
  uint32_t tag=0;
  for (unsigned i=0; i<4; ++i) tag |= static_cast<uint32_t>(in[payloadLength+i]) << (8*i);
  if (tag != checksum(in,payloadLength,key)) return false;
  memcpy(out,in,payloadLength); outLength=payloadLength; return true;
}
}
