// ============================================================================
//  AutoLee - ui_touch.cpp (ESP-IDF port)
//  LVGL touch UI, ported from src/ui_touch.cpp. Screen layout/behavior is
//  unchanged; only the plumbing differs (WiFi.*->wifi_mgr::, Arduino
//  millis()/constrain()->local equivalents, and thread-safety: esp_lvgl_port
//  runs LVGL in its own task, so every entry point here that can be called
//  from a different task (motion.cpp, web_server.cpp) takes
//  lvgl_port_lock()/unlock() - safe to nest since esp_lvgl_port's lock is a
//  recursive mutex, so it's also safe when already called from an LVGL
//  event handler.
//
//  *** UNVERIFIED ON HARDWARE - see main/display_touch.h's note. Layout,
//  color, and touch responsiveness are unverified; the connected board has
//  no screen/touch module attached. ***
// ============================================================================
#include "ui_touch.h"
#include "motion_cmd.h"
#include "globals.h"
#include "motion.h"
#include "motion_state.h"
#include "wifi_mgr.h"
#include "web_server.h"  // requestWebPasswordReset() - the "Reset Pwd" button

#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"  // ui_log_heartbeat() reads the backlight pad directly
#include "config.h"       // GFX_BL

#include "endpoint_math.h"

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Bounded LVGL port lock.
//
// lvgl_port_lock(0) waits forever. Most of the entry points below are reached
// from motion_cmd::processPendingCommands() on pump_task, which is subscribed
// to the task watchdog and is also what dispatches motion - so a stuck LVGL
// flush holding the port mutex would block motion dispatch indefinitely, and
// none of these call sites checked the return value to notice. A UI refresh is
// by definition droppable, so every one of them now waits a bounded time and
// skips the update if it can't get the lock.
//
// The exception is buildUI(), which constructs the screens at boot: skipping
// that would leave the device with no UI at all, so it keeps the infinite wait
// (and runs before pump_task exists, so nothing is watchdog-subscribed yet).
static bool ui_lock(const char *what) {
  if (lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) return true;
  webLogLevel(LogLevel::Warn, "UI", "LVGL lock timeout after %ums - skipped %s",
              (unsigned)UI_LOCK_TIMEOUT_MS, what);
  return false;
}

// Everything in this file runs on the LVGL task (event callbacks, the 100ms
// timer) or is called into from pump_task (motion.cpp / motion_cmd.cpp) - never
// as the owner of the motion state. So label updates read one
// motion_state::snapshot() and render from that local copy (self-consistent,
// and no spinlock held across LVGL calls), and the few handlers that change
// motion state write g_motion under a motion_state::Guard. See
// main/motion/motion_state.h.

// ==========================================================================
//  LVGL UI HELPERS
// ==========================================================================
void go(lv_obj_t *scr) {
  // lv_scr_load touches LVGL internals, so it must hold the port lock. go() is
  // called both from LVGL event callbacks (lock already held - the port mutex
  // is recursive, so re-taking is fine) and from app_main's boot-time
  // navigation on the main task (lock NOT held). Without this, that boot-time
  // go(wifi_scr) races the LVGL render task and can freeze the display blank.
  if (!ui_lock("go")) return;
  lv_scr_load(scr);
  lvgl_port_unlock();
}

void ui_force_full_repaint() {
  // Repaint everything once, after boot has finished.
  //
  // LVGL only redraws what has been invalidated. buildUI() draws the screen
  // once, and from then on the only thing dirtying anything is the 100ms
  // counter timer, which touches one small label. So if that single initial
  // full draw does not reach the panel, the display stays black indefinitely
  // while every software indicator - active screen, child count, LVGL tick,
  // backlight level - reads perfectly healthy. That was the observed state, and
  // a periodic full invalidate confirmed it: the UI appeared as soon as
  // something forced a repaint, so the content and the panel were fine all
  // along and only the first flush was lost.
  //
  // Why that first flush goes missing is NOT established. buildUI() runs before
  // the blocking WiFi connect and the web-server start, both of which are heavy
  // and share the SPI bus with the TMC5160 via the display - so this is called
  // once after all of it has settled, which is the point where the bus and the
  // scheduler are quiet again. Treat this as a targeted workaround with a known
  // symptom and an unknown cause, not as a fix for the underlying race.
  if (!ui_lock("repaint")) return;
  lv_obj_t *act = lv_scr_act();
  if (act) lv_obj_invalidate(act);
  lvgl_port_unlock();
}

void ui_log_heartbeat() {
  // Read the backlight pin first, outside the lock: if the LVGL lock is the
  // thing that is stuck, this is the one number still worth having, and
  // gpio_get_level() reads the pad directly rather than a cached value.
  const int bl = gpio_get_level((gpio_num_t)GFX_BL);
  const uint32_t tick = lv_tick_get();

  if (!ui_lock("heartbeat")) {
    ESP_LOGW("ui", "heartbeat: LVGL LOCK BUSY - tick=%lu bl=%d", (unsigned long)tick, bl);
    return;
  }
  lv_obj_t *act = lv_scr_act();
  // Child count separates "a screen is loaded but empty" - which renders as
  // flat black, since style_screen() paints the background black - from "the
  // screen has its widgets and they are simply not reaching the panel".
  const uint32_t kids = act ? (uint32_t)lv_obj_get_child_cnt(act) : 0;

  lvgl_port_unlock();

  ESP_LOGW("ui", "heartbeat: scr=%p main=%p match=%d kids=%lu tick=%lu bl=%d", (void *)act,
           (void *)main_scr, act == main_scr, (unsigned long)kids, (unsigned long)tick, bl);
}

static void style_screen(lv_obj_t *scr) {
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
}

static lv_obj_t *make_content(lv_obj_t *scr) {
  lv_obj_t *c = lv_obj_create(scr);
  lv_obj_set_size(c, SCR_W, CONTENT_H);
  lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(c, 10, LV_PART_MAIN);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 10, LV_PART_MAIN);
  return c;
}

