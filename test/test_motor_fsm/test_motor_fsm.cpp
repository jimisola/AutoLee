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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_start_and_stop_cycle);
  RUN_TEST(test_jam_then_home);
  RUN_TEST(test_calibration_cycle);
  RUN_TEST(test_cannot_start_while_stalled);
  RUN_TEST(test_invalid_events_are_ignored);
  return UNITY_END();
}
