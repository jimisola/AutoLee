// ============================================================================
//  test_motion_seq - host coverage for main/motion/motion.cpp's SEQUENCING
//  (docs/PLAN.md Phase 8: "fake the stepper::/tmc5160:: seams").
//
//  The other suites cover lib/autolee_logic's pure decision helpers. This one
//  covers the firmware file that calls them: the real motion.cpp is compiled
//  and linked here against fake stepper::/tmc5160:: bodies (host_test/fakes),
//  so the ORDER and ARGUMENTS of its hardware calls can be asserted -
//  jam -> forceStop -> backoff -> STALLED, the calibrate -> hit -> backoff ->
//  re-zero flow, the graceful-stop timeout, and the homing retry/timeout logic.
//
//  Nothing in motion.cpp is modified or reimplemented for these tests.
// ============================================================================
#include <string>

#include "unity.h"

#include "fake_hw.h"

#include "config.h"
#include "motion.h"
#include "motion_state.h"

// --------------------------------------------------------------------------
//  Harness
// --------------------------------------------------------------------------
void setUp(void) {
  fake::reset();
  g_motion = MotionState{};
}

void tearDown(void) {
  // motion_state.h forbids driving the stepper / touching SPI / logging inside
  // a Guard critical section. Every test asserts motion.cpp honours that.
  TEST_ASSERT_FALSE_MESSAGE(fake::sim.hwCallInCritical,
                            "a stepper/TMC/webLog call happened inside a motion_state::Guard");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake::sim.criticalDepth, "unbalanced critical section");
  TEST_ASSERT_FALSE_MESSAGE(fake::sim.tickBudgetExhausted, "fake tick budget exhausted (hang?)");
}

// One pump_task iteration: handleMotion() then 10 ms of simulated time (the
// real pump loop's vTaskDelay).
static void pump(int iterations) {
  for (int i = 0; i < iterations; i++) {
    handleMotion();
    fake::advance_ms(10);
  }
}

static void givenCalibrated(long up, long dn, int32_t pos) {
  g_motion.endpointsCalibrated = true;
  g_motion.rawUp = up;
  g_motion.rawDown = dn;
  g_motion.endpointUp = up;
  g_motion.endpointDown = dn;
  g_motion.runState = IDLE;
  fake::sim.position = pos;
  fake::sim.physical = pos;
}

// SG sources
static uint16_t sg_quiet() {  // above the "sg <= 1" ignore, below the run trip
  return 5;
}
static uint16_t sg_jammed() {
  return 500;
}
static int s_sg_calls;  // for the one-shot high-reading source below

#define ASSERT_BEFORE(a, b)                                                                 \
  do {                                                                                      \
    const int ia = fake::indexOf(a), ib = fake::indexOf(b);                                 \
    TEST_ASSERT_TRUE_MESSAGE(ia >= 0, (std::string("missing ") + a).c_str());               \
    TEST_ASSERT_TRUE_MESSAGE(ib >= 0, (std::string("missing ") + b).c_str());               \
    TEST_ASSERT_TRUE_MESSAGE(ia < ib,                                                       \
                             (std::string(a) + " not before " + b + fake::dump()).c_str()); \
  } while (0)

// --------------------------------------------------------------------------
//  Start / run
// --------------------------------------------------------------------------
// The TMC must be put into sensorless mode and the target chosen BEFORE the
// move is issued, and the move must go to the far endpoint.
static void test_start_enters_sensorless_then_moves(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;

  startRunBetweenEndpoints();

  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_EQUAL_INT32(20000, g_motion.currentTarget);
  TEST_ASSERT_TRUE(fake::saw("tmc5160::en_pwm_mode(0)"));  // SpreadCycle, not StealthChop
  TEST_ASSERT_TRUE(fake::saw("tmc5160::TCOOLTHRS(1048575)"));
  TEST_ASSERT_TRUE(fake::saw("tmc5160::sgt(-1)"));
  ASSERT_BEFORE("tmc5160::sgt(-1)", "stepper::moveTo(20000)");
  ASSERT_BEFORE("stepper::setSpeedInHz(35000)", "stepper::moveTo(20000)");
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::moveTo(20000)"));
}

