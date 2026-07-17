// ============================================================================
//  autolee_logic/sg_filter.h
//  Median-of-5 StallGuard glitch filter — pure, host-testable.
//  Mirrors the insertion-sort median in read_sg() (src/motion.cpp).
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

// Median of 5 samples. Takes a copy so the caller's buffer is not mutated.
inline uint16_t median5(uint16_t s[5]) {
  uint16_t a[5] = { s[0], s[1], s[2], s[3], s[4] };
  for (int i = 1; i < 5; i++) {
    uint16_t key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key) { a[j + 1] = a[j]; j--; }
    a[j + 1] = key;
  }
  return a[2];
}

}  // namespace autolee
