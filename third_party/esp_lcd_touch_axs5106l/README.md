# esp_lcd_touch_axs5106l (vendored)

The **AXS5106L** capacitive-touch driver for the WaveShare 1.47" ESP32-C6 Touch
LCD. It is **not** in the Arduino Library Manager, so it is vendored here and
wired into the build by PlatformIO (`lib_extra_dirs = third_party` in
`platformio.ini`). The firmware uses its `bsp_touch_init()` / `bsp_touch_read()`
/ `bsp_touch_get_coordinates()` API (`src/main.cpp`, `src/ui_touch.cpp`).

Files: `esp_lcd_touch_axs5106l.h`, `esp_lcd_touch_axs5106l.cpp` — the **Arduino**
variant, taken verbatim from WaveShare's official demo package.

**License & attribution:** see [`NOTICE`](NOTICE) (the upstream files carry no
license of their own).

## Updating

Re-download the [demo package](https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47)
and copy the two files from its `Arduino/libraries/esp_lcd_touch_axs5106l/` over
the ones here.