// Start is rejected out of STALLED by the tested FSM table - and must not touch
// the TMC or the stepper on the way out.
static void test_start_rejected_when_stalled(void) {
  givenCalibrated(0, 20000, 5000);
  g_motion.runState = STALLED;

  startRunBetweenEndpoints();

  TEST_ASSERT_EQUAL_UINT(STALLED, g_motion.runState);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)fake::events.size(), fake::dump().c_str());
  TEST_ASSERT_TRUE(fake::logContains("Start ignored"));
}

// Reaching the DOWN endpoint bumps the cycle counter and flips the target.
static void test_endpoint_arrival_flips_target_and_counts(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();

  pump(80);  // 20000 steps at 35 steps/ms == 58 ticks

  TEST_ASSERT_EQUAL_INT32(1, g_motion.counter);
  TEST_ASSERT_EQUAL_INT32(0, g_motion.currentTarget);  // flipped back to UP
  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::moveTo(0)"));
}

// The display counter saturates at COUNTER_MAX, but batch counting must NOT be
// gated by it (v1.8 bug fixed in this port - see handleMotion()'s comment).
static void test_counter_saturation_does_not_stall_batch(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;
  g_motion.counter = COUNTER_MAX;
  g_motion.batchActive = true;
  g_motion.batchTarget = 3;
  startRunBetweenEndpoints();

  pump(80);

  TEST_ASSERT_EQUAL_INT32(COUNTER_MAX, g_motion.counter);
  TEST_ASSERT_EQUAL_INT32(1, g_motion.batchCount);
  TEST_ASSERT_TRUE(g_motion.batchActive);
  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
}

// A completed batch stops gracefully instead of flipping the target again.
static void test_batch_completion_requests_graceful_stop(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;
  g_motion.batchActive = true;
  g_motion.batchTarget = 1;
  startRunBetweenEndpoints();

  pump(80);

  TEST_ASSERT_EQUAL_UINT(STOPPING, g_motion.runState);
  TEST_ASSERT_FALSE(g_motion.batchActive);
  TEST_ASSERT_TRUE(fake::logContains("Batch complete: 1/1"));
  TEST_ASSERT_TRUE(fake::saw("ui::setRunButtonState(0)"));
  TEST_ASSERT_EQUAL_INT32(0, g_motion.currentTarget);  // decel move toward UP
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::moveTo(0)"));
}

// --------------------------------------------------------------------------
//  Jam -> forceStop -> backoff -> STALLED
// --------------------------------------------------------------------------
static void test_jam_stops_backs_off_and_latches_stalled(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();
  const int after_start = (int)fake::events.size();

  fake::sg_source = sg_jammed;
  pump(30);

  TEST_ASSERT_EQUAL_UINT_MESSAGE(STALLED, g_motion.runState, fake::dump().c_str());
  TEST_ASSERT_TRUE(fake::logContains("JAM!"));
  // Stop first, then creep-speed, then back off AWAY from the DOWN endpoint by
  // exactly RUN_BACKOFF_STEPS, then tell the UI.
  ASSERT_BEFORE("stepper::forceStop", "stepper::setSpeedInHz(8000)");
  ASSERT_BEFORE("stepper::setSpeedInHz(8000)", "stepper::move(-1000)");
  ASSERT_BEFORE("stepper::move(-1000)", "ui::showJamScreen");
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::forceStop"));
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::move(-1000)"));
  // No further move was commanded after the backoff: the press must never be
  // driven on into the jam.
  TEST_ASSERT_EQUAL_INT(0, fake::indexOf("stepper::moveTo(0)", after_start) >= 0 ? 1 : 0);
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::moveTo(20000)"));  // only the initial one

  // ...and handleMotion() is inert from STALLED.
  const int n = (int)fake::events.size();
  pump(20);
  TEST_ASSERT_EQUAL_INT(n, (int)fake::events.size());
}

