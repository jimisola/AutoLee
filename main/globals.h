#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "config.h"
// The motion/endpoint/batch/profile state that used to be declared here as
// loose globals now lives in one lock-protected struct (`g_motion`) - see
// main/motion/motion_state.h for the ownership and snapshot rules. Included
// here so everything that already includes globals.h still sees RunState and
// the ui_speed_hz / RUN_SG_TRIP accessors.
#include "motion_state.h"

// Mutable, cross-module state - defined once in globals.cpp. Mirrors the
// original Arduino firmware's `AutoLee.ino` role as the single definition
// site for these globals.

// Deferred reboot (set from a handler, serviced by pump_task). Motion-affecting
// requests go through motion_cmd:: instead - see main/motion/motion_cmd.h.
extern volatile bool rebootRequested;
extern uint32_t rebootRequestMs;

// pump_task's handle, captured at creation (app_main.cpp) so web_server.cpp's
// diagnostics endpoint can read its stack high-water mark. Never used to
// suspend/delete/notify the task - purely a read-only diagnostics handle.
extern TaskHandle_t g_pump_task_handle;

// Log ring for the web UI's log panel + SSE "log" events. Every pushed line
// is prefixed "HH:MM:SS.mmm L " (uptime since boot - the board has no RTC/NTP -
// and a single-char level: D/I/W/E) so the dashboard's Log page can show and
// filter by both. webLog() is the plain call sites keep using (Info level);
// webLogLevel() is for sites that are actually Debug (high-frequency
// per-iteration diagnostic traces - StallGuard sampling, calibration search),
// a warning, or an error (jam, calibration/homing failure, NVS errors,
// security notices).
//
// g_logLevel is the single shared minimum threshold for BOTH the ring (and
// therefore the web dashboard/SSE) and the mirrored ESP_LOG* serial output -
// a line below threshold is dropped from both, not filtered per-sink, so
// there's one setting to reason about instead of two that can drift apart.
// Defaults to Info (Debug traces hidden) and is runtime-settable via
// GET/PUT /api/v1/system/log_level (main/net/web_server.cpp), persisted in
// NVS outside the versioned settings blob - same pattern as the web
// password, not a calibration/tuning value.
#include "log_ring.h"
extern autolee::LogRing<LOG_LINES, LOG_LINE_LEN> g_log;
extern uint32_t logSentSerial;  // last serial# broadcast over SSE
enum class LogLevel : uint8_t { Debug, Info, Warn, Error };
extern LogLevel g_logLevel;
const char *logLevelName(LogLevel level);
bool logLevelFromName(const char *name, LogLevel &out);
void webLog(const char *fmt, ...);
void webLogLevel(LogLevel level, const char *fmt, ...);

// LVGL screens + widgets (defined in ui_touch.cpp; single definition site
// matches the rest of this file since they're shared across ui_touch.cpp,
// motion.cpp, and web_server.cpp).
extern lv_obj_t *main_scr, *settings_scr, *config_scr, *profile_scr;
extern lv_obj_t *tuning_scr, *ep_up_scr, *ep_dn_scr;
extern lv_obj_t *wifi_scr;
extern lv_obj_t *counter_label, *main_warn, *main_warn_lbl;
extern lv_obj_t *lbl_speed_val;
extern lv_obj_t *profile_btns[NUM_PROFILES];
extern lv_obj_t *lbl_profile_info;
extern lv_obj_t *lbl_ep_up, *lbl_ep_dn, *lbl_travel;
extern lv_obj_t *lbl_up_eff, *lbl_dn_eff;
extern lv_obj_t *lbl_ep_up_val, *lbl_ep_dn_val;
extern lv_obj_t *lbl_wifi_status;
extern lv_obj_t *wifi_qr;         // WiFi-join QR code, shown in AP-setup mode
extern lv_obj_t *lbl_wifi_key;    // the AP's WPA2 key, shown in AP-setup mode
extern lv_obj_t *btn_wifi_reset;  // hidden in AP-setup mode (nothing to reset)
extern lv_obj_t *btn_wifi_skip;   // AP-setup only: go to main UI without configuring WiFi
extern lv_obj_t *btn_wifi_back;   // nav Back; hidden in AP-setup mode (Skip replaces it)
extern lv_obj_t *btn_run_global;
extern lv_obj_t *btn_calibrate;
extern lv_obj_t *jam_scr;
extern lv_obj_t *jam_status_lbl;
extern lv_obj_t *stall_scr;
extern lv_obj_t *lbl_sg_val;
extern lv_obj_t *batch_scr;
extern lv_obj_t *lbl_batch_val;
extern lv_obj_t *lbl_batch_remain;
