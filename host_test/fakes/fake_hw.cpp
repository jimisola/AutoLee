// Fake implementations of the seams described in fake_hw.h. Linked into the
// test_motion_seq suite together with the real main/motion/motion.cpp.
#include "fake_hw.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "globals.h"  // webLog()'s declaration - keep the signature honest
#include "stepper.h"
#include "tmc5160_ctrl.h"
#include "ui_touch.h"

namespace fake {

Sim sim;
std::vector<std::string> events;
std::vector<std::string> logs;
std::function<uint16_t()> sg_source;

static int64_t s_now_us = 0;

static uint16_t default_sg_source() {
  return stalled() ? sim.sgStalled : sim.sgBaseline;
}

void reset() {
  sim = Sim{};
  events.clear();
  logs.clear();
  sg_source = default_sg_source;
  s_now_us = 0;
}

// Any seam call must happen OUTSIDE a motion_state::Guard (motion_state.h:
// "no calls that can block, log, allocate, drive the stepper or touch the
// shared SPI bus" inside a critical section). Record violations instead of
// aborting so the failing test reports them.
static void seam_touched() {
  if (sim.criticalDepth > 0) sim.hwCallInCritical = true;
}

static void rec(const char *fmt, ...) {
  seam_touched();
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  events.push_back(buf);
}

void enter_critical() {
  sim.criticalDepth++;
}
void exit_critical() {
  sim.criticalDepth--;
  if (sim.criticalDepth < 0) {
    fprintf(stderr, "fake: unbalanced portEXIT_CRITICAL\n");
    abort();
  }
}

bool stalled() {
  return sim.hardStops && sim.position != sim.physical;
}

uint32_t millis_now() {
  return (uint32_t)(s_now_us / 1000);
}

static int32_t steps_per_ms() {
  if (sim.stepsPerMsOverride > 0) return sim.stepsPerMsOverride;
  int32_t s = (int32_t)(sim.speedHz / 1000);
  return s > 0 ? s : 1;
}

static void apply_stops() {
  if (!sim.hardStops) {
    sim.physical = sim.position;
    return;
  }
  sim.physical = sim.position;
  if (sim.physical < sim.hardStopUp) sim.physical = sim.hardStopUp;
  if (sim.physical > sim.hardStopDown) sim.physical = sim.hardStopDown;
}

void advance_ms(uint32_t ms) {
  s_now_us += (int64_t)ms * 1000;
  if (!sim.running) return;
  // forceStop() latency: one polling iteration, mirroring stepper.cpp's
  // one-cruise-chunk bound.
  if (sim.stopPending) {
    sim.stopPending = false;
    sim.running = false;
    return;
  }
  const int32_t delta = sim.target - sim.position;
  if (delta == 0) {  // moveTo() to the current position: move_task exits at once
    sim.running = false;
    return;
  }
  const int64_t step = (int64_t)steps_per_ms() * (int64_t)ms;
  if ((int64_t)(delta < 0 ? -delta : delta) <= step) {
    sim.position = sim.target;
    sim.running = false;
  } else {
    sim.position += (int32_t)(delta < 0 ? -step : step);
  }
  apply_stops();
}

int indexOf(const char *event, int from) {
  for (size_t i = (size_t)from; i < events.size(); i++)
    if (events[i] == event) return (int)i;
  return -1;
}
bool saw(const char *event) {
  return indexOf(event) >= 0;
}
int countOf(const char *event) {
  int n = 0;
  for (const auto &e : events)
    if (e == event) n++;
  return n;
}
bool logContains(const char *substr) {
  for (const auto &l : logs)
    if (l.find(substr) != std::string::npos) return true;
  return false;
}
std::string dump() {
  std::string s = "\n--- events ---\n";
  for (const auto &e : events) s += "  " + e + "\n";
  return s;
}

}  // namespace fake

