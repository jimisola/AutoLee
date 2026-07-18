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

### What `sdkconfig` control concretely gives

Mostly hidden/hardcoded in the precompiled Arduino/PlatformIO setup; ESP-IDF exposes them:

- **OTA auto-rollback** — if an over-the-air update fails to boot or fails a post-boot
  self-check, the device reverts to the last known-good firmware automatically, so a bad
  update can't brick the machine or leave it running broken firmware. *(Genuinely unlocked
  by ESP-IDF — needs bootloader/`sdkconfig` options the precompiled Arduino libs lack.)*
- **Core dump** — on a crash, a snapshot (stack/registers/task states) is saved to flash and
  decoded later to see exactly where/why it faulted, instead of a silent reboot. Makes rare
  field failures diagnosable. *(Genuinely unlocked.)*
- **Watchdog (fuller)** — a hung loop/ISR forces a reset rather than leaving a powered stepper
  frozen mid-stroke. *(We already ship a basic task watchdog on Arduino; ESP-IDF adds fuller,
  configurable coverage incl. the interrupt watchdog — more control, not a hard unlock.)*
- **Brownout (finer control)** — a clean reset when the 5 V rail dips (e.g. the stepper draws
  a current spike) rather than the MCU glitching mid-operation, which on a press could mean a
  mis-step or a missed jam detection. *(Available on Arduino too; ESP-IDF tunes the
  threshold/behaviour — control, not unlock.)*
- **Memory/fault checks** — stack-overflow + heap-corruption detection and configurable panic
  behaviour, catching memory bugs early.

Honest calibration: **OTA rollback and core dump** are genuinely *unlocked* by ESP-IDF;
**watchdog and brownout** partly exist on Arduino already — there the win is *fuller /
configurable* control, not "impossible today."

Two steps, taken only when there's a concrete driver (safety features, or wanting off
pioarduino/Arduino):

1. **Arduino‑as‑ESP‑IDF‑component** (smallest): ESP‑IDF build + `sdkconfig` while keeping all
   Arduino libraries/code. Unlocks the safety features with minimal rewrite.
2. **Pure ESP‑IDF**: build with **native `idf.py` + CMake** (drop PlatformIO), host tests on
   the linux target. Per the verified table below, this is **smaller than "rewrite the HAL"
   implies** — LVGL, the **web layer + SSE** (ESPAsyncWebServer runs as an IDF component),
   WiFi/captive portal (component/example), FastAccelStepper, and the whole pure core all
   carry over. The real work is the `esp_lcd` display init, the TMC5160/StallGuard driver
   (adopt the portable `TMC-API` / grblHAL Trinamic‑library + a small `spi_master` SPI shim),
   and the LVGL/wiring glue.

### Library support on ESP‑IDF

Every capability AutoLee needs exists natively on ESP‑IDF, but only some deps are the *same*
library; the rest are swapped for a native equivalent (this is the HAL rewrite):

> The rows below were **verified by websearch** (2026-07), not assumed — several
> "obvious rewrites" turned out to have easier paths.

