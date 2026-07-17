#include <unity.h>
#include "sg_blanking.h"

using namespace autolee;

// Constants mirrored from AutoLee/config.h
static constexpr uint32_t RUN_DECEL = 800000;
static constexpr int32_t WORK_ZONE = 5500;
static constexpr uint8_t CAL_REL_DROP_Q8 = 235;
static constexpr uint16_t CAL_ABS_MIN = 12;

void setUp() {}
void tearDown() {}

void test_accel_blank_ms() {
  // 35000 * 1000 / 800000 = 43 (floor), + 80 = 123
  TEST_ASSERT_EQUAL_UINT32(123, accelBlankMs(35000, RUN_DECEL));
  // 15000 * 1000 / 800000 = 18, + 80 = 98
  TEST_ASSERT_EQUAL_UINT32(98, accelBlankMs(15000, RUN_DECEL));
}

void test_decel_blank_steps() {
  // 35000^2 / (2*800000) = 765 (floor), + 500 = 1265
  TEST_ASSERT_EQUAL_INT32(1265, decelBlankSteps(35000, RUN_DECEL));
  // 45000^2 / 1600000 = 1265 (floor), + 500 = 1765
  TEST_ASSERT_EQUAL_INT32(1765, decelBlankSteps(45000, RUN_DECEL));
}

void test_work_zone_predicate() {
  long down = 9500;
  TEST_ASSERT_TRUE(inWorkZone(down, down, WORK_ZONE));               // at endpoint
  TEST_ASSERT_TRUE(inWorkZone(down - 5499, down, WORK_ZONE));        // just inside
  TEST_ASSERT_FALSE(inWorkZone(down - WORK_ZONE, down, WORK_ZONE));  // exactly = -> false
  TEST_ASSERT_FALSE(inWorkZone(down - 9000, down, WORK_ZONE));       // far away
}

void test_dynamic_trip() {
  // baseline 100: 100*235>>8 = 91, above absMin -> 91
  TEST_ASSERT_EQUAL_UINT16(91, dynamicTrip(100, CAL_REL_DROP_Q8, CAL_ABS_MIN));
  // baseline 10: 10*235>>8 = 9, below absMin -> floored to 12
  TEST_ASSERT_EQUAL_UINT16(12, dynamicTrip(10, CAL_REL_DROP_Q8, CAL_ABS_MIN));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_accel_blank_ms);
  RUN_TEST(test_decel_blank_steps);
  RUN_TEST(test_work_zone_predicate);
  RUN_TEST(test_dynamic_trip);
  return UNITY_END();
}
