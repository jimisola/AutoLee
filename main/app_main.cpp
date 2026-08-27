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
#include "driver/gpio.h"  // strapping-pin release before esp_restart() - see pump_task

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
    case ESP_RST_USB:
      return "USB peripheral";
    case ESP_RST_JTAG:
      return "JTAG";
    case ESP_RST_EFUSE:
      return "efuse error";
    // The two worth recognising on sight. A power glitch is the supply failing
    // to hold up without dipping far enough to read as a brownout, and a CPU
    // lockup is a double exception - the panic handler itself faulting, which
    // leaves no core dump to decode afterwards. Both present as "the board just
    // stopped", which is exactly the symptom this whole switch exists to name.
    case ESP_RST_PWR_GLITCH:
      return "POWER GLITCH (check supply)";
    case ESP_RST_CPU_LOCKUP:
      return "CPU LOCKUP (double exception)";
    // ESP_RST_UNKNOWN plus anything a future IDF adds. Keeping this a `default`
    // rather than enumerating ESP_RST_UNKNOWN alone is deliberate: a reset
    // reason that falls through here should still print something, not silently
    // fail to compile the day the enum grows again.
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

      // Release the strapping pins before resetting.
      //
      // On ESP32-C6, GPIO8 and GPIO9 are sampled by the ROM at reset to choose
      // the boot mode, and this board wires TMC_CS to GPIO8 (config.h). CS is
      // active-low, so any reset taken while the TMC5160 is selected presents
      // the ROM with a boot-mode combination that is not "SPI boot" - and the
      // chip then does not start at all: no console output, no core dump, the
      // panel lit but dead, and only a power cycle recovers it. Which is
      // precisely the "device never came back after WiFi setup" report, and
      // why it was intermittent: it depends on where in the SPI cycle the
      // reset lands.
      //
      // esp_restart() does not unwind the SPI driver, so nothing else puts
      // this line back high on the way out. Driving it explicitly costs one
      // register write and is correct regardless - a deselected TMC is the
      // right state to reboot in anyway.
      // Only GPIO8 matters here: the display's chip-select (GPIO14) is not a
      // strapping pin, so its state across a reset cannot affect boot mode.
      gpio_set_level((gpio_num_t)TMC_CS, 1);

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
  uint32_t lastUiLogMs = 0;
  // Panel-blackout recovery. Established by camera-on-panel measurement
  // (frame-by-frame white-pixel analysis of the live panel, see the PR):
  //
  //  - The panel intermittently dies into showing black while LVGL is alive,
  //    the right screen is active and populated, and the backlight is on.
  //    The one observed death landed exactly at the setup-AP start (WiFi PA
  //    power transient territory); every earlier report also clustered
  //    around WiFi lifecycle moments.
  //  - Full LVGL repaints DO NOT recover it: a 5s invalidate-everything sweep
  //    ran across a dead panel for 106 measured seconds with no effect. So
  //    the panel is not showing stale/blank DDRAM - it is in a
  //    dead-until-reinit state (SLPIN/DISPOFF/hardware-reset class), where
  //    RAMWR still lands but nothing is displayed.
  //  - Re-running the JD9853 vendor init sequence recovers/prevents it: a 20s
  //    re-init cadence held the panel perfect for 300 measured seconds
  //    through the same AP-mode operation.
  //  - The TMC5160 shared-SPI theory is ruled out for the idle case:
  //    SG_RESULT()/DRV_STATUS() have exactly one caller, in the motion path,
  //    so an idle rig has zero TMC bus traffic while blackouts happened.
  //
  // What is still unproven is the exact kill mechanism (PA supply transient
  // glitching the panel's RST line vs. a corrupted command byte on a 40MHz
  // bus). Until that is found, recovery is the mitigation:
  //  - uiRepaintRequested (set on every WiFi lifecycle transition, the known
  //    trigger window) re-inits the panel within ~50ms of the event;
  //  - a 30s periodic re-init covers anything else. ~130ms each (the vendor
  //    SLPOUT delay), imperceptible at this cadence, and deliberately NOT
  //    suppressed while the motor runs: a UI dead mid-run on a machine that
  //    can crush hands is worse than 130ms of paused SSE.
  uint32_t lastRepaintMs = 0;
  for (;;) {
    otaWatchdogTick();       // release a stuck OTA flag from a vanished-client upload
    webPasswordResetTick();  // apply a touch-requested web-password reset (NVS + s_auth)
    broadcastState();        // internally rate-limited to SSE_INTERVAL_MS

    // UI heartbeat, for the "panel is dark but the firmware is clearly healthy"
    // report. Every explanation checked so far has been ruled out by evidence -
    // no crash, no watchdog, no core dump, backlight commanded on (the
    // gpio_set_level(GFX_BL, 1) at the end of display_touch_init() is reached,
    // proven by the "AXS5106L touch ready" line just before it), and light
    // sleep is off (CONFIG_PM_ENABLE unset), so nothing is isolating the pin.
    //
    // These three numbers split the remaining possibilities:
    //   scr!=0 and == main - a screen really is loaded, so the problem is
    //     downstream: the panel is not being flushed, or it is dark for a
    //     hardware/bus reason (the display shares SPI with the TMC5160).
    //   scr==0 or != main - nothing is active after all, and go(main_scr)
    //     did not take effect the way the code reads.
    //   tick frozen  - the LVGL task itself has stopped running.
    // Logged from sse_task because it is not watchdog-subscribed, so taking
    // the LVGL lock here can never panic the device even if the UI is wedged.
    const uint32_t now = millis();
    if (uiRepaintRequested || now - lastRepaintMs > 30000) {
      uiRepaintRequested = false;
      lastRepaintMs = now;
      display_touch_panel_reinit();
    }
    if (now - lastUiLogMs > 10000) {
      lastUiLogMs = now;
      ui_log_heartbeat();
    }

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
    char detail[176];
    snprintf(detail, sizeof(detail),
             "NVS was unreadable (%s) and the whole partition was erased. Calibration, WiFi "
             "credentials, the setup-AP key and the web password are back to factory.",
             esp_err_to_name(ret));
    g_boot_report.add("nvs-erased", detail);
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
  // load() falls back to compiled-in defaults and forces endpointsCalibrated
  // false on a missing, old or invalid blob. On a device that has booted
  // before, that is a real loss of calibration and not a fresh-device state -
  // but the two are indistinguishable here, so the wording stays honest about
  // which it is.
  if (!g_motion.endpointsCalibrated) {
    g_boot_report.add("not-calibrated",
                      "No usable calibration was restored, so the press is uncalibrated. Normal "
                      "on a new or factory-reset device; otherwise the stored calibration was "
                      "unreadable.");
  }

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
    g_boot_report.add("display-init-failed",
                      "The touch display did not initialise. The press still runs and the web "
                      "UI still works; the on-device screen does not.");
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

  // A reset the bootloader performed because the previous image failed its
  // self-check. The image now running is not the one that was last flashed,
  // which is worth saying out loud rather than leaving someone to wonder why
  // their change is missing.
  if (esp_reset_reason() == ESP_RST_SW && running &&
      esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_VALID) {
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot && running != boot) {
      g_boot_report.add("ota-rolled-back",
                        "The firmware that was flashed did not pass its startup self-check, so "
                        "the previous image was restored. This is not the build you flashed.");
    }
  }

  // Startup is done: everything that can report a problem has run. Nothing may
  // be added after this, which is what lets the UI promise the list is stable
  // until the next restart.
  g_boot_report.freeze();

  xTaskCreate(pump_task, "pump", 8192, nullptr, 5, &g_pump_task_handle);
  // 6144, not 4096: sse_task calls buildStateJSON(), whose state buffer grew to
  // 1024 bytes when the truncation check was added.
  xTaskCreate(sse_task, "sse", 6144, nullptr, 4, nullptr);
}
