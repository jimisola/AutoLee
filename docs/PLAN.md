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

## Phase 4 — Motion + StallGuard  ⚙️ (safety‑critical — needs review + bench verification)
- [x] Vendored **`TMC-API`** (MIT, `lib/tmc_api/`); `main/tmc5160_hal.cpp` implements the SPI read/write callback on the shared bus (display CS forced high before every transfer, matching the original).
- [x] Ported TMC config (`main/tmc5160_ctrl.cpp`: rms_current, `sgt`, `TCOOLTHRS`, `TPWMTHRS`, microsteps, DIAG1) and the SG2 `DRV_STATUS`/`SG_RESULT` read + median‑of‑5 (reused from `lib/autolee_logic`).
- [x] **FastAccelStepper dropped** — verified (not assumed) it requires `arduino-esp32` running as an ESP‑IDF component even in "ESP‑IDF mode" (its own README). Replaced with a **native RMT + PCNT step generator** (`main/stepper.*`), adapted from Espressif's official `examples/peripherals/rmt/stepper_motor` reference for the pulse encoding, with PCNT hardware step counting for position and a chunked-transmission design for `forceStop()` (see `main/stepper.h`'s hardware-verification note for why, and the bounded latency it gives instead of an unverified hard-abort). A TMC5160-internal-ramp-generator alternative (no STEP/DIR at all) was considered and rejected — needs a hardware pin strap that can't be confirmed remotely. ADR 0001's table corrected accordingly.
- [x] Ported `motion.cpp`'s algorithms (calibration phases, sliding‑counter jam detection, homing/creep, `handleMotion` state machine) verbatim onto the above (`main/motion.cpp`) — only the plumbing changed (TMCStepper→`tmc5160::`, FastAccelStepper→`stepper::`, `millis()`/`delay()`→`esp_timer`/`vTaskDelay`), safety logic unchanged. UI hooks (`showJamScreen()` etc.) are logged stubs pending Phase 3's UI port.
- **DoD:** ⚙️ **not yet met — build/boot-verified only, zero hardware verification.** No motor/TMC5160 rig was connected while this was written (bare ESP32-C6 board only). Before trusting on the real press: calibration finds UP/DOWN stops; a run cycle; **jam detection triggers + safe return‑home**; speed‑profile change; and specifically confirm `forceStop()`'s actual latency (designed bound: one `kCruiseChunkMs` chunk, see `stepper.cpp`) is acceptable. Needs a safety-focused code review before that bench session, not just before shipping.

## Phase 5 — Web UI + WiFi + OTA
- [x] **WiFi** (`main/wifi_mgr.*`): ported `wifi_ota.cpp`'s flow onto `esp_wifi` + `nvs` (STA with saved creds → AP fallback, network scan, credential save/clear). ArduinoOTA dropped (Arduino-only, contradicts staying off Arduino) - OTA is web-upload only, see below.
- [x] **Captive portal DNS**: vendored ESP-IDF's official `dns_server` component (`lib/dns_server/`, from `examples/protocols/http_server/captive_portal`) verbatim rather than hand-writing DNS parsing.
- [x] **ESPAsyncWebServer dropped** — verified (by reading its own README *and* its `idf_component.yml`, not the registry listing) that it hard-depends on `espressif/arduino-esp32` even in "ESP-IDF" mode; being on the ESP Component Registry doesn't imply ESP-IDF-native (the registry also hosts Arduino libraries). Same class of mistake as FastAccelStepper - ADR 0001 corrected. **Replaced by [PsychicHttp](https://github.com/hoeken/PsychicHttp)** (`lib/psychic_http/`, vendored, MIT) - a genuinely pure-ESP-IDF async server on top of `esp_http_server`, verified via its own `CMakeLists.txt` (auto-detects Arduino presence, doesn't require it) and confirmed actively maintained + independently adopted before vendoring. All `/api/v1/*` routes + `INDEX_HTML` + SSE (`PsychicEventSource`) ported in `main/web_server.cpp`; `main/index_html.h` is the original HTML/CSS/JS extracted verbatim (framework-agnostic).
- [x] Kept the shared JSON contract unchanged: `state_json.h`'s `buildStateJson()` reused as-is, just fed from ESP-IDF globals instead of Arduino ones.
- [x] **OTA**: web Firmware-page upload → `esp_ota_ops` (`esp_ota_begin/write/end/set_boot_partition`), wired through `PsychicUploadHandler`, replacing Arduino's `Update` class.
- **DoD:** ⚙️ **partially met.** Build/boot-verified: WiFi AP (`AutoLee-Setup`) + captive-portal DNS + PsychicHttp web server all start and run stably (25+ s, no crashes/watchdog trips after fixing a real bug - see below). Confirmed via a laptop WiFi scan that the AP is genuinely broadcasting. **Not yet verified:** actually loading the page / hitting `/api/v1/*` / SSE updates / OTA upload over the network - the one attempt to join the AP from the dev machine also carried its remote-control session and got disconnected, so this needs testing from a device that isn't also the control channel. Controls, SSE cadence, and OTA update-applies are unverified.
- **Bug found + fixed during bring-up:** the pump task's `vTaskDelay(pdMS_TO_TICKS(1))` rounds down to 0 ticks at the default 100 Hz tick rate, starving the IDLE task and tripping the task watchdog (reproduced: boot to reboot in ~6.5s). Fixed by using 10ms in `app_main.cpp`'s pump loop and guaranteeing a minimum 1-tick delay in `motion.cpp`'s `delay()` helper (used by the calibration/homing polling loops - same failure mode, not yet triggered but same root cause).

