#pragma once

#include "lvgl.h"

// Bring up the shared SPI bus (ST7789), the I2C touch bus (AXS5106L), LVGL,
// and register both the display and the touch input device with LVGL via
// esp_lvgl_port. Panel geometry/offsets mirror the original Arduino
// firmware's `Arduino_ST7789(bus, 22, 0, false, 172, 320, 34, 0, 34, 0)`.
// Returns the LVGL display handle, or nullptr on failure.
lv_disp_t *display_touch_init(void);

// Re-run the JD9853 vendor init sequence (SLPOUT ... DISPON) on the live
// panel, then force LVGL to repaint everything. Recovers the panel from the
// dead-until-reinit states (hardware reset glitch, SLPIN/DISPOFF via a
// corrupted command) that a plain RAMWR repaint cannot - the blank-panel bug
// this project chased with repaints was one of these: camera-on-panel testing
// showed the panel staying black straight through periodic full repaints, and
// only re-init bringing it back. Takes the LVGL port lock, so no flush can
// interleave with the init sequence; costs ~130ms (the vendor SLPOUT delay).
// Returns false if the display was never initialized.
bool display_touch_panel_reinit(void);
