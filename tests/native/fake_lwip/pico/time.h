#pragma once
#include <stdint.h>
uint64_t get_absolute_time(void);
static inline uint64_t to_us_since_boot(uint64_t value) { return value; }
