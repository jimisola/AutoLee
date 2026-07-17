// ============================================================================
//  autolee_logic/batch.h
//  Pure stroke-counter / batch-run math — host-testable.
//  Mirrors the counter + batch handling in handleMotion() (AutoLee/motion.h).
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

// Stroke counter increments once per completed down-stroke, capped at 9999.
inline long incCounter(long counter) {
  return (counter < 9999) ? counter + 1 : counter;
}

// A batch is complete once the stroke count reaches the target.
inline bool batchComplete(int32_t count, int32_t target) {
  return count >= target;
}

// Remaining strokes for the UI, never negative.
inline int32_t batchRemaining(int32_t count, int32_t target) {
  int32_t r = target - count;
  return r > 0 ? r : 0;
}

}  // namespace autolee
