#include <unity.h>
#include "calibration.h"

using namespace autolee;

// Constants mirrored from AutoLee/config.h
static constexpr uint32_t CAL_SPEED_HZ = 8000;
static constexpr uint32_t CAL_ACCEL = 25000;
static const EarlyWindow EW{/*minTimeMs*/ 50, /*minMoveSteps*/ 200};

void setUp() {}
void tearDown() {}

void test_accel_timing() {
  // 8000*1000/25000 = 320 ms ; 8000^2/(2*25000) = 1280 steps
  TEST_ASSERT_EQUAL_UINT32(320, calAccelMs(CAL_SPEED_HZ, CAL_ACCEL));
  TEST_ASSERT_EQUAL_INT32(1280, calAccelDist(CAL_SPEED_HZ, CAL_ACCEL));
  TEST_ASSERT_EQUAL_UINT32(420, calIgnoreMs(CAL_SPEED_HZ, CAL_ACCEL));    // +100
  TEST_ASSERT_EQUAL_INT32(1024, calIgnoreDist(CAL_SPEED_HZ, CAL_ACCEL));  // 1280*8/10
}

void test_early_window_armed() {
  TEST_ASSERT_TRUE(earlyArmed(EW, 100, 500, false));
  TEST_ASSERT_FALSE(earlyArmed(EW, 40, 500, false));   // too early (<50ms)
  TEST_ASSERT_FALSE(earlyArmed(EW, 100, 100, false));  // too little move (<200)
}

// The early trip used to disarm at 300 ms / 1200 steps while the dynamic trip
// was not ready until ~620 ms at calibration speed: a stop hit in between was
// seen by neither. It now stays armed until the dynamic trip takes over.
void test_early_trip_has_no_gap_before_the_dynamic_trip() {
  TEST_ASSERT_TRUE(earlyArmed(EW, 450, 2000, false));
  TEST_ASSERT_TRUE(earlyArmed(EW, 600, 3000, false));
  TEST_ASSERT_FALSE(earlyArmed(EW, 600, 3000, true));
}

void test_baseline_ready() {
  uint32_t ignMs = calIgnoreMs(CAL_SPEED_HZ, CAL_ACCEL);       // 420
  int32_t ignDst = calIgnoreDist(CAL_SPEED_HZ, CAL_ACCEL);     // 1024
  TEST_ASSERT_FALSE(baselineReady(400, 2000, ignMs, ignDst));  // time not past
  TEST_ASSERT_FALSE(baselineReady(500, 1000, ignMs, ignDst));  // dist not past
  TEST_ASSERT_TRUE(baselineReady(500, 2000, ignMs, ignDst));   // both past
}

void test_baseline_average_clamps() {
  TEST_ASSERT_EQUAL_UINT16(0, baselineAverage(0, 0));          // no samples
  TEST_ASSERT_EQUAL_UINT16(50, baselineAverage(500, 10));      // 500/10
  TEST_ASSERT_EQUAL_UINT16(1023, baselineAverage(50000, 10));  // clamped
}

void test_baseline_sum_and_count_saturate_together() {
  BaselineAccumulator acc;
  for (int i = 0; i < BaselineAccumulator::kMaxSamples; i++) acc.add(100);
  // Past the cap, a run of high readings must not drag the average up.
  for (int i = 0; i < 500; i++) acc.add(1000);
  TEST_ASSERT_EQUAL_UINT16(BaselineAccumulator::kMaxSamples, acc.count());
  TEST_ASSERT_EQUAL_UINT16(100, acc.average());
  acc.reset();
  TEST_ASSERT_EQUAL_UINT16(0, acc.count());
}

void test_travel_shorter_than_two_guards_is_rejected() {
  TEST_ASSERT_TRUE(travelPlausible(0, 41000, 50));
  TEST_ASSERT_TRUE(travelPlausible(0, 100, 50));
  TEST_ASSERT_FALSE(travelPlausible(0, 99, 50));
  TEST_ASSERT_FALSE(travelPlausible(0, -300, 50));
}

void test_confirm_counter() {
  ConfirmCounter c(3);
  TEST_ASSERT_FALSE(c.feed(true));  // 1
  TEST_ASSERT_FALSE(c.feed(true));  // 2
  TEST_ASSERT_TRUE(c.feed(true));   // 3 -> confirmed
  c.reset();
  TEST_ASSERT_FALSE(c.feed(true));   // 1
  TEST_ASSERT_FALSE(c.feed(false));  // reset
  TEST_ASSERT_EQUAL_UINT8(0, c.count());
  TEST_ASSERT_FALSE(c.feed(true));  // 1 again
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_accel_timing);
  RUN_TEST(test_early_window_armed);
  RUN_TEST(test_early_trip_has_no_gap_before_the_dynamic_trip);
  RUN_TEST(test_baseline_sum_and_count_saturate_together);
  RUN_TEST(test_travel_shorter_than_two_guards_is_rejected);
  RUN_TEST(test_baseline_ready);
  RUN_TEST(test_baseline_average_clamps);
  RUN_TEST(test_confirm_counter);
  return UNITY_END();
}
