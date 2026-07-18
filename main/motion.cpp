// ============================================================================
//  AutoLee - motion.cpp (ESP-IDF port)
//  Faithful port of src/motion.cpp's algorithm. Plumbing swapped
//  (TMCStepper->tmc5160::, FastAccelStepper->stepper::, Arduino
//  millis()/delay()->esp_timer/vTaskDelay), safety logic unchanged.
//
//  *** UNVERIFIED ON HARDWARE - see main/stepper.h's warning. Bench-verify
//  calibration, run, jam trigger + backoff, and return-home before trusting
//  this on the real press. ***
// ============================================================================
#include "motion.h"
#include "globals.h"
#include "stepper.h"
#include "tmc5160_ctrl.h"
#include "ui_touch.h"

#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"

// Pure, host-tested logic shared with the native unit tests (test_apps/).
#include "endpoint_math.h"
#include "sg_filter.h"
#include "sg_blanking.h"
#include "batch.h"
#include "calibration.h"

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}
// pdMS_TO_TICKS(1) rounds down to 0 ticks at the default 100 Hz tick rate,
// which doesn't actually yield - found via the task WDT tripping during
// bring-up (see app_main.cpp's pump_task). Guarantee at least 1 tick so the
// tight calibration/homing polling loops below don't starve other tasks.
static inline void delay(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  vTaskDelay(ticks < 1 ? 1 : ticks);
}
static inline void wdt_feed() {
  esp_task_wdt_reset();
}

// UI hooks: real implementations in ui_touch.cpp.

// Shared SPI bus is initialized by display_touch.cpp (SCK=1, MOSI=2, MISO=3);
// this only adds the TMC5160 as a device on it and brings up the stepper.
// Mirrors src/main.cpp's setup(): SPI.begin + driver.begin/toff/microsteps/... + engine.init.
void motion_init() {
  tmc5160::init(SPI2_HOST, R_SENSE);
  stepper::init((gpio_num_t)STEP_PIN, (gpio_num_t)DIR_PIN);
  tmc5160::rms_current(RUN_CURRENT_MA);
}

// ==========================================================================
//  UTILITY
// ==========================================================================
static inline bool nearPos(long a, long b, long tol = 2) {
  return labs(a - b) <= tol;
}
static inline long flipTarget(long t) {
  return (t == endpointUp) ? endpointDown : endpointUp;
}

// Median-of-5 filter to reject SPI glitch spikes.
uint16_t read_sg() {
  uint16_t s[5];
  for (int i = 0; i < 5; i++) s[i] = tmc5160::SG_RESULT();
  return autolee::median5(s);
}
static void fas_wait_for_stop() {
  while (stepper::isRunning()) {
    wdt_feed();
    delay(1);
  }
}

// ==========================================================================
//  ENDPOINT MATH
// ==========================================================================
void recomputeEffectiveEndpoints() {
  autolee::Endpoints e =
      autolee::computeEffectiveEndpoints(endpointsCalibrated, rawUp, rawDown, upOffsetSteps,
                                         downOffsetSteps, OFFSET_MIN, OFFSET_MAX, ENDPOINT_GUARD);
  if (!endpointsCalibrated) {
    endpointUp = 0;
    endpointDown = 0;
    return;
  }
  upOffsetSteps = e.upOffset;
  downOffsetSteps = e.downOffset;
  endpointUp = e.endpointUp;
  endpointDown = e.endpointDown;
}

