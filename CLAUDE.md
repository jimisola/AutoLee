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
[`docs/FLOWS.md`](docs/FLOWS.md) for the sequence diagrams that go with it — how a control
command is gated twice, why abort bypasses the command queue, and how the event stream
distinguishes "quiet" from "dead" (read this before touching the control API),
[`docs/PLAN.md`](docs/PLAN.md) for the phased migration checklist (this is an active,
in-progress port), [`docs/UX.md`](docs/UX.md) for which destructive actions get which
confirmation gate (read this before adding any reset/destructive control to either UI)
and [`docs/adr/0001-build-tooling-and-platform.md`](docs/adr/0001-build-tooling-and-platform.md)
for why ESP-IDF was chosen.

## Build, flash, test

```bash
idf.py set-target esp32c6   # once, per clone
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # adjust the port for your OS
```

- **Board:** ESP32-C6 (WaveShare 1.47" Touch LCD module).
- **Toolchain:** ESP-IDF >= 6.0, enforced by `main/idf_component.yml`'s `idf: ">=6.0"`; CI and
  releases pin **v6.0.2**, the version this port is built and tested against (native RMT+PCNT
  stepper via `main/drivers/stepper.*`, not FastAccelStepper, which was dropped - see ADR 0001).
  5.x no longer builds: 6.0 typed `esp_lcd`'s cs/dc/reset config members as `gpio_num_t`, stopped
  re-exporting FreeRTOS headers transitively, and turned on `-Werror=missing-field-initializers`
  (see the pragma around `DNS_SERVER_CONFIG_SINGLE` in `main/net/wifi_mgr.cpp`).
  Dependencies (LVGL, `esp_lvgl_port`) are pinned in `main/idf_component.yml` and fetched by the
  Component Manager; `dependencies.lock` is committed for reproducibility.
- **Partition scheme:** custom `partitions.csv` — nvs + otadata + dual OTA app slots (~1.9 MB
  each) + a coredump partition. No SPIFFS/LittleFS partition: nothing in the firmware mounts a
  filesystem (the web UI is a compiled-in string), so that space goes to coredump instead.
- **Safety features (`sdkconfig.defaults`):** OTA rollback, task + interrupt watchdog with panic,
  core dump to flash, brownout detection — see `docs/adr/0001-*.md` for what each concretely buys
  over the old Arduino/PlatformIO setup.
- **Editing `sdkconfig.defaults` does nothing on its own.** ESP-IDF seeds `sdkconfig` from it only
  when `sdkconfig` does not exist, and `sdkconfig` is gitignored — so in a clone that has already
  been built, a changed default is silently ignored and the firmware keeps the old value. Delete
  `sdkconfig` (or `idf.py fullclean`) and rebuild, then grep the generated `sdkconfig` to confirm
  the value actually landed. This fails quietly and looks exactly like the change not working.
- **NVS survives a flash, and is format-versioned.** `idf.py flash` leaves the `nvs` partition
  alone, so calibration, WiFi credentials, the setup-AP key, the web password and the
  `hasEverJoined()` latch persist across builds. But `app_main()` erases the *whole* partition on
  `ESP_ERR_NVS_NEW_VERSION_FOUND`, which fires when flash holds a format newer than the running
  image — i.e. moving forward an ESP-IDF major is safe, going *back* wipes everything. That
  includes an automatic OTA rollback into an older image. `idf.py erase-flash` when you want the
  out-of-box state deliberately.
- **Pure logic + tests:** hardware-independent algorithms live in `lib/autolee_logic/` (registered
  as an ESP-IDF component) and are covered by Unity suites in `host_test/`, which also holds the
  CMake + CTest harness that builds them (deliberately *not* named `test_apps/` — that ESP-IDF
  convention means on-target apps you flash to the chip; these are host tests):
  ```bash
  cd host_test && cmake -B build && cmake --build build -j && cd build && ctest --output-on-failure
  ```
  Add `-DAUTOLEE_COVERAGE=ON` to the `cmake -B build` step for gcov/gcovr coverage.
- **Release-config tests:** `tools/tests/` holds a pytest suite covering `cliff.toml` — the bump
  mapping RELEASING.md documents, the `tag_pattern`, and how the release notes render. It runs the
  real `git-cliff` against throwaway git repos, and skips itself if `git-cliff` is not on `PATH`.
  Edit `cliff.toml` and run `pytest tools/tests`.
- **Lint + format:** run before every commit — `.github/workflows/lint.yml` enforces exactly this
  and it is the cheapest CI job to go red on:
  ```bash
  pre-commit run --all-files
  ```
  `.clang-format` covers `main/`, `lib/` and `host_test/`; vendored third-party code (`lib/tmc_api`,
  `lib/psychic_http`, `lib/dns_server`, `main/drivers/stepper_motor_encoder.*`) is excluded in
  `.pre-commit-config.yaml` and must stay byte-identical to upstream — never reformat it. The
  clang-format hook rewrites files in place, so a first run failing and a second passing is the
  normal path, not an error; re-stage what it changed. Two checks in that workflow are *not* in
  `.pre-commit-config.yaml` and need running by hand if you touch what they cover: `yamllint` +
  `actionlint` + `zizmor` over `.github/`, and `spectral` over `api/*.yaml`.
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
  stall FSM, batch, log ring, calibration, state JSON, motor FSM, command gates), header-only, no
  ESP-IDF or Arduino dependency. Shared by the firmware **and** the host tests in `host_test/`, so
  tested code == shipped code. `command_gate.h` is the one to know about when touching the control
  API: it answers *why* a command is refused, and is deliberately evaluated twice — once by the HTTP
  handler to answer the caller, once by `pump_task` when the command is applied. The second is
  authoritative; see [`docs/FLOWS.md`](docs/FLOWS.md).

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
  `version`, and `/api/v1/diagnostics/info`'s `version` all report that same string. Never add a version
  macro/constant to the source; releasing is just `git tag X.Y.Z && git push --tags` plus a GitHub
  Release. Tags are **bare** full three-part semver (`2.0.0`, never `2.0` or `v2.0.0` — semver.org:
  "v1.2.3 is not a semantic version") — the web UI adds a display-only `v` when rendering
  (`main/net/index_html.h`), the tag/reported version itself never has one. Two caveats:
  `PROJECT_VER` is cached at CMake *configure* time, not recomputed per
  build (`idf.py reconfigure` if you need a stale embedded version refreshed); and `git describe`
  picks the nearest tag by commit-graph distance, not the highest semver tag, which only matters
  if history stops being linear — see CONTRIBUTING.md's "Versioning & releases" for both in full.
- Constants are centralized in `main/config.h` — prefer adding a named `constexpr` there over
  hardcoding tuning values in module logic.
- The web `/api/v1/*` endpoints and the SSE `/api/v1/events` stream are the documented external
  contract (`api/openapi.yaml`, `api/asyncapi.yaml`, `api/schemas/state.schema.json`) — keep
  `state_json` (in `lib/autolee_logic/`) and the schema in sync when adding state fields.