// ============================================================================
//  ESP-IDF stubs
// ============================================================================
// Millisecond resolution is all motion.cpp's millis() helper uses.
int64_t esp_timer_get_time(void) {
  return (int64_t)fake::millis_now() * 1000;
}

esp_err_t esp_task_wdt_reset(void) {
  fake::sim.wdtFeeds++;
  return 0;
}

void vTaskDelay(TickType_t ticks) {
  if (fake::sim.ticksRemaining == 0) {
    // Safety net: a fake that never stops moving would otherwise spin forever.
    fake::sim.tickBudgetExhausted = true;
    fake::sim.running = false;
    return;
  }
  fake::sim.ticksRemaining--;
  fake::advance_ms((uint32_t)ticks * (1000 / configTICK_RATE_HZ));
}

// ============================================================================
//  stepper:: (main/drivers/stepper.h)
// ============================================================================
namespace stepper {

static bool s_enabled = false;

void init(gpio_num_t step_gpio, gpio_num_t dir_gpio, gpio_num_t enable_gpio) {
  fake::rec("stepper::init(%d,%d,%d)", (int)step_gpio, (int)dir_gpio, (int)enable_gpio);
  s_enabled = true;
}
void setEnabled(bool enabled) {
  s_enabled = enabled;
  fake::rec("stepper::setEnabled(%d)", (int)enabled);
}
bool isEnabled() {
  return s_enabled;
}
void setSpeedInHz(uint32_t hz) {
  fake::sim.speedHz = hz;
  fake::rec("stepper::setSpeedInHz(%u)", (unsigned)hz);
}
void setAcceleration(uint32_t steps_per_s2) {
  fake::sim.accel = steps_per_s2;
  fake::rec("stepper::setAcceleration(%u)", (unsigned)steps_per_s2);
}
void moveTo(int32_t absolute_position) {
  fake::sim.target = absolute_position;
  fake::sim.running = true;
  fake::sim.stopPending = false;
  fake::rec("stepper::moveTo(%d)", (int)absolute_position);
}
void move(int32_t relative_steps) {
  fake::sim.target = fake::sim.position + relative_steps;
  fake::sim.running = true;
  fake::sim.stopPending = false;
  fake::rec("stepper::move(%d)", (int)relative_steps);
}
bool isRunning() {
  fake::seam_touched();
  return fake::sim.running;
}
int32_t getCurrentPosition() {
  fake::seam_touched();
  return fake::sim.position;
}
void setCurrentPosition(int32_t position) {
  const int32_t shift = position - fake::sim.position;
  fake::sim.position = position;
  // Re-zeroing the counter re-bases the mechanical model with it, so the
  // simulated stops stay where they physically are.
  if (fake::sim.hardStops) {
    fake::sim.hardStopUp += shift;
    fake::sim.hardStopDown += shift;
  }
  fake::sim.physical += shift;
  fake::sim.target += shift;
  fake::rec("stepper::setCurrentPosition(%d)", (int)position);
}
void forceStop() {
  if (fake::sim.running) fake::sim.stopPending = true;
  fake::rec("stepper::forceStop");
}

}  // namespace stepper

