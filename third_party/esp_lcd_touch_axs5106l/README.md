# esp_lcd_touch_axs5106l (vendored)

The **AXS5106L** capacitive-touch driver for the WaveShare 1.47" ESP32-C6 Touch
LCD. It is **not** available in the Arduino Library Manager, so it is vendored
here and copied into the arduino-cli sketchbook by
`.github/workflows/release.yml`. The sketch uses its `bsp_touch_init()` /
`bsp_touch_read()` / `bsp_touch_get_coordinates()` API
(`src/main.cpp`, `src/ui_touch.cpp`).

## Source

These files are the **Arduino** variant of the driver, taken verbatim from
WaveShare's official demo package for this board:

- Wiki: https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47
- Package: `ESP32-C6-Touch-LCD-1.47-Demo.zip` →
  `Arduino/libraries/esp_lcd_touch_axs5106l/`

Files: `esp_lcd_touch_axs5106l.h`, `esp_lcd_touch_axs5106l.cpp`.

## ⚠️ License

The upstream files ship with **no license header and no LICENSE file** — see
`NOTICE`. WaveShare's demo code is published without an explicit open-source
grant, so redistribution terms are unclear. They are included here only because
the firmware cannot compile without them and they are not distributed via the
Library Manager. If this is a concern, the alternative is to **fetch** the driver
during the CI build instead of committing it (adjust the workflow's
"Install ESP32 core and libraries" step). Do not add a license you don't have
the right to grant.

## Updating

Re-download the demo package and copy the two files from
`Arduino/libraries/esp_lcd_touch_axs5106l/` over the ones here.