// ==========================================================================
//  MOTION
// ==========================================================================
void startRunBetweenEndpoints() {
  if (!endpointsCalibrated) return;
  runState = RUNNING;
  runSGHighCount = 0;
  runSGLowCount = 0;
  lastDirectionChangeMs = millis();

  tmc5160::rms_current(RUN_CURRENT_MA);
  tmc5160::en_pwm_mode(false);
  tmc5160::TPWMTHRS(0);
  tmc5160::TCOOLTHRS(0xFFFFF);
  tmc5160::semin(0);
  tmc5160::semax(0);
  tmc5160::sgt((int8_t)(CAL_SGT < -64 ? -64 : (CAL_SGT > 63 ? 63 : CAL_SGT)));

  long pos = stepper::getCurrentPosition();
  if (nearPos(pos, endpointUp))
    currentTarget = endpointDown;
  else if (nearPos(pos, endpointDown))
    currentTarget = endpointUp;
  else
    currentTarget = (labs(pos - endpointUp) < labs(pos - endpointDown)) ? endpointUp : endpointDown;

  stepper::setSpeedInHz(ui_speed_hz);
  stepper::setAcceleration(RUN_DECEL);
  stepper::moveTo(currentTarget);
}

void requestGracefulStop() {
  runState = STOPPING;
  stopEntryMs = millis();
  currentTarget = endpointUp;
  stepper::setAcceleration(RUN_DECEL);
  stepper::moveTo(endpointUp);
}

void handleMotion() {
  if (runState == CALIBRATING || runState == STALLED || runState == HOMING) return;
  switch (runState) {
    case RUNNING: {
      long pos = stepper::getCurrentPosition();

      if (!stepper::isRunning()) {
        if (currentTarget == endpointDown && counter < 9999) {
          counter++;
          if (batchActive) {
            batchCount++;
            if (autolee::batchComplete(batchCount, batchTarget)) {
              webLog("Batch complete: %ld/%ld", (long)batchCount, (long)batchTarget);
              batchActive = false;
              requestGracefulStop();
              setRunButtonState(false);
              break;
            }
          }
        }
        currentTarget = flipTarget(currentTarget);
        lastDirectionChangeMs = millis();
        runSGHighCount = 0;
        runSGLowCount = 0;
        stepper::setSpeedInHz(ui_speed_hz);
        stepper::setAcceleration(RUN_DECEL);
        stepper::moveTo(currentTarget);
        break;
      }

      // --- Runtime stall detection ---
      if (RUN_SG_TRIP == 0) break;

      uint32_t sinceChange = millis() - lastDirectionChangeMs;

      uint32_t accelWindowMs = autolee::accelBlankMs(ui_speed_hz, RUN_DECEL);
      if (sinceChange < accelWindowMs) break;

      // Work zone: skip SG near DOWN where primer-seating resistance is normal.
      if (currentTarget == endpointDown &&
          autolee::inWorkZone(pos, endpointDown, SG_WORK_ZONE_STEPS)) {
        runSGHighCount = 0;
        runSGLowCount = 0;
        break;
      }

      {
        int32_t distToTarget = labs(pos - currentTarget);
        int32_t decelBlank = autolee::decelBlankSteps(ui_speed_hz, RUN_DECEL);
        if (distToTarget < decelBlank && runSGHighCount < RUN_SG_HIGH_NEEDED) {
          runSGHighCount = 0;
          runSGLowCount = 0;
          break;
        }
      }

      uint16_t sg = read_sg();
      if (sg <= 1) break;

      static uint32_t lastSGPrintMs = 0;
      if ((millis() - lastSGPrintMs) > 500) {
        int32_t distToTarget = labs(pos - currentTarget);
        webLog("RUN SG=%u trip=%u pos=%ld dist=%ld t=%lu hi=%u/%u", sg, RUN_SG_TRIP, pos,
               (long)distToTarget, (unsigned long)sinceChange, runSGHighCount,
               RUN_SG_HIGH_NEEDED);
        lastSGPrintMs = millis();
      }

      if (sg > RUN_SG_TRIP) {
        if (runSGHighCount < RUN_SG_HIGH_NEEDED + 4) runSGHighCount++;
        runSGLowCount = 0;

        webLog("SG HIGH=%u trip=%u cnt=%u pos=%ld t=%lu", sg, RUN_SG_TRIP, runSGHighCount, pos,
               (unsigned long)sinceChange);

        if (runSGHighCount >= RUN_SG_HIGH_NEEDED) {
          webLog("JAM! SG=%u trip=%u pos=%ld tgt=%ld cnt=%u", sg, RUN_SG_TRIP, pos, currentTarget,
                 runSGHighCount);

          stepper::forceStop();
          fas_wait_for_stop();

          stepper::setSpeedInHz(CREEP_HOME_SPEED);
          stepper::setAcceleration(CREEP_HOME_ACCEL);

          int32_t backoff =
              (currentTarget == endpointDown) ? -RUN_BACKOFF_STEPS : +RUN_BACKOFF_STEPS;
          stepper::move(backoff);
          fas_wait_for_stop();

          runState = STALLED;
          runSGHighCount = 0;
          runSGLowCount = 0;
          showJamScreen();
        }
      } else {
        runSGLowCount++;
        if (runSGLowCount >= 3) {
          runSGLowCount = 0;
          if (runSGHighCount > 0) runSGHighCount--;
        }
      }
      break;
    }

    case STOPPING: {
      long pos = stepper::getCurrentPosition();
      if (!stepper::isRunning() || nearPos(pos, endpointUp, 10)) {
        if (stepper::isRunning()) stepper::forceStop();
        runState = IDLE;
        break;
      }
      if ((millis() - stopEntryMs) > STOP_TIMEOUT_MS) {
        stepper::forceStop();
        runState = IDLE;
      }
      break;
    }

    default:
      break;
  }
}

