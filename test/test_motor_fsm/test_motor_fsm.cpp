#include <unity.h>
#include "motor_fsm.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

static MotorState next(MotorState s, MotorEvent e) {
  return motorTransition(s, e).next;
}
static bool valid(MotorState s, MotorEvent e) {
  return motorTransition(s, e).valid;
}

void test_start_and_stop_cycle() {
  TEST_ASSERT_TRUE(valid(MotorState::Idle, MotorEvent::Start));
  TEST_ASSERT_EQUAL(MotorState::Running, next(MotorState::Idle, MotorEvent::Start));
  TEST_ASSERT_EQUAL(MotorState::Stopping, next(MotorState::Running, MotorEvent::GracefulStop));
  TEST_ASSERT_EQUAL(MotorState::Idle, next(MotorState::Stopping, MotorEvent::ReachedHome));
  TEST_ASSERT_EQUAL(MotorState::Idle, next(MotorState::Stopping, MotorEvent::StopTimeout));
}

void test_jam_then_home() {
  TEST_ASSERT_EQUAL(MotorState::Stalled, next(MotorState::Running, MotorEvent::Jam));
  TEST_ASSERT_EQUAL(MotorState::Homing, next(MotorState::Stalled, MotorEvent::ReturnHome));
  TEST_ASSERT_EQUAL(MotorState::Idle, next(MotorState::Homing, MotorEvent::HomeDone));
}

void test_calibration_cycle() {
  TEST_ASSERT_EQUAL(MotorState::Calibrating, next(MotorState::Idle, MotorEvent::Calibrate));
  TEST_ASSERT_EQUAL(MotorState::Idle, next(MotorState::Calibrating, MotorEvent::CalibrationDone));
}

void test_cannot_start_while_stalled() {
  // Safety: must home first — Start is ignored from Stalled.
  TEST_ASSERT_FALSE(valid(MotorState::Stalled, MotorEvent::Start));
  TEST_ASSERT_EQUAL(MotorState::Stalled, next(MotorState::Stalled, MotorEvent::Start));
  TEST_ASSERT_FALSE(canStart(MotorState::Stalled));
  TEST_ASSERT_TRUE(canStart(MotorState::Idle));
}

void test_invalid_events_are_ignored() {
  TEST_ASSERT_FALSE(valid(MotorState::Idle, MotorEvent::Jam));
  TEST_ASSERT_FALSE(valid(MotorState::Running, MotorEvent::Calibrate));
  TEST_ASSERT_FALSE(valid(MotorState::Homing, MotorEvent::Start));
}

// Exhaustive: every (state, event) pair that is NOT one of the 9 valid
// transitions must be ignored (valid=false, state unchanged). This is a real
// safety property - e.g. Start while Stopping/Calibrating/Homing must never
// take effect - and covers the fall-through `break` in every case arm.
void test_all_invalid_transitions_ignored() {
  struct V {
    MotorState s;
    MotorEvent e;
  };
  // The complete valid-transition set (mirrors motorTransition()).
  const V kValid[] = {
      {MotorState::Idle, MotorEvent::Start},
      {MotorState::Idle, MotorEvent::Calibrate},
      {MotorState::Running, MotorEvent::GracefulStop},
      {MotorState::Running, MotorEvent::Jam},
      {MotorState::Stopping, MotorEvent::ReachedHome},
      {MotorState::Stopping, MotorEvent::StopTimeout},
      {MotorState::Calibrating, MotorEvent::CalibrationDone},
      {MotorState::Stalled, MotorEvent::ReturnHome},
      {MotorState::Homing, MotorEvent::HomeDone},
  };
  const MotorState states[] = {MotorState::Idle,        MotorState::Running, MotorState::Stopping,
                               MotorState::Calibrating, MotorState::Stalled, MotorState::Homing};
  for (MotorState s : states) {
    for (uint8_t ei = 0; ei <= (uint8_t)MotorEvent::HomeDone; ei++) {
      MotorEvent e = (MotorEvent)ei;
      bool isValid = false;
      for (const V &v : kValid) {
        if (v.s == s && v.e == e) {
          isValid = true;
          break;
        }
      }
      if (isValid) continue;
      TEST_ASSERT_FALSE(valid(s, e));    // invalid combo => ignored
      TEST_ASSERT_EQUAL(s, next(s, e));  // ...and state left unchanged
    }
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_start_and_stop_cycle);
  RUN_TEST(test_jam_then_home);
  RUN_TEST(test_calibration_cycle);
  RUN_TEST(test_cannot_start_while_stalled);
  RUN_TEST(test_invalid_events_are_ignored);
  RUN_TEST(test_all_invalid_transitions_ignored);
  return UNITY_END();
}
