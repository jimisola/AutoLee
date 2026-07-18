# AutoLee — ESP‑IDF migration plan (native `idf.py`)

Working checklist for porting AutoLee from Arduino to **native ESP‑IDF**, directly off `main`
(the PlatformIO/pioarduino intermediate step, PR #3, was researched but never merged — see ADR
0001's status note). Derived from [ADR 0001](adr/0001-build-tooling-and-platform.md) — read it
first for the *why* and the per‑subsystem analysis.

- **Base:** current **v1.8** source (branch `feat/esp-idf`, off `main`). Karl's
  **v1.9** isn't available yet → we build on v1.8 and **re‑diff v1.8→v1.9 later** (Phase 7).
  `lib/autolee_logic/`, `test/`, `api/`, and the governance tooling (clang-format, commitlint,
  pre-commit, yamllint) were carried forward from the PR #3 research since they're
  framework-agnostic; everything Arduino/PlatformIO-specific was dropped.
- **Framework:** ESP‑IDF · **Build tool:** native `idf.py` + CMake (no PlatformIO). **Target:** `esp32c6`.
- **Model guidance:** plan + the two hard/safety‑critical bindings (display, StallGuard) and
  final review with **Opus**; the mechanical bulk with **Sonnet**.

**Legend:** `[ ]` todo · `[x]` done · **⚙️ = requires hardware** (bench‑verify with Karl) ·
**DoD** = definition of done. Do phases in order; each phase should build/test green before the next.

---

## Phase 0 — Prerequisites
- [ ] Install ESP‑IDF **≥ 5.3** (needed for FastAccelStepper's I2S‑mux step driver), set up `export.sh`.
- [ ] `idf.py set-target esp32c6`.
- [ ] Confirm the VS Code **ESP‑IDF extension** works for Karl (Linux + Mac) — replaces the PlatformIO one.
- **DoD:** `idf.py --version` + target set; a hello‑world builds.

## Phase 1 — Project scaffold (no hardware)
- [ ] Top‑level `CMakeLists.txt`, `main/` component (`CMakeLists.txt`, `app_main.c`), `sdkconfig.defaults`.
- [ ] `partitions.csv` mirroring Minimal‑SPIFFS: `nvs`, `otadata`, `ota_0`/`ota_1` (~1.9 MB each), `storage` — dual app slots enable OTA + rollback on 4 MB flash.
- [ ] `sdkconfig.defaults`: `esp32c6`, 4 MB / dio, size‑opt (`-Os`), log level, and the safety knobs (Phase 6): OTA rollback, task+interrupt WDT, core dump to flash, brownout.
- [ ] Minimal `app_main`: NVS init, serial banner (`FW_VERSION`), boot.
- **DoD:** `idf.py build` clean; produces the app + a merged image (`idf.py merge-bin`). ⚙️ optional boot‑on‑serial check.

## Phase 2 — Pure core + host tests (no hardware)
- [ ] Bring `lib/autolee_logic/` in as an ESP‑IDF component (or shared include dir).
- [ ] Port the Unity suites (`test/`) to ESP‑IDF host testing (**linux target** / CMake+CTest).
- [ ] Wire coverage (gcovr) for the host build.
- **DoD:** all host tests green (parity with the 39 today); coverage reported.

## Phase 3 — Display + touch + LVGL  ⚙️
- [x] Touch driver: the registry options (e.g. `mydazy/esp_lcd_touch_axs5106l`) turned out to hard-require LVGL 9's `lv_indev_create()`, incompatible with this project's LVGL 8.4 pin. Ported `third_party/esp_lcd_touch_axs5106l/`'s I2C protocol directly instead, onto the new `i2c_master` + `gpio` ISR APIs (`main/axs5106l_touch.{h,cpp}`), registered as a plain LVGL 8 `lv_indev_drv_t` (`main/display_touch.cpp`).
- [x] `esp_lcd` panel init on the shared SPI bus (`main/display_touch.cpp`). Turned out the panel is a **JD9853** (ST7789-command-compatible but needing its own vendor init, not the plain ST7789), discovered by finding `src/ui_touch.cpp`'s uncalled-out `lcd_reg_init()` / `init_operations` byte stream. Ported that sequence byte-for-byte via `esp_lcd_panel_io_tx_param()` instead of using `esp_lcd_new_panel_st7789()`'s generic default init. Confirmed from that stream: inversion **ON** (`0x21`), RGB order (`MADCTL=0x00`), offset `34,0` (`esp_lcd_panel_set_gap`), `LCD_RGB_DATA_ENDIAN_BIG` (pairs with `lv_conf.h`'s `LV_COLOR_16_SWAP=1`).
- [x] `esp_lvgl_port`: display registered (`lvgl_port_add_disp`), LVGL tick/task running.
- [ ] Port the LVGL UI (screens/widgets/events from `ui_touch.cpp`) — not started; current `app_main.cpp` only shows a bring-up test label.
- **DoD:** ⚙️ **blocked on hardware** — the connected ESP32-C6 has no screen/touch module attached yet (arrives separately). Build/boot verified clean (no SPI/I2C/LVGL init errors); UI correctness (color/rotation/offset) and touch are unverified until the display board is attached.

## Phase 4 — Motion + StallGuard  ⚙️ (safety‑critical — Opus)
- [ ] Vendor **`TMC-API`** (MIT); implement the `spi_master` read/write callback on the shared SPI bus (coordinate display CS vs TMC CS; preserve "display CS high before SG read").
- [ ] Port TMC config (rms_current, `sgt`, `TCOOLTHRS`, `TPWMTHRS`, microsteps, DIAG1) and the **SG2 `DRV_STATUS` read** + median‑of‑5. Reference: grblHAL `terjeio/Trinamic-library` / `Grbl_Esp32/TrinamicDriver` (read‑only, `NOASSERTION`‑licensed).
- [ ] **FastAccelStepper** as an ESP‑IDF component; STEP/DIR + accel profiles.
- [ ] Port `motion.cpp` algorithms (calibration phases, sliding‑counter jam detection, homing/creep) on top of `lib/autolee_logic`.
- **DoD:** ⚙️ calibration finds UP/DOWN stops; a run cycle; **jam detection triggers + safe return‑home**; speed‑profile change. Opus‑reviewed on the stop/backoff paths.

## Phase 5 — Web UI + WiFi + OTA
- [ ] **ESPAsyncWebServer** as an ESP‑IDF component (incl. SSE). Serve `INDEX_HTML`; port the `/api/v1/*` routes + the `/api/v1/events` SSE broadcast (`AsyncEventSource` carries over).
- [ ] Keep the shared JSON contract: `state_json` + `api/schemas/*` unchanged; contract test still applies.
- [ ] **WiFi**: port `wifi_ota.cpp`'s flow onto `esp_wifi` + `nvs` + the official ESP‑IDF `captive_portal` DNS example (STA → AP fallback → scan → save → reboot → reset‑wifi).
- [ ] **OTA**: web Firmware‑page upload → `esp_ota` (+ ArduinoOTA replacement if wanted).
- **DoD:** web UI reachable, controls work, SSE live updates ~250 ms, captive‑portal onboarding, OTA update applies. ⚙️ for the full flow.

## Phase 6 — Safety features (the `sdkconfig` payoff)
- [ ] **OTA rollback:** enable bootloader rollback; call `esp_ota_mark_app_valid_cancel_rollback()` after a healthy boot (self‑check).
- [ ] **Watchdog:** task + **interrupt** WDT with panic; feed in the blocking calibration/homing loops (as today's `wdt_feed()`).
- [ ] **Core dump** to flash + document how to pull/decode it.
- [ ] **Brownout** threshold tuned for the 24 V→5 V + stepper setup.
- **DoD:** ⚙️ a deliberately‑bad OTA image rolls back; a forced crash yields a decodable core dump.

## Phase 7 — CI, release, docs, v1.9
- [ ] **CI:** `idf.py build` (Espressif `esp-idf-ci-action` or the IDF Docker image) + host tests + coverage; port the lint/quality gates.
- [ ] **Release:** `idf.py build` → `idf.py merge-bin` → attach `AutoLee_vX.Y_merged.bin` + `_update.bin` to a GitHub Release; keep the `FW_VERSION` tag guard.
- [ ] **Docs:** update README/CONTRIBUTING for the `idf.py` workflow + the ESP‑IDF VS Code extension; update the ADR status (supersede the PlatformIO decision if we commit to ESP‑IDF).
- [ ] **v1.9:** diff Karl's v1.8→v1.9 and fold the deltas into this port.
- **DoD:** green CI; a release builds; docs accurate.

---

## Hardware‑required steps (bench, with Karl)
Phases **3, 4**, and the full‑flow parts of **5–6**. Everything in **0, 1, 2** and most of **5, 7**
is hardware‑independent (compile + host‑test here) and can go first.

## Reuse from the current (Arduino) tree — reference while porting
`lib/autolee_logic/` (carries over as‑is), `src/web_server.cpp` INDEX_HTML (the web UI),
`src/config.h` constants, `src/motion.cpp` / `ui_touch.cpp` / `wifi_ota.cpp` (algorithms &
screens to port), `api/` (contract), and the vendored driver folder (swap the Arduino variant
for the IDF variant).

## Key references
ADR 0001 · WaveShare ESP32‑C6‑Touch‑LCD‑1.47 ESP‑IDF demo (display + touch init) ·
[`TMC-API`](https://github.com/analogdevicesinc/TMC-API) · grblHAL Trinamic driver ·
[`esp_lvgl_port`](https://components.espressif.com/components/espressif/esp_lvgl_port) ·
[ESPAsyncWebServer (IDF component)](https://components.espressif.com/components/esp32async/espasyncwebserver) ·
ESP‑IDF `captive_portal` + `http_server` SSE examples.
