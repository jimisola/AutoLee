// ============================================================================
//  AutoLee – config.h
//  Pin definitions, speed profiles, tuning constants
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

static const char *DEFAULT_AP_SSID = "AutoLee-Setup";

// Credential limits, matching the fixed-size fields in ESP-IDF's wifi_config_t
// (ssid[32], password[64]). Anything longer cannot be stored without silent
// truncation, so wifi_mgr::saveCredentials() refuses it outright.
static constexpr size_t WIFI_SSID_MAX_LEN = 32;
static constexpr size_t WIFI_PASS_MAX_LEN = 64;

// ==========================================================================
//  PIN DEFINITIONS
// ==========================================================================
#define ENABLE_PIN 4
#define STEP_PIN 5
#define DIR_PIN 6
#define TMC_DIAG_PIN 7
#define TMC_CS 8
#define R_SENSE 0.022f

#define ROTATION 0
#define GFX_BL 23
#define Touch_I2C_SDA 18
#define Touch_I2C_SCL 19
#define Touch_RST 20
#define Touch_INT 21

// ==========================================================================
//  SPEED PROFILES
// ==========================================================================
struct SpeedProfile {
  const char *name;
  uint32_t speed_hz;
  uint16_t sg_trip;  // current (user-tweakable) SG trip
};

static constexpr uint8_t NUM_PROFILES = 3;
// The profile table itself and the active-profile index are mutable, cross-task
// state: they live in MotionState (main/motion/motion_state.h), which also
// defines the ui_speed_hz / RUN_SG_TRIP accessors that used to sit here.

static constexpr uint32_t RUN_DECEL =
    800000;  // accel/decel rate for run moves (fast ramps, max SG coverage)

// ==========================================================================
//  ENDPOINT TUNING
// ==========================================================================
// The endpoint offsets are cross-task state: MotionState::upOffsetSteps /
// ::downOffsetSteps in main/motion/motion_state.h.
static constexpr int32_t DOWN_OFFSET_DEFAULT = -500;
static constexpr int32_t OFFSET_MIN = -8000;
static constexpr int32_t OFFSET_MAX = +8000;
static constexpr int32_t ENDPOINT_GUARD = 50;

static constexpr int32_t CAL_PREMOVE_DOWN_STEPS = 5500;

// ==========================================================================
//  CALIBRATION CONSTANTS
// ==========================================================================
static constexpr int8_t CAL_SGT = -1;
// TMC5160 SGT (StallGuard2 threshold) COOLCONF field is a signed 7-bit value.
static constexpr int8_t SGT_MIN = -64;
static constexpr int8_t SGT_MAX = 63;
// Run current is cross-task state: MotionState::runCurrentMa (default 3500mA)
// in main/motion/motion_state.h.
static constexpr uint16_t RUN_CURRENT_MIN = 1000;
static constexpr uint16_t RUN_CURRENT_MAX = 4500;
static constexpr uint16_t CAL_CURRENT_MA = 3200;
static constexpr uint32_t CAL_SPEED_HZ = 8000;
static constexpr uint32_t CAL_ACCEL = 25000;
// Hardware counter wrap point. The PCNT unit in main/drivers/stepper.cpp
// counts in +-this window and accumulates each crossing in software, so the
// *reported* position range is that of int32_t, not this value. Do NOT treat
// this as a travel limit - it is only the granularity at which the hardware
// counter hands off to the accumulator.
static constexpr int32_t STEP_COUNT_WRAP = 30000;
// Relative distance a single hard-stop search may travel. This legitimately
// exceeds STEP_COUNT_WRAP (real travel is measured in the tens of thousands of
// steps - see host_test/test_settings_blob's fixture at 41000), which is
// exactly why the accumulation above is required: without it the *failure*
// path, which actually travels the full distance, wrapped the counter and
// getCurrentPosition() silently returned garbage.
static constexpr int32_t CAL_SEARCH_STEPS = 120000;
static constexpr uint16_t CAL_ABS_MIN = 12;
static constexpr uint8_t CAL_REL_DROP_Q8 = 235;
static constexpr uint8_t CAL_HIT_CONFIRM = 2;
// After hitting a mechanical stop, back off this many steps before re-zeroing
// so position 0 sits just off the hard stop (used at both endpoints + home).
static constexpr int32_t CAL_OVERSHOOT_BACKOFF_STEPS = 300;