// Same jam, heading UP: the backoff must reverse, i.e. +RUN_BACKOFF_STEPS.
static void test_jam_backoff_direction_reverses_heading_up(void) {
  givenCalibrated(0, 20000, 20000);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();
  TEST_ASSERT_EQUAL_INT32(0, g_motion.currentTarget);

  fake::sg_source = sg_jammed;
  pump(30);

  TEST_ASSERT_EQUAL_UINT(STALLED, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::move(1000)"));
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::move(-1000)"));
}

// A single high reading must NOT trip a jam (RUN_SG_HIGH_NEEDED == 2).
static void test_single_high_reading_does_not_jam(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();

  // Past the accel blanking window (accelBlankMs(35000, 800000) == 123 ms),
  // outside the work zone, outside the decel window.
  pump(14);
  // One high reading = one full read_sg(), i.e. five SG_RESULT samples (the
  // median-of-5 glitch filter), then back to quiet.
  s_sg_calls = 0;
  fake::sg_source = []() -> uint16_t { return (s_sg_calls++ < 5) ? 500 : 5; };
  pump(6);

  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::forceStop"));
  TEST_ASSERT_TRUE(fake::logContains("SG HIGH"));
}

// Inside the accel blanking window after a direction change, SG is not even
// read - a high reading there must be ignored entirely.
static void test_accel_blanking_window_ignores_high_sg(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_jammed;
  startRunBetweenEndpoints();

  pump(11);  // 110 ms < 123 ms accel blank

  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_FALSE(fake::logContains("SG HIGH"));
}

// Near the DOWN endpoint (the work zone) resistance is normal: no jam, and the
// stall counter is cleared.
static void test_work_zone_suppresses_jam_near_down(void) {
  givenCalibrated(0, 20000, 16000);  // within sgWorkZoneSteps (5500) of DOWN
  fake::sg_source = sg_jammed;
  startRunBetweenEndpoints();
  TEST_ASSERT_EQUAL_INT32(20000, g_motion.currentTarget);

  pump(15);

  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::forceStop"));
  TEST_ASSERT_EQUAL_UINT(0, g_motion.runSGHighCount);
}

// Inside the deceleration window (and with no prior jam evidence) SG is blanked:
// the ramp-down itself loads the motor and would false-trigger. The work-zone
// blank only covers the DOWN endpoint, so this is the UP-bound leg.
static void test_decel_blanking_suppresses_jam_near_target(void) {
  givenCalibrated(0, 20000, 20000);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();  // heading UP, target 0
  // High SG only once inside decelBlankSteps(35000, 800000) == 1265 of the target.
  fake::sg_source = []() -> uint16_t { return fake::sim.position < 1265 ? 500 : 5; };

  pump(70);  // 20000 steps at 35 steps/ms == 58 ticks, so this reaches UP

  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::forceStop"));
  TEST_ASSERT_EQUAL_UINT(0, g_motion.runSGHighCount);
  TEST_ASSERT_EQUAL_UINT(0, g_motion.runSGLowCount);
  // It completed the leg and turned around. (The cycle counter only counts
  // arrivals at DOWN, so it stays at 0 for this UP-bound leg.)
  TEST_ASSERT_EQUAL_INT32(20000, g_motion.currentTarget);
  TEST_ASSERT_EQUAL_INT32(0, g_motion.counter);
}

// --------------------------------------------------------------------------
//  Graceful stop
// --------------------------------------------------------------------------
static void test_graceful_stop_reaches_home_then_idles(void) {
  givenCalibrated(0, 20000, 20000);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();  // heading UP

  requestGracefulStop();
  TEST_ASSERT_EQUAL_UINT(STOPPING, g_motion.runState);

  pump(80);

  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
}

static void test_graceful_stop_times_out_and_force_stops(void) {
  givenCalibrated(0, 20000, 20000);
  fake::sg_source = sg_quiet;
  fake::sim.stepsPerMsOverride = 1;  // way too slow to arrive within the timeout
  startRunBetweenEndpoints();
  requestGracefulStop();

  pump(700);  // 7 s: still STOPPING, still moving
  TEST_ASSERT_EQUAL_UINT(STOPPING, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::forceStop"));

  pump(200);  // past STOP_TIMEOUT_MS (8 s)
  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::forceStop"));
}

// From STALLED a stray stop request must not command a fast unguarded move away
// from the jam - that is safeCreepHome()'s job.
static void test_graceful_stop_ignored_when_stalled(void) {
  givenCalibrated(0, 20000, 5000);
  g_motion.runState = STALLED;

  requestGracefulStop();

  TEST_ASSERT_EQUAL_UINT(STALLED, g_motion.runState);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)fake::events.size(), fake::dump().c_str());
  TEST_ASSERT_TRUE(fake::logContains("Graceful stop ignored"));
}

