#pragma once

#include "abi.h"

// Candidate FAPI two preserves the call offsets of ABI one, with a distinct
// version/size and an appended resident-owned RAM contract. Never pass it off as
// an ABI-one table. This allocation proof does not enable an FMS2 upload target.
struct FirmingoSketchApiV2 {
  FirmingoSketchApiV1 calls;
  uint8_t* state_address;
  uint32_t state_capacity_bytes;
  uint32_t maximum_capsule_bytes;
};

constexpr uint32_t kFirmingoStateCapacityBytes = 4096;
