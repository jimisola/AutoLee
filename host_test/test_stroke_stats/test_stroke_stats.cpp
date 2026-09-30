#include <unity.h>
#include "stroke_stats.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

void test_fresh_stats_are_empty() {
  StrokeStats s;
  TEST_ASSERT_TRUE(s.empty());
  TEST_ASSERT_TRUE(s.onlyFloor());
}

void test_min_and_max_track_the_readings() {
  StrokeStats s;
  s.add(40);
  s.add(90);
  s.add(25);
  TEST_ASSERT_EQUAL_UINT16(90, s.max());
  TEST_ASSERT_EQUAL_UINT16(25, s.min());
  TEST_ASSERT_FALSE(s.onlyFloor());
}

void test_add_reports_a_new_maximum() {
  StrokeStats s;
  TEST_ASSERT_TRUE(s.add(40));
  TEST_ASSERT_FALSE(s.add(30));
  TEST_ASSERT_FALSE(s.add(40));
  TEST_ASSERT_TRUE(s.add(41));
}

// 0/1 is the clean-running value at speed on a low supply voltage: counted,
// but kept out of min/max so they describe the readings off the floor.
void test_floor_readings_are_counted_not_ranged() {
  StrokeStats s;
  TEST_ASSERT_FALSE(s.add(0));
  TEST_ASSERT_FALSE(s.add(1));
  TEST_ASSERT_EQUAL_UINT16(2, s.floor());
  TEST_ASSERT_FALSE(s.empty());
  TEST_ASSERT_TRUE(s.onlyFloor());
  s.add(7);
  TEST_ASSERT_EQUAL_UINT16(7, s.min());
  TEST_ASSERT_EQUAL_UINT16(7, s.max());
}

void test_floor_count_saturates() {
  StrokeStats s;
  for (uint32_t i = 0; i < 70000; i++) s.add(0);
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, s.floor());
}

void test_reset_clears_everything() {
  StrokeStats s;
  s.add(0);
  s.add(50);
  s.reset();
  TEST_ASSERT_TRUE(s.empty());
  TEST_ASSERT_EQUAL_UINT16(0, s.floor());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_stats_are_empty);
  RUN_TEST(test_min_and_max_track_the_readings);
  RUN_TEST(test_add_reports_a_new_maximum);
  RUN_TEST(test_floor_readings_are_counted_not_ranged);
  RUN_TEST(test_floor_count_saturates);
  RUN_TEST(test_reset_clears_everything);
  return UNITY_END();
}
