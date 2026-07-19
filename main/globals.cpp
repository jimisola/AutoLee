#include <cstdarg>
#include <cstdio>
#include "esp_log.h"

#include "config.h"
#include "globals.h"

// config.h's cross-module mutable state (single definition site).
SpeedProfile profiles[NUM_PROFILES] = {
    {"Slow", 15000, 350},
    {"Normal", 35000, 15},
    {"Fast", 45000, 1},
};
uint8_t activeProfile = 1;  // default to Normal
int32_t upOffsetSteps = 0;
int32_t downOffsetSteps = 0;
uint16_t RUN_CURRENT_MA = 3500;
int32_t SG_WORK_ZONE_STEPS = 5500;

// globals.h's motion-state (single definition site).
volatile RunState runState = IDLE;
long currentTarget = 0;
uint32_t stopEntryMs = 0;

long rawUp = 0, rawDown = 0;
long endpointUp = 0, endpointDown = 0;
bool endpointsCalibrated = false;
long counter = 0;

uint32_t lastDirectionChangeMs = 0;
uint8_t runSGHighCount = 0;
uint8_t runSGLowCount = 0;

bool batchActive = false;
int32_t batchCount = 0;
int32_t batchTarget = 0;

volatile bool rebootRequested = false;
uint32_t rebootRequestMs = 0;

autolee::LogRing<LOG_LINES, LOG_LINE_LEN> g_log;
uint32_t logSentSerial = 0;

lv_obj_t *main_scr = nullptr, *settings_scr = nullptr, *config_scr = nullptr,
         *profile_scr = nullptr;
lv_obj_t *tuning_scr = nullptr, *ep_up_scr = nullptr, *ep_dn_scr = nullptr;
lv_obj_t *wifi_scr = nullptr;
lv_obj_t *counter_label = nullptr, *main_warn = nullptr, *main_warn_lbl = nullptr;
lv_obj_t *lbl_speed_val = nullptr;
lv_obj_t *profile_btns[NUM_PROFILES] = {nullptr};
lv_obj_t *lbl_profile_info = nullptr;
lv_obj_t *lbl_ep_up = nullptr, *lbl_ep_dn = nullptr, *lbl_travel = nullptr;
lv_obj_t *lbl_up_eff = nullptr, *lbl_dn_eff = nullptr;
lv_obj_t *lbl_ep_up_val = nullptr, *lbl_ep_dn_val = nullptr;
lv_obj_t *lbl_wifi_status = nullptr;
lv_obj_t *btn_run_global = nullptr;
lv_obj_t *btn_calibrate = nullptr;
lv_obj_t *jam_scr = nullptr;
lv_obj_t *jam_status_lbl = nullptr;
lv_obj_t *stall_scr = nullptr;
lv_obj_t *lbl_sg_val = nullptr;
lv_obj_t *batch_scr = nullptr;
lv_obj_t *lbl_batch_val = nullptr;
lv_obj_t *lbl_batch_remain = nullptr;

void webLog(const char *fmt, ...) {
  char line[LOG_LINE_LEN];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  g_log.push(line);
  ESP_LOGI("weblog", "%s", line);
}