static constexpr uint32_t EARLY_WINDOW_MS = 300;
static constexpr int32_t EARLY_WINDOW_DST_MAX = 1200;
static constexpr uint32_t EARLY_MIN_TIME_MS = 50;
static constexpr int32_t EARLY_MIN_MOVE_STEPS = 200;
static constexpr uint16_t EARLY_TRIP = CAL_ABS_MIN;

// Return-home: USE SAME SPEED as calibration to avoid decel overshoot
static constexpr uint32_t HOME_SPEED_HZ = CAL_SPEED_HZ;
static constexpr uint32_t HOME_ACCEL = CAL_ACCEL;
static constexpr uint16_t HOME_SG_TRIP = 15;
static constexpr uint8_t HOME_CONFIRM = 3;
static constexpr uint32_t HOME_MIN_MS = 200;
static constexpr int32_t HOME_MIN_MOVE = 600;
static constexpr int32_t HOME_RELEASE_STEPS = 1200;
static constexpr uint8_t HOME_MAX_RETRIES = 2;
static constexpr uint32_t HOME_TIMEOUT_MS = 15000;
// Position tolerances (steps) for "arrived at the UP endpoint":
//   - HOME_ARRIVAL_TOL: break the return-home polling loop
//   - HOME_FINAL_TOL:   accept the final settled position as home
static constexpr long HOME_ARRIVAL_TOL = 20;
static constexpr long HOME_FINAL_TOL = 50;

// ==========================================================================
//  RUNTIME STALL DETECTION
// ==========================================================================
static constexpr uint16_t RUN_SG_TRIP_MIN = 0;
static constexpr uint16_t RUN_SG_TRIP_MAX = 500;
static constexpr int32_t RUN_BACKOFF_STEPS = 1000;  // steps to back off after jam
static constexpr uint8_t RUN_SG_HIGH_NEEDED = 2;    // need this many high readings to trigger jam
// Extra headroom the high-reading counter may climb past RUN_SG_HIGH_NEEDED
// before saturating (so a burst of high readings can't overflow the counter).
static constexpr uint8_t RUN_SG_HIGH_SATURATION_MARGIN = 4;
// Consecutive below-trip readings needed to decay the high counter by one
// (debounces transient dips so a real jam isn't un-counted by noise).
static constexpr uint8_t RUN_SG_LOW_DECAY_COUNT = 3;
// Throttle for the periodic RUN-phase SG telemetry line to the web log.
static constexpr uint32_t RUN_SG_LOG_INTERVAL_MS = 500;
// Throttle for the periodic calibration (move-until-stall) SG telemetry line.
static constexpr uint32_t CAL_MUS_LOG_INTERVAL_MS = 400;

// Work zone: skip SG monitoring near the DOWN endpoint where the tool
// does useful work (e.g. pushing primers). The resistance here is normal
// and would false-trigger stall detection at low trip thresholds.
// SG is still active for the rest of the travel and near the UP endpoint.
// The work-zone width is cross-task state: MotionState::sgWorkZoneSteps
// (default 5500) in main/motion/motion_state.h.
static constexpr int32_t SG_WORK_ZONE_MIN = 0;
static constexpr int32_t SG_WORK_ZONE_MAX = 20000;
static constexpr uint32_t CREEP_HOME_SPEED = CAL_SPEED_HZ;
static constexpr uint32_t CREEP_HOME_ACCEL = CAL_ACCEL;

// ==========================================================================
//  DISPLAY / LAYOUT
// ==========================================================================
static constexpr int SCR_W = 172, SCR_H = 320, NAV_H = 60, CONTENT_H = SCR_H - NAV_H;

// How long the destructive "Reset Cal" button on the Config screen stays armed
// after the first tap. A second tap inside this window commits; anything else
// (or the timeout) disarms it - the touch UI has no modal dialog, and a
// stray/curious tap must never wipe a calibration on its own.
static constexpr uint32_t UI_CONFIRM_ARM_MS = 5000;

// ==========================================================================
//  LOG RING BUFFER
// ==========================================================================
static constexpr uint16_t LOG_LINES = 500;
static constexpr uint16_t LOG_LINE_LEN = 140;

// ==========================================================================
//  SSE / BROADCAST
// ==========================================================================
static constexpr uint32_t SSE_INTERVAL_MS = 250;
// broadcastState() only actually sends the state payload when it differs from
// the last one sent (see web_server.cpp) - this is the fallback cadence for a
// heartbeat event sent even when nothing changed, so a client stuck on an
// unchanged state can still tell the connection is alive vs. dead.
static constexpr uint32_t SSE_HEARTBEAT_MS = 8000;

