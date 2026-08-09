// ============================================================================
//  AutoLee - motion.cpp (ESP-IDF port)
//  Faithful port of the original Arduino firmware's motion algorithm. Plumbing swapped
//  (TMCStepper->tmc5160::, FastAccelStepper->stepper::, Arduino
//  millis()/delay()->esp_timer/vTaskDelay), safety logic unchanged.
//
//  *** UNVERIFIED ON HARDWARE - see main/stepper.h's warning. Bench-verify
//  calibration, run, jam trigger + backoff, and return-home before trusting
//  this on the real press. ***
// ============================================================================
#include "motion.h"
#include "globals.h"
#include "motion_state.h"
#include "stepper.h"
#include "tmc5160_ctrl.h"
#include "ui_touch.h"

#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"

// Pure, host-tested logic shared with the native unit tests (host_test/).
#include "endpoint_math.h"
#include "sg_filter.h"
#include "stall_monitor.h"
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

// Everything in this file runs on pump_task, which OWNS the shared motion state
// `g_motion`: reads of it here are deliberately unlocked (see motion_state.h's
// rules), while every write takes a motion_state::Guard - grouped so a
// snapshotting reader (web / SSE / LVGL) can never observe a half-applied
// update. Guards stay off the stepper/SPI/webLog calls: compute into locals
// first, then take the guard.

// Shared SPI bus is initialized by display_touch.cpp (SCK=1, MOSI=2, MISO=3);
// this only adds the TMC5160 as a device on it and brings up the stepper.
// Mirrors the original Arduino firmware's setup(): SPI.begin +
// driver.begin/toff/microsteps/... + engine.init.
void motion_init() {
  tmc5160::init(SPI2_HOST, R_SENSE);
  stepper::init((gpio_num_t)STEP_PIN, (gpio_num_t)DIR_PIN, (gpio_num_t)ENABLE_PIN);
  tmc5160::rms_current(g_motion.runCurrentMa);
}

// ==========================================================================
//  UTILITY
// ==========================================================================
static inline bool nearPos(long a, long b, long tol = 2) {
  return labs(a - b) <= tol;
}
static inline long flipTarget(long t) {
  return (t == g_motion.endpointUp) ? g_motion.endpointDown : g_motion.endpointUp;
}

// Runtime jam detection uses the host-tested StallCounter (review finding #4:
// tested == shipped) instead of an inline copy of the same sliding-counter
// logic. runSGHighCount/runSGLowCount are mirrored from it purely so the
// telemetry lines keep reporting the same numbers.
static autolee::StallCounter s_stall(RUN_SG_HIGH_NEEDED, RUN_SG_HIGH_SATURATION_MARGIN,
                                     RUN_SG_LOW_DECAY_COUNT);

static inline void resetStallCounter() {
  s_stall.reset();
  motion_state::Guard g;
  g_motion.runSGHighCount = 0;
  g_motion.runSGLowCount = 0;
}

static inline int8_t clampSgt(int v) {
  return (int8_t)(v < SGT_MIN ? SGT_MIN : (v > SGT_MAX ? SGT_MAX : v));
}

// Put the TMC5160 into sensorless (StallGuard2) mode: SpreadCycle (StealthChop
// off), CoolStep threshold wide open so SG2 is always active, and the SGT
// threshold applied. Shared by run, calibration, and homing - all fields are
// independent COOLCONF/PWMCONF register fields (each a read-modify-write), so
// callers may set semin/semax etc. separately before/after without ordering
// concerns.
static inline void tmc_enter_sensorless_mode(int8_t sgt) {
  tmc5160::en_pwm_mode(false);
  tmc5160::TPWMTHRS(0);
  tmc5160::TCOOLTHRS(0xFFFFF);
  tmc5160::sgt(sgt);
}

