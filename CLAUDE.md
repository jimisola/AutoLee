# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

AutoLee is Arduino/C++ firmware for a **WaveShare 1.47" ESP32-C6** touchscreen module that automates a Lee APP decapping press. It drives a NEMA 23 stepper via a **TMC5160** driver (SPI) using sensorless StallGuard homing and jam detection, and exposes a full control surface over both an on-device LVGL touch UI and an async web UI. All code lives in `AutoLee/` (the sketch folder). Prebuilt `.bin` releases are **not** committed — they are built by CI (`.github/workflows/release.yml`) and attached to each [GitHub Release](https://github.com/jimisola/AutoLee/releases).

⚠️ This firmware controls a motorized press that can crush hands. Stall/jam detection guards against brass jams only — never treat it as a safety system for people. Preserve the safety semantics of motion code; do not weaken stop/backoff/homing logic.

## Build, flash, test

Build & test with **PlatformIO** (see `platformio.ini` and CONTRIBUTING.md):
`pio run -e esp32-c6` (firmware), `pio test -e native` (host unit tests, no hardware). The Arduino IDE still works (the sketch stays in `AutoLee/`). CI (`.github/workflows/ci.yml`) runs both on every PR.

- **Board:** ESP32-C6. Platform: pioarduino `55.03.39` (arduino-esp32 3.3.x).
- **Partition scheme:** *Minimal SPIFFS (1.9 MB APP with OTA / 128 KB SPIFFS)* — required; firmware overflows the default layout.
- **Libraries (pinned, verified):** LVGL 8.4.0, GFX Library for Arduino 1.6.6, TMCStepper 0.7.3, FastAccelStepper 1.2.7, **ESP32Async** forks of ESP Async WebServer 3.11.2 + Async TCP 3.4.10, plus the vendored **`esp_lcd_touch_axs5106l`** driver in `third_party/` (not in the Library Manager). PlatformIO wires the driver and `lv_conf.h` automatically.
- **LVGL setup (Arduino IDE only):** copy `AutoLee/lv_conf.h` next to (not inside) the `lvgl` library folder. The LVGL `demos` folder is **not** needed.
- **Pure logic + tests:** hardware-independent algorithms live in `lib/autolee_logic/` and are covered by Unity suites in `test/`; the firmware includes the same headers, so tested code == shipped code.
- **Task watchdog:** `ENABLE_TASK_WDT` in `config.h`; blocking calibration/homing loops feed it via `wdt_feed()`.
- **Flashing a prebuilt binary:** download `AutoLee_vX.X_merged.bin` from the [latest GitHub Release](https://github.com/jimisola/AutoLee/releases/latest) and flash at offset `0x0` via the Espressif Web Flasher (Chrome/Edge, dio mode, 4 MB). The `_update.bin` (app-only) is for OTA, not initial USB flash.
- **CI release:** `.github/workflows/release.yml` builds both `.bin`s with `arduino-cli` and attaches them when a Release is published. The release tag must be `vX.Y` and match `FW_VERSION` in `config.h`, or the build fails the version guard. The Waveshare touch driver is vendored under `third_party/esp_lcd_touch_axs5106l/` because it is not in the Library Manager.
- **OTA:** web UI Firmware page (drag-drop app-only `.bin`), or ArduinoOTA (hostname `autolee`, password `autolee`).

Pure-logic changes can be verified with `pio test -e native`; full hardware behavior still requires flashing to the board.

## Architecture

### Single translation unit — include order is load-bearing

`AutoLee.ino` is the *only* compilation unit. It defines every global (the storage for all `extern`s), then `#include`s the module headers **in a fixed order** that resolves circular dependencies:

```
config.h → (globals defined in .ino) → motion.h → ui_touch.h → wifi_ota.h → web_server.h
```

Because everything is one unit, headers are not independently compilable and there are no include guards protecting against reorder. Forward declarations in `AutoLee.ino` (near the includes) let `motion.h` call UI functions defined later in `ui_touch.h`. **If you add a cross-module call, add its forward declaration in `AutoLee.ino` and keep the include order intact.**

`globals.h` is a **reference/documentation file only — it is NOT `#include`d in the build.** It mirrors all shared `extern` declarations and forward declarations for human readers. If you add or rename a shared global or function, update `globals.h` too so it stays an accurate map, but know that changing it alone has no compile effect.

### Module responsibilities

- **`config.h`** — pins, `SpeedProfile profiles[3]` (Slow/Normal/Fast, each with its own `sg_trip`), and all tuning constants. Access the active profile through the macros `ui_speed_hz` and `RUN_SG_TRIP` rather than indexing `profiles[activeProfile]` directly.
- **`motion.h`** — the motion state machine and StallGuard logic. `handleMotion()` is the pump called every `loop()`. Key routines: `startRunBetweenEndpoints`, `requestGracefulStop`, `calibrateEndpointsSensorless`, `move_until_stall`, `return_home_up_safe`, `safeCreepHome`, `read_sg`/`read_sg_raw` (median-of-5 filtered SG2 reads). State is the `RunState` enum (`IDLE/RUNNING/STOPPING/CALIBRATING/STALLED/HOMING`).
- **`ui_touch.h`** — LVGL screen builders (`buildUI`), `make_*` widget helpers, `ui_update_*` refreshers, display flush/touch callbacks (`my_disp_flush`, `touchpad_read_cb`, `lcd_reg_init`).
- **`web_server.h`** — async routes, `buildStateJSON`, SSE broadcast (`broadcastState`), and the entire web UI as a `PROGMEM` string literal (`INDEX_HTML`) with inline CSS/JS. Also OTA upload handling.
- **`wifi_ota.h`** — STA connect with saved creds, AP fallback + captive portal, network scan, credential persistence via `Preferences`, ArduinoOTA.

### Concurrency: async callbacks must not touch motion

Web request handlers run in AsyncTCP context, separate from the main loop. They **must not** perform motion or reboots directly. Instead they set `volatile` flags — `webCalRequested`, `webHomeRequested`, `rebootRequested` — which the main `loop()` services via `handleWebCalibration()`, `handleWebHome()`, and a deferred `ESP.restart()`. Follow this pattern for any new action a web endpoint triggers that affects the motor or reboots.

### Shared SPI bus

The ST7789 display and the TMC5160 share the SPI bus (SCK/MOSI on GPIO 1/2). Chip-select is managed in software: TMC CS on GPIO 8, display CS on GPIO 14. **The display CS is forced HIGH before every StallGuard SPI read** to prevent bus contention — preserve this when touching SPI/SG code.

### Endpoints & calibration

Calibration finds raw UP/DOWN mechanical stops sensorlessly (`rawUp`/`rawDown`); user offsets (`upOffsetSteps`/`downOffsetSteps`) produce the effective `endpointUp`/`endpointDown` via `recomputeEffectiveEndpoints()`. The **work zone** (`SG_WORK_ZONE_STEPS` before DOWN) blanks StallGuard where primer-seating resistance is normal, so jam detection isn't falsely tripped there.

## Conventions

- Version lives in `config.h` as `FW_VERSION` (single source of truth — read by the serial banner and the CI release guard). Keep the README version history consistent when bumping; don't hardcode the version anywhere else.
- Constants are centralized in `config.h` — prefer adding a named `constexpr` there over hardcoding tuning values in module logic.
- The web `/api/*` endpoints (all POST except `GET /api/state`) and the SSE `/events` stream are the documented external contract (see README's API Reference); keep `buildStateJSON` and the JS `upd()` handler in sync when adding state fields.