// --------------------------------------------------------------------------
//  Sensorless calibration
// --------------------------------------------------------------------------
// Simulated press: hard stops CAL_* steps apart, with the pulse counter free to
// run past them (PCNT keeps counting while the carriage is stopped), which is
// exactly what StallGuard sees as a stall.
static void givenPress(int32_t upStop, int32_t downStop) {
  fake::sim.hardStops = true;
  fake::sim.hardStopUp = upStop;
  fake::sim.hardStopDown = downStop;
  fake::sim.physical = fake::sim.position;
  // 10 steps/ms so the ignore/baseline windows (420 ms + 200 ms) elapse before
  // the carriage reaches a stop - i.e. the baseline is sampled while moving
  // freely, as on the real press.
  fake::sim.stepsPerMsOverride = 10;
}

static void test_calibration_finds_both_stops_and_rezeros(void) {
  g_motion.runState = IDLE;
  givenPress(-2000, 40000);

  const bool ok = calibrateEndpointsSensorless();

  TEST_ASSERT_TRUE_MESSAGE(ok, fake::dump().c_str());
  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_TRUE(g_motion.endpointsCalibrated);
  TEST_ASSERT_EQUAL_INT32(0, g_motion.rawUp);  // re-zeroed at the UP stop
  TEST_ASSERT_TRUE_MESSAGE(g_motion.rawDown > 0, "DOWN endpoint must be past UP");
  // Sequence: calibration current/speed -> pre-move down -> UP search ->
  // back off the stop -> re-zero -> DOWN search -> back off -> run current.
  ASSERT_BEFORE("tmc5160::rms_current(3200)", "stepper::move(5500)");
  ASSERT_BEFORE("stepper::move(5500)", "stepper::move(300)");
  ASSERT_BEFORE("stepper::move(300)", "stepper::setCurrentPosition(0)");
  ASSERT_BEFORE("stepper::setCurrentPosition(0)", "stepper::move(-300)");
  ASSERT_BEFORE("stepper::move(-300)", "tmc5160::rms_current(3500)");
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::setCurrentPosition(0)"));
  // Both searches are aborted with a forceStop the moment the stop is confirmed.
  TEST_ASSERT_EQUAL_INT(2, fake::countOf("stepper::forceStop"));
  TEST_ASSERT_TRUE(fake::logContains("MUS: DYN HIT"));
  // Offsets go live with the calibrated flag; DOWN keeps its default offset.
  TEST_ASSERT_EQUAL_INT32(0, g_motion.upOffsetSteps);
  TEST_ASSERT_EQUAL_INT32(DOWN_OFFSET_DEFAULT, g_motion.downOffsetSteps);
  TEST_ASSERT_EQUAL_INT32(0, g_motion.endpointUp);
  TEST_ASSERT_EQUAL_INT32(g_motion.rawDown + DOWN_OFFSET_DEFAULT, g_motion.endpointDown);
  // Ends parked at the UP endpoint.
  TEST_ASSERT_EQUAL_INT32(g_motion.endpointUp, fake::sim.position);
}

