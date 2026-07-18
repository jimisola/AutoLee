# ADR 0001 — Build tooling & platform (PlatformIO + pioarduino now; ESP‑IDF as the long‑term direction)

- **Status:** Accepted
- **Date:** 2026-07-18
- **Scope:** AutoLee firmware — ESP32‑C6, Arduino framework

## Context

AutoLee is written against the **Arduino framework** (LVGL, GFX Library for Arduino,
TMCStepper, FastAccelStepper, ESP Async WebServer/Async TCP, plus Arduino WiFi/OTA/NVS
wrappers). We needed reproducible builds, CI, host unit tests, and a release pipeline.

Key constraint: **PlatformIO's _official_ Espressif platform doesn't support the Arduino
framework on the ESP32‑C6.** That's a PlatformIO Labs ↔ Espressif **governance standoff**
(Espressif declined to renew its PlatformIO service agreement and treats the Arduino IDE as
the only officially‑supported IDE), **not a technical gap** — see the
[summary in platform-espressif32#1225](https://github.com/platformio/platform-espressif32/issues/1225#issuecomment-4216264938).
The community **pioarduino** fork (maintained primarily by one unaffiliated developer,
@Jason2866) bundles arduino‑esp32 3.x and enables Arduino builds for the C6/C5/H2/P4.

## Decision

Use **PlatformIO with the pioarduino platform** (Arduino framework), everything pinned in
`platformio.ini`.

Rationale: PlatformIO's **native host‑test harness** (`pio test -e native`, Unity) underpins
the `lib/autolee_logic` unit tests + coverage — the core of the project's tested, modular
architecture — plus one‑file dependency pinning and strong DX (build/test/CI/release all
from `platformio.ini`).

## Consequences

- **Positive:** reproducible one‑file config; native test + coverage; `pio check`;
  CI == local == release; auto merged/factory binary; good VS Code DX.
- **Negative:** a dependency on the **pioarduino** community fork (bus‑factor / governance
  risk — currently low). The `src/` translation‑unit layout is a PlatformIO project (not an
  Arduino sketch), which deepens PlatformIO coupling.

## Alternatives considered

- **arduino‑cli** — builds against the **official** Espressif arduino‑esp32 core, so the C6
  works with **no fork**. Not chosen now: no built‑in host‑test runner (we'd hand‑roll
  CMake/CTest for `lib/autolee_logic`), and the `src/` layout isn't an Arduino sketch. Remains
  a viable exit ramp.
- **ESP‑IDF** — see below.

## Future direction — ESP‑IDF

ESP‑IDF is the strongest long‑term foundation: fully Espressif‑maintained, first‑class C6+
support, full `sdkconfig` control (unlocks OTA auto‑rollback, watchdog panic mode, core
dumps), no Arduino/pioarduino in the critical path, and a well‑maintained official VS Code
extension. Because `lib/autolee_logic` is framework‑agnostic, an ESP‑IDF move is a **HAL
rewrite, not from‑scratch**.

Two steps, taken only when there's a concrete driver (safety features, or wanting off
pioarduino/Arduino):

1. **Arduino‑as‑ESP‑IDF‑component** (smallest): ESP‑IDF build + `sdkconfig` while keeping all
   Arduino libraries/code. Unlocks the safety features with minimal rewrite.
2. **Pure ESP‑IDF** (largest): rewrite the HAL and build with **native `idf.py` + CMake**
   (drop PlatformIO — its main value is the Arduino glue), using ESP‑IDF's host/"linux‑target"
   testing in place of `pio test`.

### Library support on ESP‑IDF

Every capability AutoLee needs exists natively on ESP‑IDF, but only some deps are the *same*
library; the rest are swapped for a native equivalent (this is the HAL rewrite):

| Today (Arduino) | On ESP‑IDF? | Native path |
|---|---|---|
| LVGL | ✅ same library | `esp_lvgl_port` (LVGL 8 & 9) + LVGL in the ESP Registry; UI code stays, display/touch wiring changes |
| GFX Library for Arduino (display) | ❌ Arduino‑only | native `esp_lcd` — ST7789 is built in (`esp_lcd_new_panel_st7789()`) |
| esp_lcd_touch_axs5106l (touch) | ✅ native variant | ESP‑IDF variant ships in the WaveShare demo (via `esp_lcd_touch`) |
| FastAccelStepper (motion) | ✅ supports ESP‑IDF | same library (esp‑idf build matrix + CI on master; C6) |
| TMCStepper | ❌ Arduino‑only | native `esp-tmc5160` (or port over SPI) |
| ESP Async WebServer + Async TCP | ❌ Arduino‑only | native `esp_http_server` (+ hand‑rolled SSE) |
| WiFi / Preferences / ArduinoOTA / DNSServer / Update / Wire / SPI | ❌ Arduino wrappers | native `esp_wifi` / `nvs_flash` / `esp_https_ota` / i2c+spi master, etc. |

### What leaving PlatformIO (for native `idf.py`) costs

- **Build‑tool losses (even keeping Arduino):** the one‑file `platformio.ini` DX; frictionless
  `lib_deps` registry fetching; and our `pio test`/coverage/`pio check`/simple‑CI flow — the
  tests aren't lost but are **re‑homed** onto ESP‑IDF's host testing + CMake/CTest.
- **Framework losses (the pure part):** the Arduino API and Arduino library ecosystem (the HAL
  rewrite).
- **Gains / wash:** full `sdkconfig`; better crash diagnostics (`idf.py monitor` backtrace
  decode); Espressif‑official maintenance + a maintained VS Code extension; no fork dependency;
  Espressif‑managed CI.

## Opinion / recommendation

ESP‑IDF is where AutoLee should **eventually** live, but a full port now would be a large
rewrite of working, safety‑critical firmware for benefits mostly reachable via step 1. So:
ship the current PlatformIO + pioarduino setup; take **step 1 (Arduino‑as‑component)** if/when
the safety features are wanted; go **pure ESP‑IDF (native `idf.py`)** only to fully shed
Arduino/pioarduino.
