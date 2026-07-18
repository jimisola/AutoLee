#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display_touch.h"
#include "motion.h"
#include "stepper.h"
#include "wifi_mgr.h"
#include "web_server.h"
#include "globals.h"
#include "config.h" // FW_VERSION single source of truth

static const char *TAG = "autolee";

static inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

// Replaces the Arduino loop(): handleMotion/handleWebCalibration/
// handleWebHome/broadcastState were all pumped from loop() every iteration;
// the DNS captive-portal server and web server both run their own tasks now,
// so this is just motion + web state + the deferred-reboot handling.
static void pump_task(void *) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        handleMotion();
        handleWebCalibration();
        handleWebHome();
        broadcastState();

        if (rebootRequested && (millis() - rebootRequestMs) > 500) {
            if (stepper::isRunning()) stepper::forceStop();
            vTaskDelay(pdMS_TO_TICKS(100));
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

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "AutoLee firmware v%s (ESP-IDF)", FW_VERSION);

    lv_display_t *disp = display_touch_init();

    // motion_init() adds the TMC5160 to the SPI bus display_touch_init()
    // already set up - must run after it.
    motion_init();

    wifi_mgr::start();
    setupWebServer();

    if (disp) {
        // Phase 3 bring-up check: a visible screen confirms panel geometry,
        // color, and rotation are right before the real UI (ui_touch.cpp) is
        // ported in a follow-up. See docs/PLAN.md Phase 3.
        lvgl_port_lock(0);
        lv_obj_t *scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "AutoLee\nESP-IDF bring-up");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_center(label);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "LVGL display+touch bring-up complete");
    } else {
        ESP_LOGE(TAG, "display_touch_init() failed");
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

    xTaskCreate(pump_task, "pump", 8192, nullptr, 5, nullptr);
}
