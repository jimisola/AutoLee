#include <unity.h>
#include <cstring>
#include "log_ring.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

void test_push_below_capacity() {
  LogRing<3, 8> ring;
  ring.push("a");
  ring.push("b");
  TEST_ASSERT_EQUAL_UINT32(2, ring.serial());
  TEST_ASSERT_EQUAL_UINT16(2, ring.size());
  TEST_ASSERT_EQUAL_STRING("a", ring.at(0));
  TEST_ASSERT_EQUAL_STRING("b", ring.at(1));
}

void test_truncates_long_lines() {
  LogRing<2, 4> ring;               // max 3 chars + NUL
  ring.push("abcdef");
  TEST_ASSERT_EQUAL_STRING("abc", ring.at(0));
}

void test_wraparound_keeps_newest() {
  LogRing<3, 8> ring;
  ring.push("1");
  ring.push("2");
  ring.push("3");
  ring.push("4");                   // evicts "1"
  ring.push("5");                   // evicts "2"
  TEST_ASSERT_EQUAL_UINT32(5, ring.serial());
  TEST_ASSERT_EQUAL_UINT16(3, ring.size());
  TEST_ASSERT_EQUAL_STRING("3", ring.at(0));  // oldest retained
  TEST_ASSERT_EQUAL_STRING("4", ring.at(1));
  TEST_ASSERT_EQUAL_STRING("5", ring.at(2));  // newest
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_push_below_capacity);
  RUN_TEST(test_truncates_long_lines);
  RUN_TEST(test_wraparound_keeps_newest);
  return UNITY_END();
}
