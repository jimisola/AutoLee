// ============================================================================
//  autolee_logic/motor_fsm.h
//  Typed run-state machine — pure, host-testable. Encodes the *valid*
//  transitions of the firmware's RunState (src/globals.h) so the safety
//  rules (e.g. you cannot Start from Stalled — you must home first) are
//  explicit and tested rather than implied by scattered assignments.
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

enum class MotorState : uint8_t { Idle, Running, Stopping, Calibrating, Stalled, Homing };

enum class MotorEvent : uint8_t {
  Start,            // begin running between endpoints
  GracefulStop,     // user/stop requested
  Jam,              // StallGuard jam detected while running
  ReachedHome,      // stopping move reached the UP endpoint
  StopTimeout,      // stopping timed out
  Calibrate,        // begin sensorless calibration
  CalibrationDone,  // calibration finished (ok or fail)
  ReturnHome,       // "return home" pressed (jam screen, or to re-reference the axis)
  HomeDone,         // creep-home finished
  // Operator cancelled an in-progress calibration or creep-home. Deliberately
  // NOT accepted from Running: a run must decelerate through Stopping, and
  // letting anything jump straight to Idle would skip that. Also not accepted
  // from Stalled - the "you must home first" rule is the whole reason Stalled
  // exists, and an abort that cleared it would be a way around it.
  Abort
};

struct Transition {
  bool valid;  // false => event ignored, state unchanged
  MotorState next;
};

// Returns the next state for (state, event). Invalid combinations keep the
// current state and set valid=false.
inline Transition motorTransition(MotorState s, MotorEvent e) {
  switch (s) {
    case MotorState::Idle:
      if (e == MotorEvent::Start) return {true, MotorState::Running};
      if (e == MotorEvent::Calibrate) return {true, MotorState::Calibrating};
      // Homing from Idle is legal too, and is the *only* way to re-establish the
      // position reference after a reboot: the stepper's counter always comes up
      // at 0 while the carriage sits wherever power was lost, so restored
      // endpoints (settings_store) mean nothing until a real stall search has
      // re-zeroed at the UP hard stop. Idle->Homing is strictly conservative -
      // it is a slow, current-limited creep, and Start is refused meanwhile
      // (MotionState::positionReferenceStale).
      if (e == MotorEvent::ReturnHome) return {true, MotorState::Homing};
      break;
    case MotorState::Running:
      if (e == MotorEvent::GracefulStop) return {true, MotorState::Stopping};
      if (e == MotorEvent::Jam) return {true, MotorState::Stalled};
      break;
    case MotorState::Stopping:
      if (e == MotorEvent::ReachedHome || e == MotorEvent::StopTimeout)
        return {true, MotorState::Idle};
      break;
    case MotorState::Calibrating:
      if (e == MotorEvent::CalibrationDone) return {true, MotorState::Idle};
      // A sensorless search is a slow, current-limited creep that can run for
      // tens of seconds; before Abort existed it could not be interrupted at
      // all, because the search blocks pump_task and nothing else gets to run.
      // The caller must leave the position reference stale on this path - the
      // axis stopped mid-search and nothing re-zeroed it.
      if (e == MotorEvent::Abort) return {true, MotorState::Idle};
      break;
    case MotorState::Stalled:
      if (e == MotorEvent::ReturnHome) return {true, MotorState::Homing};
      break;
    case MotorState::Homing:
      if (e == MotorEvent::HomeDone) return {true, MotorState::Idle};
      // Same reasoning as Calibrating. Note this is the one way out of a
      // jam-recovery home that has not reached the stop: the result is Idle with
      // an unreferenced axis, so a run is still refused and Return Home is still
      // available - an abort must not become a shortcut past the recovery.
      if (e == MotorEvent::Abort) return {true, MotorState::Idle};
      break;
  }
  return {false, s};
}

// Is the motor allowed to be commanded to move between endpoints right now?
inline bool canStart(MotorState s) {
  return s == MotorState::Idle;
}

}  // namespace autolee
