#include <unity.h>
#include "batch.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

void test_counter_increments() {
  TEST_ASSERT_EQUAL_INT32(1, incCounter(0));
  TEST_ASSERT_EQUAL_INT32(4322, incCounter(4321));
}

void test_counter_caps_at_9999() {
  TEST_ASSERT_EQUAL_INT32(9999, incCounter(9998) + 0);  // 9999
  TEST_ASSERT_EQUAL_INT32(9999, incCounter(9999));      // stays
}

void test_batch_complete() {
  TEST_ASSERT_FALSE(batchComplete(4, 5));
  TEST_ASSERT_TRUE(batchComplete(5, 5));
  TEST_ASSERT_TRUE(batchComplete(6, 5));
}

void test_batch_remaining_clamps() {
  TEST_ASSERT_EQUAL_INT32(3, batchRemaining(2, 5));
  TEST_ASSERT_EQUAL_INT32(0, batchRemaining(5, 5));
  TEST_ASSERT_EQUAL_INT32(0, batchRemaining(9, 5));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_counter_increments);
  RUN_TEST(test_counter_caps_at_9999);
  RUN_TEST(test_batch_complete);
  RUN_TEST(test_batch_remaining_clamps);
  return UNITY_END();
}
