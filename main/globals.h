#pragma once

#include <cstdint>
#include "lvgl.h"
#include "config.h"

// Mutable, cross-module state - defined once in globals.cpp. Mirrors the
// original Arduino firmware's `AutoLee.ino` role as the single definition
// site for these globals; config.h's `extern` declarations point here now
// instead.

enum RunState : uint8_t { IDLE, RUNNING, STOPPING, CALIBRATING, STALLED, HOMING };
extern volatile RunState runState;
extern long currentTarget;
extern uint32_t stopEntryMs;

extern long rawUp, rawDown;
extern long endpointUp, endpointDown;
extern bool endpointsCalibrated;
extern long counter;

extern uint32_t lastDirectionChangeMs;
extern uint8_t runSGHighCount;
extern uint8_t runSGLowCount;

extern bool batchActive;
extern int32_t batchCount;
extern int32_t batchTarget;

// Web request flags (set from the HTTP server's task, serviced from the main
// loop) - mirrors the original's "async callbacks must not touch motion"
// rule (see CLAUDE.md).
extern volatile bool webCalRequested;
extern volatile bool webHomeRequested;
extern volatile bool rebootRequested;
extern uint32_t rebootRequestMs;

// Log ring for the web UI's log panel + SSE "log" events.
#include "log_ring.h"
extern autolee::LogRing<LOG_LINES, LOG_LINE_LEN> g_log;
extern uint32_t logSentSerial;  // last serial# broadcast over SSE
void webLog(const char *fmt, ...);

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
extern lv_obj_t *btn_run_global;
extern lv_obj_t *jam_scr;
extern lv_obj_t *jam_status_lbl;
extern lv_obj_t *stall_scr;
extern lv_obj_t *lbl_sg_val;
extern lv_obj_t *batch_scr;
extern lv_obj_t *lbl_batch_val;
extern lv_obj_t *lbl_batch_remain;
