#pragma once
#include <cstddef>
#include <cstdint>

namespace firmingo { namespace protocol {
constexpr std::size_t header_size = 16;
constexpr std::size_t max_payload = 4096;
constexpr std::size_t max_frame = header_size + max_payload;
constexpr std::size_t min_offer = 512;
enum class Type : uint8_t { request = 1, response = 2, serial = 3, upload = 4, event = 5 };
enum class Decode { incomplete, ready, malformed, unsupported_version, oversized };
uint16_t get16(const uint8_t* bytes);
uint32_t get32(const uint8_t* bytes);
void put16(uint8_t* bytes, uint16_t value);
void put32(uint8_t* bytes, uint32_t value);
// Returns zero for invalid fields, insufficient capacity or missing nonempty
// input. Payload must not overlap destination. No JSON/operation validation.
std::size_t encode(uint8_t* destination, std::size_t capacity, Type type,
                   uint32_t request, const uint8_t* payload, std::size_t size);

// Owns one bounded frame, with no transport/platform dependencies or allocation.
// feed consumes at most needed(); the caller retains any coalesced tail.
// All methods run in one application context. Access fields only in READY state.
class Decoder {
 public:
  std::size_t feed(const uint8_t* bytes, std::size_t size);
  std::size_t needed() const;
  Decode state() const { return state_; }
  bool started() const { return used_ != 0; }
  Type type() const { return static_cast<Type>(bytes_[5]); }
  uint32_t request() const { return get32(bytes_ + 8); }
  uint32_t size() const { return get32(bytes_ + 12); }
  const uint8_t* payload() const { return bytes_ + header_size; }
  void reset(); // discards frame/error, retains negotiated limit
  // Change only between frames. Compiled device offer 4096; smaller host
  // offers >=512 reduce it. Larger offers retain the compiled device cap.
  bool negotiate(uint32_t peer_offer);
  std::size_t limit() const { return limit_; }
 private:
  uint8_t bytes_[max_frame]{};
  std::size_t used_ = 0, limit_ = max_payload;
  Decode state_ = Decode::incomplete;
};
} }  // namespace firmingo::protocol
