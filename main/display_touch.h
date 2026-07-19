#pragma once

#include "lvgl.h"

// Bring up the shared SPI bus (ST7789), the I2C touch bus (AXS5106L), LVGL,
// and register both the display and the touch input device with LVGL via
// esp_lvgl_port. Panel geometry/offsets mirror the original Arduino
// firmware's `Arduino_ST7789(bus, 22, 0, false, 172, 320, 34, 0, 34, 0)`.
// Returns the LVGL display handle, or nullptr on failure.
lv_disp_t *display_touch_init(void);
