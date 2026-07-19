#pragma once

#include <cstdint>
#include "driver/i2c_master.h"
#include "driver/gpio.h"

// Native ESP-IDF port of third_party/esp_lcd_touch_axs5106l/ (the vendored
// Arduino driver for the same AXS5106L chip) - same I2C protocol, ported off
// Wire.h/attachInterrupt onto the new i2c_master + gpio ISR APIs. See
// docs/PLAN.md Phase 3.

#define AXS5106L_MAX_TOUCH_POINTS 5

struct axs5106l_coords_t {
  uint16_t x;
  uint16_t y;
};

struct axs5106l_touch_data_t {
  axs5106l_coords_t coords[AXS5106L_MAX_TOUCH_POINTS];
  uint8_t touch_num;
};

// rotation: 0-3, matches Arduino_GFX's setRotation(); width/height are the
// *panel's* (post-rotation) resolution, i.e. config.h's SCR_W / SCR_H.
void axs5106l_touch_init(i2c_master_bus_handle_t i2c_bus, gpio_num_t rst_gpio, gpio_num_t int_gpio,
                         uint16_t rotation, uint16_t width, uint16_t height);

// Poll for a new touch frame (only does I2C work if the INT line fired since
// the last call - call this every LVGL indev read tick).
void axs5106l_touch_read(void);

// Coordinates from the last axs5106l_touch_read(), rotation-adjusted.
// Returns false if there's no active touch.
bool axs5106l_touch_get_coordinates(axs5106l_touch_data_t *out);
