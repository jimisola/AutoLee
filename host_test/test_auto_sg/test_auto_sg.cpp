#include <unity.h>
#include "auto_sg.h"

using namespace autolee;

static constexpr AutoSgConfig kCfg{/*strokes*/ 3, /*margin*/ 1, /*tripMin*/ 0, /*tripMax*/ 1023};

void setUp() {}
void tearDown() {}

static StrokeStats stroke(uint16_t max, uint16_t floorReadings = 0) {
  StrokeStats s;
  for (uint16_t i = 0; i < floorReadings; i++) s.add(0);
  if (max > 1) {
    s.add(max / 2 > 1 ? max / 2 : 2);
    s.add(max);
  }
  return s;
}

// First stroke discarded, then kCfg.strokes measured ones.
static AutoSg::Step runProfile(AutoSg &a, uint16_t firstMax, uint16_t measuredMax) {
  AutoSg::Step step = a.strokeDone(stroke(firstMax));
  for (uint16_t i = 0; i < kCfg.strokes; i++) step = a.strokeDone(stroke(measuredMax));
  return step;
}

void test_measures_every_profile_then_finishes() {
  AutoSg a(kCfg);
  a.begin(3);
  TEST_ASSERT_TRUE(a.active());
  TEST_ASSERT_EQUAL(AutoSg::Step::NextProfile, runProfile(a, 10, 300));
  TEST_ASSERT_EQUAL_UINT8(1, a.profile());
  TEST_ASSERT_EQUAL(AutoSg::Step::NextProfile, runProfile(a, 10, 40));
  TEST_ASSERT_EQUAL(AutoSg::Step::Done, runProfile(a, 10, 12));
  TEST_ASSERT_FALSE(a.active());
  TEST_ASSERT_EQUAL_UINT16(301, a.tripFor(0));
  TEST_ASSERT_EQUAL_UINT16(41, a.tripFor(1));
  TEST_ASSERT_EQUAL_UINT16(13, a.tripFor(2));
}

// The first stroke starts wherever the ram was; its readings must not count.
void test_first_stroke_of_a_profile_is_discarded() {
  AutoSg a(kCfg);
  a.begin(1);
  TEST_ASSERT_EQUAL(AutoSg::Step::Done, runProfile(a, 900, 50));
  TEST_ASSERT_EQUAL_UINT16(50, a.measured(0));
}

void test_highest_stroke_wins() {
  AutoSg a(kCfg);
  a.begin(1);
  a.strokeDone(stroke(5));
  a.strokeDone(stroke(40));
  a.strokeDone(stroke(70));
  TEST_ASSERT_EQUAL(AutoSg::Step::Done, a.strokeDone(stroke(55)));
  TEST_ASSERT_EQUAL_UINT16(70, a.measured(0));
}

void test_stroke_progress_excludes_the_discarded_one() {
  AutoSg a(kCfg);
  a.begin(3);
  TEST_ASSERT_EQUAL_UINT16(0, a.stroke());
  a.strokeDone(stroke(5));
  TEST_ASSERT_EQUAL_UINT16(0, a.stroke());
  a.strokeDone(stroke(5));
  TEST_ASSERT_EQUAL_UINT16(1, a.stroke());
}

// Only floor readings is a clean measurement at speed on a low supply
// voltage: max 1, trip 1 + margin, flagged.
void test_floor_only_profile_measures_as_one() {
  AutoSg a(kCfg);
  a.begin(1);
  a.strokeDone(stroke(0, 10));
  for (int i = 0; i < 3; i++) a.strokeDone(stroke(0, 10));
  TEST_ASSERT_FALSE(a.active());
  TEST_ASSERT_TRUE(a.floorOnly(0));
  TEST_ASSERT_EQUAL_UINT16(1, a.measured(0));
  TEST_ASSERT_EQUAL_UINT16(2, a.tripFor(0));
  TEST_ASSERT_TRUE(tripAtFloor(a.tripFor(0), floorTripMax(kCfg)));
}

// Nothing sampled at all: the blanking windows cover the whole stroke. That
// is not a measurement, and must not become a trip.
void test_no_samples_fails() {
  AutoSg a(kCfg);
  a.begin(3);
  TEST_ASSERT_EQUAL(AutoSg::Step::Failed, runProfile(a, 0, 0));
  TEST_ASSERT_FALSE(a.active());
}

void test_trip_is_clamped_to_the_range() {
  AutoSg a(AutoSgConfig{3, 1, 0, 1023});
  a.begin(1);
  runProfile(a, 5, 1023);
  TEST_ASSERT_EQUAL_UINT16(1023, a.tripFor(0));
}

void test_aborted_run_takes_no_more_strokes() {
  AutoSg a(kCfg);
  a.begin(3);
  a.abort();
  TEST_ASSERT_FALSE(a.active());
  TEST_ASSERT_EQUAL(AutoSg::Step::Failed, a.strokeDone(stroke(50)));
}

void test_all_trips_set_needs_every_profile() {
  const uint16_t set[3] = {301, 41, 13};
  const uint16_t oneMissing[3] = {301, 0, 13};
  TEST_ASSERT_TRUE(allTripsSet(3, [&](uint8_t i) { return set[i]; }));
  TEST_ASSERT_FALSE(allTripsSet(3, [&](uint8_t i) { return oneMissing[i]; }));
}

void test_floor_trip_classification() {
  TEST_ASSERT_FALSE(tripAtFloor(0, 2));  // not set is not "limited"
  TEST_ASSERT_TRUE(tripAtFloor(1, 2));
  TEST_ASSERT_TRUE(tripAtFloor(2, 2));
  TEST_ASSERT_FALSE(tripAtFloor(3, 2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_measures_every_profile_then_finishes);
  RUN_TEST(test_first_stroke_of_a_profile_is_discarded);
  RUN_TEST(test_highest_stroke_wins);
  RUN_TEST(test_stroke_progress_excludes_the_discarded_one);
  RUN_TEST(test_floor_only_profile_measures_as_one);
  RUN_TEST(test_no_samples_fails);
  RUN_TEST(test_trip_is_clamped_to_the_range);
  RUN_TEST(test_aborted_run_takes_no_more_strokes);
  RUN_TEST(test_all_trips_set_needs_every_profile);
  RUN_TEST(test_floor_trip_classification);
  return UNITY_END();
}
