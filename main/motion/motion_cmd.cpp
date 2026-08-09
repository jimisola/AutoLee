#include "motion_cmd.h"

#include <atomic>

#include "command_gate.h"
#include "config.h"
#include "globals.h"
#include "motion.h"
#include "motion_state.h"
#include "settings_store.h"
#include "tmc5160_ctrl.h"
#include "ui_touch.h"

namespace motion_cmd {
namespace {

// std::atomic rather than `volatile`: volatile gives no cross-task ordering or
// atomicity guarantees in C++, it only stops the compiler caching the value.
// These are all lock-free on this target.
std::atomic<bool> s_toggleRun{false};
std::atomic<bool> s_stop{false};
std::atomic<bool> s_calibrate{false};
std::atomic<bool> s_returnHome{false};
std::atomic<bool> s_batchStart{false};
std::atomic<bool> s_uiRefresh{false};
std::atomic<bool> s_logClear{false};
std::atomic<bool> s_resetSettings{false};
// Not consumed by processPendingCommands() - see the header. Polled directly by
// motion.cpp's blocking search loops, which is the only place that can observe
// it in time to matter.
std::atomic<bool> s_abort{false};
std::atomic<int32_t> s_profile{-1};    // -1 = none pending
std::atomic<int32_t> s_currentMa{-1};  // -1 = none pending

// The gate's view of the machine. pump_task owns g_motion and may read it
// unlocked (motion_state.h), so this is a plain field copy.
autolee::GateInput gateInput() {
  autolee::GateInput in;
  in.state = toMotorState(g_motion.runState);
  in.calibrated = g_motion.endpointsCalibrated;
  in.positionStale = g_motion.positionReferenceStale;
  in.batchTarget = g_motion.batchTarget;
  return in;
}

// Severity follows the refusal, not the call site: a refusal the operator has to
// do something about (calibrate, home) is a warning wherever it came from, while
// a transient one - tapping stop on an idle press, starting a batch with no
// target - is ordinary information. The state is always included; for a
// WrongState refusal it is the whole explanation, and for the others it is
// context worth having in a support log.
void logRefusal(const char *category, const char *what, autolee::Refusal r) {
  const LogLevel level =
      (r == autolee::Refusal::NotCalibrated || r == autolee::Refusal::PositionUnreferenced)
          ? LogLevel::Warn
          : LogLevel::Info;
  webLogLevel(level, category, "%s refused: %s (state %u)", what, autolee::refusalMessage(r),
              (unsigned)g_motion.runState);
}

}  // namespace

void requestToggleRun() {
  s_toggleRun.store(true);
}
void requestStop() {
  s_stop.store(true);
}
void requestCalibrate() {
  s_calibrate.store(true);
}
void requestReturnHome() {
  s_returnHome.store(true);
}
void requestBatchStart() {
  s_batchStart.store(true);
}
void requestUiRefresh() {
  s_uiRefresh.store(true);
}
void requestLogClear() {
  s_logClear.store(true);
}
void requestResetSettings() {
  s_resetSettings.store(true);
}
void requestAbort() {
  s_abort.store(true);
}
bool abortRequested() {
  return s_abort.load();
}
void clearAbort() {
  s_abort.store(false);
}
void requestProfile(uint8_t idx) {
  s_profile.store((int32_t)idx);
}
void requestCurrentMa(int32_t ma) {
  s_currentMa.store(ma);
}

// Runs on pump_task only (see the header), so g_motion reads here are unlocked
// - but every write still takes motion_state::Guard, per motion_state.h's rules.
void processPendingCommands() {
  // Calibration and homing block for seconds; they were already deferred
  // before this module existed, so keep using their existing entry points.
  //
  // Every gate below asks lib/autolee_logic/command_gate.h - the same pure,
  // host-tested predicates the HTTP layer uses to answer 400/409. This is the
  // authoritative evaluation of the two: the HTTP one ran on another task and
  // the press may have jammed, finished a batch or been stopped from the panel
  // since. motion.cpp re-checks the FSM transition again when it applies it, so
  // nothing here is the only thing standing between a bad request and the
  // stepper. See docs/FLOWS.md §1.
  if (s_calibrate.exchange(false)) {
    const autolee::Refusal r = autolee::gateCalibrate(gateInput());
    if (r == autolee::Refusal::None)
      calibrateEndpointsSensorless();
    else
      logRefusal("Motion", "Calibration", r);
  }

  if (s_returnHome.exchange(false)) {
    const autolee::Refusal r = autolee::gateReturnHome(gateInput());
    if (r == autolee::Refusal::None)
      safeCreepHome();
    else
      logRefusal("Motion", "Return home", r);
  }

  if (s_toggleRun.exchange(false)) {
    // One button, two meanings: from IDLE this is a start, from RUNNING a stop.
    // gateToggleRun() decides which, and names the refusal for whichever it was
    // - so an uncalibrated press sitting at IDLE is told it needs calibrating
    // rather than the technically-true but useless "wrong state". That case used
    // to return in complete silence from inside startRunBetweenEndpoints().
    const autolee::GateInput in = gateInput();
    const autolee::Refusal r = autolee::gateToggleRun(in);
    if (r != autolee::Refusal::None) {
      logRefusal("Motion", "Run/stop", r);
      // NOT a gate - gateToggleRun() has already allowed this. canStart() is
      // only asking which of the two meanings the tap had, and it is the same
      // predicate the gate used to decide that, so the two cannot disagree.
    } else if (autolee::canStart(in.state)) {
      startRunBetweenEndpoints();
      ui_update_run_button();
    } else {
      requestGracefulStop();
      ui_update_run_button();
      motion_state::Guard g;
      g_motion.batchActive = false;
    }
  }

  if (s_stop.exchange(false)) {
    const autolee::Refusal r = autolee::gateStop(gateInput());
    if (r == autolee::Refusal::None) {
      requestGracefulStop();
      ui_update_run_button();
    } else {
      logRefusal("Motion", "Stop", r);
    }
  }

  if (s_batchStart.exchange(false)) {
    // A batch start is a plain start plus a target, so it inherits every gate a
    // run has. positionReferenceStale is the one that used to be missing
    // entirely, and it was the expensive one: after any reboot with a restored
    // calibration the axis is unreferenced, motion.cpp refused the start - but
    // batchActive and a red STOP label had *already* been set two lines earlier.
    // Both UIs then reported a batch running on a press that was standing still
    // ("Running: 0/N", Start Batch disabled), with no way out but a toggle, and
    // nothing anywhere saying why.
    const autolee::Refusal r = autolee::gateBatchStart(gateInput());
    if (r != autolee::Refusal::None) {
      logRefusal("Motion", "Batch start", r);
    } else {
      startRunBetweenEndpoints();
      // Arm the batch only once the run is genuinely under way. Anything that
      // refuses inside startRunBetweenEndpoints() must leave the flag clear, or
      // the dashboard reports a batch in progress forever.
      if (g_motion.runState == RUNNING) {
        motion_state::Guard g;
        g_motion.batchCount = 0;
        g_motion.batchActive = true;
      }
      ui_update_run_button();
    }
  }

  int32_t profile = s_profile.exchange(-1);
  if (profile >= 0 && profile < (int32_t)NUM_PROFILES) {
    setActiveProfile((uint8_t)profile);
    ui_update_speed_val();
    ui_update_profile_screen();
    ui_update_sg_val();
  }

  int32_t ma = s_currentMa.exchange(-1);
  if (ma >= 0) {
    const uint16_t clamped = (uint16_t)(ma < RUN_CURRENT_MIN   ? RUN_CURRENT_MIN
                                        : ma > RUN_CURRENT_MAX ? RUN_CURRENT_MAX
                                                               : ma);
    {
      motion_state::Guard g;
      g_motion.runCurrentMa = clamped;
    }
    // SPI write on the bus shared with the display - must not run from the
    // HTTP task alongside pump_task's StallGuard reads.
    tmc5160::rms_current(clamped);
    webLog("Motion", "Current set to %u mA", clamped);
  }

  if (s_logClear.exchange(false)) {
    // clear() takes LogRing's internal lock, so this is now safe even
    // though webLog() may be pushing concurrently from another task.
    g_log.clear();
    logSentSerial = 0;
  }

  if (s_resetSettings.exchange(false)) {
    // gateResetSettings() is deliberately NOT routed through the motor FSM: this
    // is not a motion command, and coupling it to that table would silently
    // widen the gate if the table ever gains a state. It checks the literal
    // condition - fully idle - so a run/home/calibration can never have the
    // endpoints, the run current or the active profile pulled out from under it
    // mid-stroke.
    const autolee::Refusal r = autolee::gateResetSettings(gateInput());
    if (r == autolee::Refusal::None) {
      // Erases the NVS blob and restores the compiled-in defaults (calibration
      // cleared, positionReferenceStale latched). WiFi credentials, the AP key
      // and the web password are in other NVS keys and are not touched.
      settings_store::resetToDefaults();
      // Push the restored values down to the hardware from pump_task: the TMC
      // write shares the SPI bus with the display, and re-applying the profile
      // is what feeds stepper::setSpeedInHz() the default profile's speed.
      tmc5160::rms_current(g_motion.runCurrentMa);
      setActiveProfile(g_motion.activeProfile);
      ui_update_speed_val();
      s_uiRefresh.store(true);  // handled by the block below, same pass
    } else {
      logRefusal("Settings", "Reset", r);
    }
  }

  if (s_uiRefresh.exchange(false)) {
    ui_update_batch_val();
    ui_update_batch_remain();
    ui_update_sg_val();
    ui_update_profile_screen();
    ui_update_endpoint_edit_values();
    ui_update_tuning_numbers();
    ui_update_main_warning();
  }

  // Persist calibration/tuning changes. Self-throttling (a few seconds) and a
  // no-op unless something in the persisted set actually differs from the last
  // write, so the common path is one snapshot + one memcmp per pump iteration.
  // An actual NVS commit costs a few ms - three orders of magnitude inside the
  // 8s task-watchdog budget, and pump_task resets the watchdog on the very next
  // iteration.
  settings_store::tick();
}

}  // namespace motion_cmd