| Today (Arduino) | On ESP‑IDF? | Path | Effort |
|---|---|---|---|
| LVGL | ✅ same library | `esp_lvgl_port` (LVGL 8 & 9) + LVGL in the ESP Registry; UI code stays, only the display/touch wiring changes | **Low** |
| GFX Library for Arduino (display) | ↔ replace | native `esp_lcd` — ST7789 built in (`esp_lcd_new_panel_st7789()`). Panel params (offset `34`, color inversion, RGB order, rotation/MADCTL, RGB565 byte‑order, vendor gamma/VCOM init) must match this panel and are only confirmable on‑screen. **De‑risker:** Waveshare's own ESP‑IDF demo ships the `esp_lcd` init for *this exact panel* (same package as the touch driver) | **Med** |
| esp_lcd_touch_axs5106l (touch) | ✅ native variant | ESP‑IDF variant ships in the WaveShare demo (via `esp_lcd_touch`) | **Low** |
| FastAccelStepper (motion) | ✅ supports ESP‑IDF | same library (esp‑idf build matrix + CI on master; C6) | **Low** |
| TMCStepper (motion / StallGuard) | ✅ portable lib + small shim | Skip `esp-tmc5160` (source‑checked: lacks `SG_RESULT`/`SGT`/`TCOOLTHRS`, abandoned). **Recommended: official [`TMC-API`](https://github.com/analogdevicesinc/TMC-API) (MIT, CPU‑independent C, full register map, one SPI callback, ADI‑maintained)** + a small `spi_master` callback. It's low‑level, so use grblHAL's [`terjeio/Trinamic-library`](https://github.com/terjeio/Trinamic-library) + [`Grbl_Esp32/TrinamicDriver`](https://github.com/bdring/Grbl_Esp32/blob/main/Grbl_Esp32/src/Motors/TrinamicDriver.cpp) (StallGuard sensorless homing on ESP32 — our exact pattern) as a **read‑only reference** (`NOASSERTION`‑licensed) | **Med** |
| **ESP Async WebServer + Async TCP + SSE** | ✅ **keep — ESP‑IDF component** | The ESP32Async/mathieucarbou fork is on the [ESP Registry](https://components.espressif.com/components/esp32async/espasyncwebserver) and runs pure‑ESP‑IDF (no Arduino), **including SSE / `AsyncEventSource`** → web + SSE **carry over, no rewrite**. (Optional: native `esp_http_server` + the [official SSE example](https://github.com/espressif/esp-idf/blob/master/examples/protocols/http_server/simple/README.md) + a client‑fd registry) | **Low** |
| WiFi + captive portal | ↔ roll‑your‑own | **Recommended: port AutoLee's own (~117‑line) `wifi_ota.cpp` flow onto `esp_wifi` + `nvs` + the official ESP‑IDF `captive_portal` DNS example** — preserves the exact current UX (custom SSID, scan dropdown, reset button), no third‑party dep. *(Rejected on inspection:* [MycilaESPConnect](https://github.com/mathieucarbou/MycilaESPConnect) *is Arduino‑coupled — no ESP‑IDF component, uses the Arduino `begin()/loop()` model — and its captive‑portal UI is **GPLv3**; it fits neither pure ESP‑IDF nor our licensing. Under Arduino‑as‑component, `wifi_ota.cpp` already carries over, so no new lib is needed either.)* | **Low–Med** |
| Preferences / ArduinoOTA / Update / Wire / SPI | ↔ replace | native `nvs_flash` / `esp_ota` / `esp_https_ota` / i2c+spi master (standard IDF) | **Low** |

**Effort summary: only Display and Motion are Med; everything else Low; nothing High.**

**Why `TMC-API`, not grblHAL's library, for the *shipped* code:** MIT vs an
unidentifiable (`NOASSERTION`) license — cleanest for a repo that already has license
ambiguity, and it matters most on the safety‑critical StallGuard path; it's the
**vendor's own** register map (least chance of a wrong SG read); it's actively ADI‑
maintained; and the single SPI callback is the lowest‑coupling shim. Its one downside —
being low‑level — is covered by reading grblHAL's proven ESP32 StallGuard config as a
reference.

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

ESP‑IDF is where AutoLee should **eventually** live. A full port is **more tractable than it
first looked** (the web/SSE/LVGL/WiFi layers carry over — see the verified table), but it's
still real work and, crucially, **entirely hardware‑unvalidated until flashed** — on
safety‑critical firmware that's the binding risk (display params, the shared‑CS StallGuard
read, motion stop paths). So: ship the current PlatformIO + pioarduino setup; take **step 1
(Arduino‑as‑component)** if/when the safety features are wanted; go **pure ESP‑IDF (native
`idf.py`)** to fully shed Arduino/pioarduino — the biggest remaining costs are the display
init and the TMC5160/StallGuard driver (via a portable Trinamic lib + SPI shim), not the web
layer.