// No mechanical stop anywhere: the search must give up, restore the run
// current/speed, leave the endpoints INVALID and return to IDLE.
static void test_calibration_fails_cleanly_without_a_stop(void) {
  g_motion.runState = IDLE;
  g_motion.endpointsCalibrated = true;  // must be invalidated
  // A move still in flight when calibration is requested (e.g. left over from a
  // timed-out stop) must be force-stopped before the blind search starts.
  fake::sim.running = true;
  fake::sim.target = 50000;
  fake::sim.speedHz = 1000;

  const bool ok = calibrateEndpointsSensorless();

  ASSERT_BEFORE("stepper::forceStop", "stepper::move(5500)");
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_FALSE(g_motion.endpointsCalibrated);
  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_TRUE(fake::logContains("Calibration: FAILED (no UP stop found)"));
  ASSERT_BEFORE("stepper::move(-120000)", "tmc5160::rms_current(3500)");
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::setCurrentPosition(0)"));
  TEST_ASSERT_EQUAL_INT(1,
                        fake::countOf("stepper::setAcceleration(800000)"));  // RUN_DECEL restored
}

// The UP stop is found, but nothing stops the carriage on the way DOWN: the
// second search must fail the same way - endpoints left invalid, back to IDLE.
static void test_calibration_fails_on_the_down_search(void) {
  g_motion.runState = IDLE;
  givenPress(-2000, 10000000);  // effectively no DOWN stop within the search range

  TEST_ASSERT_FALSE(calibrateEndpointsSensorless());

  TEST_ASSERT_FALSE(g_motion.endpointsCalibrated);
  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_TRUE(fake::logContains("Calibration: FAILED (no DOWN stop found)"));
  TEST_ASSERT_TRUE(fake::logContains("MUS: NO STALL DETECTED"));
  // The UP re-zero already happened; the DOWN back-off must NOT.
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::setCurrentPosition(0)"));
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::move(-300)"));
  TEST_ASSERT_TRUE(fake::saw("tmc5160::rms_current(3500)"));  // run current restored
}

static void test_calibration_rejected_while_running(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();
  const int n = (int)fake::events.size();

  TEST_ASSERT_FALSE(calibrateEndpointsSensorless());

  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_TRUE(g_motion.endpointsCalibrated);  // a good calibration survives
  TEST_ASSERT_EQUAL_INT(n, (int)fake::events.size());
  TEST_ASSERT_TRUE(fake::logContains("Calibration ignored"));
}

// --------------------------------------------------------------------------
//  Creep home out of a jam
// --------------------------------------------------------------------------
static void test_creep_home_finds_stop_rezeros_and_returns_idle(void) {
  givenCalibrated(0, 20000, 9000);
  g_motion.runState = STALLED;
  givenPress(2000, 40000);  // UP stop 7000 steps above where the jam left us

  safeCreepHome();

  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_EQUAL_INT32(0, g_motion.rawUp);
  // Creep current/speed first, search, back off the stop, re-zero, park at UP,
  // then hand the run current/speed back.
  ASSERT_BEFORE("tmc5160::rms_current(3200)", "stepper::move(-120000)");
  ASSERT_BEFORE("stepper::move(-120000)", "stepper::forceStop");
  ASSERT_BEFORE("stepper::forceStop", "stepper::move(300)");
  ASSERT_BEFORE("stepper::move(300)", "stepper::setCurrentPosition(0)");
  ASSERT_BEFORE("stepper::setCurrentPosition(0)", "tmc5160::rms_current(3500)");
  TEST_ASSERT_TRUE(fake::logContains("Creep home: found stop"));
  TEST_ASSERT_TRUE(fake::saw("ui::setRunButtonState(0)"));
  TEST_ASSERT_EQUAL_INT32(g_motion.endpointUp, fake::sim.position);
}

