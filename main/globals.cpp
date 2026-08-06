#include <cstdarg>
#include <cstdio>
#include <strings.h>  // strcasecmp
#include "esp_log.h"
#include "esp_timer.h"

#include "config.h"
#include "globals.h"

// The motion/endpoint/batch/profile state moved to motion_state.cpp's
// `g_motion` (one struct behind one spinlock) - see motion_state.h.

volatile bool rebootRequested = false;
uint32_t rebootRequestMs = 0;

TaskHandle_t g_pump_task_handle = nullptr;

autolee::LogRing<LOG_LINES, LOG_LINE_LEN> g_log;
uint32_t logSentSerial = 0;
LogLevel g_logLevel = LogLevel::Info;

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
lv_obj_t *wifi_qr = nullptr;
lv_obj_t *lbl_wifi_key = nullptr;
lv_obj_t *btn_wifi_reset = nullptr;
lv_obj_t *btn_wifi_skip = nullptr;
lv_obj_t *btn_wifi_back = nullptr;
lv_obj_t *btn_run_global = nullptr;
lv_obj_t *btn_calibrate = nullptr;
lv_obj_t *jam_scr = nullptr;
lv_obj_t *jam_status_lbl = nullptr;
lv_obj_t *stall_scr = nullptr;
lv_obj_t *lbl_sg_val = nullptr;
lv_obj_t *batch_scr = nullptr;
lv_obj_t *lbl_batch_val = nullptr;
lv_obj_t *lbl_batch_remain = nullptr;

const char *logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Error:
      return "error";
    default:
      return "info";
  }
}

bool logLevelFromName(const char *name, LogLevel &out) {
  if (strcasecmp(name, "debug") == 0) {
    out = LogLevel::Debug;
  } else if (strcasecmp(name, "info") == 0) {
    out = LogLevel::Info;
  } else if (strcasecmp(name, "warn") == 0) {
    out = LogLevel::Warn;
  } else if (strcasecmp(name, "error") == 0) {
    out = LogLevel::Error;
  } else {
    return false;
  }
  return true;
}

static void webLogImpl(LogLevel level, const char *category, const char *fmt, va_list args) {
  // ESP-IDF gates ESP_LOG* on two independent things beyond our own check
  // below: a compile-time ceiling (CONFIG_LOG_MAXIMUM_LEVEL, raised to Debug
  // in sdkconfig.defaults so ESP_LOGD isn't compiled out entirely) and a
  // per-tag runtime default (esp_log_level_set(), which otherwise defaults
  // to CONFIG_LOG_DEFAULT_LEVEL = Info and would silently eat our own
  // Debug-level calls). Raise the "weblog" tag's runtime ceiling once so
  // g_logLevel is the only gate that matters - not two that can drift apart.
  static bool tagLevelRaised = false;
  if (!tagLevelRaised) {
    esp_log_level_set("weblog", ESP_LOG_DEBUG);
    tagLevelRaised = true;
  }

  // Below the configured threshold: drop before formatting anything - this
  // is the path Debug-tier per-iteration traces take most of the time, so
  // skipping the snprintf/ESP_LOG work (not just the ring push) matters.
  if (level < g_logLevel) return;

  // Uses esp_timer_get_time() directly rather than the 32-bit millis() helper,
  // which wraps at ~49.7 days - a press left running that long would otherwise
  // have its log timestamps silently jump back to 00:00:00. Hours is
  // deliberately unbounded width (not %02u): a fixed 2-digit field would wrap
  // the *display* back to 00 at 100h even though the underlying count is fine.
  const uint64_t ms = (uint64_t)(esp_timer_get_time() / 1000);
  const unsigned long h = (unsigned long)(ms / 3600000ull);
  const unsigned m = (unsigned)((ms / 60000ull) % 60ull);
  const unsigned s = (unsigned)((ms / 1000ull) % 60ull);
  const unsigned msPart = (unsigned)(ms % 1000ull);
  const char levelChar = level == LogLevel::Error   ? 'E'
                         : level == LogLevel::Warn  ? 'W'
                         : level == LogLevel::Debug ? 'D'
                                                    : 'I';

  // Millisecond resolution matters here: jam/stall detection and StallGuard
  // sampling happen well within a single second, so second-only timestamps
  // would leave rapid-fire lines unordered relative to each other.
  char line[LOG_LINE_LEN];
  const int prefixLen = snprintf(line, sizeof(line), "%lu:%02u:%02u.%03u %c [%s] ", h, m, s, msPart,
                                 levelChar, category);
  if (prefixLen > 0 && (size_t)prefixLen < sizeof(line)) {
    vsnprintf(line + prefixLen, sizeof(line) - (size_t)prefixLen, fmt, args);
  }
  g_log.push(line);
  // Mirrored to serial at matching ESP-IDF severity (not always LOGI) so
  // esp_log_level_set()/idf.py monitor's own filtering agrees with the level
  // embedded in the line instead of everything showing as plain Info there.
  switch (level) {
    case LogLevel::Debug:
      ESP_LOGD("weblog", "%s", line);
      break;
    case LogLevel::Warn:
      ESP_LOGW("weblog", "%s", line);
      break;
    case LogLevel::Error:
      ESP_LOGE("weblog", "%s", line);
      break;
    default:
      ESP_LOGI("weblog", "%s", line);
      break;
  }
}

void webLog(const char *category, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  webLogImpl(LogLevel::Info, category, fmt, args);
  va_end(args);
}

void webLogLevel(LogLevel level, const char *category, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  webLogImpl(level, category, fmt, args);
  va_end(args);
}
