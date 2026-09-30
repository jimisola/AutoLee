#include <unity.h>
#include "tmc_current.h"

using namespace autolee;

static constexpr float kRsense = 0.022f;  // BTT TMC5160T Plus, main/config.h R_SENSE

void setUp() {}
void tearDown() {}

// The register values TMCStepper writes on the Arduino firmware for the two
// currents the press uses (run default, calibration/homing).
void test_run_current_matches_tmcstepper() {
  const TmcCurrent c = tmc5160Current(3500, kRsense);
  TEST_ASSERT_EQUAL_UINT8(130, c.globalScaler);
  TEST_ASSERT_EQUAL_UINT8(20, c.irun);
  TEST_ASSERT_EQUAL_UINT8(10, c.ihold);
}

void test_calibration_current_matches_tmcstepper() {
  const TmcCurrent c = tmc5160Current(3200, kRsense);
  TEST_ASSERT_EQUAL_UINT8(132, c.globalScaler);
  TEST_ASSERT_EQUAL_UINT8(18, c.irun);
  TEST_ASSERT_EQUAL_UINT8(9, c.ihold);
}

// What the registers actually drive, across the whole settable range: within a
// few percent of the request, never the ~1.9x the TMC2130 formula produced.
void test_delivered_current_tracks_the_request() {
  for (uint16_t mA = 1000; mA <= 4500; mA += 250) {
    const TmcCurrent c = tmc5160Current(mA, kRsense);
    const float rms = tmc5160RmsMa(c.globalScaler, c.irun, kRsense);
    TEST_ASSERT_FLOAT_WITHIN(mA * 0.03f, (float)mA, rms);
  }
}

// GLOBAL_SCALER must stay in its usable range (128..255, or 0 = 256).
void test_scaler_stays_in_its_usable_range() {
  for (uint16_t mA = 1000; mA <= 4500; mA += 100) {
    const TmcCurrent c = tmc5160Current(mA, kRsense);
    TEST_ASSERT_TRUE(c.globalScaler == 0 || c.globalScaler >= 128);
    TEST_ASSERT_TRUE(c.irun <= 31);
  }
}

void test_hold_is_half_the_run_current_by_default() {
  const TmcCurrent c = tmc5160Current(2000, kRsense);
  TEST_ASSERT_EQUAL_UINT8(c.irun / 2, c.ihold);
  TEST_ASSERT_EQUAL_UINT8(c.irun, tmc5160Current(2000, kRsense, 1.0f).ihold);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_run_current_matches_tmcstepper);
  RUN_TEST(test_calibration_current_matches_tmcstepper);
  RUN_TEST(test_delivered_current_tracks_the_request);
  RUN_TEST(test_scaler_stays_in_its_usable_range);
  RUN_TEST(test_hold_is_half_the_run_current_by_default);
  return UNITY_END();
}
