#include <unity.h>
#include "sg_filter.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

void test_median_of_shuffled() {
  uint16_t s[5] = {5, 1, 3, 2, 4};
  TEST_ASSERT_EQUAL_UINT16(3, median5(s));
}

void test_median_with_duplicates() {
  uint16_t s[5] = {7, 7, 2, 2, 9};
  TEST_ASSERT_EQUAL_UINT16(7, median5(s));  // sorted: 2 2 7 7 9 -> 7
}

void test_median_all_same() {
  uint16_t s[5] = {42, 42, 42, 42, 42};
  TEST_ASSERT_EQUAL_UINT16(42, median5(s));
}

void test_rejects_single_spike() {
  uint16_t s[5] = {100, 102, 1023, 101, 99};  // 1023 is a glitch
  TEST_ASSERT_EQUAL_UINT16(101, median5(s));
}

void test_does_not_mutate_input() {
  uint16_t s[5] = {5, 1, 3, 2, 4};
  median5(s);
  TEST_ASSERT_EQUAL_UINT16(5, s[0]);
  TEST_ASSERT_EQUAL_UINT16(1, s[1]);
  TEST_ASSERT_EQUAL_UINT16(4, s[4]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_median_of_shuffled);
  RUN_TEST(test_median_with_duplicates);
  RUN_TEST(test_median_all_same);
  RUN_TEST(test_rejects_single_spike);
  RUN_TEST(test_does_not_mutate_input);
  return UNITY_END();
}
