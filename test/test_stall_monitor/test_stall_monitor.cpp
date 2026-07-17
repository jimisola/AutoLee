#include <unity.h>
#include "stall_monitor.h"

using namespace autolee;

// RUN_SG_HIGH_NEEDED = 2 in AutoLee/config.h
static constexpr uint8_t NEEDED = 2;
static constexpr uint16_t TRIP = 15;

void setUp() {}
void tearDown() {}

void test_two_consecutive_highs_trigger_jam() {
  StallCounter sc(NEEDED);
  TEST_ASSERT_FALSE(sc.update(100, TRIP));  // high #1
  TEST_ASSERT_TRUE(sc.update(100, TRIP));   // high #2 -> jam
}

void test_single_high_then_lows_no_jam() {
  StallCounter sc(NEEDED);
  TEST_ASSERT_FALSE(sc.update(100, TRIP));  // high -> 1
  TEST_ASSERT_FALSE(sc.update(0, TRIP));    // low 1
  TEST_ASSERT_FALSE(sc.update(0, TRIP));    // low 2
  TEST_ASSERT_FALSE(sc.update(0, TRIP));    // low 3 -> decay high to 0
  TEST_ASSERT_EQUAL_UINT8(0, sc.highCount());
}

void test_high_count_is_capped() {
  StallCounter sc(NEEDED);
  for (int i = 0; i < 20; i++) sc.update(100, TRIP);
  TEST_ASSERT_EQUAL_UINT8(NEEDED + 4, sc.highCount());  // capped at 6
}

void test_low_decay_every_three() {
  StallCounter sc(NEEDED);
  sc.update(100, TRIP);        // high -> 1 (below needed after reset chain)
  // push high up to 3 to observe decay clearly
  sc.update(100, TRIP);        // -> jam true but keep going; high=2
  sc.update(100, TRIP);        // high=3
  TEST_ASSERT_EQUAL_UINT8(3, sc.highCount());
  sc.update(0, TRIP);          // low 1
  sc.update(0, TRIP);          // low 2
  TEST_ASSERT_EQUAL_UINT8(3, sc.highCount());  // not yet
  sc.update(0, TRIP);          // low 3 -> decay
  TEST_ASSERT_EQUAL_UINT8(2, sc.highCount());
  TEST_ASSERT_EQUAL_UINT8(0, sc.lowCount());
}

void test_reset() {
  StallCounter sc(NEEDED);
  sc.update(100, TRIP);
  sc.reset();
  TEST_ASSERT_EQUAL_UINT8(0, sc.highCount());
  TEST_ASSERT_EQUAL_UINT8(0, sc.lowCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_consecutive_highs_trigger_jam);
  RUN_TEST(test_single_high_then_lows_no_jam);
  RUN_TEST(test_high_count_is_capped);
  RUN_TEST(test_low_decay_every_three);
  RUN_TEST(test_reset);
  return UNITY_END();
}
