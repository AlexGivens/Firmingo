#pragma once

#include <cstddef>
#include <cstdint>

namespace firmingo_managed {
namespace stateful {

// Candidate only: opt-in FMS2 runtimes; consumer recipes require explicit adoption.
constexpr std::uint16_t kImageVersion = 2;
constexpr std::uint32_t kHeaderBytes = 128;
constexpr std::uint32_t kMaximumCapsuleBytes = 4096;

// Trusted resident facts, never populated from an image. The portable validator
// cannot establish ownership: the port must reserve RAM and supply exact facts.
struct Contract {
  std::uint32_t api_address;
  std::uint32_t state_address;
  std::uint32_t state_capacity_bytes;
  std::uint32_t maximum_capsule_bytes;
};

// One caller-owned, writable window mapped to the contract's target address.
// Native tests use owned host storage with a synthetic mapping, not MCU pointers.
struct Arena {
  std::uint8_t* data;
  std::size_t bytes;
  std::uint32_t mapped_address;
};

enum class Error {
  ok, invalid_contract, short_header, bad_magic, unsupported_version,
  bad_header_size, wrong_board, wrong_api, wrong_code_address, bad_code_size,
  bad_entry, bad_data_layout, bad_state_layout, bad_image_size,
  nonzero_reserved, header_hash_mismatch, payload_hash_mismatch,
  wrong_arena, overlapping_storage, active_generation, bad_generation,
};

// Validated byte extents and Thumb entries. Failure results always contain zeros.
// This is a report, not a capability that bypasses validation at initialization.
struct ImageInfo {
  std::uint32_t image_bytes;
  std::uint32_t code_bytes;
  std::uint32_t setup_address;
  std::uint32_t loop_address;
  std::uint32_t data_load_offset;
  std::uint32_t data_bytes;
  std::uint32_t bss_bytes;
};

struct Result {
  Error error;
  ImageInfo image;
};

// Exact capsule span only, at most the resident's declared upload maximum.
// Validates metadata and payload hashes without writing memory or executing code.
Result validate_image(const std::uint8_t* image, std::size_t size,
                      const Contract& contract);

// Single runtime owner, called only after the old sketch core is quiescent.
// stop() revokes activity but does not itself park a core or erase RAM.
// begin() revalidates, clears the entire owned arena and copies initialized data
// before publishing activity. It never calls setup/loop or performs a flash write.
class Generation {
 public:
  explicit Generation(Arena arena) : arena_(arena) {}
  Generation(const Generation&) = delete;
  Generation& operator=(const Generation&) = delete;
  Result begin(const std::uint8_t* image, std::size_t size,
               const Contract& contract, std::uint32_t generation);
  void stop() { active_ = false; }
  bool active() const { return active_; }
  std::uint32_t last_generation() const { return last_generation_; }

 private:
  Arena arena_;
  bool active_ = false;
  // Strictly increasing and nonzero; exhaustion requires a new owner after reboot.
  std::uint32_t last_generation_ = 0;
};

}  // namespace stateful
}  // namespace firmingo_managed
