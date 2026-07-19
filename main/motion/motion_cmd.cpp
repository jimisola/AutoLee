#include "motion_cmd.h"

#include <atomic>

#include "config.h"
#include "globals.h"
#include "motion.h"
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
void requestProfile(uint8_t idx) {
  s_profile.store((int32_t)idx);
}
void requestCurrentMa(int32_t ma) {
  s_currentMa.store(ma);
}

void processPendingCommands() {
  // Calibration and homing block for seconds; they were already deferred
  // before this module existed, so keep using their existing entry points.
  if (s_calibrate.exchange(false)) {
    if (runState == IDLE) calibrateEndpointsSensorless();
  }

  if (s_returnHome.exchange(false)) {
    if (runState == STALLED) safeCreepHome();
  }

  if (s_toggleRun.exchange(false)) {
    // Re-check state here, not at request time: the press may have jammed or
    // finished a batch between the tap and now.
    if (runState == IDLE) {
      startRunBetweenEndpoints();
      setRunButtonState(runState == RUNNING);
    } else if (runState == RUNNING) {
      requestGracefulStop();
      setRunButtonState(false);
      batchActive = false;
    }
  }

  if (s_stop.exchange(false)) {
    if (runState == RUNNING) {
      requestGracefulStop();
      setRunButtonState(false);
    }
  }

  if (s_batchStart.exchange(false)) {
    if (batchTarget > 0 && runState == IDLE && endpointsCalibrated) {
      batchCount = 0;
      batchActive = true;
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
    RUN_CURRENT_MA = (uint16_t)(ma < RUN_CURRENT_MIN   ? RUN_CURRENT_MIN
                                : ma > RUN_CURRENT_MAX ? RUN_CURRENT_MAX
                                                       : ma);
    // SPI write on the bus shared with the display - must not run from the
    // HTTP task alongside pump_task's StallGuard reads.
    tmc5160::rms_current(RUN_CURRENT_MA);
    webLog("Current set to %u mA", RUN_CURRENT_MA);
  }

  if (s_logClear.exchange(false)) {
    // Reassigning the ring from another task would race webLog()'s push.
    g_log = autolee::LogRing<LOG_LINES, LOG_LINE_LEN>();
    logSentSerial = 0;
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
}

}  // namespace motion_cmd
