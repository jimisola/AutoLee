#include <unity.h>
#include "endpoint_math.h"

using namespace autolee;

// Constants mirrored from AutoLee/config.h
static constexpr int32_t OFFSET_MIN = -8000, OFFSET_MAX = 8000;
static constexpr int32_t GUARD = 50;
static constexpr int32_t DOWN_DEFAULT = -500;

void setUp() {}
void tearDown() {}

void test_clamp_basic() {
  TEST_ASSERT_EQUAL_INT32(5, clamp_i32(5, 0, 10));
  TEST_ASSERT_EQUAL_INT32(0, clamp_i32(-3, 0, 10));
  TEST_ASSERT_EQUAL_INT32(10, clamp_i32(99, 0, 10));
}

void test_not_calibrated_is_zero() {
  Endpoints e =
      computeEffectiveEndpoints(false, 0, 10000, 0, DOWN_DEFAULT, OFFSET_MIN, OFFSET_MAX, GUARD);
  TEST_ASSERT_EQUAL_INT32(0, e.endpointUp);
  TEST_ASSERT_EQUAL_INT32(0, e.endpointDown);
}

void test_nominal_endpoints() {
  // rawUp=0, rawDown=10000, downOffset=-500 -> up=0, down=9500
  Endpoints e =
      computeEffectiveEndpoints(true, 0, 10000, 0, DOWN_DEFAULT, OFFSET_MIN, OFFSET_MAX, GUARD);
  TEST_ASSERT_EQUAL_INT32(0, e.endpointUp);
  TEST_ASSERT_EQUAL_INT32(9500, e.endpointDown);
  TEST_ASSERT_EQUAL_INT32(-500, e.downOffset);
}

void test_offsets_are_clamped() {
  Endpoints e =
      computeEffectiveEndpoints(true, 0, 10000, 99999, -99999, OFFSET_MIN, OFFSET_MAX, GUARD);
  TEST_ASSERT_EQUAL_INT32(OFFSET_MAX, e.upOffset);
  // downOffset clamped to MIN then guard may adjust; endpointUp = 0+8000 = 8000
  TEST_ASSERT_EQUAL_INT32(8000, e.endpointUp);
}

void test_offsets_clamped_low_side() {
  // Mirror of test_offsets_are_clamped for the LOWER bound, with rawDown large
  // enough that the guard never fires - so both endpoints reflect a clean
  // clamp to OFFSET_MIN (the up-side test masks the down clamp behind the guard).
  Endpoints e =
      computeEffectiveEndpoints(true, 10000, 20000, -99999, -99999, OFFSET_MIN, OFFSET_MAX, GUARD);
  TEST_ASSERT_EQUAL_INT32(OFFSET_MIN, e.upOffset);    // -99999 clamped to -8000
  TEST_ASSERT_EQUAL_INT32(OFFSET_MIN, e.downOffset);  // -99999 clamped to -8000
  TEST_ASSERT_EQUAL_INT32(2000, e.endpointUp);        // rawUp 10000 + (-8000)
  TEST_ASSERT_EQUAL_INT32(12000, e.endpointDown);     // rawDown 20000 + (-8000), guard clear
}

void test_guard_enforced_and_downoffset_backcomputed() {
  // rawUp=0, rawDown=100, downOffset=-500 -> dnEff=-400 <= 50 -> forced to 50
  Endpoints e = computeEffectiveEndpoints(true, 0, 100, 0, -500, OFFSET_MIN, OFFSET_MAX, GUARD);
  TEST_ASSERT_EQUAL_INT32(0, e.endpointUp);
  TEST_ASSERT_EQUAL_INT32(50, e.endpointDown);  // upEff + guard
  TEST_ASSERT_EQUAL_INT32(-50, e.downOffset);   // dnEff - rawDown = 50 - 100
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_basic);
  RUN_TEST(test_not_calibrated_is_zero);
  RUN_TEST(test_nominal_endpoints);
  RUN_TEST(test_offsets_are_clamped);
  RUN_TEST(test_offsets_clamped_low_side);
  RUN_TEST(test_guard_enforced_and_downoffset_backcomputed);
  return UNITY_END();
}