// Safe creep home: slow sensorless move toward UP until mechanical stop, then
// back off and re-establish position. Uses the same move_until_stall() as
// calibration.
static bool move_until_stall(int dir, long &hit_pos);

void safeCreepHome() {
  runState = HOMING;

  tmc5160::rms_current(CAL_CURRENT_MA);
  stepper::setSpeedInHz(CAL_SPEED_HZ);
  stepper::setAcceleration(CAL_ACCEL);

  webLog("Creep home: start, I=%umA spd=%lu", CAL_CURRENT_MA, (unsigned long)CAL_SPEED_HZ);

  long hit_pos = 0;
  bool found = move_until_stall(-1, hit_pos);  // -1 = toward UP

  if (found) {
    webLog("Creep home: found stop at %ld", hit_pos);

    stepper::move(+300);
    fas_wait_for_stop();

    stepper::setCurrentPosition(0);
    rawUp = 0;

    recomputeEffectiveEndpoints();

    stepper::moveTo(endpointUp);
    fas_wait_for_stop();
  } else {
    webLog("Creep home: FAILED to find stop!");
  }

  tmc5160::rms_current(RUN_CURRENT_MA);
  stepper::setSpeedInHz(ui_speed_hz);
  stepper::setAcceleration(RUN_DECEL);

  runState = IDLE;

  webLog("Creep home: done pos=%ld", (long)stepper::getCurrentPosition());

  setRunButtonState(false);
  ui_update_main_warning();
  ui_update_tuning_numbers();
  ui_update_endpoint_edit_values();
}

