#include <unity.h>
#include <cstring>
#include <initializer_list>
#include "command_gate.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

// A press that is ready to go: calibrated, referenced, idle, no batch armed.
static GateInput ready() {
  GateInput in;
  in.state = MotorState::Idle;
  in.calibrated = true;
  in.positionStale = false;
  in.batchTarget = 0;
  return in;
}

// ---------------------------------------------------------------------------
//  Start
// ---------------------------------------------------------------------------
void test_start_allowed_when_ready() {
  TEST_ASSERT_EQUAL(Refusal::None, gateStart(ready()));
}

void test_start_refused_uncalibrated() {
  GateInput in = ready();
  in.calibrated = false;
  TEST_ASSERT_EQUAL(Refusal::NotCalibrated, gateStart(in));
}

// The reboot case: settings_store restores the endpoints but the stepper's
// counter comes up at 0 wherever the carriage happens to be, so the stored
// targets mean nothing until a home re-zeroes against the UP hard stop.
void test_start_refused_when_position_unreferenced() {
  GateInput in = ready();
  in.positionStale = true;
  TEST_ASSERT_EQUAL(Refusal::PositionUnreferenced, gateStart(in));
}

// Calibration is the more fundamental problem of the two, and it is the one
// whose fix (calibrate) also clears the other. Report it first.
void test_uncalibrated_outranks_unreferenced() {
  GateInput in = ready();
  in.calibrated = false;
  in.positionStale = true;
  TEST_ASSERT_EQUAL(Refusal::NotCalibrated, gateStart(in));
}

void test_start_refused_from_every_non_idle_state() {
  const MotorState busy[] = {MotorState::Running, MotorState::Stopping, MotorState::Calibrating,
                             MotorState::Stalled, MotorState::Homing};
  for (MotorState s : busy) {
    GateInput in = ready();
    in.state = s;
    TEST_ASSERT_EQUAL(Refusal::WrongState, gateStart(in));
  }
}

// ---------------------------------------------------------------------------
//  Stop
// ---------------------------------------------------------------------------
void test_stop_allowed_only_while_running() {
  const MotorState all[] = {MotorState::Idle,        MotorState::Running, MotorState::Stopping,
                            MotorState::Calibrating, MotorState::Stalled, MotorState::Homing};
  for (MotorState s : all) {
    GateInput in = ready();
    in.state = s;
    const Refusal want = (s == MotorState::Running) ? Refusal::None : Refusal::WrongState;
    TEST_ASSERT_EQUAL(want, gateStop(in));
  }
}

// Already decelerating. The FSM accepts neither Start nor another GracefulStop
// from Stopping, so a second tap has nowhere to go - and must say so rather
// than being swallowed.
void test_stop_refused_while_already_stopping() {
  GateInput in = ready();
  in.state = MotorState::Stopping;
  TEST_ASSERT_EQUAL(Refusal::WrongState, gateStop(in));
}

// ---------------------------------------------------------------------------
//  Toggle
// ---------------------------------------------------------------------------
void test_toggle_from_idle_is_a_start_and_reports_start_refusals() {
  GateInput in = ready();
  TEST_ASSERT_EQUAL(Refusal::None, gateToggleRun(in));

  // Idle but not runnable: the useful answer is *why* it cannot start, not the
  // technically-true "wrong state".
  in.calibrated = false;
  TEST_ASSERT_EQUAL(Refusal::NotCalibrated, gateToggleRun(in));

  in = ready();
  in.positionStale = true;
  TEST_ASSERT_EQUAL(Refusal::PositionUnreferenced, gateToggleRun(in));
}

void test_toggle_from_running_is_a_stop() {
  GateInput in = ready();
  in.state = MotorState::Running;
  TEST_ASSERT_EQUAL(Refusal::None, gateToggleRun(in));
  // Calibration state is irrelevant to stopping - a press that is moving can
  // always be told to stop, whatever it thinks about its endpoints.
  in.calibrated = false;
  in.positionStale = true;
  TEST_ASSERT_EQUAL(Refusal::None, gateToggleRun(in));
}

