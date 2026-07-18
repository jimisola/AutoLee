#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"

#include "display_touch.h"
#include "motion.h"
#include "config.h" // FW_VERSION single source of truth

static const char *TAG = "autolee";

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
}
