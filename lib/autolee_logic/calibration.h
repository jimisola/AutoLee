// ============================================================================
//  autolee_logic/calibration.h
//  Pure sensorless-calibration decision logic — host-testable.
//  Mirrors the timing/threshold math and the early/baseline/dynamic hit
//  detection in move_until_stall() (src/motion.cpp). The actual stepper
//  moves stay in the firmware; the *decisions* live here.
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

// Accel ramp time (ms) and distance (steps) at the calibration speed/accel.
inline uint32_t calAccelMs(uint32_t speedHz, uint32_t accel) {
  return (uint32_t)((uint64_t)speedHz * 1000ULL / (uint64_t)accel);
}
inline int32_t calAccelDist(uint32_t speedHz, uint32_t accel) {
  return (int32_t)((uint64_t)speedHz * (uint64_t)speedHz / (2ULL * (uint64_t)accel));
}

// SG is ignored until past this time AND distance (accel blanking during ramp-up).
inline uint32_t calIgnoreMs(uint32_t speedHz, uint32_t accel) {
  return calAccelMs(speedHz, accel) + 100;
}
inline int32_t calIgnoreDist(uint32_t speedHz, uint32_t accel) {
  return (calAccelDist(speedHz, accel) * 8) / 10;
}

// Early-trip window: catch a stall very close to the start (short move + low SG).
struct EarlyWindow {
  uint32_t windowMs;     // EARLY_WINDOW_MS
  int32_t windowDstMax;  // EARLY_WINDOW_DST_MAX
  uint32_t minTimeMs;    // EARLY_MIN_TIME_MS
  int32_t minMoveSteps;  // EARLY_MIN_MOVE_STEPS
};
inline bool earlyArmed(const EarlyWindow &w, uint32_t elapsedMs, int32_t dist) {
  return elapsedMs <= w.windowMs && dist <= w.windowDstMax && elapsedMs >= w.minTimeMs &&
         dist >= w.minMoveSteps;
}

// Baseline sampling starts once past the ignore windows.
inline bool baselineReady(uint32_t elapsedMs, int32_t dist, uint32_t ignoreMs, int32_t ignoreDist) {
  return elapsedMs > ignoreMs && dist > ignoreDist;
}

// Baseline SG average, clamped to the 10-bit SG range.
inline uint16_t baselineAverage(uint32_t sum, uint16_t cnt) {
  if (cnt == 0) return 0;
  uint32_t avg = sum / cnt;
  return (uint16_t)(avg < 1023 ? avg : 1023);
}

// Consecutive-confirmation counter (the "++confirm >= N ? hit : reset" pattern
// used by the early, dynamic, home, and creep-home stall checks).
class ConfirmCounter {
 public:
  explicit ConfirmCounter(uint8_t needed) : needed_(needed) {}
  void reset() { count_ = 0; }
  // Feed a per-sample condition; returns true when it has held `needed` times.
  bool feed(bool condition) {
    if (condition) return ++count_ >= needed_;
    count_ = 0;
    return false;
  }
  uint8_t count() const { return count_; }

 private:
  uint8_t needed_;
  uint8_t count_ = 0;
};

}  // namespace autolee
