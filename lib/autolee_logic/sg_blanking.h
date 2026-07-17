// ============================================================================
//  autolee_logic/sg_blanking.h
//  Pure StallGuard blanking / trip formulas — host-testable.
//  Mirrors the accel/decel/work-zone windows in handleMotion() and the
//  dynamic-trip calc in move_until_stall() (AutoLee/motion.h).
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

// Runtime accel blanking window (ms): v/a scaled + fixed 80 ms margin.
// From handleMotion(): ui_speed_hz * 1000 / RUN_DECEL + 80.
inline uint32_t accelBlankMs(uint32_t speedHz, uint32_t decel) {
  return (uint32_t)((uint64_t)speedHz * 1000ULL / (uint64_t)decel) + 80;
}

// Position-based decel blanking distance (steps): v^2/(2a) + 500 margin.
// From handleMotion(): decelDist + 500.
inline int32_t decelBlankSteps(uint32_t speedHz, uint32_t decel) {
  int32_t decelDist =
      (int32_t)((uint64_t)speedHz * (uint64_t)speedHz / (2ULL * (uint64_t)decel));
  return decelDist + 500;
}

// Work-zone predicate: SG is blanked within workZoneSteps of the DOWN endpoint
// (heading DOWN) where tool resistance is normal.
inline bool inWorkZone(long pos, long endpointDown, int32_t workZoneSteps) {
  long d = pos - endpointDown;
  if (d < 0) d = -d;
  return d < (long)workZoneSteps;
}

// Dynamic calibration trip: relative drop from baseline (Q8 fixed point),
// floored at absMin. From move_until_stall(): max(baseline*relDropQ8>>8, absMin).
inline uint16_t dynamicTrip(uint16_t baseline, uint8_t relDropQ8, uint16_t absMin) {
  uint16_t rel = (uint16_t)(((uint32_t)baseline * (uint32_t)relDropQ8) >> 8);
  return rel > absMin ? rel : absMin;
}

}  // namespace autolee
