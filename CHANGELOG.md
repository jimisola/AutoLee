# Changelog

All notable changes to AutoLee firmware.

The firmware version is derived from git: ESP-IDF fills `esp_app_desc_t.version`
at build time from `git describe --always --tags --dirty`, surfaced identically by
the boot banner, `/api/v1/state` and `/api/v1/info`. There is no version constant
in the source to bump — a release is just a `vX.Y.Z` tag (always full three-part
semver, e.g. `v2.0.0` not `v2.0`) plus a GitHub Release, and the release workflow
uses the tag only to name its artifacts.

---

## [Unreleased] — native ESP-IDF port

Migration from the Arduino framework to native **ESP-IDF** (`idf.py`, no Arduino
IDE / PlatformIO). In progress — see [docs/PLAN.md](docs/PLAN.md) for the phased
checklist and what is/isn't hardware-verified, and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the resulting design.

> ⚠️ The motion path is written and network-verified but **not yet bench-verified**
> — no motor has turned under it. Do not run the press on this build without the
> Phase 4 bench session and safety review.

### Added
- Native `idf.py` + CMake project; custom `partitions.csv` (dual OTA slots + coredump).
- Safety options on from first boot: OTA rollback, task + interrupt watchdog with
  panic, core dump to flash, brownout detection.
- **HTTP Digest authentication** on every state-changing endpoint and the OTA
  upload; password stored in NVS and changeable from the web UI.
- Deferred motion-command layer (`main/motion/motion_cmd.*`) — only `pump_task`
  touches the stepper, TMC5160 or motion FSM.
- Host test suite (`host_test/`) with 100% line coverage of `lib/autolee_logic/`,
  plus a coverage regression floor in CI.
- CI (build + host tests + lint) and a release workflow with a version guard.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/wiring.md](docs/wiring.md),
  [docs/bill-of-materials.md](docs/bill-of-materials.md), and
  [docs/upstream-v1.10.0-diff.md](docs/upstream-v1.10.0-diff.md).

### Changed
- **FastAccelStepper → native RMT + PCNT** step generator (the library requires
  `arduino-esp32` even in "ESP-IDF mode").
- **ESPAsyncWebServer → PsychicHttp** (vendored, MIT) for the same reason.
- **TMCStepper → TMC-API** (vendored, MIT) for TMC5160 control.
- **Arduino_GFX → `esp_lcd`**; the panel is a **JD9853** needing its own vendor
  init, not the generic ST7789 sequence.
- AXS5106L touch driver written natively against ESP-IDF I2C (registry
  components require LVGL 9; this project is pinned to LVGL 8.4).
- ArduinoOTA dropped — OTA is web-upload only, via `esp_ota_ops`.
- Repo layout: `main/` grouped into `drivers/`, `motion/`, `net/`, `ui/`; host
  tests in `host_test/` (ESP-IDF's convention for host-run tests).
- TMC5160 tuning from upstream v1.10.0: `TOFF` 5→4, `TBL`=1, `INTPOL` enabled.
  **Unverified on hardware** — changes torque and smoothness.

### Fixed

Ported from Karl's upstream v1.10.0 (see
[docs/upstream-v1.10.0-diff.md](docs/upstream-v1.10.0-diff.md)):
- Batch counting was gated by the display counter cap, so batches could never
  complete once the lifetime counter saturated at 9999.
- Decel-blank carry-through discarded jam evidence — the guard condition could
  never be false, so partial jam evidence entering the deceleration window was
  silently thrown away. *Safety-relevant.*
- `&` was not HTML-escaped in the WiFi scan dropdown.

Found during this port's own hardware testing:
- SSE broadcasts ran on the watchdog-subscribed motion task; a stalled web
  client could starve the watchdog and force a **hard reset mid-stroke**,
  bypassing every controlled-stop path. SSE now runs on its own task.
- A graceful stop did not interrupt the in-flight move — the press ran to the
  end of its travel first, and `STOPPING→IDLE` could be reported while still
  moving. `moveTo()` now retargets.
- TMC SPI transfers were not bus-locked and could be interleaved by an LVGL
  display flush, corrupting a StallGuard read.
- OTA leaked its handle on failure, and an aborted upload left OTA state stuck
  until reboot.
- `isConnected()` reported true forever after a WiFi drop, with no reconnect.
- Captive-portal page served freed memory; DNS replies were malformed for
  non-A queries; WiFi scan always failed in AP mode; the dashboard position
  field was hardcoded to 0; motion/safety events never reached the web log.

---

## [1.8]
Firmware split into modular files (`config.h`, `motion.h`, `ui_touch.h`,
`web_server.h`, `wifi_ota.h`) for maintainability — no functional changes from
v1.7.

## [1.7]
WiFi Info moved to Configuration sub-menu; Reset WiFi button on the WiFi info
screen; speed profile buttons resized to fit the display; WiFi info centered in
its card.

## [1.6]
Adjustable motor current (1,000–4,500 mA) via web; multi-page web UI (Main,
Configuration, Log, Firmware, WiFi); touch UI restructured (Settings →
Configuration sub-menu); WiFi page shows SSID + IP; SG text inputs with
auto-submit on blur; profiles retuned (Slow 15 kHz/350, Normal 35 kHz/15, Fast
45 kHz/1); all labels fitted to the 172 px display.

## [1.5]
Speed profiles (Slow/Normal/Fast) replace the speed slider; per-profile
StallGuard thresholds; profile API.

## [1.4]
Captive portal WiFi; work zone SG blanking; `RUN_DECEL` 800k; median-of-5 SPI
filter; sliding counter stall detection; 500-line log; redesigned web UI.

## [1.3]
Batch run; jam screen with return-home; runtime StallGuard monitoring; web log
viewer.

## [1.2]
Web UI with SSE; OTA updates; endpoint tuning; WiFi AP/STA.

## [1.1]
Sensorless calibration; basic touch UI.

## [1.0]
Initial release.

---

## A note on upstream versioning

Karl's Arduino source had a release after 1.8 labelled **"2.0"**. It is a
feature/bugfix drop with no breaking change, so this repo refers to it as
**v1.10.0**; the upstream file itself still reads `2.0`. See
[docs/upstream-v1.10.0-diff.md](docs/upstream-v1.10.0-diff.md). The version this
ESP-IDF port ships under is still to be decided.
