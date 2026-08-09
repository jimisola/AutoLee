// ============================================================================
//  autolee_logic/command_gate.h
//  Why a command is refused — pure, host-testable.
//
//  motor_fsm.h answers "is this transition legal from this state". That is not
//  the whole gate: a Start is also refused when the press was never calibrated,
//  or when a restored calibration has left the axis unreferenced, and a batch
//  Start is refused with no target set. Those rules used to live inline in
//  main/motion/motion_cmd.cpp and, in one case, one layer further down inside
//  startRunBetweenEndpoints() - so the HTTP layer had no way to consult them
//  without duplicating them, and answered 200 to requests it was about to drop.
//
//  Everything here is a pure function of a MotionState snapshot, so the same
//  rule can be evaluated twice: once by the HTTP handler
//  (main/net/web_server.cpp), to answer the caller synchronously, and again on
//  pump_task (main/motion/motion_cmd.cpp) when the command is actually applied.
//  The second evaluation stays authoritative - state can change between the two,
//  and a stale "yes" must never be allowed to move the press. Both call THESE
//  functions; there is no second copy of the rules anywhere. See docs/FLOWS.md
//  §1 for the whole path.
// ============================================================================
#pragma once
#include <cstdint>

#include "motor_fsm.h"

namespace autolee {

// Why a command cannot be carried out. None == it can (as of this snapshot).
enum class Refusal : uint8_t {
  None = 0,
  WrongState,            // the FSM has no transition for this event from here
  NotCalibrated,         // endpoints were never discovered
  PositionUnreferenced,  // calibration restored, axis not homed since
  NoBatchTarget,         // batch start with target 0
};

// The subset of MotionState the gates read. Passed by value: it is a snapshot,
// and copying it is what keeps these functions callable from the HTTP task
// without holding a lock while deciding.
struct GateInput {
  MotorState state = MotorState::Idle;
  bool calibrated = false;
  bool positionStale = false;
  int32_t batchTarget = 0;
};

// Begin a run between the endpoints. Mirrors, in order, the checks
// startRunBetweenEndpoints() applies before it touches the TMC or the stepper.
inline Refusal gateStart(const GateInput &in) {
  if (!in.calibrated) return Refusal::NotCalibrated;
  if (in.positionStale) return Refusal::PositionUnreferenced;
  if (!canStart(in.state)) return Refusal::WrongState;
  return Refusal::None;
}

inline Refusal gateStop(const GateInput &in) {
  return motorTransition(in.state, MotorEvent::GracefulStop).valid ? Refusal::None
                                                                   : Refusal::WrongState;
}

// One button, two meanings: from Idle it starts, from Running it stops. Which
// one a tap means is decided by the state, so the refusal has to be too -
// answering "wrong state" for an uncalibrated press sitting at Idle would be
// both true and useless.
inline Refusal gateToggleRun(const GateInput &in) {
  if (canStart(in.state)) return gateStart(in);
  return gateStop(in);
}

// Batch start is a plain start plus a target. Target is checked first: it is the
// one refusal the operator can clear without touching the press.
inline Refusal gateBatchStart(const GateInput &in) {
  if (in.batchTarget <= 0) return Refusal::NoBatchTarget;
  return gateStart(in);
}

inline Refusal gateCalibrate(const GateInput &in) {
  return motorTransition(in.state, MotorEvent::Calibrate).valid ? Refusal::None
                                                                : Refusal::WrongState;
}

inline Refusal gateReturnHome(const GateInput &in) {
  return motorTransition(in.state, MotorEvent::ReturnHome).valid ? Refusal::None
                                                                 : Refusal::WrongState;
}

// Deliberately NOT routed through the motor FSM: a settings reset is not a
// motion event, and hanging it off that table would silently widen the gate if
// the table ever gains a state. The condition needed is the literal one - fully
// idle, so a run/home/calibration can never have the endpoints, the run current
// or the active profile pulled out from under it mid-stroke.
inline Refusal gateResetSettings(const GateInput &in) {
  return in.state == MotorState::Idle ? Refusal::None : Refusal::WrongState;
}

// Stable machine-readable token for the "error" field of an API error body.
inline const char *refusalSlug(Refusal r) {
  switch (r) {
    case Refusal::WrongState:
      return "wrong_state";
    case Refusal::NotCalibrated:
      return "not_calibrated";
    case Refusal::PositionUnreferenced:
      return "position_unreferenced";
    case Refusal::NoBatchTarget:
      return "no_batch_target";
    case Refusal::None:
      break;
  }
  return "ok";
}

// Operator-facing sentence. Says what to do next, not just what went wrong -
// this string is what the dashboard puts in front of someone standing at a
// press that just refused to move.
inline const char *refusalMessage(Refusal r) {
  switch (r) {
    case Refusal::WrongState:
      return "not allowed in the current state";
    case Refusal::NotCalibrated:
      return "the press is not calibrated - run a calibration first";
    case Refusal::PositionUnreferenced:
      return "position reference unconfirmed after a reboot - return home first";
    case Refusal::NoBatchTarget:
      return "no batch target set";
    case Refusal::None:
      break;
  }
  return "ok";
}

}  // namespace autolee