// ============================================================================
//  tmc5160:: (main/drivers/tmc5160_ctrl.h)
// ============================================================================
namespace tmc5160 {

void init(spi_host_device_t spi_host, float r_sense_ohm) {
  (void)r_sense_ohm;
  fake::rec("tmc5160::init(%d)", (int)spi_host);
}
void rms_current(uint16_t mA) {
  fake::sim.runCurrentMa = mA;
  fake::rec("tmc5160::rms_current(%u)", (unsigned)mA);
}
void en_pwm_mode(bool enabled) {
  fake::sim.stealthChop = enabled;
  fake::rec("tmc5160::en_pwm_mode(%d)", (int)enabled);
}
void TPWMTHRS(uint32_t threshold) {
  fake::rec("tmc5160::TPWMTHRS(%u)", (unsigned)threshold);
}
void TCOOLTHRS(uint32_t threshold) {
  fake::sim.tcoolthrs = threshold;
  fake::rec("tmc5160::TCOOLTHRS(%u)", (unsigned)threshold);
}
void semin(uint8_t value) {
  fake::rec("tmc5160::semin(%u)", (unsigned)value);
}
void semax(uint8_t value) {
  fake::rec("tmc5160::semax(%u)", (unsigned)value);
}
void seup(uint8_t value) {
  fake::rec("tmc5160::seup(%u)", (unsigned)value);
}
void sedn(uint8_t value) {
  fake::rec("tmc5160::sedn(%u)", (unsigned)value);
}
void sgt(int8_t value) {
  fake::sim.sgt = value;
  fake::rec("tmc5160::sgt(%d)", (int)value);
}
uint32_t DRV_STATUS() {
  fake::seam_touched();
  return 0;
}
uint16_t SG_RESULT() {
  fake::seam_touched();
  return fake::sg_source ? fake::sg_source() : 0;
}

}  // namespace tmc5160

// ============================================================================
//  webLog() + the ui_touch.h hooks motion.cpp calls
// ============================================================================
void webLog(const char * /*category*/, const char *fmt, ...) {
  fake::seam_touched();
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fake::logs.push_back(buf);
}

void webLogLevel(LogLevel /*level*/, const char * /*category*/, const char *fmt, ...) {
  fake::seam_touched();
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fake::logs.push_back(buf);
}

void showJamScreen() {
  fake::rec("ui::showJamScreen");
}
void ui_jam_recovery_finished(bool homed) {
  fake::rec("ui::jam_recovery_finished(%d)", (int)homed);
}
// Records the label the real implementation would render, derived from the same
// snapshot it reads, so a test can assert the button never claims "RUN" while
// the press is still decelerating - or "STOP" while it is standing still.
void ui_update_run_button() {
  const RunState s = motion_state::snapshot().runState;
  const char *txt = (s == RUNNING) ? "STOP" : (s == STOPPING) ? "STOPPING" : "RUN";
  fake::rec("ui::run_button(%s)", txt);
}
void ui_update_main_warning() {
  fake::rec("ui::update_main_warning");
}
void ui_update_tuning_numbers() {
  fake::rec("ui::update_tuning_numbers");
}
void ui_update_endpoint_edit_values() {
  fake::rec("ui::update_endpoint_edit_values");
}

// ============================================================================
//  Seams reached only by main/motion/motion_cmd.cpp
// ============================================================================
// The remaining ui_touch.h hooks, the log ring, and settings_store. motion.cpp
// needs none of these; they exist so motion_cmd.cpp can be compiled into this
// suite and its command gating tested (it holds the batch/toggle/reset gates,
// which are pure decision logic over MotionState and were previously covered by
// nothing at all).
void ui_update_speed_val() {
  fake::rec("ui::update_speed_val");
}
void ui_update_profile_screen() {
  fake::rec("ui::update_profile_screen");
}
void ui_update_sg_val() {
  fake::rec("ui::update_sg_val");
}
void ui_update_batch_val() {
  fake::rec("ui::update_batch_val");
}
void ui_update_batch_remain() {
  fake::rec("ui::update_batch_remain");
}

// Real LogRing (header-only pure logic, no ESP-IDF dependency) rather than a
// fake: motion_cmd's log-clear path asserts on its observable effect.
autolee::LogRing<LOG_LINES, LOG_LINE_LEN> g_log;
uint32_t logSentSerial = 0;

namespace settings_store {
void tick() {
  // Deliberately silent: tick() runs on every pump iteration, so recording it
  // would bury every other event in the trace.
}
void saveNow() {
  fake::rec("settings::saveNow");
}
void resetToDefaults() {
  fake::rec("settings::resetToDefaults");
}
}  // namespace settings_store
