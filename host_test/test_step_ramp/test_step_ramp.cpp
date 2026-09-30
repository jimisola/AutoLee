#include <unity.h>
#include <cmath>
#include "step_ramp.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

struct Run {
  int32_t steps = 0;
  double seconds = 0;
  float peak = 0;
  double secondsToPeak = 0;
};

// Drives the ramp to `target` and records what the motor would see.
static Run run(StepRamp &r, int32_t target, float vmax, float accel) {
  Run out;
  for (int guard = 0; guard < 2000000; guard++) {
    const float t = nextStep(r, target, vmax, accel);
    if (t <= 0.0f) break;
    out.steps++;
    out.seconds += t;
    if (r.v > out.peak) {
      out.peak = r.v;
      out.secondsToPeak = out.seconds;
    }
  }
  return out;
}

void test_lands_exactly_on_target_and_stops() {
  StepRamp r;
  const Run m = run(r, 20000, 30000, 800000);
  TEST_ASSERT_EQUAL_INT32(20000, m.steps);
  TEST_ASSERT_EQUAL_INT32(20000, r.pos);
  TEST_ASSERT_EQUAL_INT8(0, r.dir);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, nextStep(r, 20000, 30000, 800000));
}

void test_negative_moves_count_down() {
  StepRamp r;
  r.pos = 500;
  const Run m = run(r, -1500, 8000, 25000);
  TEST_ASSERT_EQUAL_INT32(2000, m.steps);
  TEST_ASSERT_EQUAL_INT32(-1500, r.pos);
}

// Reaching top speed takes exactly v / a - what the SG accel-blanking window
// (accelBlankMs = v/a + 80 ms) assumes. The old ramp needed 280 ms to get to
// 30 kHz at 800000 steps/s^2, blanking out only its first ~200 Hz.
void test_time_to_top_speed_is_v_over_a() {
  const struct {
    float vmax, accel;
  } cases[] = {{30000, 800000}, {40000, 800000}, {15000, 800000}, {8000, 25000}};
  for (const auto &c : cases) {
    StepRamp r;
    const Run m = run(r, 200000, c.vmax, c.accel);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, c.vmax, m.peak);
    TEST_ASSERT_FLOAT_WITHIN(0.002, c.vmax / c.accel, m.secondsToPeak);
  }
}

// A short move peaks below top speed instead of accelerating harder: the
// 300-step back-off at 8 kHz / 25000 steps/s^2 used to hit 8 kHz regardless
// (~17x the set acceleration); FastAccelStepper peaks at sqrt(a * n).
void test_short_move_caps_its_peak_speed() {
  StepRamp r;
  const Run m = run(r, 300, 8000, 25000);
  TEST_ASSERT_EQUAL_INT32(300, m.steps);
  TEST_ASSERT_FLOAT_WITHIN(60.0f, std::sqrt(25000.0f * 300.0f), m.peak);
}

// No step changes speed faster than the set acceleration.
void test_acceleration_never_exceeds_the_setting() {
  StepRamp r;
  const float accel = 25000;
  float prevV = 0;
  for (int i = 0; i < 20000; i++) {
    const float before = r.v;
    const float t = nextStep(r, 20000, 8000, accel);
    if (t <= 0.0f) break;
    const float rate = std::fabs(r.v - before) / t;
    TEST_ASSERT_TRUE_MESSAGE(rate <= accel * 1.001f, "step accelerates harder than the setting");
    prevV = r.v;
  }
  (void)prevV;
}

// A moveTo() behind the direction of travel - a graceful stop issued during a
// downstroke - brakes to rest at the set rate, then reverses. It used to jump
// straight from cruise to 100 Hz the other way.
void test_retarget_behind_brakes_then_reverses() {
  StepRamp r;
  const float vmax = 30000, accel = 800000;
  for (int i = 0; i < 5000; i++) nextStep(r, 20000, vmax, accel);  // at cruise, heading up
  TEST_ASSERT_FLOAT_WITHIN(1.0f, vmax, r.v);
  const int32_t at = r.pos;

  int32_t furthest = at;
  float prev = r.v;
  bool reversed = false;
  for (int i = 0; i < 100000; i++) {
    const float t = nextStep(r, 0, vmax, accel);
    if (t <= 0.0f) break;
    if (r.dir > 0) {
      TEST_ASSERT_TRUE(r.v <= prev);  // only slowing while still going the old way
      furthest = r.pos;
    } else {
      reversed = true;
    }
    prev = r.v;
  }
  TEST_ASSERT_TRUE(reversed);
  TEST_ASSERT_EQUAL_INT32(0, r.pos);
  // Overshoot is the stopping distance v^2 / 2a (563 steps), not zero.
  TEST_ASSERT_INT_WITHIN(2, (int)std::ceil(vmax * vmax / (2 * accel)), furthest - at);
}

void test_retarget_ahead_keeps_going() {
  StepRamp r;
  for (int i = 0; i < 1000; i++) nextStep(r, 20000, 30000, 800000);
  const float v = r.v;
  nextStep(r, 40000, 30000, 800000);
  TEST_ASSERT_TRUE(r.v >= v);
  run(r, 40000, 30000, 800000);
  TEST_ASSERT_EQUAL_INT32(40000, r.pos);
}

void test_zero_speed_or_acceleration_yields_no_step() {
  StepRamp r;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, nextStep(r, 100, 0, 25000));
  TEST_ASSERT_EQUAL_INT32(0, r.pos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lands_exactly_on_target_and_stops);
  RUN_TEST(test_negative_moves_count_down);
  RUN_TEST(test_time_to_top_speed_is_v_over_a);
  RUN_TEST(test_short_move_caps_its_peak_speed);
  RUN_TEST(test_acceleration_never_exceeds_the_setting);
  RUN_TEST(test_retarget_behind_brakes_then_reverses);
  RUN_TEST(test_retarget_ahead_keeps_going);
  RUN_TEST(test_zero_speed_or_acceleration_yields_no_step);
  return UNITY_END();
}