// A stop hit almost immediately (the press is already against the top): the
// early-trip window catches it before the dynamic baseline is even sampled.
static void test_creep_home_early_trip_catches_an_immediate_stop(void) {
  givenCalibrated(0, 20000, 9000);
  g_motion.runState = STALLED;
  givenPress(8800, 40000);           // only 200 steps of travel before the stop
  fake::sim.stepsPerMsOverride = 4;  // slow enough to stay inside the early window

  safeCreepHome();

  TEST_ASSERT_TRUE_MESSAGE(fake::logContains("MUS: EARLY HIT"), fake::dump().c_str());
  TEST_ASSERT_FALSE(fake::logContains("MUS: DYN HIT"));
  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_EQUAL_INT32(0, g_motion.rawUp);
}

static void test_creep_home_reports_failure_when_no_stop_found(void) {
  givenCalibrated(0, 20000, 9000);
  g_motion.runState = STALLED;  // no hard stops configured

  safeCreepHome();

  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_TRUE(fake::logContains("Creep home: FAILED to find stop!"));
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::setCurrentPosition(0)"));
  TEST_ASSERT_TRUE(fake::saw("tmc5160::rms_current(3500)"));  // run current restored anyway
}

// ReturnHome is legal from IDLE as well as STALLED, but not from a state where
// the press is already moving - that would fight the run in progress.
static void test_creep_home_ignored_while_running(void) {
  givenCalibrated(0, 20000, 0);
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();
  const int n = (int)fake::events.size();

  safeCreepHome();

  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
  TEST_ASSERT_EQUAL_INT_MESSAGE(n, (int)fake::events.size(), fake::dump().c_str());
  TEST_ASSERT_TRUE(fake::logContains("Return home ignored"));
}

// --------------------------------------------------------------------------
//  Position reference staleness (settings_store restores calibration, but the
//  stepper counter always comes up at 0 wherever the carriage physically is)
// --------------------------------------------------------------------------
// Calibrated + IDLE is not enough: with the reference unconfirmed, Start must
// refuse without touching the TMC or the stepper.
static void test_start_refused_when_position_reference_stale(void) {
  givenCalibrated(0, 20000, 0);
  g_motion.positionReferenceStale = true;

  startRunBetweenEndpoints();

  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)fake::events.size(), fake::dump().c_str());
  TEST_ASSERT_TRUE(fake::logContains("Start refused: position reference unconfirmed"));
}

// Return Home from IDLE is the way out of that state: a real stall search
// re-zeroes at the UP hard stop, which re-establishes the reference - and only
// then does Start work.
static void test_creep_home_from_idle_clears_stale_reference(void) {
  givenCalibrated(0, 20000, 9000);  // IDLE, as after a reboot with a restore
  g_motion.positionReferenceStale = true;
  givenPress(2000, 40000);

  safeCreepHome();

  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_TRUE(fake::logContains("Creep home: found stop"));
  TEST_ASSERT_EQUAL_INT32(0, g_motion.rawUp);
  TEST_ASSERT_FALSE(g_motion.positionReferenceStale);

  fake::events.clear();
  fake::sg_source = sg_quiet;
  startRunBetweenEndpoints();
  TEST_ASSERT_EQUAL_UINT(RUNNING, g_motion.runState);
}

// A home that never found the stop re-referenced nothing: the flag must survive
// so Start stays refused.
static void test_failed_creep_home_keeps_stale_reference(void) {
  givenCalibrated(0, 20000, 9000);
  g_motion.positionReferenceStale = true;  // no hard stops configured

  safeCreepHome();

  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_TRUE(fake::logContains("Creep home: FAILED to find stop!"));
  TEST_ASSERT_TRUE(g_motion.positionReferenceStale);

  fake::events.clear();
  startRunBetweenEndpoints();
  TEST_ASSERT_EQUAL_UINT(IDLE, g_motion.runState);
  TEST_ASSERT_EQUAL_INT(0, (int)fake::events.size());
}