// ==========================================================================
//  SENSORLESS CALIBRATION
// ==========================================================================
static bool move_until_stall(int dir, long &hit_pos) {
  const int32_t target = (dir > 0) ? +CAL_SEARCH_STEPS : -CAL_SEARCH_STEPS;
  const int32_t start_pos = stepper::getCurrentPosition();
  const uint32_t ignore_ms = autolee::calIgnoreMs(CAL_SPEED_HZ, CAL_ACCEL);
  const int32_t ignore_dst = autolee::calIgnoreDist(CAL_SPEED_HZ, CAL_ACCEL);

  tmc5160::en_pwm_mode(false);
  tmc5160::TPWMTHRS(0);
  tmc5160::TCOOLTHRS(0xFFFFF);
  int8_t sgt = (int8_t)(CAL_SGT < -64 ? -64 : (CAL_SGT > 63 ? 63 : CAL_SGT));
  tmc5160::sgt(sgt);

  webLog("MUS: dir=%d pos=%ld ign_ms=%lu ign_dst=%ld sgt=%d", dir, (long)start_pos,
         (unsigned long)ignore_ms, (long)ignore_dst, sgt);

  stepper::move(target);
  const uint32_t start_ms = millis();
  delay(5);

  bool baseline_started = false;
  uint32_t base_start_ms = 0, base_sum = 0;
  uint16_t base_cnt = 0;
  bool dyn_ready = false;
  uint16_t dyn_trip = CAL_ABS_MIN;
  uint8_t confirm_dyn = 0, confirm_early = 0;

  while (stepper::isRunning()) {
    const uint32_t now = millis();
    const uint32_t elapsed_ms = now - start_ms;
    const int32_t dist = labs(stepper::getCurrentPosition() - start_pos);
    const uint16_t sg = read_sg();

    static uint32_t lastMUSPrint = 0;
    if ((now - lastMUSPrint) > 400) {
      webLog("MUS: sg=%u dist=%ld el=%lu bl=%d dr=%d dtrip=%u", sg, (long)dist,
             (unsigned long)elapsed_ms, baseline_started, dyn_ready, dyn_trip);
      lastMUSPrint = now;
    }

    if (autolee::earlyArmed(
            {EARLY_WINDOW_MS, EARLY_WINDOW_DST_MAX, EARLY_MIN_TIME_MS, EARLY_MIN_MOVE_STEPS},
            elapsed_ms, dist)) {
      if (sg <= EARLY_TRIP) {
        if (++confirm_early >= CAL_HIT_CONFIRM) {
          webLog("MUS: EARLY HIT sg=%u pos=%ld", sg, (long)stepper::getCurrentPosition());
          stepper::forceStop();
          fas_wait_for_stop();
          hit_pos = stepper::getCurrentPosition();
          return true;
        }
      } else
        confirm_early = 0;
    }

    if (!baseline_started && autolee::baselineReady(elapsed_ms, dist, ignore_ms, ignore_dst)) {
      baseline_started = true;
      base_start_ms = now;
      base_sum = 0;
      base_cnt = 0;
      confirm_dyn = 0;
    }
    if (baseline_started && !dyn_ready) {
      base_sum += sg;
      if (base_cnt < 1000) base_cnt++;
      if ((now - base_start_ms) >= 200 && base_cnt > 0) {
        uint16_t baseline = autolee::baselineAverage(base_sum, base_cnt);
        dyn_trip = autolee::dynamicTrip(baseline, CAL_REL_DROP_Q8, CAL_ABS_MIN);
        dyn_ready = true;
        webLog("MUS: baseline=%u dyn_trip=%u", baseline, dyn_trip);
      }
    }

    if (dyn_ready) {
      if (sg <= dyn_trip) {
        if (++confirm_dyn >= CAL_HIT_CONFIRM) {
          webLog("MUS: DYN HIT sg=%u trip=%u pos=%ld", sg, dyn_trip,
                 (long)stepper::getCurrentPosition());
          stepper::forceStop();
          fas_wait_for_stop();
          hit_pos = stepper::getCurrentPosition();
          return true;
        }
      } else
        confirm_dyn = 0;
    }

    wdt_feed();
    delay(1);
  }
  hit_pos = stepper::getCurrentPosition();
  webLog("MUS: NO STALL DETECTED, ended at pos=%ld", hit_pos);
  return false;
}

