# Contributing to AutoLee

Thanks for helping improve AutoLee. This guide covers the project layout and how
to build, test, and release the firmware.

## Repository layout

| Path | Purpose |
|---|---|
| `AutoLee/` | The Arduino sketch (1 `.ino` + 7 `.h`). Compiled as a single translation unit; include order in `AutoLee.ino` is significant. |
| `lib/autolee_logic/` | Pure, hardware-independent logic (endpoint math, SG filter/blanking, stall FSM, batch, log ring). Shared by the firmware **and** the host tests, so tested code == shipped code. |
| `test/` | Host (native) unit tests — one folder per module, run with `pio test -e native`. |
| `third_party/esp_lcd_touch_axs5106l/` | Vendored WaveShare touch driver (not in the Library Manager). See its README for licensing. |
| `platformio.ini` | Build config: `esp32-c6` firmware env + `native` test env. |
| `.github/workflows/` | CI (build + tests) and the release pipeline. |

Firmware binaries are **not** committed — they are built by CI and attached to
each [GitHub Release](https://github.com/jimisola/AutoLee/releases).

## Toolchain (pinned)

Targets **ESP32-C6**, **Minimal SPIFFS** partition. Versions verified to compile:

| Component | Version |
|---|---|
| pioarduino platform (arduino-esp32 3.3.x) | `55.03.39` |
| LVGL | 8.4.0 |
| GFX Library for Arduino | 1.6.6 |
| TMCStepper | 0.7.3 |
| FastAccelStepper | 1.2.7 |
| ESP Async WebServer (ESP32Async) | 3.11.2 |
| Async TCP (ESP32Async) | 3.4.10 |

> ⚠️ Use the **ESP32Async** forks of the async libraries. Other forks with
> similar names will not compile against a recent esp32 core.

## Build & test with PlatformIO (recommended)

All versions and board settings are pinned in `platformio.ini`; the vendored
driver and `lv_conf.h` are wired in automatically — no manual library copying.

```bash
# Install PlatformIO Core: https://docs.platformio.org/en/latest/core/installation/
pio run  -e esp32-c6      # build the firmware (app + merged .bin)
pio test -e native        # run the host unit tests (no hardware needed)
pio run  -e esp32-c6 -t upload   # flash over USB
pio device monitor -b 115200     # serial monitor
```

Or open the folder in **VS Code** with the *PlatformIO IDE* extension and use the
Build / Upload / Test buttons.

Build outputs are under `.pio/build/esp32-c6/`:

- `firmware.merged.bin` — full-flash image, flashed at offset `0x0`
  (the `..._merged.bin` release asset).
- `firmware.bin` — app-only image, used for OTA updates
  (the `..._update.bin` release asset).

### Writing tests

Pure logic goes in `lib/autolee_logic/` and is covered by a Unity suite in
`test/test_<module>/`. Keep new algorithmic logic in that library (not inline in
the sketch) so it can be tested on the host and reused by the firmware.

## Build with the Arduino IDE (alternative)

The sketch stays a normal Arduino sketch, so the IDE still works:

1. Install the ESP32 core (Boards Manager → "esp32 by Espressif", 3.3.x) and the
   libraries above at the pinned versions.
2. Copy `AutoLee/lv_conf.h` to sit **next to** your `lvgl` library folder (not
   inside it). *(The LVGL `demos` folder is not needed.)*
3. Copy `third_party/esp_lcd_touch_axs5106l/` into your Arduino `libraries` dir.
4. Open `AutoLee/AutoLee.ino`, select board **ESP32-C6**, partition scheme
   **Minimal SPIFFS (1.9 MB APP with OTA / 128 KB SPIFFS)**, and compile.

## Build with arduino-cli (alternative)

Install the `esp32:esp32` core and the libraries above, copy `AutoLee/lv_conf.h`
next to the `lvgl` folder and `third_party/esp_lcd_touch_axs5106l/` into the
libraries dir, then:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32c6:PartitionScheme=min_spiffs,FlashMode=dio,FlashSize=4M" \
  --export-binaries ./AutoLee
```

(CI and the release pipeline both use PlatformIO — `platformio.ini` is the single
source of truth for versions.)

## Safety features

- **Task watchdog** (`ENABLE_TASK_WDT` in `config.h`): resets the board if the
  main loop stalls. The blocking calibration/homing loops feed it via
  `wdt_feed()`. Set `ENABLE_TASK_WDT 0` to disable during bring-up.
- **OTA rollback** (planned): true bootloader-level auto-rollback needs an
  `sdkconfig` option not available with the precompiled Arduino libraries; it is
  a follow-up that would come with an ESP-IDF-component build.

## Versioning & releases

- The firmware version lives in `AutoLee/config.h` as `FW_VERSION` (single source
  of truth; read by the serial banner and the CI release guard).
- To cut a release: bump `FW_VERSION`, merge it, then publish a GitHub Release
  with a tag of the form `vX.Y` **matching** `FW_VERSION`. CI validates the match
  and fails the build otherwise, then attaches `AutoLee_vX.Y_merged.bin` and
  `AutoLee_vX.Y_update.bin`.

## Conventions

- [Conventional Commits](https://www.conventionalcommits.org/) for commits and PR titles.
- Work on a branch and open a PR; don't push directly to `main`.
- Don't hardcode the version anywhere except `FW_VERSION`; keep the README
  version history consistent when bumping.