// A fresh calibration finds both stops and re-zeroes at UP, so it is not stale
// by definition.
static void test_calibration_clears_stale_reference(void) {
  g_motion.runState = IDLE;
  g_motion.positionReferenceStale = true;
  givenPress(-2000, 40000);

  TEST_ASSERT_TRUE_MESSAGE(calibrateEndpointsSensorless(), fake::dump().c_str());

  TEST_ASSERT_TRUE(g_motion.endpointsCalibrated);
  TEST_ASSERT_FALSE(g_motion.positionReferenceStale);
}

// --------------------------------------------------------------------------
//  return_home_up_safe(): retry / timeout logic
// --------------------------------------------------------------------------
static void test_return_home_succeeds_when_travel_is_clear(void) {
  givenCalibrated(0, 20000, 20000);

  TEST_ASSERT_TRUE(return_home_up_safe());

  TEST_ASSERT_EQUAL_INT32(0, fake::sim.position);
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::forceStop"));
  TEST_ASSERT_EQUAL_INT(0, fake::countOf("stepper::move(1200)"));
}

// An obstruction on the way home: release, retry, and give up after
// HOME_MAX_RETRIES (so 3 stall detections in total).
static void test_return_home_retries_then_gives_up(void) {
  givenCalibrated(0, 20000, 20000);
  givenPress(15000, 40000);
  fake::sim.stepsPerMsOverride = 0;  // HOME_SPEED_HZ (8000) -> 8 steps/ms

  TEST_ASSERT_FALSE(return_home_up_safe());

  TEST_ASSERT_EQUAL_INT(3, fake::countOf("stepper::move(1200)"));  // HOME_RELEASE_STEPS
  TEST_ASSERT_EQUAL_INT(3, fake::countOf("stepper::forceStop"));
  TEST_ASSERT_EQUAL_INT(3, fake::countOf("stepper::moveTo(0)"));  // initial + 2 retries
  TEST_ASSERT_TRUE(fake::logContains("Home: max retries"));
  TEST_ASSERT_TRUE(fake::logContains("retry 0"));
  TEST_ASSERT_TRUE(fake::logContains("retry 1"));
}

static void test_return_home_times_out(void) {
  givenCalibrated(0, 20000, 20000);
  fake::sim.stepsPerMsOverride = 1;  // 20 s of travel vs HOME_TIMEOUT_MS 15 s

  TEST_ASSERT_FALSE(return_home_up_safe());

  TEST_ASSERT_TRUE(fake::logContains("Home: TIMEOUT"));
  TEST_ASSERT_EQUAL_INT(1, fake::countOf("stepper::forceStop"));
}

static void test_return_home_requires_calibration(void) {
  TEST_ASSERT_FALSE(return_home_up_safe());
  TEST_ASSERT_EQUAL_INT(0, (int)fake::events.size());
}

// --------------------------------------------------------------------------
//  Misc dispatch
// --------------------------------------------------------------------------
static void test_handle_motion_is_inert_in_blocking_states(void) {
  givenCalibrated(0, 20000, 5000);
  const RunState blocked[] = {CALIBRATING, STALLED, HOMING};
  for (RunState s : blocked) {
    g_motion.runState = s;
    fake::events.clear();
    pump(5);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)fake::events.size(), fake::dump().c_str());
    TEST_ASSERT_EQUAL_UINT(s, g_motion.runState);
  }
}