bool return_home_up_safe() {
  if (!endpointsCalibrated) return false;

  stepper::setSpeedInHz(HOME_SPEED_HZ);
  stepper::setAcceleration(HOME_ACCEL);

  tmc5160::en_pwm_mode(false);
  tmc5160::TPWMTHRS(0);
  tmc5160::TCOOLTHRS(0xFFFFF);
  tmc5160::sgt((int8_t)(CAL_SGT < -64 ? -64 : (CAL_SGT > 63 ? 63 : CAL_SGT)));

  const uint32_t start_ms = millis();
  uint8_t retries = 0;
  long move_origin = stepper::getCurrentPosition();
  uint32_t move_start_ms = millis();
  uint8_t confirm_count = 0;

  stepper::moveTo(endpointUp);

  while (stepper::isRunning()) {
    const uint32_t now = millis();
    const long pos = stepper::getCurrentPosition();

    if ((now - start_ms) > HOME_TIMEOUT_MS) {
      webLog("Home: TIMEOUT");
      stepper::forceStop();
      fas_wait_for_stop();
      return false;
    }

    if (nearPos(pos, endpointUp, 20)) break;

    const int32_t moved = labs(pos - move_origin);
    const uint32_t time_moving = now - move_start_ms;

    if (time_moving >= HOME_MIN_MS && moved >= HOME_MIN_MOVE) {
      const uint16_t sg = read_sg();
      if (sg <= HOME_SG_TRIP) {
        if (++confirm_count >= HOME_CONFIRM) {
          webLog("Home: stall @%ld retry %d", pos, retries);
          stepper::forceStop();
          fas_wait_for_stop();
          stepper::move(+HOME_RELEASE_STEPS);
          fas_wait_for_stop();

          if (retries >= HOME_MAX_RETRIES) {
            webLog("Home: max retries");
            return false;
          }
          retries++;
          confirm_count = 0;
          move_origin = stepper::getCurrentPosition();
          move_start_ms = millis();
          stepper::moveTo(endpointUp);
        }
      } else
        confirm_count = 0;
    }

    wdt_feed();
    delay(1);
  }

  fas_wait_for_stop();
  long finalPos = stepper::getCurrentPosition();
  webLog("Home: pos=%ld tgt=%ld diff=%ld", finalPos, endpointUp, finalPos - endpointUp);
  return nearPos(finalPos, endpointUp, 50);
}

void setActiveProfile(uint8_t idx) {
  if (idx >= NUM_PROFILES) return;
  activeProfile = idx;
  stepper::setSpeedInHz(ui_speed_hz);
}

bool calibrateEndpointsSensorless() {
  runState = CALIBRATING;
  endpointsCalibrated = false;
  webLog("Calibration: start");
  if (stepper::isRunning()) {
    stepper::forceStop();
    fas_wait_for_stop();
  }

  const uint32_t saved_speed = ui_speed_hz;
  stepper::setSpeedInHz(CAL_SPEED_HZ);
  stepper::setAcceleration(CAL_ACCEL);
  tmc5160::rms_current(CAL_CURRENT_MA);

  stepper::move(+CAL_PREMOVE_DOWN_STEPS);
  fas_wait_for_stop();

  long hit_up = 0;
  if (!move_until_stall(-1, hit_up)) {
    webLog("Calibration: FAILED (no UP stop found)");
    tmc5160::rms_current(RUN_CURRENT_MA);
    stepper::setSpeedInHz(saved_speed);
    stepper::setAcceleration(RUN_DECEL);
    runState = IDLE;
    return false;
  }

  stepper::move(+300);
  fas_wait_for_stop();
  stepper::setCurrentPosition(0);
  rawUp = 0;

  long hit_down = 0;
  if (!move_until_stall(+1, hit_down)) {
    webLog("Calibration: FAILED (no DOWN stop found)");
    tmc5160::rms_current(RUN_CURRENT_MA);
    stepper::setSpeedInHz(saved_speed);
    stepper::setAcceleration(RUN_DECEL);
    runState = IDLE;
    return false;
  }

  stepper::move(-300);
  fas_wait_for_stop();
  rawDown = stepper::getCurrentPosition();

  webLog("CAL: up=%ld dn=%ld travel=%ld", rawUp, rawDown, rawDown - rawUp);
  endpointsCalibrated = true;
  upOffsetSteps = 0;
  downOffsetSteps = DOWN_OFFSET_DEFAULT;
  recomputeEffectiveEndpoints();
  tmc5160::rms_current(RUN_CURRENT_MA);

  stepper::setSpeedInHz(saved_speed);
  stepper::setAcceleration(RUN_DECEL);
  stepper::moveTo(endpointUp);
  fas_wait_for_stop();

  runState = IDLE;
  return true;
}
