#include "motion_cmd.h"

#include <atomic>

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
std::atomic<int32_t> s_profile{-1};    // -1 = none pending
std::atomic<int32_t> s_currentMa{-1};  // -1 = none pending

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
  // The state gates below ask the host-tested transition table
  // (lib/autolee_logic/motor_fsm.h) rather than re-hardcoding which states each
  // command is legal from; motion.cpp re-checks the same table when it actually
  // applies the transition, so the rule lives in exactly one tested place.
  if (s_calibrate.exchange(false)) {
    if (motionEventAllowed(autolee::MotorEvent::Calibrate)) calibrateEndpointsSensorless();
  }

  if (s_returnHome.exchange(false)) {
    if (motionEventAllowed(autolee::MotorEvent::ReturnHome)) safeCreepHome();
  }

  if (s_toggleRun.exchange(false)) {
    // Re-check state here, not at request time: the press may have jammed or
    // finished a batch between the tap and now.
    if (autolee::canStart(toMotorState(g_motion.runState))) {
      startRunBetweenEndpoints();
      setRunButtonState(g_motion.runState == RUNNING);
    } else if (motionEventAllowed(autolee::MotorEvent::GracefulStop)) {
      requestGracefulStop();
      setRunButtonState(false);
      motion_state::Guard g;
      g_motion.batchActive = false;
    }
  }

  if (s_stop.exchange(false)) {
    if (motionEventAllowed(autolee::MotorEvent::GracefulStop)) {
      requestGracefulStop();
      setRunButtonState(false);
    }
  }

  if (s_batchStart.exchange(false)) {
    if (g_motion.batchTarget > 0 && autolee::canStart(toMotorState(g_motion.runState)) &&
        g_motion.endpointsCalibrated) {
      {
        motion_state::Guard g;
        g_motion.batchCount = 0;
        g_motion.batchActive = true;
      }
      startRunBetweenEndpoints();
      setRunButtonState(true);
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
    // Deliberately NOT gated through motionEventAllowed()/canStart(): this is
    // not a motion command, and coupling it to the motion-permission table
    // would silently widen the gate if that table ever gains a state. The
    // condition we actually need is the literal one - the machine must be fully
    // idle, so a run/home/calibration can never have the endpoints, the run
    // current or the active profile pulled out from under it mid-stroke.
    if (g_motion.runState == IDLE) {
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
      webLog("Settings", "Settings reset ignored in state %u", (unsigned)g_motion.runState);
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