// Uncalibrated: both effective endpoints must stay pinned at 0 whatever the
// offsets say, so nothing can command a move to a made-up endpoint.
static void test_effective_endpoints_are_zero_until_calibrated(void) {
  g_motion.endpointsCalibrated = false;
  g_motion.rawUp = 0;
  g_motion.rawDown = 40000;
  g_motion.upOffsetSteps = 1234;
  g_motion.downOffsetSteps = -1234;

  recomputeEffectiveEndpoints();

  TEST_ASSERT_EQUAL_INT32(0, g_motion.endpointUp);
  TEST_ASSERT_EQUAL_INT32(0, g_motion.endpointDown);
  TEST_ASSERT_EQUAL_INT(0, (int)fake::events.size());
}

static void test_set_active_profile_pushes_new_speed(void) {
  givenCalibrated(0, 20000, 0);

  setActiveProfile(2);  // Fast
  TEST_ASSERT_EQUAL_UINT8(2, g_motion.activeProfile);
  TEST_ASSERT_TRUE(fake::saw("stepper::setSpeedInHz(45000)"));

  fake::events.clear();
  setActiveProfile(NUM_PROFILES);  // out of range: ignored
  TEST_ASSERT_EQUAL_UINT8(2, g_motion.activeProfile);
  TEST_ASSERT_EQUAL_INT(0, (int)fake::events.size());
}

// motion_init()'s bring-up order: TMC first (it owns the SPI device), then the
// stepper, then the run current.
static void test_motion_init_order(void) {
  motion_init();
  ASSERT_BEFORE("tmc5160::init(1)", "stepper::init(5,6)");
  ASSERT_BEFORE("stepper::init(5,6)", "tmc5160::rms_current(3500)");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_start_enters_sensorless_then_moves);
  RUN_TEST(test_start_rejected_when_stalled);
  RUN_TEST(test_endpoint_arrival_flips_target_and_counts);
  RUN_TEST(test_counter_saturation_does_not_stall_batch);
  RUN_TEST(test_batch_completion_requests_graceful_stop);
  RUN_TEST(test_jam_stops_backs_off_and_latches_stalled);
  RUN_TEST(test_jam_backoff_direction_reverses_heading_up);
  RUN_TEST(test_single_high_reading_does_not_jam);
  RUN_TEST(test_accel_blanking_window_ignores_high_sg);
  RUN_TEST(test_work_zone_suppresses_jam_near_down);
  RUN_TEST(test_decel_blanking_suppresses_jam_near_target);
  RUN_TEST(test_graceful_stop_reaches_home_then_idles);
  RUN_TEST(test_graceful_stop_times_out_and_force_stops);
  RUN_TEST(test_graceful_stop_ignored_when_stalled);
  RUN_TEST(test_calibration_finds_both_stops_and_rezeros);
  RUN_TEST(test_calibration_fails_cleanly_without_a_stop);
  RUN_TEST(test_calibration_fails_on_the_down_search);
  RUN_TEST(test_calibration_rejected_while_running);
  RUN_TEST(test_creep_home_finds_stop_rezeros_and_returns_idle);
  RUN_TEST(test_creep_home_early_trip_catches_an_immediate_stop);
  RUN_TEST(test_creep_home_reports_failure_when_no_stop_found);
  RUN_TEST(test_creep_home_ignored_while_running);
  RUN_TEST(test_start_refused_when_position_reference_stale);
  RUN_TEST(test_creep_home_from_idle_clears_stale_reference);
  RUN_TEST(test_failed_creep_home_keeps_stale_reference);
  RUN_TEST(test_calibration_clears_stale_reference);
  RUN_TEST(test_return_home_succeeds_when_travel_is_clear);
  RUN_TEST(test_return_home_retries_then_gives_up);
  RUN_TEST(test_return_home_times_out);
  RUN_TEST(test_return_home_requires_calibration);
  RUN_TEST(test_handle_motion_is_inert_in_blocking_states);
  RUN_TEST(test_effective_endpoints_are_zero_until_calibrated);
  RUN_TEST(test_set_active_profile_pushes_new_speed);
  RUN_TEST(test_motion_init_order);
  return UNITY_END();
}
