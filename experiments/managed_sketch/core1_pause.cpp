#include "core1_pause.h"

#ifdef FIRMINGO_MANAGED_EMBEDDED
#include <hardware/sync.h>
#define FIRMINGO_PARK_RAM __attribute__((section(".time_critical.firmingo_core1_park"), noinline))
#else
#define FIRMINGO_PARK_RAM __attribute__((noinline))
#endif

namespace firmingo_managed {

bool Core1Pause::request() {
  // One core-0 owner calls this method, outside callbacks.
  if (requested_.load(std::memory_order_acquire) ||
      parked_.load(std::memory_order_acquire)) return false;
  requested_.store(1, std::memory_order_release);
  return true;
}

void Core1Pause::release() {
  requested_.store(0, std::memory_order_release);
}

bool Core1Pause::parked() const {
  return parked_.load(std::memory_order_acquire) != 0;
}

FIRMINGO_PARK_RAM void Core1Pause::park_if_requested() {
  if (!requested_.load(std::memory_order_acquire)) return;
#ifdef FIRMINGO_MANAGED_EMBEDDED
  const std::uint32_t irq_state = save_and_disable_interrupts();
#endif
  if (requested_.load(std::memory_order_acquire)) {
    parked_.store(1, std::memory_order_release);
    while (requested_.load(std::memory_order_acquire)) {
      // No flash fetches or helper calls are permitted in this loop.
      __asm__ volatile("" ::: "memory");
    }
    parked_.store(0, std::memory_order_release);
  }
#ifdef FIRMINGO_MANAGED_EMBEDDED
  restore_interrupts(irq_state);
#endif
}

}  // namespace firmingo_managed
