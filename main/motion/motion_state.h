#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "config.h"  // SpeedProfile, NUM_PROFILES

// ============================================================================
//  Cross-task motion state - the READ half of the concurrency rework
//  (motion_cmd.h is the WRITE half: commands flowing INTO pump_task).
//
//  These fields used to be ~20 loose, unsynchronized globals in globals.h.
//  pump_task writes them from handleMotion()/calibration/homing while the HTTP
//  task (buildStateJSON), sse_task (same, via broadcastState) and the LVGL task
//  (ui_touch's label updates) read them concurrently. With real preemption a
//  reader could see a half-updated set - e.g. batchCount already incremented
//  but batchActive not yet cleared, or endpointUp updated with endpointDown
//  still stale - and on a 32-bit target a `long`/`int32_t` read is only
//  accidentally atomic.
//
//  Rules (checkable by grepping for `g_motion`):
//
//   1. pump_task owns the state. It may READ `g_motion` directly, unlocked -
//      nothing else writes it behind pump_task's back except under the lock,
//      and pump_task cannot race itself.
//   2. EVERY write to `g_motion`, from ANY task (pump_task included), happens
//      under `motion_state::Guard` (a portENTER_CRITICAL/portEXIT_CRITICAL
//      spinlock section). Group writes that belong together into ONE guard so
//      readers never observe a partial update.
//   3. Every read from any task OTHER than pump_task goes through
//      `motion_state::snapshot()`, which copies the whole struct under the
//      lock. Readers then work off their local copy - self-consistent, and no
//      lock held while they call LVGL / build JSON / touch a socket.
//
//  Critical sections here must stay tiny and must contain NO calls that can
//  block, log, allocate, drive the stepper or touch the shared SPI bus - the
//  section disables interrupts, and stalling in one would be far worse than
//  the race it closes. Compute into locals first, then take the guard.
// ============================================================================

enum RunState : uint8_t { IDLE, RUNNING, STOPPING, CALIBRATING, STALLED, HOMING };

// One struct instead of loose globals so that every access site is visibly a
// `g_motion.` access, and the ownership/locking rules above can be audited by
// grep. Trivially copyable on purpose: snapshot() is a plain struct copy.
struct MotionState {
  RunState runState = IDLE;
  long currentTarget = 0;
  uint32_t stopEntryMs = 0;

  long rawUp = 0, rawDown = 0;
  long endpointUp = 0, endpointDown = 0;
  bool endpointsCalibrated = false;
  // The endpoints above are trusted, but where the carriage physically IS is
  // not: the stepper's counter always comes up at 0 on boot while the carriage
  // sits wherever a power cut or watchdog reset left it. Set when a stored
  // calibration is restored (settings_store.h), cleared only by an action that
  // re-establishes ground truth against the UP hard stop - a successful
  // safeCreepHome() or a fresh calibration. While it is set,
  // startRunBetweenEndpoints() refuses to start.
  bool positionReferenceStale = false;
  long counter = 0;

  uint32_t lastDirectionChangeMs = 0;
  // Mirrored from motion.cpp's host-tested StallCounter purely for telemetry.
  uint8_t runSGHighCount = 0;
  uint8_t runSGLowCount = 0;

  bool batchActive = false;
  int32_t batchCount = 0;
  int32_t batchTarget = 0;

  int32_t upOffsetSteps = 0;
  int32_t downOffsetSteps = 0;

  uint8_t activeProfile = 1;  // default to Normal
  SpeedProfile profiles[NUM_PROFILES] = {
      {"Slow", 15000, 350},
      {"Normal", 35000, 15},
      {"Fast", 45000, 1},
  };

  uint16_t runCurrentMa = 3500;
  int32_t sgWorkZoneSteps = 5500;

  // --- Lifetime health counters (persisted; see main/settings_blob.h) -------
  // Support/diagnostics only - nothing in the motion logic reads these, they
  // are never a gate on anything. Written under the same Guard as the event
  // that produced them (motion.cpp's jam latch, stroke completion and
  // successful calibration; web_server.cpp's OTA success; settings_store's
  // load()), so a snapshotting reader never sees a counter that disagrees with
  // the state change it belongs to. `counter` above is the "successful cycles"
  // statistic and is deliberately not duplicated here.
  //
  // The uint16_t counters wrap at 65535. Accepted: 65536 lifetime jams / OTA
  // updates / calibrations / boots is far past the point where the exact number
  // matters, and saturating arithmetic inside a critical section would cost
  // more than the edge case is worth.
  uint32_t totalCycleTimeMs = 0;  // summed duration of successful UP->DOWN strokes
  uint32_t longestCycleMs = 0;    // longest single successful stroke
  uint16_t stallCount = 0;        // jams that latched STALLED
  uint16_t calibrationCount = 0;  // successful calibrations
  uint16_t otaCount = 0;          // successful OTA updates
  uint16_t resetCount = 0;        // boots
};

// Single definition site: motion_state.cpp.
extern MotionState g_motion;

namespace motion_state {

// A FreeRTOS spinlock rather than a mutex: the protected sections are a handful
// of stores or one struct copy, so blocking primitives would cost more than the
// work, and pump_task must never risk waiting on a lower-priority task holding
// a mutex. (motion_cmd uses std::atomic for its single-flag handoffs; a
// multi-field snapshot needs mutual exclusion, which atomics cannot give.)
extern portMUX_TYPE g_lock;

inline void lock() {
  portENTER_CRITICAL(&g_lock);
}
inline void unlock() {
  portEXIT_CRITICAL(&g_lock);
}

// RAII critical section. Keep the scope as small as possible - see the header
// comment on what must not happen inside one.
struct Guard {
  Guard() { lock(); }
  ~Guard() { unlock(); }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;
};

// Coherent copy of the whole state for readers outside pump_task. The copy is
// taken atomically; work off the returned value, never re-read g_motion.
inline MotionState snapshot() {
  Guard g;
  return g_motion;
}

}  // namespace motion_state

// Accessors for the active profile's fields (moved here from config.h with the
// profile table itself). Still macros because they are used as lvalues; they
// resolve to the LIVE state, so outside pump_task read the equivalent fields
// off a snapshot instead, and only assign under a Guard.
#define ui_speed_hz (g_motion.profiles[g_motion.activeProfile].speed_hz)
#define RUN_SG_TRIP (g_motion.profiles[g_motion.activeProfile].sg_trip)