void test_toggle_refused_in_transient_states() {
  const MotorState transient[] = {MotorState::Stopping, MotorState::Calibrating,
                                  MotorState::Stalled, MotorState::Homing};
  for (MotorState s : transient) {
    GateInput in = ready();
    in.state = s;
    TEST_ASSERT_EQUAL(Refusal::WrongState, gateToggleRun(in));
  }
}

// ---------------------------------------------------------------------------
//  Batch start
// ---------------------------------------------------------------------------
void test_batch_start_needs_a_target() {
  GateInput in = ready();
  in.batchTarget = 0;
  TEST_ASSERT_EQUAL(Refusal::NoBatchTarget, gateBatchStart(in));
  in.batchTarget = -1;
  TEST_ASSERT_EQUAL(Refusal::NoBatchTarget, gateBatchStart(in));
  in.batchTarget = 1;
  TEST_ASSERT_EQUAL(Refusal::None, gateBatchStart(in));
}

// The missing target is the only refusal here the operator can clear without
// touching the machine, so it is worth reporting ahead of the others.
void test_no_target_outranks_the_motion_refusals() {
  GateInput in = ready();
  in.batchTarget = 0;
  in.calibrated = false;
  in.state = MotorState::Running;
  TEST_ASSERT_EQUAL(Refusal::NoBatchTarget, gateBatchStart(in));
}

// A batch start is a start: every gate that stops a plain run must stop this
// too. This is the regression that let both UIs report "Running: 0/N" on a
// press that was standing still.
void test_batch_start_inherits_every_start_refusal() {
  GateInput in = ready();
  in.batchTarget = 50;
  in.positionStale = true;
  TEST_ASSERT_EQUAL(Refusal::PositionUnreferenced, gateBatchStart(in));

  in = ready();
  in.batchTarget = 50;
  in.calibrated = false;
  TEST_ASSERT_EQUAL(Refusal::NotCalibrated, gateBatchStart(in));

  in = ready();
  in.batchTarget = 50;
  in.state = MotorState::Running;
  TEST_ASSERT_EQUAL(Refusal::WrongState, gateBatchStart(in));
}

// ---------------------------------------------------------------------------
//  Calibrate / return home / settings reset
// ---------------------------------------------------------------------------
void test_calibrate_only_from_idle() {
  const MotorState all[] = {MotorState::Idle,        MotorState::Running, MotorState::Stopping,
                            MotorState::Calibrating, MotorState::Stalled, MotorState::Homing};
  for (MotorState s : all) {
    GateInput in = ready();
    in.state = s;
    const Refusal want = (s == MotorState::Idle) ? Refusal::None : Refusal::WrongState;
    TEST_ASSERT_EQUAL(want, gateCalibrate(in));
  }
}

// Homing is legal from Idle *and* from Stalled - it is the jam-recovery action
// and the way to clear an unreferenced axis, so it must not require calibration.
void test_return_home_from_idle_and_stalled_regardless_of_calibration() {
  for (MotorState s : {MotorState::Idle, MotorState::Stalled}) {
    GateInput in = ready();
    in.state = s;
    in.calibrated = false;
    in.positionStale = true;
    TEST_ASSERT_EQUAL(Refusal::None, gateReturnHome(in));
  }
  for (MotorState s :
       {MotorState::Running, MotorState::Stopping, MotorState::Calibrating, MotorState::Homing}) {
    GateInput in = ready();
    in.state = s;
    TEST_ASSERT_EQUAL(Refusal::WrongState, gateReturnHome(in));
  }
}

void test_settings_reset_only_when_fully_idle() {
  const MotorState all[] = {MotorState::Idle,        MotorState::Running, MotorState::Stopping,
                            MotorState::Calibrating, MotorState::Stalled, MotorState::Homing};
  for (MotorState s : all) {
    GateInput in = ready();
    in.state = s;
    const Refusal want = (s == MotorState::Idle) ? Refusal::None : Refusal::WrongState;
    TEST_ASSERT_EQUAL(want, gateResetSettings(in));
  }
}

