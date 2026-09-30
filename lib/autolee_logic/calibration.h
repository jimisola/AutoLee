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

// Early (absolute) trip: armed after a short minimum time and move, and kept
// armed until the dynamic trip takes over, so no stretch of the search is blind.
struct EarlyWindow {
  uint32_t minTimeMs;    // EARLY_MIN_TIME_MS
  int32_t minMoveSteps;  // EARLY_MIN_MOVE_STEPS
};
inline bool earlyArmed(const EarlyWindow &w, uint32_t elapsedMs, int32_t dist, bool dynReady) {
  return !dynReady && elapsedMs >= w.minTimeMs && dist >= w.minMoveSteps;
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

// No-load baseline accumulator. Sum and count saturate together; letting the
// sum run on past a capped count skews the average upward.
class BaselineAccumulator {
 public:
  static constexpr uint16_t kMaxSamples = 1000;
  void reset() {
    sum_ = 0;
    cnt_ = 0;
  }
  void add(uint16_t sg) {
    if (cnt_ >= kMaxSamples) return;
    sum_ += sg;
    cnt_++;
  }
  uint16_t count() const { return cnt_; }
  uint16_t average() const { return baselineAverage(sum_, cnt_); }

 private:
  uint32_t sum_ = 0;
  uint16_t cnt_ = 0;
};

// Two "stops" closer together than this are a false hit, not a press.
inline bool travelPlausible(long rawUp, long rawDown, int32_t guard) {
  return (rawDown - rawUp) >= 2L * guard;
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