// Median-of-5 filter to reject SPI glitch spikes.
uint16_t read_sg() {
  uint16_t s[5];
  for (int i = 0; i < 5; i++) s[i] = tmc5160::SG_RESULT();
  return autolee::median5(s);
}
// Waits for the current move to finish. Bounded: an unbounded version would
// feed the task watchdog from inside its own spin, so a stepper that never
// clears s_running (e.g. move_task wedged in rmt_tx_wait_all_done) would hang
// pump_task forever *and* suppress the watchdog that exists to catch exactly
// that. On timeout it escalates to forceStop() and gives the move a short
// grace period; if even that doesn't clear, it gives up and returns false so
// the caller can treat the axis as untrustworthy.
static bool fas_wait_for_stop() {
  const uint32_t start = millis();
  while (stepper::isRunning()) {
    if ((millis() - start) > MOVE_WAIT_TIMEOUT_MS) {
      webLogLevel(LogLevel::Error, "Motion", "Move did not stop within %lums - forcing stop",
                  (unsigned long)MOVE_WAIT_TIMEOUT_MS);
      stepper::forceStop();

      const uint32_t grace = millis();
      while (stepper::isRunning() && (millis() - grace) < MOVE_STOP_GRACE_MS) {
        wdt_feed();
        delay(1);
      }
      if (stepper::isRunning()) {
        webLogLevel(LogLevel::Error, "Motion",
                    "Stepper still running after forceStop - position reference lost");
        motion_state::Guard g;
        g_motion.positionReferenceStale = true;
      }
      return false;
    }
    wdt_feed();
    delay(1);
  }
  return true;
}

// ==========================================================================
//  ENDPOINT MATH
// ==========================================================================
// Callable from any task (the web and touch endpoint-offset handlers call it
// straight after nudging an offset), so the read-modify-write of the offsets
// plus both effective endpoints is done as one guarded transaction:
// autolee::computeEffectiveEndpoints() is pure math, safe inside the section.
// Callers must NOT already hold the guard.
void recomputeEffectiveEndpoints() {
  motion_state::Guard g;
  autolee::Endpoints e = autolee::computeEffectiveEndpoints(
      g_motion.endpointsCalibrated, g_motion.rawUp, g_motion.rawDown, g_motion.upOffsetSteps,
      g_motion.downOffsetSteps, OFFSET_MIN, OFFSET_MAX, ENDPOINT_GUARD);
  if (!g_motion.endpointsCalibrated) {
    g_motion.endpointUp = 0;
    g_motion.endpointDown = 0;
    return;
  }
  g_motion.upOffsetSteps = e.upOffset;
  g_motion.downOffsetSteps = e.downOffset;
  g_motion.endpointUp = e.endpointUp;
  g_motion.endpointDown = e.endpointDown;
}

// ==========================================================================
//  MOTION
// ==========================================================================
void startRunBetweenEndpoints() {
  if (!g_motion.endpointsCalibrated) return;
  // Calibrated, but the axis may not be where the endpoints assume: a restored
  // calibration (settings_store) comes back with the stepper counter at 0 while
  // the carriage sits wherever a power cut left it, so running would drive to
  // targets offset from reality. Refuse until a Return Home (or a fresh
  // calibration) re-zeroes against the UP hard stop. Bail out before touching
  // the TMC or the stepper, like the rejected-transition path below.
  if (g_motion.positionReferenceStale) {
    webLogLevel(LogLevel::Warn, "Motion",
                "Start refused: position reference unconfirmed - return home first");
    return;
  }
  const uint32_t now = millis();
  bool started;
  {
    motion_state::Guard g;
    started = applyMotorEventLocked(autolee::MotorEvent::Start);
    if (started) g_motion.lastDirectionChangeMs = now;
  }
  // Rejected => not IDLE (e.g. STALLED: the tested table's "must home first"
  // rule). Bail out before touching the TMC or the stepper.
  if (!started) {
    webLog("Motion", "Start ignored in state %u", (unsigned)g_motion.runState);
    return;
  }
  resetStallCounter();

  // sg_trip == 0 turns runtime jam detection off entirely (see handleMotion's
  // early break). That is a legitimate setting - jam detection guards brass,
  // not people - but it persists across reboots and was previously silent, so
  // a press could run indefinitely with no stall detection and nothing
  // anywhere saying so. Say it, loudly, on every run start.
  if (RUN_SG_TRIP == 0) {
    webLogLevel(LogLevel::Warn, "Motion",
                "Jam detection is DISABLED (sg_trip=0) - starting run with no stall protection");
  }

  tmc5160::rms_current(g_motion.runCurrentMa);
  tmc5160::semin(0);
  tmc5160::semax(0);
  tmc_enter_sensorless_mode(clampSgt(CAL_SGT));

  const long pos = stepper::getCurrentPosition();
  const long up = g_motion.endpointUp;
  const long dn = g_motion.endpointDown;
  long target;
  if (nearPos(pos, up))
    target = dn;
  else if (nearPos(pos, dn))
    target = up;
  else
    target = (labs(pos - up) < labs(pos - dn)) ? up : dn;
  {
    motion_state::Guard g;
    g_motion.currentTarget = target;
  }

  stepper::setSpeedInHz(ui_speed_hz);
  stepper::setAcceleration(RUN_DECEL);
  stepper::moveTo(target);
}

