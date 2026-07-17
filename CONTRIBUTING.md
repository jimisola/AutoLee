# Contributing to AutoLee

Thanks for helping improve AutoLee. This guide covers how to build the firmware
locally so your changes match what CI produces.

## Repository layout

- `AutoLee/` — the Arduino sketch (1 `.ino` + 7 `.h`). Compiled as a single
  translation unit; include order in `AutoLee.ino` is significant.
- `third_party/esp_lcd_touch_axs5106l/` — vendored WaveShare touch driver (not in
  the Arduino Library Manager). See its README for licensing.
- `.github/workflows/release.yml` — builds the firmware and publishes binaries to
  GitHub Releases.

Firmware binaries are **not** committed — they are built by CI and attached to
each [GitHub Release](https://github.com/jimisola/AutoLee/releases).

## Toolchain

The build targets **ESP32-C6** with the **Minimal SPIFFS** partition scheme. The
exact versions CI uses (and that are known to compile) are:

| Component | Version |
|---|---|
| `esp32` Arduino core | 3.3.10 |
| LVGL | 8.4.0 |
| GFX Library for Arduino | 1.6.6 |
| TMCStepper | 0.7.3 |
| FastAccelStepper | 1.2.7 |
| ESP Async WebServer (ESP32Async) | 3.11.2 |
| Async TCP (ESP32Async) | 3.4.10 |

> ⚠️ Use the **ESP32Async** forks of the async libraries. Other forks with
> similar names will not compile against a recent esp32 core.

## Build with arduino-cli (matches CI)

```bash
# 1. Install arduino-cli (https://arduino.github.io/arduino-cli/latest/installation/)

# 2. Install the ESP32 core
arduino-cli config init --overwrite \
  --additional-urls "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.10

# 3. Install libraries (pinned)
arduino-cli lib install "lvgl@8.4.0"
arduino-cli lib install "GFX Library for Arduino@1.6.6"
arduino-cli lib install "TMCStepper@0.7.3"
arduino-cli lib install "FastAccelStepper@1.2.7"
arduino-cli lib install "ESP Async WebServer@3.11.2"
arduino-cli lib install "Async TCP@3.4.10"

# 4. LVGL config: lv_conf.h must sit NEXT TO the lvgl folder (libraries root),
#    not inside it. (The LVGL 'demos' folder is not required.)
LIB_DIR="$(arduino-cli config get directories.user)/libraries"
cp AutoLee/lv_conf.h "$LIB_DIR/lv_conf.h"

# 5. Vendored touch driver -> libraries dir
cp -r third_party/esp_lcd_touch_axs5106l "$LIB_DIR/esp_lcd_touch_axs5106l"

# 6. Compile (produces the app + merged .bin under ./AutoLee/build/…)
arduino-cli compile \
  --fqbn "esp32:esp32:esp32c6:PartitionScheme=min_spiffs,FlashMode=dio,FlashSize=4M" \
  --export-binaries \
  ./AutoLee
```

Outputs (in `AutoLee/build/esp32.esp32.esp32c6/`):

- `AutoLee.ino.merged.bin` — full-flash image, flashed at offset `0x0` (this is
  the `..._merged.bin` release asset).
- `AutoLee.ino.bin` — app-only image, used for OTA updates (the `..._update.bin`
  release asset).

## Build with the Arduino IDE

Same as above but via the GUI: install the core + libraries at the versions in
the table, place `lv_conf.h` next to the `lvgl` folder, copy
`third_party/esp_lcd_touch_axs5106l/` into your Arduino `libraries` directory,
open `AutoLee/AutoLee.ino`, select board **ESP32-C6**, and set the partition
scheme to **Minimal SPIFFS (1.9 MB APP with OTA / 128 KB SPIFFS)**.

## Versioning & releases

- The firmware version lives in `AutoLee/config.h` as `FW_VERSION`.
- To cut a release: bump `FW_VERSION`, merge it, then publish a GitHub Release
  with a tag of the form `vX.Y` **matching** `FW_VERSION` (e.g. `v1.9` for
  `FW_VERSION "1.9"`). CI validates the match and fails the build otherwise.
- CI then compiles and attaches `AutoLee_vX.Y_merged.bin` and
  `AutoLee_vX.Y_update.bin` to that release.

## Conventions

- Use [Conventional Commits](https://www.conventionalcommits.org/) for commits
  and PR titles.
- Work on a branch and open a PR; don't push directly to `main`.
- `FW_VERSION` in `config.h` is the single source of truth for the version;
  keep the README version history consistent when bumping it, and don't hardcode
  the version anywhere else.
