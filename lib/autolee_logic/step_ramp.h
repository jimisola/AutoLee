// Step timing with constant acceleration from rest - the profile
// FastAccelStepper gives the Arduino firmware, and the one the press's tuning
// (StallGuard blanking windows, trips, time-outs) was made against. One call
// yields one step; the stepper driver turns the durations into pulses.
#pragma once

#include <cmath>
#include <cstdint>

namespace autolee {

struct StepRamp {
  int32_t pos = 0;  // position after the steps yielded so far
  int8_t dir = 0;   // +1 / -1 while moving, 0 at rest
  float v = 0.0f;   // speed at `pos`, steps/s
};

// Next step towards `target`, with top speed `vmax` (steps/s) and acceleration
// `accel` (steps/s^2). Returns the step's duration in seconds and advances
// `r`, or 0 when at rest on the target.
//
// Speed changes by exactly `accel` per unit time: v^2 moves by 2*accel per
// step, and a step lasts 2 / (v_before + v_after), so reaching speed v from
// rest takes v / accel. A short move peaks below `vmax` rather than hitting it
// harder, and a target behind the direction of travel is reached by
// decelerating to rest first - overshooting by the stopping distance - and
// then reversing, as FastAccelStepper does for a moveTo() during a move.
inline float nextStep(StepRamp &r, int32_t target, float vmax, float accel) {
  if (r.dir == 0 || r.v <= 0.0f) {
    r.v = 0.0f;
    if (target == r.pos) {
      r.dir = 0;
      return 0.0f;
    }
    r.dir = target > r.pos ? 1 : -1;
  }

  const int32_t remaining = (target - r.pos) * r.dir;  // steps left in the current direction
  if (remaining == 0) {  // on target: the last decel step leaves at most one step's worth of speed
    r.v = 0.0f;
    r.dir = 0;
    return 0.0f;
  }

  const float twoA = 2.0f * accel;
  const float v2 = r.v * r.v;
  const float vmax2 = vmax * vmax;
  const float stopSteps = std::ceil(v2 / twoA);

  float next2;
  if (remaining < 0 || (float)remaining <= stopSteps) {
    next2 = v2 - twoA;  // brake: for the target, or before reversing
    if (next2 < 0.0f) next2 = 0.0f;
  } else if (v2 < vmax2) {
    next2 = v2 + twoA;
    if (next2 > vmax2) next2 = vmax2;
  } else if (v2 > vmax2) {  // top speed lowered mid-move
    next2 = v2 - twoA;
    if (next2 < vmax2) next2 = vmax2;
  } else {
    next2 = v2;
  }

  const float next = next2 == v2 ? r.v : std::sqrt(next2);
  if (r.v + next <= 0.0f) {  // vmax or accel of 0: no step can be timed
    r.dir = 0;
    return 0.0f;
  }
  const float seconds = 2.0f / (r.v + next);
  r.v = next;
  r.pos += r.dir;
  return seconds;
}

}  // namespace autolee
