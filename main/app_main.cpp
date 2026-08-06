#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display_touch.h"
#include "motion.h"
#include "motion_cmd.h"
#include "stepper.h"
#include "wifi_mgr.h"
#include "web_server.h"
#include "ui_touch.h"
#include "globals.h"
#include "settings_store.h"
#include "config.h"

static const char *TAG = "autolee";

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// Why this is worth a line in the boot banner: without it, a boot log gives no
// way to tell a clean esp_restart() (the deferred reboot in pump_task) from a
// brownout, a watchdog panic, or an external reset. Those have completely
// different causes and completely different fixes, and on a board where the
// LCD backlight and the WiFi PA come up together, brownout is a live
// possibility rather than a theoretical one.
static const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external pin";
    case ESP_RST_SW:
      return "software (esp_restart)";
    case ESP_RST_PANIC:
      return "panic / exception";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "other watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep wake";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT (check supply)";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "unknown";
  }
}

// Replaces the Arduino loop(): handleMotion + the deferred web/UI commands
// are pumped from here every iteration; the DNS
// captive-portal server and web server both run their own tasks now, so
// this is just motion + web state + the deferred-reboot handling.
//
// broadcastState() (SSE) deliberately does NOT run here - see sse_task()
// below for why. This task is watchdog-subscribed and must never block on
// anything whose latency depends on a network client.
static void pump_task(void *) {
  esp_task_wdt_add(nullptr);
  for (;;) {
    esp_task_wdt_reset();
    handleMotion();
    motion_cmd::processPendingCommands();

    if (rebootRequested && (millis() - rebootRequestMs) > 500) {
      if (stepper::isRunning()) stepper::forceStop();
      vTaskDelay(pdMS_TO_TICKS(100));
      // Motor first (above), network second, reset last. See
      // wifi_mgr::stopForReboot(): a reset taken with the AP + DNS responder
      // still live can hang the chip hard enough to need a power cycle. That
      // is the leading (not yet confirmed) explanation for a reported "device
      // never came back after WiFi setup" - check the reset reason logged at
      // boot below against a reproduction before treating it as settled.
      wifi_mgr::stopForReboot();
      esp_restart();
    }

    // pdMS_TO_TICKS(1) rounds down to 0 ticks at the default 100 Hz tick
    // rate, which starves the IDLE task of runtime it needs to pet its
    // own watchdog (found by the task WDT actually tripping on this
    // exact bug during bring-up). 10ms is still frequent enough for
    // this pump loop and leaves IDLE real headroom.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// broadcastState()'s SSE send is a plain blocking socket send() that can
// stall for the socket's ~5s send timeout if a client's connection is
// slow/stalled (weak WiFi, backgrounded browser tab, or - as reproduced
// during OTA hardware testing - another big request tying up the same
// HTTP server). It used to run inline in pump_task, sharing that task's
// watchdog subscription with handleMotion()'s jam/homing dispatch: a
// stalled SSE client was enough to starve the watchdog reset and force a
// hard chip reset mid-motion, with none of motion.cpp's controlled-stop
// sequencing. Moved to its own task specifically so a slow web client can
// never affect motion timing or force a reset - and deliberately NOT
// watchdog-subscribed, since blocking here is an expected network
// condition, not a bug that should panic the whole device.
static void sse_task(void *) {
  for (;;) {
    otaWatchdogTick();  // release a stuck OTA flag from a vanished-client upload
    broadcastState();   // internally rate-limited to SSE_INTERVAL_MS
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // This erases the WHOLE nvs partition, not one namespace: calibration, WiFi
    // credentials, the setup-AP key, the web password and the hasEverJoined()
    // latch all go at once. It is the correct recovery (an unreadable NVS
    // leaves no alternative), but it used to be completely silent, which made
    // it indistinguishable from a bug - a lifetime boot counter dropping from
    // 14 to 1 with no explanation anywhere in the log.
    //
    // NEW_VERSION_FOUND in particular is not an exotic failure: it fires when
    // the NVS on flash was written by a build using a NEWER NVS format than the
    // running image understands. Moving forward across an ESP-IDF major is
    // therefore safe; going back is what wipes - which includes the automatic
    // OTA rollback, since CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE can hand
    // control to an older image in the other slot without anyone choosing to.
    // Calibration goes, and the security-relevant hasEverJoined() latch with it.
    ESP_LOGW(TAG,
             "NVS unreadable (%s) - ERASING the whole nvs partition "
             "(calibration, WiFi creds, AP key, web password all reset)",
             esp_err_to_name(ret));
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Version comes from esp_app_desc_t, auto-populated at build time from
  // `git describe --always --tags --dirty` - no source file to bump.
  ESP_LOGI(TAG, "AutoLee firmware %s (ESP-IDF)", esp_app_get_description()->version);
  ESP_LOGI(TAG, "Reset reason: %s", resetReasonName(esp_reset_reason()));

  // Restore the persisted calibration/tuning subset of g_motion BEFORE
  // motion_init(), so the run current it pushes to the TMC5160 is the saved one
  // rather than the compiled-in default. Fails safe (defaults +
  // endpointsCalibrated=false) on a missing/old/invalid blob - see
  // settings_store.h.
  settings_store::load();

  lv_display_t *disp = display_touch_init();

  // motion_init() adds the TMC5160 to the SPI bus display_touch_init()
  // already set up - must run after it.
  motion_init();

  // Build the UI BEFORE the (blocking, up to ~10s) WiFi connect, so the main
  // screen is on the display immediately instead of leaving it blank for the
  // whole connect. The AP-setup screen can only be selected once wifi_mgr::start()
  // has decided AP vs STA, so that go() happens after start() below.
  if (disp) {
    buildUI();
    ESP_LOGI(TAG, "LVGL UI built");
  } else {
    ESP_LOGE(TAG, "display_touch_init() failed");
  }

  wifi_mgr::start();
  setupWebServer();

  // A fresh/unconfigured device boots into the WPA2 setup AP - jump straight to
  // the WiFi screen so its join QR + key are the first thing shown, instead of
  // making the user hunt through Config -> WiFi. (In STA mode we stay on the
  // main screen buildUI() already showed.)
  if (disp) {
    ui_update_wifi_label();  // refresh QR/key/status now that WiFi state is known
    if (wifi_mgr::isApMode() && !wifi_mgr::isConnected()) go(wifi_scr);
  }

  // Reaching this point (display, motion, WiFi, and the web server all
  // initialized without crashing) is the self-check: mark this OTA image
  // valid so the bootloader's rollback won't revert it on the next boot.
  // Must run every boot, not just after an OTA - the currently-running
  // image starts "pending verify" until this is called at least once.
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "OTA: image marked valid (rollback canceled)");
  }

  xTaskCreate(pump_task, "pump", 8192, nullptr, 5, &g_pump_task_handle);
  // 6144, not 4096: sse_task calls buildStateJSON(), whose state buffer grew to
  // 1024 bytes when the truncation check was added.
  xTaskCreate(sse_task, "sse", 6144, nullptr, 4, nullptr);
}