// ==========================================================================
//  WEB AUTHENTICATION
// ==========================================================================
// Every state-changing endpoint (all POSTs + the OTA upload) requires HTTP
// Digest auth - Digest, not Basic, because the device serves plain HTTP, so
// Basic would put the password on the wire in reversible base64 on every
// request. Reads (dashboard, /api/v1/state, SSE) are deliberately left open:
// they only expose the cycle counter / SSID / IP, and gating them would break
// EventSource and add a login prompt to merely glancing at the counter.
//
// The password lives in NVS and is changeable from the web UI's WiFi page.
// WEB_AUTH_DEFAULT_PASS is only the FACTORY DEFAULT used until it is changed -
// this repo is public, so treat the default as public knowledge and change it
// on first setup. The firmware logs a warning on every boot while it is still
// at the default.
static const char *WEB_AUTH_USER = "autolee";
static const char *WEB_AUTH_DEFAULT_PASS = "autolee";
static const char *WEB_AUTH_REALM = "AutoLee";
static constexpr size_t WEB_AUTH_PASS_MAX = 64;
// Floor for a password the operator chooses themselves (the WiFi setup page and
// POST /api/v1/system/web_password). Deliberately not applied to
// WEB_AUTH_DEFAULT_PASS, which is shorter and is the state this floor exists to
// get the device out of.
static constexpr size_t WEB_AUTH_PASS_MIN = 8;

// Both of the above apply only in STA mode (device joined a real network). On
// its own setup AP the web control surface is deliberately unauthenticated -
// the WPA2 AP key lives only on the LCD and its join QR, so physical presence
// at the rig is the gate. See the middleware in web_server.cpp.

// ==========================================================================
//  OTA
// ==========================================================================
// If an in-flight OTA upload goes this long with no new chunk, treat it as a
// dead/aborted transfer (client vanished mid-upload) so the in-progress flag
// can't stay stuck forever. Generous, since a slow-but-live upload still sends
// chunks far more often than this.
static constexpr uint32_t OTA_STALE_TIMEOUT_MS = 15000;

// ==========================================================================
//  STOP TIMEOUT
// ==========================================================================
static constexpr uint32_t STOP_TIMEOUT_MS = 8000;
// Position tolerance (steps) for treating the graceful stop as arrived at UP.
static constexpr long STOP_ARRIVAL_TOL = 10;

// Upper bound on how long motion code waits for an in-flight move to report
// itself stopped. Must comfortably exceed the longest legitimate single move
// (a full CAL_SEARCH_STEPS search at CAL_SPEED_HZ, ~3.5s) - this is a
// stuck-stepper backstop, not a motion deadline.
static constexpr uint32_t MOVE_WAIT_TIMEOUT_MS = 30000;
// Grace period after the escalation forceStop() before declaring the axis lost.
static constexpr uint32_t MOVE_STOP_GRACE_MS = 500;

// How long a UI refresh waits for the LVGL port lock before giving up and
// skipping that update. Bounded because several of these entry points are
// reached from the watchdog-subscribed pump_task - see ui_touch.cpp's ui_lock().
static constexpr uint32_t UI_LOCK_TIMEOUT_MS = 500;

// ==========================================================================
//  UI COUNTER / BATCH LIMITS
// ==========================================================================
// 4-digit ceiling shared by the lifetime cycle counter and the batch target
// (both rendered in a 4-char field on the 172px display).
static constexpr long COUNTER_MAX = 9999;
static constexpr int BATCH_TARGET_MAX = 9999;

// ==========================================================================
//  TASK WATCHDOG (safety)
//  If the main loop stalls (hung SPI, stuck WiFi stack, etc.) the hardware
//  watchdog resets the board rather than leaving a powered stepper in an
//  undefined state. The blocking calibration/homing loops feed it explicitly
//  (see wdt_feed() in motion.cpp), so the timeout only needs to exceed a
//  single feed gap. The actual timeout is an ESP-IDF Kconfig setting
//  (CONFIG_ESP_TASK_WDT_TIMEOUT_S, set to 8 in sdkconfig.defaults), not a
//  compile-time constant here - there is no build-time knob in this file.
// ==========================================================================
