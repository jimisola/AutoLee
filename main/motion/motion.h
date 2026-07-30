#pragma once

#include <cstdint>

#include "motion_state.h"  // RunState, g_motion, motion_state::Guard
#include "motor_fsm.h"     // autolee::MotorState/MotorEvent/motorTransition/canStart

// ============================================================================
//  Bridge to the host-tested run-state machine (lib/autolee_logic/motor_fsm.h)
//
//  `RunState` is the wire/telemetry enum (its numeric values leak into the JSON
//  state and the UI); `autolee::MotorState` is the tested FSM's enum. They are
//  declared with the same enumerator order on purpose, asserted below, so the
//  conversion is a plain cast and the shipped firmware can route every
//  transition through the same table the host tests exercise.
// ============================================================================
static_assert((uint8_t)autolee::MotorState::Idle == (uint8_t)IDLE, "RunState/MotorState drift");
static_assert((uint8_t)autolee::MotorState::Running == (uint8_t)RUNNING,
              "RunState/MotorState drift");
static_assert((uint8_t)autolee::MotorState::Stopping == (uint8_t)STOPPING,
              "RunState/MotorState drift");
static_assert((uint8_t)autolee::MotorState::Calibrating == (uint8_t)CALIBRATING,
              "RunState/MotorState drift");
static_assert((uint8_t)autolee::MotorState::Stalled == (uint8_t)STALLED,
              "RunState/MotorState drift");
static_assert((uint8_t)autolee::MotorState::Homing == (uint8_t)HOMING, "RunState/MotorState drift");

inline autolee::MotorState toMotorState(RunState s) {
  return (autolee::MotorState)s;
}
inline RunState toRunState(autolee::MotorState s) {
  return (RunState)s;
}

// Would `e` be accepted from the CURRENT state? Pure read of g_motion.runState,
// so this is for pump_task only (it owns the state - see motion_state.h);
// other tasks must ask the same question off a snapshot instead.
inline bool motionEventAllowed(autolee::MotorEvent e) {
  return autolee::motorTransition(toMotorState(g_motion.runState), e).valid;
}

// Apply `e` to g_motion.runState through the tested transition table. Returns
// false and leaves the state untouched if the table rejects the transition.
//
// PRECONDITION: the caller already holds a `motion_state::Guard`, so this can
// be grouped into the same critical section as the other fields that must go
// live with the state change. motorTransition() is a pure switch - it does not
// log, block, allocate or touch hardware - so it is safe in there. Do any
// logging of a rejected event AFTER leaving the guard.
inline bool applyMotorEventLocked(autolee::MotorEvent e) {
  const autolee::Transition t = autolee::motorTransition(toMotorState(g_motion.runState), e);
  if (t.valid) g_motion.runState = toRunState(t.next);
  return t.valid;
}

// Motion state machine + StallGuard logic, ported from the original Arduino
// firmware's motion logic.
// *** UNVERIFIED ON HARDWARE - see main/stepper.h. This is the safety-critical
// jam-detection/stop/backoff code; bench-verify before trusting on the press. ***
void motion_init();

void handleMotion();  // pump, call every loop iteration
void startRunBetweenEndpoints();
void requestGracefulStop();
bool calibrateEndpointsSensorless();
void safeCreepHome();
bool return_home_up_safe();
void recomputeEffectiveEndpoints();
uint16_t read_sg();
void setActiveProfile(uint8_t idx);