static lv_obj_t *make_content_free(lv_obj_t *scr) {
  lv_obj_t *c = lv_obj_create(scr);
  lv_obj_set_size(c, SCR_W, CONTENT_H);
  lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static lv_obj_t *make_nav(lv_obj_t *scr) {
  lv_obj_t *n = lv_obj_create(scr);
  lv_obj_set_size(n, SCR_W, NAV_H);
  lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(n, lv_color_hex(0x0C0C0C), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(n, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(n, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(n, 10, LV_PART_MAIN);
  lv_obj_clear_flag(n, LV_OBJ_FLAG_SCROLLABLE);
  return n;
}

static lv_obj_t *make_title(lv_obj_t *p, const char *txt) {
  lv_obj_t *t = lv_label_create(p);
  lv_label_set_text(t, txt);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_26, LV_PART_MAIN);
  lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  return t;
}

static lv_obj_t *make_btn(lv_obj_t *p, const char *txt, int w, int h, uint32_t bg,
                          const lv_font_t *f) {
  lv_obj_t *b = lv_btn_create(p);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(l, f, LV_PART_MAIN);
  lv_obj_center(l);
  return b;
}

static lv_obj_t *make_btn_multiline(lv_obj_t *p, const char *txt, int w, int h, uint32_t bg,
                                    const lv_font_t *f) {
  lv_obj_t *b = lv_btn_create(p);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, w - 8);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(l, f, LV_PART_MAIN);
  lv_obj_center(l);
  return b;
}

static lv_obj_t *make_card(lv_obj_t *p, int w, int h) {
  lv_obj_t *c = lv_obj_create(p);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(0x242424), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(c, 10, LV_PART_MAIN);
  lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(c, 10, LV_PART_MAIN);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

// ==========================================================================
//  JAM SCREEN FUNCTIONS
// ==========================================================================
void showJamScreen() {
  if (!ui_lock("showJamScreen")) return;
  if (jam_status_lbl) lv_label_set_text(jam_status_lbl, "Press to return home");
  go(jam_scr);
  lvgl_port_unlock();
}

// Called from pump_task when the jam-recovery home finishes. Without this the
// jam screen is a dead end: its only control is "Return Home", nothing else
// navigates off it, and the port dropped the original's status-label update -
// so after a jam the operator got no feedback and needed a reboot or the web
// UI to carry on. Only navigates if the jam screen is still the active one, so
// it can't yank the display out from under someone who has navigated away.
void ui_jam_recovery_finished(bool homed) {
  if (!ui_lock("ui_jam_recovery_finished")) return;
  if (jam_status_lbl) {
    lv_label_set_text(jam_status_lbl, homed ? "Homed - returning to main"
                                            : "Home FAILED - recalibrate before running");
  }
  if (homed && jam_scr && main_scr && lv_scr_act() == jam_scr) go(main_scr);
  lvgl_port_unlock();
}

static void onJamReturnHome(lv_event_t *e) {
  LV_UNUSED(e);
  // While the home is running, this button cancels it - same one-button pattern
  // as Calibrate. safeCreepHome() is the same blocking sensorless search and was
  // equally uninterruptible. Cancelling lands on IDLE with the axis
  // unreferenced, so a run is still refused and Return Home is still offered:
  // an abort is never a way past the jam recovery, only a way to stop the
  // machine creeping while you look at it.
  if (motion_state::snapshot().runState == HOMING) {
    motion_cmd::requestAbort();
    return;
  }
  // Deferred to pump_task: safeCreepHome() drives the stepper + TMC SPI for
  // seconds. Running it here would block the LVGL task and race pump_task.
  motion_cmd::requestReturnHome();
}

// ==========================================================================
//  UI UPDATE FUNCTIONS
// ==========================================================================
// The main-screen warning banner has two reasons to appear, in priority order:
//   1. no calibration at all      -> "NOT CALIBRATED" (inert, Calibrate is on
//      the settings screen, as before)
//   2. calibration restored from NVS but the axis has not been re-referenced
//      since the reboot (MotionState::positionReferenceStale) -> the banner
//      turns into the affordance for fixing it, because in that state
//      startRunBetweenEndpoints() refuses to run and the only other Return Home
//      button lives on the jam screen, which is unreachable from IDLE.
// Same object, same style, same update entry point - only the text and whether
// it is clickable change.
static void onMainWarnReturnHome(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  // Re-check off a snapshot: the banner may still be clickable for one timer
  // period after a home already cleared the flag.
  if (!motion_state::snapshot().positionReferenceStale) return;
  // Deferred to pump_task, exactly like the jam screen's button: safeCreepHome()
  // drives the stepper + TMC SPI for seconds.
  motion_cmd::requestReturnHome();
}

void ui_update_main_warning() {
  const MotionState ms = motion_state::snapshot();
  const bool calibrated = ms.endpointsCalibrated;
  const bool stale = ms.positionReferenceStale;
  if (!ui_lock("ui_update_main_warning")) return;
  if (main_warn) {
    if (!calibrated) {
      if (main_warn_lbl) lv_label_set_text(main_warn_lbl, "NOT CALIBRATED");
      lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_HIDDEN);
    } else if (stale) {
      if (main_warn_lbl) lv_label_set_text(main_warn_lbl, "TAP: RETURN HOME");
      lv_obj_add_flag(main_warn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(main_warn, LV_OBJ_FLAG_HIDDEN);
    }
  }
  lvgl_port_unlock();
}

static void ui_create_main_warning(lv_obj_t *parent) {
  main_warn = lv_obj_create(parent);
  lv_obj_set_size(main_warn, 132, 24);
  lv_obj_set_style_radius(main_warn, 12, LV_PART_MAIN);
  lv_obj_set_style_border_width(main_warn, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(main_warn, lv_color_hex(0x3A2B12), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(main_warn, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(main_warn, 0, LV_PART_MAIN);
  lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_SCROLLABLE);
  main_warn_lbl = lv_label_create(main_warn);
  lv_label_set_text(main_warn_lbl, "NOT CALIBRATED");
  lv_obj_set_style_text_font(main_warn_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(main_warn_lbl, lv_color_hex(0xFFD37C), LV_PART_MAIN);
  lv_obj_set_style_text_align(main_warn_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(main_warn_lbl);
  // Registered once; the CLICKABLE flag (managed by ui_update_main_warning())
  // decides whether taps can reach it at all, and the handler re-checks anyway.
  lv_obj_add_event_cb(main_warn, onMainWarnReturnHome, LV_EVENT_CLICKED, nullptr);
  const MotionState ms = motion_state::snapshot();
  if (!ms.endpointsCalibrated) {
    lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_HIDDEN);
  } else if (ms.positionReferenceStale) {
    lv_label_set_text(main_warn_lbl, "TAP: RETURN HOME");
    lv_obj_add_flag(main_warn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(main_warn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(main_warn, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_update_speed_val() {
  const MotionState ms = motion_state::snapshot();
  if (!ui_lock("ui_update_speed_val")) return;
  if (lbl_speed_val)
    lv_label_set_text_fmt(lbl_speed_val, "%s  %lukHz", ms.profiles[ms.activeProfile].name,
                          (unsigned long)(ms.profiles[ms.activeProfile].speed_hz / 1000));
  lvgl_port_unlock();
}

void ui_update_profile_screen() {
  const MotionState ms = motion_state::snapshot();
  if (!ui_lock("ui_update_profile_screen")) return;
  for (uint8_t i = 0; i < NUM_PROFILES; i++) {
    if (!profile_btns[i]) continue;
    if (i == ms.activeProfile) {
      lv_obj_set_style_bg_color(profile_btns[i], lv_color_hex(0x1F6FEB), LV_PART_MAIN);
      lv_obj_set_style_border_width(profile_btns[i], 2, LV_PART_MAIN);
      lv_obj_set_style_border_color(profile_btns[i], lv_color_hex(0x00FF00), LV_PART_MAIN);
    } else {
      lv_obj_set_style_bg_color(profile_btns[i], lv_color_hex(0x3A3A3A), LV_PART_MAIN);
      lv_obj_set_style_border_width(profile_btns[i], 0, LV_PART_MAIN);
    }
  }
  if (lbl_profile_info) {
    lv_label_set_text_fmt(lbl_profile_info, "%luHz  SG=%u",
                          (unsigned long)ms.profiles[ms.activeProfile].speed_hz,
                          ms.profiles[ms.activeProfile].sg_trip);
  }
  lvgl_port_unlock();
}

void ui_update_tuning_numbers() {
  const MotionState ms = motion_state::snapshot();
  if (!ui_lock("ui_update_tuning_numbers")) return;
  if (!ms.endpointsCalibrated) {
    if (lbl_ep_up) lv_label_set_text(lbl_ep_up, "UP: -");
    if (lbl_ep_dn) lv_label_set_text(lbl_ep_dn, "DOWN: -");
    if (lbl_travel) lv_label_set_text(lbl_travel, "TRAVEL: -");
    if (lbl_up_eff) lv_label_set_text(lbl_up_eff, "Eff UP: -");
    if (lbl_dn_eff) lv_label_set_text(lbl_dn_eff, "Eff DN: -");
    lvgl_port_unlock();
    return;
  }
  if (lbl_ep_up) lv_label_set_text_fmt(lbl_ep_up, "UP: %ld", ms.rawUp);
  if (lbl_ep_dn) lv_label_set_text_fmt(lbl_ep_dn, "DOWN: %ld", ms.rawDown);
  if (lbl_travel) lv_label_set_text_fmt(lbl_travel, "TRAVEL: %ld", ms.rawDown - ms.rawUp);
  if (lbl_up_eff)
    lv_label_set_text_fmt(lbl_up_eff, "UP: %ld (%+ld)", ms.endpointUp, (long)ms.upOffsetSteps);
  if (lbl_dn_eff)
    lv_label_set_text_fmt(lbl_dn_eff, "DN: %ld (%+ld)", ms.endpointDown, (long)ms.downOffsetSteps);
  lvgl_port_unlock();
}

void ui_update_endpoint_edit_values() {
  const MotionState ms = motion_state::snapshot();
  if (!ui_lock("ui_update_endpoint_edit_values")) return;
  if (lbl_ep_up_val) lv_label_set_text_fmt(lbl_ep_up_val, "%ld", ms.endpointUp);
  if (lbl_ep_dn_val) lv_label_set_text_fmt(lbl_ep_dn_val, "%ld", ms.endpointDown);
  lvgl_port_unlock();
}

void ui_update_sg_val() {
  const MotionState ms = motion_state::snapshot();
  if (!ui_lock("ui_update_sg_val")) return;
  if (lbl_sg_val) lv_label_set_text_fmt(lbl_sg_val, "%u", ms.profiles[ms.activeProfile].sg_trip);
  lvgl_port_unlock();
}

void ui_update_batch_val() {
  const int32_t batchTarget = motion_state::snapshot().batchTarget;
  if (!ui_lock("ui_update_batch_val")) return;
  if (lbl_batch_val) {
    if (batchTarget <= 0)
      lv_label_set_text(lbl_batch_val, "OFF");
    else
      lv_label_set_text_fmt(lbl_batch_val, "%ld", batchTarget);
  }
  lvgl_port_unlock();
}

void ui_update_batch_remain() {
  const MotionState ms = motion_state::snapshot();
  if (!ui_lock("ui_update_batch_remain")) return;
  if (lbl_batch_remain) {
    if (ms.batchActive && ms.batchTarget > 0) {
      int32_t remain = ms.batchTarget - ms.batchCount;
      if (remain < 0) remain = 0;
      lv_label_set_text_fmt(lbl_batch_remain, "Batch: %ld left", remain);
      lv_obj_clear_flag(lbl_batch_remain, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(lbl_batch_remain, LV_OBJ_FLAG_HIDDEN);
    }
  }
  lvgl_port_unlock();
}

// Escape a field for a WiFi-join QR payload: backslash-escape the reserved
// characters. Our SSID and generated key never contain these, but the spec
// requires it, so do it defensively.
static std::string qrEscape(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

void ui_update_wifi_label() {
  if (!ui_lock("ui_update_wifi_label")) return;
  const bool apSetup = wifi_mgr::isApMode() && !wifi_mgr::isConnected();

  // AP-setup view: the join QR + the key text.
  if (wifi_qr && lbl_wifi_key) {
    if (apSetup) {
      // Standard WiFi-join QR: WIFI:T:WPA;S:<ssid>;P:<key>;;  ("WPA" covers
      // WPA2 and is what iOS/Android accept).
      std::string payload = "WIFI:T:WPA;S:" + qrEscape(DEFAULT_AP_SSID) +
                            ";P:" + qrEscape(wifi_mgr::apPassword()) + ";;";
      lv_qrcode_update(wifi_qr, payload.c_str(), payload.length());
      lv_obj_clear_flag(wifi_qr, LV_OBJ_FLAG_HIDDEN);
      // The URL line is the fallback for when the captive portal doesn't pop
      // by itself - which happens often enough to matter (a phone that has
      // already "seen" this AP, a browser that suppresses the CNA, or plain
      // Android/desktop behaviour). Without it, a device that has joined the
      // AP has no way to discover where the setup page lives, since the AP's
      // address is never shown anywhere else in AP mode: the status card that
      // normally carries the IP is hidden below in this view.
      // Blank line before the URL: SSID and key are what you type into the
      // phone's WiFi dialog, the URL is what you do afterwards in a browser.
      // Two steps, so they read as two groups rather than one list of three.
      lv_label_set_text_fmt(lbl_wifi_key, "SSID: %s\nKey: %s\n\nhttp://%s", DEFAULT_AP_SSID,
                            wifi_mgr::apPassword().c_str(), wifi_mgr::ipAddress().c_str());
      lv_obj_clear_flag(lbl_wifi_key, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(wifi_qr, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(lbl_wifi_key, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Connected/disconnected view: the status card. Hidden during AP setup.
  if (lbl_wifi_status) {
    lv_obj_t *card = lv_obj_get_parent(lbl_wifi_status);
    if (apSetup) {
      if (card) lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    } else {
      if (card) lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
      if (wifi_mgr::isConnected()) {
        lv_label_set_text_fmt(lbl_wifi_status, "%s\nIP: %s", wifi_mgr::ssid().c_str(),
                              wifi_mgr::ipAddress().c_str());
      } else {
        lv_label_set_text(lbl_wifi_status, "Disconnected");
      }
    }
  }

  // Nothing to reset while unconfigured - hide the button in AP-setup mode.
  if (btn_wifi_reset) {
    if (apSetup)
      lv_obj_add_flag(btn_wifi_reset, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_clear_flag(btn_wifi_reset, LV_OBJ_FLAG_HIDDEN);
  }

  // Skip is only meaningful on the auto-shown AP-setup screen; it replaces the
  // nav Back there (showing both is redundant), so they toggle inversely.
  if (btn_wifi_skip) {
    if (apSetup)
      lv_obj_clear_flag(btn_wifi_skip, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(btn_wifi_skip, LV_OBJ_FLAG_HIDDEN);
  }
  if (btn_wifi_back) {
    if (apSetup)
      lv_obj_add_flag(btn_wifi_back, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_clear_flag(btn_wifi_back, LV_OBJ_FLAG_HIDDEN);
  }
  lvgl_port_unlock();
}

// Renders one of three states, read from the motion state rather than passed in
// by the caller. This used to take a `bool running` that every call site worked
// out for itself, which went wrong in two separate ways:
//
//   - A caller could pass a value that disagreed with runState. The batch-start
//     path passed a hardcoded `true` even when startRunBetweenEndpoints() had
//     refused the start, leaving a red STOP on a press that was standing still.
//   - STOPPING rendered as plain green "RUN" while the carriage was still
//     decelerating toward UP. The tested table (motor_fsm.h) accepts neither
//     Start nor GracefulStop from STOPPING, so the button advertised "ready to
//     start", the tap was silently discarded, and the press kept moving.
//
// Driving it off the snapshot makes both impossible by construction. Called at
// the known transition points for an immediate response, and again from the
// 100ms counter_timer_cb so the button self-corrects on any path that doesn't
// go through one - the same belt-and-braces the Calibrate button already uses.
void ui_update_run_button() {
  const RunState state = motion_state::snapshot().runState;
  if (!ui_lock("ui_update_run_button")) return;
  if (btn_run_global) {
    lv_obj_t *l = lv_obj_get_child(btn_run_global, 0);
    uint32_t bg, fg;
    const char *txt;
    // Disabled only for STOPPING: it is the one state where a tap has no legal
    // effect at all, so the button says so instead of swallowing it.
    bool tappable = true;
    switch (state) {
      case RUNNING:
        bg = 0xFF0000;
        fg = 0xFFFFFF;
        txt = "STOP";
        break;
      case STOPPING:
        bg = 0xB4540A;
        fg = 0xFFFFFF;
        txt = "STOPPING";
        tappable = false;
        break;
      default:
        bg = 0x00FF00;
        fg = 0x000000;
        txt = "RUN";
        break;
    }
    lv_obj_set_style_bg_color(btn_run_global, lv_color_hex(bg), LV_PART_MAIN);
    if (l) {
      lv_label_set_text(l, txt);
      lv_obj_set_style_text_color(l, lv_color_hex(fg), LV_PART_MAIN);
    }
    if (tappable)
      lv_obj_clear_state(btn_run_global, LV_STATE_DISABLED);
    else
      lv_obj_add_state(btn_run_global, LV_STATE_DISABLED);
  }
  lvgl_port_unlock();
}

// ==========================================================================
//  UI EVENT HANDLERS
// ==========================================================================
static void on_ep_up_delta(lv_event_t *e) {
  if (!motion_state::snapshot().endpointsCalibrated) return;
  int32_t d = (int32_t)(intptr_t)lv_event_get_user_data(e);
  {
    motion_state::Guard g;
    g_motion.upOffsetSteps = autolee::clamp_i32(g_motion.upOffsetSteps + d, OFFSET_MIN, OFFSET_MAX);
  }
  recomputeEffectiveEndpoints();  // takes the guard itself
  ui_update_endpoint_edit_values();
  ui_update_tuning_numbers();
}

static void on_ep_dn_delta(lv_event_t *e) {
  if (!motion_state::snapshot().endpointsCalibrated) return;
  int32_t d = (int32_t)(intptr_t)lv_event_get_user_data(e);
  {
    motion_state::Guard g;
    g_motion.downOffsetSteps =
        autolee::clamp_i32(g_motion.downOffsetSteps + d, OFFSET_MIN, OFFSET_MAX);
  }
  recomputeEffectiveEndpoints();  // takes the guard itself
  ui_update_endpoint_edit_values();
  ui_update_tuning_numbers();
}

static void build_endpoint_screen(lv_obj_t *scr, const char *titleTxt, bool isUp) {
  style_screen(scr);
  lv_obj_t *content = make_content(scr);
  lv_obj_t *nav = make_nav(scr);
  lv_obj_t *t = make_title(content, titleTxt);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 2);

  lv_obj_t *card = make_card(content, 150, 92);
  lv_obj_t *ln = lv_label_create(card);
  lv_label_set_text(ln, "Steps");
  lv_obj_set_style_text_color(ln, lv_color_hex(0xCFCFCF), LV_PART_MAIN);
  lv_obj_set_style_text_font(ln, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(ln, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *lv = lv_label_create(card);
  lv_obj_set_style_text_color(lv, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_set_style_text_font(lv, &lv_font_montserrat_26, LV_PART_MAIN);
  lv_obj_center(lv);
  if (isUp)
    lbl_ep_up_val = lv;
  else
    lbl_ep_dn_val = lv;

  lv_obj_t *grid = lv_obj_create(content);
  lv_obj_set_size(grid, 150, 110);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  const int bw = 48, bh = 44, gap = 3;
  const int x0 = 0, x1 = bw + gap, x2 = 2 * (bw + gap), y0 = 0, y1 = bh + gap;
  lv_obj_t *bN100 = make_btn(grid, "-100", bw, bh, 0x3A3A3A, &lv_font_montserrat_16);
  lv_obj_t *bN10 = make_btn(grid, "-10", bw, bh, 0x3A3A3A, &lv_font_montserrat_16);
  lv_obj_t *bN1 = make_btn(grid, "-1", bw, bh, 0x3A3A3A, &lv_font_montserrat_16);
  lv_obj_t *bP100 = make_btn(grid, "+100", bw, bh, 0x1F6FEB, &lv_font_montserrat_16);
  lv_obj_t *bP10 = make_btn(grid, "+10", bw, bh, 0x1F6FEB, &lv_font_montserrat_16);
  lv_obj_t *bP1 = make_btn(grid, "+1", bw, bh, 0x1F6FEB, &lv_font_montserrat_16);
  lv_obj_set_pos(bN100, x0, y0);
  lv_obj_set_pos(bP100, x0, y1);
  lv_obj_set_pos(bN10, x1, y0);
  lv_obj_set_pos(bP10, x1, y1);
  lv_obj_set_pos(bN1, x2, y0);
  lv_obj_set_pos(bP1, x2, y1);

  lv_event_cb_t cb = isUp ? on_ep_up_delta : on_ep_dn_delta;
  lv_obj_add_event_cb(bN100, cb, LV_EVENT_CLICKED, (void *)(intptr_t)-100);
  lv_obj_add_event_cb(bN10, cb, LV_EVENT_CLICKED, (void *)(intptr_t)-10);
  lv_obj_add_event_cb(bN1, cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
  lv_obj_add_event_cb(bP100, cb, LV_EVENT_CLICKED, (void *)(intptr_t)+100);
  lv_obj_add_event_cb(bP10, cb, LV_EVENT_CLICKED, (void *)(intptr_t)+10);
  lv_obj_add_event_cb(bP1, cb, LV_EVENT_CLICKED, (void *)(intptr_t)+1);

  lv_obj_t *back = make_btn(nav, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(back, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(
      back,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(tuning_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

static void on_go_profile(lv_event_t *e) {
  LV_UNUSED(e);
  ui_update_speed_val();
  ui_update_profile_screen();
  go(profile_scr);
}
static void on_go_tuning(lv_event_t *e) {
  LV_UNUSED(e);
  recomputeEffectiveEndpoints();
  ui_update_tuning_numbers();
  go(tuning_scr);
}
static void on_go_ep_up(lv_event_t *e) {
  LV_UNUSED(e);
  recomputeEffectiveEndpoints();
  ui_update_endpoint_edit_values();
  go(ep_up_scr);
}
static void on_go_ep_dn(lv_event_t *e) {
  LV_UNUSED(e);
  recomputeEffectiveEndpoints();
  ui_update_endpoint_edit_values();
  go(ep_dn_scr);
}

static void on_calibrate(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  LV_UNUSED(e);
  // While a calibration is running this same button is the cancel. The label
  // already says so (counter_timer_cb below), and a button that changes meaning
  // with the state it displays beats a second button that is disabled 99% of
  // the time on a 172x320 panel. A calibration blocks pump_task for tens of
  // seconds, so before this there was no way to stop one at all.
  if (motion_state::snapshot().runState == CALIBRATING) {
    motion_cmd::requestAbort();
    return;
  }
  // Deferred to pump_task rather than blocking the LVGL task here.
  //
  // This also fixes the "Calibrating..." label never rendering: calibration
  // blocks for seconds, and on esp_lvgl_port this handler runs ON the LVGL
  // task, so nothing could repaint until it returned. Karl's upstream v1.10.0
  // worked around the same symptom with lv_refr_now(); moving the work off the
  // UI task is the better fix for this threading model. The button label and
  // enabled state now follow runState via counter_timer_cb().
  motion_cmd::requestCalibrate();
}

// ==========================================================================
//  "Reset Cal" - destructive, so it needs a confirm step
// ==========================================================================
// There is no modal-dialog pattern anywhere in this UI (the only other
// destructive buttons, "Reset WiFi" and "Reset Count", fire on the first tap),
// and a whole extra confirm SCREEN for one rarely-used action would be a lot of
// machinery plus another navigation dead-end on a 172x320 display. So the
// button arms itself instead: the first tap turns it amber and relabels it
// "Sure? Tap", the second tap within UI_CONFIRM_ARM_MS commits, and the
// timeout disarms it. A single stray tap can therefore never wipe a
// calibration, and the operator is told what the next tap will do.
static lv_obj_t *btn_reset_cal = nullptr;
static lv_timer_t *reset_cal_timer = nullptr;  // non-null == armed

static void reset_cal_show_idle() {
  if (!btn_reset_cal) return;
  lv_obj_t *lbl = lv_obj_get_child(btn_reset_cal, 0);
  if (lbl) lv_label_set_text(lbl, "Reset Cal");
  lv_obj_set_style_bg_color(btn_reset_cal, lv_color_hex(0xB42318), LV_PART_MAIN);
}

// The arming timer is one-shot: LVGL deletes it right after this returns, so
// only clear the handle here - deleting it again would be a double free.
static void reset_cal_timeout_cb(lv_timer_t *t) {
  LV_UNUSED(t);
  reset_cal_timer = nullptr;
  reset_cal_show_idle();
}

static void reset_cal_disarm() {
  if (reset_cal_timer) {
    lv_timer_del(reset_cal_timer);
    reset_cal_timer = nullptr;
  }
  reset_cal_show_idle();
}

static void on_reset_cal(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!reset_cal_timer) {  // first tap: arm, change nothing else
    if (btn_reset_cal) {
      lv_obj_t *lbl = lv_obj_get_child(btn_reset_cal, 0);
      if (lbl) lv_label_set_text(lbl, "Sure? Tap");
      lv_obj_set_style_bg_color(btn_reset_cal, lv_color_hex(0xB4540A), LV_PART_MAIN);
    }
    reset_cal_timer = lv_timer_create(reset_cal_timeout_cb, UI_CONFIRM_ARM_MS, nullptr);
    if (reset_cal_timer) lv_timer_set_repeat_count(reset_cal_timer, 1);
    return;
  }
  reset_cal_disarm();
  // Deferred to pump_task like every other state-changing button: the reset
  // erases NVS and re-programs the TMC5160 over the SPI bus shared with this
  // display. pump_task also re-checks that the machine is IDLE before applying
  // it, so a run started between the two taps is not affected.
  motion_cmd::requestResetSettings();
}

// ==========================================================================
//  "Reset Pwd" - forgotten-password recovery, same two-tap confirm
// ==========================================================================
// The escape hatch for an operator locked out of the web UI. Lives here, on the
// LCD, because pressing a button on the panel is the strongest proof of
// physical presence the device has - stronger than the setup AP, whose key can
// be read from across the room. See webPasswordResetTick() in web_server.cpp
// for what it restores and why that is safe.
static lv_obj_t *btn_reset_pwd = nullptr;
static lv_timer_t *reset_pwd_timer = nullptr;  // non-null == armed

static void reset_pwd_set_label(const char *txt, uint32_t color) {
  if (!btn_reset_pwd) return;
  lv_obj_t *lbl = lv_obj_get_child(btn_reset_pwd, 0);
  if (lbl) lv_label_set_text(lbl, txt);
  lv_obj_set_style_bg_color(btn_reset_pwd, lv_color_hex(color), LV_PART_MAIN);
}

static void reset_pwd_timeout_cb(lv_timer_t *t) {
  LV_UNUSED(t);
  reset_pwd_timer = nullptr;
  reset_pwd_set_label("Reset Pwd", 0xB42318);
}

static void reset_pwd_disarm() {
  if (reset_pwd_timer) {
    lv_timer_del(reset_pwd_timer);
    reset_pwd_timer = nullptr;
  }
  reset_pwd_set_label("Reset Pwd", 0xB42318);
}

static void on_reset_pwd(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!reset_pwd_timer) {  // first tap: arm only
    reset_pwd_set_label("Sure? Tap", 0xB4540A);
    reset_pwd_timer = lv_timer_create(reset_pwd_timeout_cb, UI_CONFIRM_ARM_MS, nullptr);
    if (reset_pwd_timer) lv_timer_set_repeat_count(reset_pwd_timer, 1);
    return;
  }
  if (reset_pwd_timer) {
    lv_timer_del(reset_pwd_timer);
    reset_pwd_timer = nullptr;
  }
  // No optimistic "done" here - the label stays neutral until sse_task reports
  // the NVS write actually landed (ui_web_password_reset_finished). Telling an
  // operator their password is back to the default when the write failed would
  // send them off to log in with a credential the device does not have.
  reset_pwd_set_label("Resetting...", 0x2A2A2A);
  requestWebPasswordReset();
}

// The reset itself is a one-shot: once it reports, leave the outcome on screen
// until the operator navigates away (build_config_screen's Back disarms/clears).
void ui_web_password_reset_finished(bool ok) {
  if (!ui_lock("ui_web_password_reset_finished")) return;
  reset_pwd_set_label(ok ? "Pwd = autolee" : "Reset FAILED", ok ? 0x1F6FEB : 0xB42318);
  lvgl_port_unlock();
}

// The counter is capped for display only - motion.cpp keeps counting past
// COUNTER_MAX, and the web dashboard shows the true figure. A bare "9999" here
// therefore diverges from the web permanently once the cap is reached, with
// nothing to say which one is wrong; the "+" marks it as saturated instead.
static void set_counter_label(long c) {
  if (!counter_label) return;
  if (c < COUNTER_MAX)
    lv_label_set_text_fmt(counter_label, "%ld", c);
  else
    lv_label_set_text_fmt(counter_label, "%ld+", (long)COUNTER_MAX);
}

static void counter_timer_cb(lv_timer_t *t) {
  LV_UNUSED(t);
  const MotionState ms = motion_state::snapshot();
  set_counter_label(ms.counter);
  if (main_scr && lv_scr_act() == main_scr) {
    ui_update_main_warning();
    ui_update_batch_remain();
  }
  // Same reasoning as the Calibrate button below: pump_task owns the state, so
  // the button follows runState here rather than trusting every caller to have
  // pushed the right appearance. Catches STOPPING -> IDLE, which is reached by
  // handleMotion() without any explicit UI call.
  ui_update_run_button();
  // Calibration runs on pump_task now, so the button reflects runState here
  // instead of being driven inline by a blocking handler. It stays TAPPABLE
  // while busy - that tap is the cancel (on_calibrate above); disabling it was
  // what made an in-progress calibration a dead end.
  if (btn_calibrate) {
    lv_obj_t *lbl = lv_obj_get_child(btn_calibrate, 0);
    const bool busy = (ms.runState == CALIBRATING);
    if (lbl) lv_label_set_text(lbl, busy ? "Cancel Cal" : "Calibrate");
    lv_obj_set_style_bg_color(btn_calibrate, lv_color_hex(busy ? 0xB42318 : 0x444444),
                              LV_PART_MAIN);
    lv_obj_clear_state(btn_calibrate, LV_STATE_DISABLED);
  }
  // Same one-button pattern on the jam screen: Return Home becomes the cancel
  // for the home it started. Driven from here rather than from
  // ui_jam_recovery_finished() so it also follows a home started from the main
  // screen's stale-position banner, and so HOMING -> IDLE reverts it whatever
  // ended the home.
  if (btn_jam_home) {
    lv_obj_t *jl = lv_obj_get_child(btn_jam_home, 0);
    const bool homing = (ms.runState == HOMING);
    if (jl) lv_label_set_text(jl, homing ? "Cancel" : "Return Home");
    lv_obj_set_style_bg_color(btn_jam_home, lv_color_hex(homing ? 0xB42318 : 0x1F6FEB),
                              LV_PART_MAIN);
  }
}

// ==========================================================================
//  BUILD UI - called from app_main
// ==========================================================================
// Split into one build_<name>_screen() function per screen (was previously
// one ~510-line buildUI() with a centralized "build everything, then wire
// everything" tail block). That tail block turned out to be a purely
// stylistic artifact, not a real dependency: every callback in it only
// referenced navigation targets (the screen pointers, e.g. main_scr,
// config_scr - already file-scope extern globals in globals.h, shared with
// motion.cpp/web_server.cpp) or widgets already promoted to file-scope
// statics for other reasons (btn_calibrate, btn_reset_cal, profile_btns[],
// btn_wifi_reset/skip/back, btn_run_global). No callback reached into a
// button handle local to a *different* screen's construction block, so each
// screen's event wiring moves inline, right after that screen's widgets are
// created - no new promotions were needed beyond what was already global.
static void build_main_screen() {
  main_scr = lv_scr_act();
  style_screen(main_scr);
  lv_obj_t *mc = make_content_free(main_scr);
  lv_obj_t *mn = make_nav(main_scr);

  lv_obj_t *title = make_title(mc, "AutoLee");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *sub = lv_label_create(mc);
  lv_label_set_text(sub, "by K.L Design");
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(sub, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
  lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  lbl_speed_val = lv_label_create(mc);
  lv_label_set_text(lbl_speed_val, "");
  lv_obj_set_style_text_font(lbl_speed_val, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl_speed_val, lv_color_hex(0x6FA8FF), LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl_speed_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(lbl_speed_val, SCR_W - 20);
  lv_obj_align_to(lbl_speed_val, sub, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  ui_create_main_warning(mc);
  lv_obj_align_to(main_warn, lbl_speed_val, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  counter_label = lv_label_create(mc);
  set_counter_label(motion_state::snapshot().counter);
  lv_obj_set_style_text_font(counter_label, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_obj_set_style_text_color(counter_label, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 8);
  lv_obj_add_flag(counter_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      counter_label,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        {
          motion_state::Guard g;
          g_motion.counter = 0;
        }
        lv_label_set_text(counter_label, "0");
        // Original flashed the label red for 200ms as reset feedback via
        // a blocking delay() + manual lv_timer_handler() call - unsafe
        // here (this callback already runs inside esp_lvgl_port's
        // lv_timer_handler(), so re-entering it is undefined behavior).
        // Simplified to an immediate reset; no flash.
      },
      LV_EVENT_LONG_PRESSED, nullptr);

  lbl_batch_remain = lv_label_create(mc);
  lv_label_set_text(lbl_batch_remain, "");
  lv_obj_set_style_text_font(lbl_batch_remain, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl_batch_remain, lv_color_hex(0xFFD37C), LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl_batch_remain, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(lbl_batch_remain, LV_ALIGN_CENTER, 0, 44);
  lv_obj_add_flag(lbl_batch_remain, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *btn_batch = make_btn(mc, "Batch Run", 140, 36, 0x1F6FEB, &lv_font_montserrat_16);
  lv_obj_align(btn_batch, LV_ALIGN_BOTTOM_MID, 0, -56);
  lv_obj_add_event_cb(
      btn_batch,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        ui_update_batch_val();
        go(batch_scr);
      },
      LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_settings = make_btn(mc, "Settings", 140, 36, 0x3A3A3A, &lv_font_montserrat_16);
  lv_obj_align(btn_settings, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_add_event_cb(
      btn_settings,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(settings_scr);
      },
      LV_EVENT_CLICKED, nullptr);

  btn_run_global = make_btn(mn, "RUN", 140, 44, 0x00FF00, &lv_font_montserrat_22);
  lv_obj_align(btn_run_global, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(
      btn_run_global,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        // Deferred: touches the stepper + TMC SPI.
        motion_cmd::requestToggleRun();
      },
      LV_EVENT_CLICKED, nullptr);
}

// Settings screen
static void build_settings_screen() {
  settings_scr = lv_obj_create(nullptr);
  style_screen(settings_scr);
  lv_obj_t *sc = make_content(settings_scr);
  lv_obj_set_style_pad_row(sc, 6, LV_PART_MAIN);
  lv_obj_add_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *sn = make_nav(settings_scr);
  lv_obj_t *st2 = make_title(sc, "Settings");
  lv_obj_align(st2, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_t *b_cal = make_btn(sc, "Calibrate", 140, 44, 0x444444, &lv_font_montserrat_20);
  btn_calibrate = b_cal;
  lv_obj_t *b_config = make_btn(sc, "Config", 140, 44, 0x1F6FEB, &lv_font_montserrat_20);
  lv_obj_t *b_reset = make_btn(sc, "Reset Count", 140, 44, 0xB42318, &lv_font_montserrat_20);
  lv_obj_t *b_back_s = make_btn(sn, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(b_back_s, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(b_cal, on_calibrate, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_config,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        // Never enter the screen with either destructive button pre-armed, or
        // still showing the outcome of a previous password reset. Back does the
        // same on the way out, but Back is not the only way off this screen -
        // a jam takes the display over from anywhere.
        reset_cal_disarm();
        reset_pwd_disarm();
        go(config_scr);
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_reset,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        {
          motion_state::Guard g;
          g_motion.counter = 0;
        }
        if (counter_label) lv_label_set_text(counter_label, "0");
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_back_s,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(main_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

// Configuration screen
static void build_config_screen() {
  config_scr = lv_obj_create(nullptr);
  style_screen(config_scr);
  lv_obj_t *cfgc = make_content(config_scr);
  lv_obj_set_style_pad_row(cfgc, 6, LV_PART_MAIN);
  lv_obj_add_flag(cfgc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *cfgn = make_nav(config_scr);
  lv_obj_t *cfgt = make_title(cfgc, "Config");
  lv_obj_align(cfgt, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_t *b_speed = make_btn(cfgc, "Speed", 140, 44, 0x1F6FEB, &lv_font_montserrat_20);
  lv_obj_t *b_tuning = make_btn(cfgc, "Endpoints", 140, 44, 0x1F6FEB, &lv_font_montserrat_20);
  lv_obj_t *b_stall = make_btn(cfgc, "Stall Guard", 140, 44, 0x1F6FEB, &lv_font_montserrat_20);
  lv_obj_t *b_wifi = make_btn(cfgc, "WiFi Info", 140, 44, 0x1F6FEB, &lv_font_montserrat_20);
  // Last on the (scrollable) Config screen, deliberately out of the way of
  // normal operation: it discards the calibration + tuning. Two-tap confirm,
  // see on_reset_cal().
  btn_reset_cal = make_btn(cfgc, "Reset Cal", 140, 44, 0xB42318, &lv_font_montserrat_20);
  // Alongside Reset Cal, for the same reason: destructive, rarely used, and the
  // screen scrolls so it costs nothing above the fold. Two-tap confirm.
  btn_reset_pwd = make_btn(cfgc, "Reset Pwd", 140, 44, 0xB42318, &lv_font_montserrat_20);
  lv_obj_t *b_back_cfg = make_btn(cfgn, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(b_back_cfg, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(b_speed, on_go_profile, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(b_tuning, on_go_tuning, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_stall,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        ui_update_sg_val();
        go(stall_scr);
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_wifi,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        ui_update_wifi_label();
        go(wifi_scr);
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btn_reset_cal, on_reset_cal, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btn_reset_pwd, on_reset_pwd, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_back_cfg,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        reset_cal_disarm();  // leaving the screen cancels a pending confirm
        reset_pwd_disarm();  // and clears any lingering reset outcome
        go(settings_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

// Profile (speed) screen
static void build_profile_screen() {
  profile_scr = lv_obj_create(nullptr);
  style_screen(profile_scr);
  lv_obj_t *pc = make_content(profile_scr);
  lv_obj_set_style_pad_row(pc, 6, LV_PART_MAIN);
  lv_obj_t *pn = make_nav(profile_scr);
  lv_obj_t *pt = make_title(pc, "Speed");
  lv_obj_align(pt, LV_ALIGN_TOP_MID, 0, 2);

  lv_obj_t *pcard = make_card(pc, 150, 40);
  lbl_profile_info = lv_label_create(pcard);
  lv_obj_set_style_text_color(lbl_profile_info, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_profile_info, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_center(lbl_profile_info);

  const MotionState boot_ms = motion_state::snapshot();
  for (uint8_t i = 0; i < NUM_PROFILES; i++) {
    char label[32];
    snprintf(label, sizeof(label), "%s  %lukHz", boot_ms.profiles[i].name,
             (unsigned long)(boot_ms.profiles[i].speed_hz / 1000));
    profile_btns[i] = make_btn(pc, label, 140, 40, 0x3A3A3A, &lv_font_montserrat_16);
    lv_obj_add_event_cb(
        profile_btns[i],
        [](lv_event_t *e) {
          uint8_t idx = (uint8_t)(intptr_t)lv_event_get_user_data(e);
          // Deferred: setActiveProfile() drives the stepper. pump_task
          // refreshes the labels once applied.
          motion_cmd::requestProfile(idx);
          const MotionState ms = motion_state::snapshot();
          webLog("Motion", "Profile: %s spd=%lu sg=%u", ms.profiles[idx].name,
                 (unsigned long)ms.profiles[ms.activeProfile].speed_hz,
                 ms.profiles[ms.activeProfile].sg_trip);
        },
        LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  lv_obj_t *b_back_p = make_btn(pn, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(b_back_p, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(
      b_back_p,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(config_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

// Tuning screen
static void build_tuning_screen() {
  tuning_scr = lv_obj_create(nullptr);
  style_screen(tuning_scr);
  lv_obj_t *tc = make_content_free(tuning_scr);
  lv_obj_t *tn = make_nav(tuning_scr);
  lv_obj_t *tt = make_title(tc, "Tuning");
  lv_obj_align(tt, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t *raw_card = make_card(tc, 156, 80);
  lv_obj_set_pos(raw_card, 8, 44);
  lbl_ep_up = lv_label_create(raw_card);
  lv_obj_set_style_text_color(lbl_ep_up, lv_color_hex(0xCFCFCF), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_ep_up, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_ep_up, LV_ALIGN_TOP_LEFT, 0, 0);
  lbl_ep_dn = lv_label_create(raw_card);
  lv_obj_set_style_text_color(lbl_ep_dn, lv_color_hex(0xCFCFCF), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_ep_dn, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_ep_dn, LV_ALIGN_TOP_LEFT, 0, 18);
  lbl_travel = lv_label_create(raw_card);
  lv_obj_set_style_text_color(lbl_travel, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_travel, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_travel, LV_ALIGN_TOP_LEFT, 0, 36);

  lv_obj_t *eff_card = make_card(tc, 156, 64);
  lv_obj_set_pos(eff_card, 8, 130);
  lbl_up_eff = lv_label_create(eff_card);
  lv_obj_set_style_text_color(lbl_up_eff, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_up_eff, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_up_eff, LV_ALIGN_TOP_LEFT, 0, 0);
  lbl_dn_eff = lv_label_create(eff_card);
  lv_obj_set_style_text_color(lbl_dn_eff, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_dn_eff, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_dn_eff, LV_ALIGN_TOP_LEFT, 0, 22);

  lv_obj_t *btn_eu = make_btn_multiline(tc, "EP UP", 76, 52, 0x1F6FEB, &lv_font_montserrat_14);
  lv_obj_t *btn_ed = make_btn_multiline(tc, "EP DOWN", 76, 52, 0x1F6FEB, &lv_font_montserrat_14);
  lv_obj_set_pos(btn_eu, 8, 204);
  lv_obj_set_pos(btn_ed, 88, 204);
  lv_obj_t *b_back_t = make_btn(tn, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(b_back_t, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(btn_eu, on_go_ep_up, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btn_ed, on_go_ep_dn, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_back_t,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(config_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

// Endpoint screens - build_endpoint_screen() (defined above) already wires its
// own Back button and its own +/- delta buttons inline; it is a shared,
// parameterized per-screen builder (isUp selects EP UP vs EP DOWN), so it
// needs no further splitting here.
static void build_endpoint_up_screen() {
  ep_up_scr = lv_obj_create(nullptr);
  build_endpoint_screen(ep_up_scr, "EP UP", true);
}

static void build_endpoint_down_screen() {
  ep_dn_scr = lv_obj_create(nullptr);
  build_endpoint_screen(ep_dn_scr, "EP DOWN", false);
}

// WiFi screen
static void build_wifi_screen() {
  wifi_scr = lv_obj_create(nullptr);
  style_screen(wifi_scr);
  lv_obj_t *wc = make_content(wifi_scr);
  // Tighter row spacing than the default 10: the AP-setup view stacks title +
  // 112px QR + key + Skip and needs to fit CONTENT_H (260) without scrolling.
  // The key label is four wrapped lines (SSID / key / blank / setup URL - see
  // ui_update_wifi_label()), so the margin here is thin. Re-check on hardware
  // before adding anything else to this view.
  lv_obj_set_style_pad_row(wc, 6, LV_PART_MAIN);
  lv_obj_t *wn = make_nav(wifi_scr);
  lv_obj_t *wt = make_title(wc, "WiFi");
  lv_obj_align(wt, LV_ALIGN_TOP_MID, 0, 2);

  // AP-setup view: join QR + key + Skip. `wc` is a flex column, so hidden
  // children collapse - only the QR+key+Skip (AP mode) OR the status card +
  // reset button (connected mode) are visible at a time; visibility is set in
  // ui_update_wifi_label(). Created before the card so it flows above it.
  wifi_qr = lv_qrcode_create(wc, 112, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  lv_obj_set_style_border_color(wifi_qr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(wifi_qr, 4, LV_PART_MAIN);  // quiet zone for scanners
  lv_obj_add_flag(wifi_qr, LV_OBJ_FLAG_HIDDEN);
  lbl_wifi_key = lv_label_create(wc);
  lv_obj_set_style_text_color(lbl_wifi_key, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_wifi_key, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_label_set_long_mode(lbl_wifi_key, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_wifi_key, 150);
  lv_obj_set_style_text_align(lbl_wifi_key, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_add_flag(lbl_wifi_key, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *wcard = make_card(wc, 150, 100);
  lbl_wifi_status = lv_label_create(wcard);
  lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_label_set_long_mode(lbl_wifi_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_wifi_status, 130);
  lv_obj_set_style_text_align(lbl_wifi_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(lbl_wifi_status);
  lv_obj_t *b_wifi_reset = make_btn(wc, "Reset WiFi", 140, 44, 0xB42318, &lv_font_montserrat_20);
  btn_wifi_reset = b_wifi_reset;  // for visibility toggling in ui_update_wifi_label
  lv_obj_t *b_back_w = make_btn(wn, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(b_back_w, LV_ALIGN_CENTER, 0, 0);
  btn_wifi_back = b_back_w;  // hidden in AP-setup mode; Skip takes its spot in the nav bar

  // AP-setup only: let a user who doesn't want WiFi leave the auto-shown QR
  // screen and use the press touch-only. Lives in the nav bar, in Back's spot,
  // so the bar is never an empty rectangle (Back and Skip toggle inversely).
  // The device stays in AP mode; WiFi can be configured later via Config -> WiFi.
  btn_wifi_skip = make_btn(wn, "Skip", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(btn_wifi_skip, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(btn_wifi_skip, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(
      btn_wifi_skip,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(main_scr);  // leave setup; device stays in AP mode, configurable later
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_wifi_reset,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        // Live reset, no reboot: reset_task clears the credentials and brings
        // the setup AP up on the running WiFi driver. Runs on its own task, so
        // this LVGL callback returns immediately; the WiFi screen's status
        // updates via ui_update_wifi_label() when the task finishes.
        wifi_mgr::requestResetToSetupAp();
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      b_back_w,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(config_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

// Jam screen
static void build_jam_screen() {
  jam_scr = lv_obj_create(nullptr);
  style_screen(jam_scr);
  lv_obj_t *jc = make_content_free(jam_scr);
  lv_obj_t *jn = make_nav(jam_scr);

  lv_obj_t *jam_title = lv_label_create(jc);
  lv_label_set_text(jam_title, LV_SYMBOL_WARNING " JAM");
  lv_obj_set_style_text_font(jam_title, &lv_font_montserrat_26, LV_PART_MAIN);
  lv_obj_set_style_text_color(jam_title, lv_color_hex(0xFF4444), LV_PART_MAIN);
  lv_obj_align(jam_title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *jam_msg = lv_label_create(jc);
  lv_label_set_text(jam_msg, "Stall detected!\nMotor stopped and\nbacked off safely.");
  lv_obj_set_style_text_font(jam_msg, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(jam_msg, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
  lv_obj_set_style_text_align(jam_msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(jam_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(jam_msg, SCR_W - 20);
  lv_obj_align(jam_msg, LV_ALIGN_TOP_MID, 0, 60);

  jam_status_lbl = lv_label_create(jc);
  lv_label_set_text(jam_status_lbl, "Press to return home");
  lv_obj_set_style_text_font(jam_status_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(jam_status_lbl, lv_color_hex(0xFFD37C), LV_PART_MAIN);
  lv_obj_set_style_text_align(jam_status_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(jam_status_lbl, LV_ALIGN_CENTER, 0, 20);

  btn_jam_home = make_btn(jn, "Return Home", 140, 44, 0x1F6FEB, &lv_font_montserrat_18);
  lv_obj_align(btn_jam_home, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(btn_jam_home, onJamReturnHome, LV_EVENT_CLICKED, nullptr);
}

// Stall sensitivity screen
static void build_stall_screen() {
  stall_scr = lv_obj_create(nullptr);
  style_screen(stall_scr);
  lv_obj_t *ssc = make_content(stall_scr);
  lv_obj_t *ssn = make_nav(stall_scr);

  lv_obj_t *sst = make_title(ssc, "Stall Sens.");
  lv_obj_align(sst, LV_ALIGN_TOP_MID, 0, 2);

  lv_obj_t *sg_card = make_card(ssc, 150, 92);

  lv_obj_t *sg_name = lv_label_create(sg_card);
  lv_label_set_text(sg_name, "SG Trip");
  lv_obj_set_style_text_color(sg_name, lv_color_hex(0xCFCFCF), LV_PART_MAIN);
  lv_obj_set_style_text_font(sg_name, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(sg_name, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_sg_val = lv_label_create(sg_card);
  lv_obj_set_style_text_color(lbl_sg_val, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_sg_val, &lv_font_montserrat_26, LV_PART_MAIN);
  lv_obj_center(lbl_sg_val);

  lv_obj_t *sg_hint = lv_label_create(sg_card);
  lv_label_set_text(sg_hint, "0=off  lower=sensitive");
  lv_obj_set_style_text_color(sg_hint, lv_color_hex(0x888888), LV_PART_MAIN);
  lv_obj_set_style_text_font(sg_hint, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(sg_hint, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *sg_grid = lv_obj_create(ssc);
  lv_obj_set_size(sg_grid, 150, 96);
  lv_obj_set_style_bg_opa(sg_grid, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(sg_grid, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sg_grid, 0, LV_PART_MAIN);
  lv_obj_clear_flag(sg_grid, LV_OBJ_FLAG_SCROLLABLE);

  const int sgbw = 70, sgbh = 44, sggap = 4;
  lv_obj_t *sgM5 = make_btn(sg_grid, "-5", sgbw, sgbh, 0x3A3A3A, &lv_font_montserrat_18);
  lv_obj_t *sgM1 = make_btn(sg_grid, "-1", sgbw, sgbh, 0x3A3A3A, &lv_font_montserrat_18);
  lv_obj_t *sgP1 = make_btn(sg_grid, "+1", sgbw, sgbh, 0x1F6FEB, &lv_font_montserrat_18);
  lv_obj_t *sgP5 = make_btn(sg_grid, "+5", sgbw, sgbh, 0x1F6FEB, &lv_font_montserrat_18);
  lv_obj_set_pos(sgM5, 0, 0);
  lv_obj_set_pos(sgP5, sgbw + sggap, 0);
  lv_obj_set_pos(sgM1, 0, sgbh + sggap);
  lv_obj_set_pos(sgP1, sgbw + sggap, sgbh + sggap);

  auto sg_cb = [](lv_event_t *e) {
    int32_t d = (int32_t)(intptr_t)lv_event_get_user_data(e);
    {
      // Read-modify-write of the active profile's trip - pump_task reads it on
      // every StallGuard sample, so it has to be one guarded transaction.
      motion_state::Guard g;
      int32_t v = (int32_t)RUN_SG_TRIP + d;
      RUN_SG_TRIP = (uint16_t)clampi(v, (int32_t)RUN_SG_TRIP_MIN, (int32_t)RUN_SG_TRIP_MAX);
    }
    ui_update_sg_val();
  };
  lv_obj_add_event_cb(sgM5, sg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-5);
  lv_obj_add_event_cb(sgM1, sg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
  lv_obj_add_event_cb(sgP1, sg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)+1);
  lv_obj_add_event_cb(sgP5, sg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)+5);

  lv_obj_t *b_back_ss = make_btn(ssn, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(b_back_ss, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(
      b_back_ss,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(config_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

// Batch run screen
static void build_batch_screen() {
  batch_scr = lv_obj_create(nullptr);
  style_screen(batch_scr);
  lv_obj_t *brc = make_content(batch_scr);
  lv_obj_set_style_pad_row(brc, 4, LV_PART_MAIN);
  lv_obj_t *brn = make_nav(batch_scr);

  lv_obj_t *brt = make_title(brc, "Batch Run");
  lv_obj_align(brt, LV_ALIGN_TOP_MID, 0, 2);

  lv_obj_t *br_card = make_card(brc, 150, 60);

  lv_obj_t *br_name = lv_label_create(br_card);
  lv_label_set_text(br_name, "Count");
  lv_obj_set_style_text_color(br_name, lv_color_hex(0xCFCFCF), LV_PART_MAIN);
  lv_obj_set_style_text_font(br_name, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(br_name, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_batch_val = lv_label_create(br_card);
  lv_obj_set_style_text_color(lbl_batch_val, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_batch_val, &lv_font_montserrat_26, LV_PART_MAIN);
  lv_obj_align(lbl_batch_val, LV_ALIGN_TOP_RIGHT, 0, 0);

  lv_obj_t *br_hint = lv_label_create(br_card);
  lv_label_set_text(br_hint, "0 = off (unlimited)");
  lv_obj_set_style_text_color(br_hint, lv_color_hex(0x888888), LV_PART_MAIN);
  lv_obj_set_style_text_font(br_hint, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(br_hint, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *br_grid = lv_obj_create(brc);
  lv_obj_set_size(br_grid, 150, 86);
  lv_obj_set_style_bg_opa(br_grid, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(br_grid, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(br_grid, 0, LV_PART_MAIN);
  lv_obj_clear_flag(br_grid, LV_OBJ_FLAG_SCROLLABLE);

  const int bbw = 48, bbh = 40, bgap = 3;
  lv_obj_t *brM100 = make_btn(br_grid, "-100", bbw, bbh, 0x3A3A3A, &lv_font_montserrat_14);
  lv_obj_t *brM10 = make_btn(br_grid, "-10", bbw, bbh, 0x3A3A3A, &lv_font_montserrat_16);
  lv_obj_t *brM1 = make_btn(br_grid, "-1", bbw, bbh, 0x3A3A3A, &lv_font_montserrat_16);
  lv_obj_t *brP1 = make_btn(br_grid, "+1", bbw, bbh, 0x1F6FEB, &lv_font_montserrat_16);
  lv_obj_t *brP10 = make_btn(br_grid, "+10", bbw, bbh, 0x1F6FEB, &lv_font_montserrat_16);
  lv_obj_t *brP100 = make_btn(br_grid, "+100", bbw, bbh, 0x1F6FEB, &lv_font_montserrat_14);
  lv_obj_set_pos(brM100, 0, 0);
  lv_obj_set_pos(brM10, bbw + bgap, 0);
  lv_obj_set_pos(brM1, 2 * (bbw + bgap), 0);
  lv_obj_set_pos(brP1, 0, bbh + bgap);
  lv_obj_set_pos(brP10, bbw + bgap, bbh + bgap);
  lv_obj_set_pos(brP100, 2 * (bbw + bgap), bbh + bgap);

  auto br_cb = [](lv_event_t *e) {
    int32_t d = (int32_t)(intptr_t)lv_event_get_user_data(e);
    {
      motion_state::Guard g;
      g_motion.batchTarget = clampi(g_motion.batchTarget + d, 0, BATCH_TARGET_MAX);
    }
    ui_update_batch_val();
  };
  lv_obj_add_event_cb(brM100, br_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-100);
  lv_obj_add_event_cb(brM10, br_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-10);
  lv_obj_add_event_cb(brM1, br_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
  lv_obj_add_event_cb(brP1, br_cb, LV_EVENT_CLICKED, (void *)(intptr_t)+1);
  lv_obj_add_event_cb(brP10, br_cb, LV_EVENT_CLICKED, (void *)(intptr_t)+10);
  lv_obj_add_event_cb(brP100, br_cb, LV_EVENT_CLICKED, (void *)(intptr_t)+100);

  lv_obj_t *btn_start_batch =
      make_btn(brc, "Start Batch", 140, 38, 0x00FF00, &lv_font_montserrat_18);
  lv_obj_add_event_cb(
      btn_start_batch,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        // Deferred: validity is re-checked in pump_task at execution time.
        motion_cmd::requestBatchStart();
        go(main_scr);
      },
      LV_EVENT_CLICKED, nullptr);

  lv_obj_t *b_back_br = make_btn(brn, "Back", 140, 44, 0x2A2A2A, &lv_font_montserrat_20);
  lv_obj_align(b_back_br, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(
      b_back_br,
      [](lv_event_t *e) {
        LV_UNUSED(e);
        go(main_scr);
      },
      LV_EVENT_CLICKED, nullptr);
}

void buildUI() {
  // Deliberately the infinite wait, unlike every refresh helper above: this
  // constructs all the screens once at boot, so "skip it" would leave the
  // device with no UI at all. It also runs before pump_task is created, so
  // there is no watchdog-subscribed task to starve. See ui_lock().
  lvgl_port_lock(0);

  build_main_screen();
  build_settings_screen();
  build_config_screen();
  build_profile_screen();
  build_tuning_screen();
  build_endpoint_up_screen();
  build_endpoint_down_screen();
  build_wifi_screen();
  build_jam_screen();
  build_stall_screen();
  build_batch_screen();

  lv_timer_create(counter_timer_cb, 100, nullptr);

  ui_update_speed_val();
  ui_update_profile_screen();
  recomputeEffectiveEndpoints();
  ui_update_tuning_numbers();
  ui_update_endpoint_edit_values();
  ui_update_main_warning();
  ui_update_sg_val();
  ui_update_batch_val();
  ui_update_wifi_label();  // sets the AP-setup QR/key vs connected view

  // Actually put a screen on the display. Every screen above is built with
  // lv_obj_create(nullptr), which creates it detached - none of them becomes
  // active on its own, and lv_scr_load() runs only via go(). Until this call
  // the sole thing that loaded a screen at boot was app_main()'s
  // `if (isApMode() && !isConnected()) go(wifi_scr)`, so a device that came up
  // in STA mode showed LVGL's default empty screen: a blank panel, backlit,
  // with a fully working UI behind it that no touch could reach because no
  // screen was active. Reported as "screen is blankish", and it only surfaced
  // once the rig started joining a network instead of sitting on its own AP.
  //
  // main_scr is the right default; app_main() still overrides it with wifi_scr
  // for the AP-setup case, after this returns.
  go(main_scr);

  lvgl_port_unlock();
}