// Only meaningful while RUNNING, which is also the only state the tested table
// accepts GracefulStop from. A rejected request is deliberately a no-op rather
// than "stop anyway": from STOPPING the decel move toward UP is already in
// flight (and its timeout already armed), and from IDLE/STALLED there is no
// endpoint run to stop - issuing moveTo(endpointUp) out of STALLED would be a
// fast unguarded move away from a jam, which is exactly what safeCreepHome()
// exists to avoid. So this narrows what a stray stop can do; it does not weaken
// the stop path for the state that matters.
void requestGracefulStop() {
  const uint32_t now = millis();
  long up;
  bool stopping;
  {
    motion_state::Guard g;
    stopping = applyMotorEventLocked(autolee::MotorEvent::GracefulStop);
    up = g_motion.endpointUp;
    if (stopping) {
      g_motion.stopEntryMs = now;
      g_motion.currentTarget = up;
    }
  }
  if (!stopping) {
    webLog("Motion", "Graceful stop ignored in state %u", (unsigned)g_motion.runState);
    return;
  }
  stepper::setAcceleration(RUN_DECEL);
  stepper::moveTo(up);
}

void handleMotion() {
  const RunState state = g_motion.runState;
  if (state == CALIBRATING || state == STALLED || state == HOMING) return;
  switch (state) {
    case RUNNING: {
      long pos = stepper::getCurrentPosition();

      if (!stepper::isRunning()) {
        if (g_motion.currentTarget == g_motion.endpointDown) {
          // Duration of the stroke that just finished. lastDirectionChangeMs is
          // stamped where the move toward this target was issued (the flip
          // below, or startRunBetweenEndpoints()), so this is exactly the
          // UP->DOWN stroke time. Computed here, outside the Guard, and only on
          // this arm: the jam path never reaches it, so jam + backoff time is
          // never averaged in as if it were a normal cycle.
          const uint32_t strokeMs = millis() - g_motion.lastDirectionChangeMs;
          bool complete = false;
          int32_t doneCount = 0, doneTarget = 0;
          {
            // One guarded transaction: the cycle counter, the health counters,
            // the batch counter and the batchActive flag must never be observed
            // half-updated by a snapshotting reader (web/SSE/LVGL).
            motion_state::Guard g;
            // COUNTER_MAX caps the *display* counter only (cosmetic, 4-digit
            // field). Batch counting must NOT be gated by it, or a batch stalls
            // forever once the lifetime counter saturates. (Fixed upstream in
            // Karl's v1.10.0; this port inherited the v1.8 bug.)
            if (g_motion.counter < COUNTER_MAX) g_motion.counter++;
            // Lifetime statistics (diagnostics only - nothing in the motion
            // logic reads them). Unlike `counter` these are NOT capped by
            // COUNTER_MAX and are never zeroed by Reset Counter: the display cap
            // and the reset are both properties of the operator-facing counter,
            // and letting either reach the lifetime figures is what made the
            // health block's mean stroke time drift.
            g_motion.lifetimeCycles++;
            g_motion.totalCycleTimeMs += strokeMs;
            if (strokeMs > g_motion.longestCycleMs) g_motion.longestCycleMs = strokeMs;
            if (g_motion.batchActive) {
              g_motion.batchCount++;
              if (autolee::batchComplete(g_motion.batchCount, g_motion.batchTarget)) {
                complete = true;
                g_motion.batchActive = false;
                doneCount = g_motion.batchCount;
                doneTarget = g_motion.batchTarget;
              }
            }
          }
          if (complete) {
            webLog("Motion", "Batch complete: %ld/%ld", (long)doneCount, (long)doneTarget);
            requestGracefulStop();
            setRunButtonState(false);
            break;
          }
        }
        const uint32_t now = millis();
        long target;
        {
          motion_state::Guard g;
          target = flipTarget(g_motion.currentTarget);
          g_motion.currentTarget = target;
          g_motion.lastDirectionChangeMs = now;
        }
        resetStallCounter();
        stepper::setSpeedInHz(ui_speed_hz);
        stepper::setAcceleration(RUN_DECEL);
        stepper::moveTo(target);
        break;
      }

      // --- Runtime stall detection ---
      if (RUN_SG_TRIP == 0) break;

      uint32_t sinceChange = millis() - g_motion.lastDirectionChangeMs;

      uint32_t accelWindowMs = autolee::accelBlankMs(ui_speed_hz, RUN_DECEL);
      if (sinceChange < accelWindowMs) break;

      // Work zone: skip SG near DOWN where primer-seating resistance is normal.
      if (g_motion.currentTarget == g_motion.endpointDown &&
          autolee::inWorkZone(pos, g_motion.endpointDown, g_motion.sgWorkZoneSteps)) {
        resetStallCounter();
        break;
      }

      // Decel blank: skip SG monitoring inside the deceleration window, UNLESS
      // we already have jam evidence to carry through. The carry-through must
      // key on ANY prior high reading (count > 0), not on count >=
      // RUN_SG_HIGH_NEEDED: reaching NEEDED would already have fired the jam
      // above, so the old `< RUN_SG_HIGH_NEEDED` test was always true here and
      // silently discarded partial jam evidence entering the decel window.
      // (Fixed upstream in Karl's v1.10.0; this port inherited the v1.8 bug.)
      {
        int32_t distToTarget = labs(pos - g_motion.currentTarget);
        int32_t decelBlank = autolee::decelBlankSteps(ui_speed_hz, RUN_DECEL);
        if (distToTarget < decelBlank && g_motion.runSGHighCount == 0) {
          motion_state::Guard g;
          g_motion.runSGLowCount = 0;
          break;
        }
      }

      uint16_t sg = read_sg();
      if (sg <= 1) break;

      static uint32_t lastSGPrintMs = 0;
      if ((millis() - lastSGPrintMs) > RUN_SG_LOG_INTERVAL_MS) {
        int32_t distToTarget = labs(pos - g_motion.currentTarget);
        webLogLevel(LogLevel::Debug, "Motion", "RUN SG=%u trip=%u pos=%ld dist=%ld t=%lu hi=%u/%u",
                    sg, RUN_SG_TRIP, pos, (long)distToTarget, (unsigned long)sinceChange,
                    g_motion.runSGHighCount, RUN_SG_HIGH_NEEDED);
        lastSGPrintMs = millis();
      }

      const bool jam = s_stall.update(sg, RUN_SG_TRIP);
      {
        // Telemetry mirror of the host-tested counter - one guard so the pair
        // is never seen half-updated.
        motion_state::Guard g;
        g_motion.runSGHighCount = s_stall.highCount();
        g_motion.runSGLowCount = s_stall.lowCount();
      }

      if (sg > RUN_SG_TRIP) {
        webLogLevel(LogLevel::Warn, "Motion", "SG HIGH=%u trip=%u cnt=%u pos=%ld t=%lu", sg,
                    RUN_SG_TRIP, g_motion.runSGHighCount, pos, (unsigned long)sinceChange);

        if (jam) {
          webLogLevel(LogLevel::Error, "Motion", "JAM! SG=%u trip=%u pos=%ld tgt=%ld cnt=%u", sg,
                      RUN_SG_TRIP, pos, g_motion.currentTarget, g_motion.runSGHighCount);

          stepper::forceStop();
          fas_wait_for_stop();

          stepper::setSpeedInHz(CREEP_HOME_SPEED);
          stepper::setAcceleration(CREEP_HOME_ACCEL);

          int32_t backoff = (g_motion.currentTarget == g_motion.endpointDown) ? -RUN_BACKOFF_STEPS
                                                                              : +RUN_BACKOFF_STEPS;
          stepper::move(backoff);
          fas_wait_for_stop();

          bool latched;
          {
            motion_state::Guard g;
            latched = applyMotorEventLocked(autolee::MotorEvent::Jam);
            // Lifetime jam count, in the same transaction as the latch so the
            // two can never be seen to disagree. uint16_t: wraps to 0 after
            // 65536 lifetime jams, which is well past diagnostic relevance.
            g_motion.stallCount++;
            // Fail-safe: this arm only runs with runState == RUNNING, from which
            // the tested table always accepts Jam, so `latched` is true. If a
            // future change ever broke that invariant, leaving the state as-is
            // would let the loop below flip target and drive on into the jam -
            // so force the jam latch regardless. STALLED is the most
            // restrictive state (blocks Start, requires a home first), so this
            // fallback can only ever be more conservative than the table.
            if (!latched) g_motion.runState = STALLED;
          }
          if (!latched)
            webLogLevel(LogLevel::Error, "Motion",
                        "BUG: Jam event rejected by FSM - latched STALLED anyway");
          resetStallCounter();
          showJamScreen();
        }
      }
      break;
    }

    case STOPPING: {
      // Both exits below are ReachedHome/StopTimeout out of STOPPING, which the
      // tested table always accepts, and the stepper is force-stopped either
      // way - so the transition result needs no separate handling here.
      long pos = stepper::getCurrentPosition();
      if (!stepper::isRunning() || nearPos(pos, g_motion.endpointUp, STOP_ARRIVAL_TOL)) {
        if (stepper::isRunning()) stepper::forceStop();
        motion_state::Guard g;
        applyMotorEventLocked(autolee::MotorEvent::ReachedHome);
        break;
      }
      if ((millis() - g_motion.stopEntryMs) > STOP_TIMEOUT_MS) {
        stepper::forceStop();
        motion_state::Guard g;
        applyMotorEventLocked(autolee::MotorEvent::StopTimeout);
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
  bool homing;
  {
    motion_state::Guard g;
    homing = applyMotorEventLocked(autolee::MotorEvent::ReturnHome);
  }
  // Rejected => neither STALLED nor IDLE (the two states the tested table
  // accepts ReturnHome from - IDLE so a reboot-stale position reference can be
  // re-established). Bail out before re-currenting the TMC or creeping the
  // press; the caller (motion_cmd) gates on the same table, this is it being
  // enforced at the point of no return.
  if (!homing) {
    webLog("Motion", "Return home ignored in state %u", (unsigned)g_motion.runState);
    return;
  }

  tmc5160::rms_current(CAL_CURRENT_MA);
  stepper::setSpeedInHz(CAL_SPEED_HZ);
  stepper::setAcceleration(CAL_ACCEL);

  webLog("Motion", "Creep home: start, I=%umA spd=%lu", CAL_CURRENT_MA,
         (unsigned long)CAL_SPEED_HZ);

  long hit_pos = 0;
  bool found = move_until_stall(-1, hit_pos);  // -1 = toward UP

  if (found) {
    webLog("Motion", "Creep home: found stop at %ld", hit_pos);

    stepper::move(+CAL_OVERSHOOT_BACKOFF_STEPS);
    fas_wait_for_stop();

    stepper::setCurrentPosition(0);
    {
      motion_state::Guard g;
      g_motion.rawUp = 0;
    }

    recomputeEffectiveEndpoints();

    stepper::moveTo(g_motion.endpointUp);
    fas_wait_for_stop();
  } else {
    webLogLevel(LogLevel::Error, "Motion", "Creep home: FAILED to find stop!");
  }

  tmc5160::rms_current(g_motion.runCurrentMa);
  stepper::setSpeedInHz(ui_speed_hz);
  stepper::setAcceleration(RUN_DECEL);

  {
    motion_state::Guard g;
    applyMotorEventLocked(autolee::MotorEvent::HomeDone);  // HOMING -> IDLE
    // A confirmed UP hard stop + re-zero IS the position reference, so it goes
    // live in the same transaction as the state change. On failure the search
    // ran its full length without finding a stop, so the counter is no longer
    // trustworthy - latch it stale rather than leaving a previously-good
    // reference standing.
    g_motion.positionReferenceStale = !found;
  }

  webLog("Motion", "Creep home: done pos=%ld", (long)stepper::getCurrentPosition());

  setRunButtonState(false);
  ui_update_main_warning();
  ui_update_tuning_numbers();
  ui_update_endpoint_edit_values();
  // Tell the jam screen how this went and let it hand the display back, so a
  // jam is not a dead end on the touch UI (it navigates only if the jam screen
  // is still showing).
  ui_jam_recovery_finished(found);
}

// ==========================================================================
//  SENSORLESS CALIBRATION
// ==========================================================================
static bool move_until_stall(int dir, long &hit_pos) {
  const int32_t target = (dir > 0) ? +CAL_SEARCH_STEPS : -CAL_SEARCH_STEPS;
  const int32_t start_pos = stepper::getCurrentPosition();
  const uint32_t ignore_ms = autolee::calIgnoreMs(CAL_SPEED_HZ, CAL_ACCEL);
  const int32_t ignore_dst = autolee::calIgnoreDist(CAL_SPEED_HZ, CAL_ACCEL);

  int8_t sgt = clampSgt(CAL_SGT);
  tmc_enter_sensorless_mode(sgt);

  webLogLevel(LogLevel::Debug, "Motion", "MUS: dir=%d pos=%ld ign_ms=%lu ign_dst=%ld sgt=%d", dir,
              (long)start_pos, (unsigned long)ignore_ms, (long)ignore_dst, sgt);

  stepper::move(target);
  const uint32_t start_ms = millis();
  delay(5);

  bool baseline_started = false;
  uint32_t base_start_ms = 0, base_sum = 0;
  uint16_t base_cnt = 0;
  bool dyn_ready = false;
  uint16_t dyn_trip = CAL_ABS_MIN;
  // Host-tested ConfirmCounter instead of inline ++/reset counters (#4).
  autolee::ConfirmCounter confirm_early(CAL_HIT_CONFIRM);
  autolee::ConfirmCounter confirm_dyn(CAL_HIT_CONFIRM);

  while (stepper::isRunning()) {
    const uint32_t now = millis();
    const uint32_t elapsed_ms = now - start_ms;
    const int32_t dist = labs(stepper::getCurrentPosition() - start_pos);
    const uint16_t sg = read_sg();

    static uint32_t lastMUSPrint = 0;
    if ((now - lastMUSPrint) > CAL_MUS_LOG_INTERVAL_MS) {
      webLogLevel(LogLevel::Debug, "Motion", "MUS: sg=%u dist=%ld el=%lu bl=%d dr=%d dtrip=%u", sg,
                  (long)dist, (unsigned long)elapsed_ms, baseline_started, dyn_ready, dyn_trip);
      lastMUSPrint = now;
    }

    if (autolee::earlyArmed(
            {EARLY_WINDOW_MS, EARLY_WINDOW_DST_MAX, EARLY_MIN_TIME_MS, EARLY_MIN_MOVE_STEPS},
            elapsed_ms, dist)) {
      if (confirm_early.feed(sg <= EARLY_TRIP)) {
        webLogLevel(LogLevel::Debug, "Motion", "MUS: EARLY HIT sg=%u pos=%ld", sg,
                    (long)stepper::getCurrentPosition());
        stepper::forceStop();
        fas_wait_for_stop();
        hit_pos = stepper::getCurrentPosition();
        return true;
      }
    }

    if (!baseline_started && autolee::baselineReady(elapsed_ms, dist, ignore_ms, ignore_dst)) {
      baseline_started = true;
      base_start_ms = now;
      base_sum = 0;
      base_cnt = 0;
      confirm_dyn.reset();
    }
    if (baseline_started && !dyn_ready) {
      base_sum += sg;
      if (base_cnt < 1000) base_cnt++;
      if ((now - base_start_ms) >= 200 && base_cnt > 0) {
        uint16_t baseline = autolee::baselineAverage(base_sum, base_cnt);
        dyn_trip = autolee::dynamicTrip(baseline, CAL_REL_DROP_Q8, CAL_ABS_MIN);
        dyn_ready = true;
        webLogLevel(LogLevel::Debug, "Motion", "MUS: baseline=%u dyn_trip=%u", baseline, dyn_trip);
      }
    }

    if (dyn_ready) {
      if (confirm_dyn.feed(sg <= dyn_trip)) {
        webLogLevel(LogLevel::Debug, "Motion", "MUS: DYN HIT sg=%u trip=%u pos=%ld", sg, dyn_trip,
                    (long)stepper::getCurrentPosition());
        stepper::forceStop();
        fas_wait_for_stop();
        hit_pos = stepper::getCurrentPosition();
        return true;
      }
    }

    wdt_feed();
    delay(1);
  }
  hit_pos = stepper::getCurrentPosition();
  webLogLevel(LogLevel::Debug, "Motion", "MUS: NO STALL DETECTED, ended at pos=%ld", hit_pos);
  return false;
}

void setActiveProfile(uint8_t idx) {
  if (idx >= NUM_PROFILES) return;
  {
    motion_state::Guard g;
    g_motion.activeProfile = idx;
  }
  stepper::setSpeedInHz(ui_speed_hz);
}

bool calibrateEndpointsSensorless() {
  bool entered;
  {
    motion_state::Guard g;
    entered = applyMotorEventLocked(autolee::MotorEvent::Calibrate);
    // Invalidating the endpoints belongs to the same transaction - but only if
    // we actually entered CALIBRATING, otherwise a rejected request would
    // discard a good calibration.
    if (entered) g_motion.endpointsCalibrated = false;
  }
  // Rejected => not IDLE (running, stopping, stalled, homing or already
  // calibrating). Never start a blind sensorless search from those states.
  if (!entered) {
    webLog("Motion", "Calibration ignored in state %u", (unsigned)g_motion.runState);
    return false;
  }
  webLog("Motion", "Calibration: start");
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
    webLogLevel(LogLevel::Error, "Motion", "Calibration: FAILED (no UP stop found)");
    tmc5160::rms_current(g_motion.runCurrentMa);
    stepper::setSpeedInHz(saved_speed);
    stepper::setAcceleration(RUN_DECEL);
    motion_state::Guard g;
    applyMotorEventLocked(autolee::MotorEvent::CalibrationDone);  // CALIBRATING -> IDLE
    // The search ran its full length without finding a stop: nothing re-zeroed
    // the axis, so whatever reference existed before is no longer trustworthy.
    g_motion.positionReferenceStale = true;
    return false;
  }

  stepper::move(+CAL_OVERSHOOT_BACKOFF_STEPS);
  fas_wait_for_stop();
  stepper::setCurrentPosition(0);
  {
    motion_state::Guard g;
    g_motion.rawUp = 0;
  }

  long hit_down = 0;
  if (!move_until_stall(+1, hit_down)) {
    webLogLevel(LogLevel::Error, "Motion", "Calibration: FAILED (no DOWN stop found)");
    tmc5160::rms_current(g_motion.runCurrentMa);
    stepper::setSpeedInHz(saved_speed);
    stepper::setAcceleration(RUN_DECEL);
    motion_state::Guard g;
    applyMotorEventLocked(autolee::MotorEvent::CalibrationDone);  // CALIBRATING -> IDLE
    // The UP stop was found and re-zeroed, but the DOWN search then ran its
    // full length; the counter has travelled an unbounded distance since.
    g_motion.positionReferenceStale = true;
    return false;
  }

  stepper::move(-CAL_OVERSHOOT_BACKOFF_STEPS);
  fas_wait_for_stop();
  const long measuredDown = stepper::getCurrentPosition();
  {
    motion_state::Guard g;
    g_motion.rawDown = measuredDown;
  }

  webLog("Motion", "CAL: up=%ld dn=%ld travel=%ld", g_motion.rawUp, g_motion.rawDown,
         g_motion.rawDown - g_motion.rawUp);
  {
    // The "calibrated" flag and the offsets it validates go live together.
    motion_state::Guard g;
    g_motion.endpointsCalibrated = true;
    // Both hard stops were just found and the axis re-zeroed at UP, so the
    // position reference is fresh by definition.
    g_motion.positionReferenceStale = false;
    g_motion.upOffsetSteps = 0;
    g_motion.downOffsetSteps = DOWN_OFFSET_DEFAULT;
  }
  recomputeEffectiveEndpoints();
  tmc5160::rms_current(g_motion.runCurrentMa);

  stepper::setSpeedInHz(saved_speed);
  stepper::setAcceleration(RUN_DECEL);
  stepper::moveTo(g_motion.endpointUp);
  fas_wait_for_stop();

  {
    motion_state::Guard g;
    applyMotorEventLocked(autolee::MotorEvent::CalibrationDone);  // CALIBRATING -> IDLE
    // Lifetime successful-calibration count, in the same transaction as the
    // transition. Only this path bumps it - the two `return false` arms above
    // never found both hard stops, so nothing was calibrated.
    g_motion.calibrationCount++;
  }
  return true;
}
