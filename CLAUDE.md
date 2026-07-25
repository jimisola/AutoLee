# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

AutoLee is **native ESP-IDF** firmware (`idf.py`, no Arduino IDE/PlatformIO) for a **WaveShare
1.47" ESP32-C6** touchscreen module that automates a Lee APP decapping press. It drives a NEMA 23
stepper via a **TMC5160** driver (SPI) using sensorless StallGuard homing and jam detection, and
exposes a full control surface over both an on-device LVGL touch UI and an async web UI.
Hardware-independent logic lives in `lib/autolee_logic/`; the firmware app lives in `main/`.

⚠️ This firmware controls a motorized press that can crush hands. Stall/jam detection guards
against brass jams only — never treat it as a safety system for people. Preserve the safety
semantics of motion code; do not weaken stop/backoff/homing logic.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the task model, the shared-SPI
rules and the motion FSM (read this before touching motion or concurrency),
[`docs/PLAN.md`](docs/PLAN.md) for the phased migration checklist (this is an active,
in-progress port) and [`docs/adr/0001-build-tooling-and-platform.md`](docs/adr/0001-build-tooling-and-platform.md)
for why ESP-IDF was chosen.

## Build, flash, test

```bash
idf.py set-target esp32c6   # once, per clone
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # adjust the port for your OS
```

- **Board:** ESP32-C6 (WaveShare 1.47" Touch LCD module).
- **Toolchain:** ESP-IDF >= 5.3, enforced by `main/idf_component.yml`'s `idf: ">=5.3"` - the
  version this port was built and tested against (native RMT+PCNT stepper via `main/drivers/stepper.*`,
  not FastAccelStepper, which was dropped - see ADR 0001).
  Dependencies (LVGL, `esp_lvgl_port`) are pinned in `main/idf_component.yml` and fetched by the
  Component Manager; `dependencies.lock` is committed for reproducibility.
- **Partition scheme:** custom `partitions.csv` — nvs + otadata + dual OTA app slots (~1.9 MB
  each) + a coredump partition. No SPIFFS/LittleFS partition: nothing in the firmware mounts a
  filesystem (the web UI is a compiled-in string), so that space goes to coredump instead.
- **Safety features (`sdkconfig.defaults`):** OTA rollback, task + interrupt watchdog with panic,
  core dump to flash, brownout detection — see `docs/adr/0001-*.md` for what each concretely buys
  over the old Arduino/PlatformIO setup.
- **Pure logic + tests:** hardware-independent algorithms live in `lib/autolee_logic/` (registered
  as an ESP-IDF component) and are covered by Unity suites in `host_test/`, which also holds the
  CMake + CTest harness that builds them (deliberately *not* named `test_apps/` — that ESP-IDF
  convention means on-target apps you flash to the chip; these are host tests):
  ```bash
  cd host_test && cmake -B build && cmake --build build -j && cd build && ctest --output-on-failure
  ```
  Add `-DAUTOLEE_COVERAGE=ON` to the `cmake -B build` step for gcov/gcovr coverage.
- **LVGL setup:** `include/lv_conf.h` is found via `-D LV_CONF_INCLUDE_SIMPLE` (set project-wide in
  the root `CMakeLists.txt`) — must stay visible to every component that includes `lvgl.h`.

Pure-logic changes can be verified with the `host_test/` host tests; full hardware behavior still
requires flashing to the board.

## Architecture

### `main/` is the firmware app; `lib/autolee_logic/` is the tested core

`main/` is grouped by concern. All groups are on `INCLUDE_DIRS` (see `main/CMakeLists.txt`), so
`#include "motion.h"` works from anywhere — the grouping is for humans navigating the tree, not a
module boundary the compiler enforces:

| Dir | Holds |
|---|---|
| `main/` | `app_main.cpp` (entry), `config.h`, `globals.{h,cpp}` |
| `main/drivers/` | `display_touch`, `axs5106l_touch`, `tmc5160_hal`, `tmc5160_ctrl`, `stepper`, `stepper_motor_encoder` |
| `main/motion/` | `motion.{h,cpp}` — the safety-critical run/jam/calibration/homing state machine |
| `main/net/` | `wifi_mgr`, `web_server`, `index_html.h` |
| `main/ui/` | `ui_touch` — the on-device LVGL UI |

- **`main/config.h`** — pins, `SpeedProfile profiles[3]` (Slow/Normal/Fast, each with its own
  `sg_trip`), and all tuning constants. It holds no version macro — see Conventions below.
- **`main/drivers/display_touch.{h,cpp}`** — SPI bus + `esp_lcd` panel bring-up and I2C touch bus
  bring-up, LVGL display registration via `esp_lvgl_port`. The panel is a **JD9853**
  (ST7789-command-compatible but needs its own vendor init sequence — `jd9853_send_init_sequence()`
  is a byte-for-byte port of the original Arduino driver's `init_operations`; do not replace it
  with `esp_lcd_new_panel_st7789()`'s generic default init).
- **`main/drivers/axs5106l_touch.{h,cpp}`** — native I2C driver for the AXS5106L touch controller
  (registry components require LVGL 9; this project is pinned to LVGL 8.4), registered as an LVGL
  8 `lv_indev_drv_t`.
- **`lib/autolee_logic/`** — pure, hardware-independent modules (endpoint math, SG filter/blanking,
  stall FSM, batch, log ring, calibration, state JSON, motor FSM), header-only, no ESP-IDF or
  Arduino dependency. Shared by the firmware **and** the host tests in `host_test/`, so tested code
  == shipped code.

### Shared SPI bus

The ST7789/JD9853 display and the TMC5160 share the SPI bus (SCK=GPIO1, MOSI=GPIO2, MISO=GPIO3 —
MISO is unused by the display, only needed for TMC5160 StallGuard reads). Chip-select is managed
in software: TMC CS on GPIO 8, display CS on GPIO 14. **The display CS must be forced HIGH before
every StallGuard SPI read** to prevent bus contention — preserve this when touching SPI/SG code
(carried over from the original Arduino firmware; implemented in `main/drivers/tmc5160_hal.cpp`'s
`tmc5160_readWriteSPI()`).

## Conventions

- **Version is derived from git, never hand-maintained.** ESP-IDF populates
  `esp_app_desc_t.version` at build time from `git describe --always --tags --dirty` (no
  `version.txt`, no `VERSION` in `project()`, no `PROJECT_VER`). The firmware reads it via
  `esp_app_get_description()->version` — the boot banner in `app_main.cpp`, `/api/v1/state`'s
  `version`, and `/api/v1/info`'s `version` all report that same string. Never add a version
  macro/constant to the source; releasing is just `git tag vX.Y && git push --tags` plus a GitHub
  Release. Two caveats: `PROJECT_VER` is cached at CMake *configure* time, not recomputed per
  build (`idf.py reconfigure` if you need a stale embedded version refreshed); and `git describe`
  picks the nearest tag by commit-graph distance, not the highest semver tag, which only matters
  if history stops being linear — see CONTRIBUTING.md's "Versioning & releases" for both in full.
- Constants are centralized in `main/config.h` — prefer adding a named `constexpr` there over
  hardcoding tuning values in module logic.
- The web `/api/v1/*` endpoints and the SSE `/api/v1/events` stream are the documented external
  contract (`api/openapi.yaml`, `api/asyncapi.yaml`, `api/schemas/state.schema.json`) — keep
  `state_json` (in `lib/autolee_logic/`) and the schema in sync when adding state fields.
