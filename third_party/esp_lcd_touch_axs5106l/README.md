# esp_lcd_touch_axs5106l (vendored)

**⚠️ Placeholder — the actual driver source must be added here before CI can build.**

This directory vendors the **AXS5106L** touch-controller driver for the WaveShare
1.47" ESP32-C6 Touch LCD. It is **not** available in the Arduino Library Manager, so
it is committed into the repo and copied into the arduino-cli sketchbook by
`.github/workflows/release.yml`.

## How to populate this folder

1. Download the [WaveShare ESP32-C6-Touch-LCD-1.47 demo package](https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47).
2. Locate the `esp_lcd_touch_axs5106l` library folder inside the package (the one
   containing `esp_lcd_touch_axs5106l.h` and its source, plus any `library.properties`
   / `LICENSE`).
3. Copy its **contents** into this directory (so that
   `third_party/esp_lcd_touch_axs5106l/esp_lcd_touch_axs5106l.h` exists).
4. **Preserve the upstream `LICENSE` and file headers verbatim** — see below.
5. Replace this README with (or keep it alongside) the upstream one; keep the
   attribution note in `NOTICE`.

The layout must be an Arduino-installable library folder — the workflow copies the
whole folder to `~/Arduino/libraries/esp_lcd_touch_axs5106l` and expects the sketch's
`#include "esp_lcd_touch_axs5106l.h"` to resolve.

## License / attribution

The AutoLee project is licensed CC BY-NC 4.0, but **this vendored driver is
third-party code that retains its own upstream license** (see `NOTICE` and the
driver's own `LICENSE` once added). Do not relicense it or strip its headers.

If the upstream license does **not** permit redistribution, do not commit the source
here — instead fetch it during the CI run and adjust the workflow's
"Install ESP32 core and libraries" step accordingly.