## Phase 6 — Safety features (the `sdkconfig` payoff)
- [x] **OTA rollback:** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (Phase 1). Added the missing `esp_ota_mark_app_valid_cancel_rollback()` self-check call in `app_main.cpp`, gated on `esp_ota_get_state_partition()` returning `ESP_OTA_IMG_PENDING_VERIFY`. Not yet exercised end-to-end - the log line correctly doesn't fire when flashed directly via `idf.py flash` (only an actual OTA update leaves the image in `PENDING_VERIFY`), so the full "bad OTA image rolls back" path still needs a real OTA cycle to test.
- [x] **Watchdog:** task + interrupt WDT with panic (Phase 1); `main/motion.cpp`'s `wdt_feed()` in the calibration/homing loops, `app_main.cpp`'s `pump_task` subscribes + resets every iteration. **Genuinely exercised during Phase 5 bring-up**: a real bug (0-tick `vTaskDelay` starving the idle task) tripped this exact watchdog and force-rebooted the device - confirming it isn't just configured but actually catches hangs.
- [x] **Core dump** to flash - **fully verified end-to-end**: deliberately crashed the device (null-pointer write), confirmed via serial log it panics ("Guru Meditation Error... Store access fault"), saves to the `coredump` partition, and survives reboot; then decoded it with `idf.py coredump-info`, which pinpointed the exact crash line (`app_main.cpp:64`) with a full backtrace and thread dump. Reproduced across 6+ crash/save/reboot cycles.
- [ ] **Brownout** threshold: `CONFIG_ESP_BROWNOUT_DET=y` (Phase 1, default threshold). Tuning it against the actual 24V→5V + stepper draw needs the physical press - not done.
- **DoD:** ⚙️ **core dump fully verified** (see above). OTA rollback and brownout tuning still need real hardware (an actual OTA cycle; the press's power supply under load) - not done.

## Phase 7 — CI, release, docs, v1.9
- [x] **CI** (`.github/workflows/ci.yml`): three jobs, **confirmed green in real GitHub Actions runs on PR #4** (not just locally) - `idf.py build` (via `espressif/install-esp-idf-action`, pinned to ESP-IDF v5.3.2), host tests (`test_apps/` ctest + gcovr coverage), and lint (pre-commit's clang-format/ruff/yamllint + Spectral on the API specs + a JSON Schema contract check). Actions pinned to commit SHAs (verified live via `gh api`, not from memory).
  - Getting host-tests green took 4 real iterations against actual CI: `install-esp-idf-action`'s cross-toolchain (Xtensa/RISC-V) leaked into `test_apps/`'s native host build's `as`/`ld` resolution. Pinning `CC`/`CXX`, resetting `PATH`, and additionally resetting `COMPILER_PATH` all failed identically (`as: unrecognized option '--64'`), implying a system-level change (e.g. a gcc specs file), not a process-env one. Fixed by not using that action for this job at all - `test_apps/` only needs one file from ESP-IDF (`components/unity/unity`, itself a `ThrowTheSwitch/Unity` submodule), fetched directly pinned to the exact commit ESP-IDF v5.3.2 references, verified in a fully clean (`env -i`) local simulation before trusting it in CI.
- [x] **Release** (`.github/workflows/release.yml`): `idf.py build` → `idf.py merge-bin -o ..._merged.bin --fill-flash-size 4MB` (verified by hand: produces exactly a 4 MB raw image, matching the README's "flash at offset 0x0" instructions) + the app-only image as `..._update.bin`, both attached to the GitHub Release. Kept the `FW_VERSION`-must-match-tag guard from the old workflow. **Not yet run** - only exercised on `release: published`, untested until an actual release is cut.
- [x] **Docs:** README/CONTRIBUTING/CLAUDE.md already rewritten for the ESP-IDF-only layout as part of the Phase 0-3 rebase (see the `refactor!` commit); nothing further pending here.
- [ ] **v1.9:** diff Karl's v1.8→v1.9 and fold the deltas into this port - blocked, v1.9 not available yet (per the original decision to build on v1.8 and re-diff later).
- **DoD:** ✅ **CI is green** (verified in real GitHub Actions, not just claimed). Release workflow's build steps verified by hand; the full release-on-publish flow untested (needs an actual release to exercise). v1.9 diff blocked on Karl.

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
