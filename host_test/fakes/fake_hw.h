// ============================================================================
//  host_test/fakes - fake stepper::/tmc5160:: seams + minimal ESP-IDF header
//  stubs, so main/motion/motion.cpp can be COMPILED AND RUN UNCHANGED on the
//  host and its jam/backoff/homing/calibration sequencing gets test coverage.
//  (docs/PLAN.md Phase 8.)
//
//  Deliberately NOT a refactor of motion.cpp: the file under test is the exact
//  source that ships. What is faked is everything below it -
//
//    * `stepper::` (main/drivers/stepper.h)   - the REAL header, fake bodies
//    * `tmc5160::` (main/drivers/tmc5160_ctrl.h) - ditto
//    * `webLog()` / the `ui_touch.h` UI hooks - recorded, not rendered
//    * esp_timer / esp_task_wdt / vTaskDelay / portMUX - fakes/include/*
//
//  Using the real driver headers means a signature change in a seam breaks this
//  build, rather than silently drifting from the firmware.
//
//  The stepper fake models the two things the sequencing actually depends on:
//   1. moves take time (isRunning() stays true across several polling
//      iterations, and forceStop() only takes effect on the next one - the
//      same one-chunk latency stepper.cpp documents), and
//   2. position is a PULSE COUNT (PCNT), so it keeps advancing even while the
//      carriage is physically against a hard stop. StallGuard "sees" a stall
//      exactly when the pulse count has outrun the physical position.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace fake {

struct Sim {
  // ---- simulated stepper ----
  int32_t position = 0;  // pulse counter == stepper::getCurrentPosition()
  int32_t target = 0;
  bool running = false;
  bool stopPending = false;  // forceStop() takes effect on the next tick
  uint32_t speedHz = 0;
  uint32_t accel = 0;
  // Simulated travel per millisecond. 0 = derive from speedHz (speedHz/1000),
  // i.e. instant ramp-up; set explicitly to model a slow start.
  int32_t stepsPerMsOverride = 0;

  // ---- simulated mechanics ----
  bool hardStops = false;
  int32_t hardStopUp = 0;    // lowest physically reachable position
  int32_t hardStopDown = 0;  // highest physically reachable position
  int32_t physical = 0;      // clamped to the stops; != position while stalled

  // ---- simulated TMC5160 ----
  uint16_t runCurrentMa = 0;
  int8_t sgt = 0;
  bool stealthChop = true;
  uint32_t tcoolthrs = 0;

  // ---- bookkeeping ----
  uint32_t wdtFeeds = 0;
  int criticalDepth = 0;
  // Set if any stepper/TMC/webLog/UI call happened inside a
  // motion_state::Guard critical section - motion_state.h forbids that.
  bool hwCallInCritical = false;
  // Safety net so a fake that never stops moving can't hang the test run.
  uint32_t ticksRemaining = 200000;
  bool tickBudgetExhausted = false;

  uint16_t sgStalled = 4;     // SG when the carriage is against a stop
  uint16_t sgBaseline = 120;  // SG while moving freely
};

extern Sim sim;

// Ordered log of every seam call that mutates hardware state (queries such as
// isRunning()/getCurrentPosition()/SG_RESULT() are not logged - they would
// drown the interesting sequence).
extern std::vector<std::string> events;
// Everything motion.cpp passed to webLog(), formatted.
extern std::vector<std::string> logs;

// StallGuard source. Default: sgStalled while stalled(), sgBaseline otherwise.
extern std::function<uint16_t()> sg_source;

void reset();

// Advance the fake clock by `ms`, stepping the simulated move. Called
// automatically by vTaskDelay() (so motion.cpp's own polling loops drive it);
// tests call it directly to space out handleMotion() iterations.
void advance_ms(uint32_t ms);

uint32_t millis_now();
bool stalled();  // pulse count has outrun the physical carriage

// --- assertion helpers ---
int indexOf(const char *event, int from = 0);  // -1 if absent
bool saw(const char *event);
int countOf(const char *event);
bool logContains(const char *substr);
std::string dump();  // whole event log, one per line (for failure messages)

}  // namespace fake