// ---------------------------------------------------------------------------
//  Reporting
// ---------------------------------------------------------------------------
// The slugs are the "error" field of the HTTP error body and are therefore part
// of the published contract (api/openapi.yaml) - changing one breaks clients.
void test_slugs_are_the_documented_tokens() {
  TEST_ASSERT_EQUAL_STRING("wrong_state", refusalSlug(Refusal::WrongState));
  TEST_ASSERT_EQUAL_STRING("not_calibrated", refusalSlug(Refusal::NotCalibrated));
  TEST_ASSERT_EQUAL_STRING("position_unreferenced", refusalSlug(Refusal::PositionUnreferenced));
  TEST_ASSERT_EQUAL_STRING("no_batch_target", refusalSlug(Refusal::NoBatchTarget));
}

void test_every_refusal_has_a_distinct_slug_and_a_message() {
  const Refusal all[] = {Refusal::WrongState, Refusal::NotCalibrated, Refusal::PositionUnreferenced,
                         Refusal::NoBatchTarget};
  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
    TEST_ASSERT_NOT_NULL(refusalSlug(all[i]));
    TEST_ASSERT_NOT_NULL(refusalMessage(all[i]));
    TEST_ASSERT_TRUE(strlen(refusalSlug(all[i])) > 0);
    TEST_ASSERT_TRUE(strlen(refusalMessage(all[i])) > 0);
    // Nothing may collide with the not-an-error value.
    TEST_ASSERT_TRUE(strcmp(refusalSlug(all[i]), refusalSlug(Refusal::None)) != 0);
    for (size_t j = i + 1; j < sizeof(all) / sizeof(all[0]); j++) {
      TEST_ASSERT_TRUE(strcmp(refusalSlug(all[i]), refusalSlug(all[j])) != 0);
      TEST_ASSERT_TRUE(strcmp(refusalMessage(all[i]), refusalMessage(all[j])) != 0);
    }
  }
}

// The JSON error body embeds both unescaped, so neither may contain a character
// that would have to be escaped (or would end the string early).
void test_reporting_strings_are_json_safe() {
  const Refusal all[] = {Refusal::None, Refusal::WrongState, Refusal::NotCalibrated,
                         Refusal::PositionUnreferenced, Refusal::NoBatchTarget};
  for (Refusal r : all) {
    for (const char *s : {refusalSlug(r), refusalMessage(r)}) {
      for (const char *p = s; *p; p++) {
        TEST_ASSERT_TRUE(*p != '"' && *p != '\\');
        TEST_ASSERT_TRUE((unsigned char)*p >= 0x20);
      }
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_allowed_when_ready);
  RUN_TEST(test_start_refused_uncalibrated);
  RUN_TEST(test_start_refused_when_position_unreferenced);
  RUN_TEST(test_uncalibrated_outranks_unreferenced);
  RUN_TEST(test_start_refused_from_every_non_idle_state);
  RUN_TEST(test_stop_allowed_only_while_running);
  RUN_TEST(test_stop_refused_while_already_stopping);
  RUN_TEST(test_toggle_from_idle_is_a_start_and_reports_start_refusals);
  RUN_TEST(test_toggle_from_running_is_a_stop);
  RUN_TEST(test_toggle_refused_in_transient_states);
  RUN_TEST(test_batch_start_needs_a_target);
  RUN_TEST(test_no_target_outranks_the_motion_refusals);
  RUN_TEST(test_batch_start_inherits_every_start_refusal);
  RUN_TEST(test_calibrate_only_from_idle);
  RUN_TEST(test_return_home_from_idle_and_stalled_regardless_of_calibration);
  RUN_TEST(test_settings_reset_only_when_fully_idle);
  RUN_TEST(test_slugs_are_the_documented_tokens);
  RUN_TEST(test_every_refusal_has_a_distinct_slug_and_a_message);
  RUN_TEST(test_reporting_strings_are_json_safe);
  return UNITY_END();
}
