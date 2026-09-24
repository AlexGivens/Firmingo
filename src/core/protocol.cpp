#include "protocol.h"
#include <algorithm>
#include <cstring>

namespace firmingo { namespace protocol {
uint16_t get16(const uint8_t* b) { return (uint16_t(b[0]) << 8) | uint16_t(b[1]); }
uint32_t get32(const uint8_t* b) {
  return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}
void put16(uint8_t* b, uint16_t v) { b[0] = uint8_t(v >> 8); b[1] = uint8_t(v); }
void put32(uint8_t* b, uint32_t v) {
  for (unsigned i = 0; i != 4; ++i) b[i] = uint8_t(v >> (24 - 8 * i));
}
static bool valid_fields(uint8_t type, uint32_t request) {
  if (type < 1 || type > 5) return false;
  return (type == 3 || type == 5) ? request == 0 : request != 0;
}
std::size_t encode(uint8_t* b, std::size_t capacity, Type type, uint32_t request,
                   const uint8_t* payload, std::size_t size) {
  if (!b || size > max_payload || capacity < header_size + size || (size && !payload) ||
      !valid_fields(uint8_t(type),request)) return 0;
  b[0] = 'F'; b[1] = 'M'; b[2] = 'G'; b[3] = 'O'; b[4] = 1; b[5] = uint8_t(type);
  put16(b + 6, 0); put32(b + 8, request); put32(b + 12, uint32_t(size));
  if (size) std::memcpy(b + header_size, payload, size);
  return header_size + size;
}
void Decoder::reset() { used_ = 0; state_ = Decode::incomplete; }
bool Decoder::negotiate(uint32_t peer_offer) {
  if (started() || state_ != Decode::incomplete || peer_offer < min_offer || peer_offer > 65536) return false;
  limit_ = std::min<std::size_t>(max_payload, peer_offer); return true;
}
std::size_t Decoder::needed() const {
  if (state_ != Decode::incomplete) return 0;
  return used_ < header_size ? header_size - used_ : header_size + size() - used_;
}
std::size_t Decoder::feed(const uint8_t* bytes, std::size_t input_size) {
  const auto count = std::min(input_size, needed());
  if (!count || !bytes) return 0;
  std::memcpy(bytes_ + used_, bytes, count); used_ += count;
  if (used_ >= header_size) {
    if (std::memcmp(bytes_, "FMGO", 4) || get16(bytes_ + 6) || !valid_fields(bytes_[5],request())) state_ = Decode::malformed;
    else if (bytes_[4] != 1) state_ = Decode::unsupported_version;
    else if (size() > limit_) state_ = Decode::oversized;
    else if (used_ == header_size + size()) state_ = Decode::ready;
  }
  return count;
}
} }  // namespace firmingo::protocol
