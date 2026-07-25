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
- [x] Install ESP‑IDF **≥ 5.3**, set up `export.sh`. (Version pinned in `main/idf_component.yml`
  matches what this port was built/tested against - not tied to FastAccelStepper, which was
  dropped; see Phase 4 and ADR 0001.)
- [x] `idf.py set-target esp32c6`.
- [x] Confirm the VS Code **ESP‑IDF extension** works for Karl (Linux + Mac) — replaces the PlatformIO one.
- **DoD:** `idf.py --version` + target set; a hello‑world builds.

## Phase 1 — Project scaffold (no hardware)
- [x] Top‑level `CMakeLists.txt`, `main/` component (`CMakeLists.txt`, `app_main.c`), `sdkconfig.defaults`.
- [x] `partitions.csv`: `nvs`, `otadata`, `ota_0`/`ota_1` (~1.9 MB each), `coredump` — dual app
  slots enable OTA + rollback on 4 MB flash. No `storage`/SPIFFS partition: nothing in the
  firmware mounts a filesystem, so that space went to coredump instead (see CLAUDE.md).
- [x] `sdkconfig.defaults`: `esp32c6`, 4 MB / dio, size‑opt (`-Os`), log level, and the safety knobs (Phase 6): OTA rollback, task+interrupt WDT, core dump to flash, brownout.
- [x] Minimal `app_main`: NVS init, serial banner (`FW_VERSION`), boot.
- **DoD:** `idf.py build` clean; produces the app + a merged image (`idf.py merge-bin`). ⚙️ optional boot‑on‑serial check.

## Phase 2 — Pure core + host tests (no hardware)
- [x] Bring `lib/autolee_logic/` in as an ESP‑IDF component (or shared include dir).
- [x] Port the Unity suites (`test/` sources) to ESP‑IDF host testing (**linux target** / CMake+CTest).
- [x] Wire coverage (gcovr) for the host build.
- **DoD:** all host tests green (parity with the 39 today); coverage reported.

## Phase 3 — Display + touch + LVGL  ⚙️
- [x] Touch driver: the registry options (e.g. `mydazy/esp_lcd_touch_axs5106l`) turned out to hard-require LVGL 9's `lv_indev_create()`, incompatible with this project's LVGL 8.4 pin. Ported `third_party/esp_lcd_touch_axs5106l/`'s I2C protocol directly instead, onto the new `i2c_master` + `gpio` ISR APIs (`main/axs5106l_touch.{h,cpp}`), registered as a plain LVGL 8 `lv_indev_drv_t` (`main/display_touch.cpp`).
- [x] `esp_lcd` panel init on the shared SPI bus (`main/display_touch.cpp`). Turned out the panel is a **JD9853** (ST7789-command-compatible but needing its own vendor init, not the plain ST7789), discovered by finding `src/ui_touch.cpp`'s uncalled-out `lcd_reg_init()` / `init_operations` byte stream. Ported that sequence byte-for-byte via `esp_lcd_panel_io_tx_param()` instead of using `esp_lcd_new_panel_st7789()`'s generic default init. Confirmed from that stream: inversion **ON** (`0x21`), RGB order (`MADCTL=0x00`), offset `34,0` (`esp_lcd_panel_set_gap`), `LCD_RGB_DATA_ENDIAN_BIG` (pairs with `lv_conf.h`'s `LV_COLOR_16_SWAP=1`).
- [x] `esp_lvgl_port`: display registered (`lvgl_port_add_disp`), LVGL tick/task running.
- [x] Ported the LVGL UI (`main/ui_touch.{h,cpp}`, all screens: main, settings, config, profile/speed, tuning, endpoint-up/down, wifi, jam, stall sensitivity, batch run) from the original `ui_touch.cpp` onto `esp_lvgl_port`, wired into `app_main.cpp` via `buildUI()`; `motion.cpp`'s UI hooks now call the real implementations instead of logging stubs. Every public entry point wraps in `lvgl_port_lock()/unlock()` (recursive mutex - safe to nest) since callers include both the LVGL task and `pump_task`.
  - Surfaced two LVGL/Kconfig gotchas along the way, both fixed: `CONFIG_LV_CONF_SKIP` defaults to `y` in the managed `lvgl/lvgl` component and is misleadingly named (`y` = "skip `include/lv_conf.h`", not "skip Kconfig") - without `CONFIG_LV_CONF_SKIP=n` in `sdkconfig.defaults`, our font/color settings silently fell back to Kconfig defaults. And once `lv_conf.h` was actually honored, `LV_TICK_CUSTOM=1` hid LVGL's `lv_tick_inc()` declaration that `esp_lvgl_port` calls directly to drive its own tick - fixed by setting it to `0` (esp_lvgl_port owns the tick, not a custom source).
  - Known limitation: `on_calibrate()`'s "Calibrating..." status text won't reach the screen until `calibrateEndpointsSensorless()` returns (still a synchronous/blocking call) - cosmetic only, revisit once hardware allows visual confirmation.
- **DoD:** ✅ **verified on a real display+touch module.** The JD9853 panel renders the UI correctly on-screen - colours, rotation, offset, fonts all confirmed by eye. Touch works with correct coordinates (button navigation lands on the right elements). Two hardware-surfaced bugs fixed in the process:
  - **Touch driver:** the AXS5106L ACKed its address but every register read returned 0/garbage. Root cause: `i2c_master_transmit_receive()` issues an I2C repeated-START, which this chip doesn't support - it needs a STOP between the register-pointer write and the read (the original Arduino driver did `endTransmission()`+`requestFrom()`, i.e. two transactions). Fixed with separate `i2c_master_transmit`/`i2c_master_receive`; the ID register now reads `51 06 01`.
  - **Layout:** the centre-anchored counter overlapped the top-stacked NOT CALIBRATED badge (inherited from upstream, only visible on the real screen). Counter + batch-remaining label nudged down to clear it.
  - **Open, but a Phase 4 item:** pressing **Calibrate** blanks the screen. Not a crash (clean recovery, no core dump). Calibration runs on `pump_task` and drives the TMC5160/motor - **neither is on this display-only board** - so it runs a ~15s fake sensorless search on the SPI bus that the display shares (both on `SPI2_HOST`), hammering it with StallGuard reads while the display tries to flush. **Hypothesis to check first on the bench rig: display flush starved by continuous SG polling over the shared bus during the long calibration move** (possibly aggravated by the per-transfer `spi_device_acquire_bus()` added for review #3). Can't be cleanly diagnosed without the motor+TMC attached.

## Phase 4 — Motion + StallGuard  ⚙️ (safety‑critical — needs review + bench verification)
- [x] Vendored **`TMC-API`** (MIT, `lib/tmc_api/`); `main/tmc5160_hal.cpp` implements the SPI read/write callback on the shared bus (display CS forced high before every transfer, matching the original).
- [x] Ported TMC config (`main/tmc5160_ctrl.cpp`: rms_current, `sgt`, `TCOOLTHRS`, `TPWMTHRS`, microsteps, DIAG1) and the SG2 `DRV_STATUS`/`SG_RESULT` read + median‑of‑5 (reused from `lib/autolee_logic`).
- [x] **FastAccelStepper dropped** — verified (not assumed) it requires `arduino-esp32` running as an ESP‑IDF component even in "ESP‑IDF mode" (its own README). Replaced with a **native RMT + PCNT step generator** (`main/stepper.*`), adapted from Espressif's official `examples/peripherals/rmt/stepper_motor` reference for the pulse encoding, with PCNT hardware step counting for position and a chunked-transmission design for `forceStop()` (see `main/stepper.h`'s hardware-verification note for why, and the bounded latency it gives instead of an unverified hard-abort). A TMC5160-internal-ramp-generator alternative (no STEP/DIR at all) was considered and rejected — needs a hardware pin strap that can't be confirmed remotely. ADR 0001's table corrected accordingly.
- [x] Ported `motion.cpp`'s algorithms (calibration phases, sliding‑counter jam detection, homing/creep, `handleMotion` state machine) verbatim onto the above (`main/motion.cpp`) — only the plumbing changed (TMCStepper→`tmc5160::`, FastAccelStepper→`stepper::`, `millis()`/`delay()`→`esp_timer`/`vTaskDelay`), safety logic unchanged. UI hooks (`showJamScreen()` etc.) are logged stubs pending Phase 3's UI port.
- **DoD:** ⚙️ **not yet met — build/boot-verified only, zero hardware verification.** No motor/TMC5160 rig was connected while this was written (bare ESP32-C6 board only). Before trusting on the real press: calibration finds UP/DOWN stops; a run cycle; **jam detection triggers + safe return‑home**; speed‑profile change; and specifically confirm `forceStop()`'s actual latency (designed bound: one `kCruiseChunkMs` chunk, see `stepper.cpp`) is acceptable. Needs a safety-focused code review before that bench session, not just before shipping.

### Concurrency/motion-safety rework — partially done, bench verification + two items still outstanding (from PR #4 review, findings #2-#8)
The Arduino cooperative `loop()` became several FreeRTOS tasks (`pump_task`, LVGL, HTTP, `sse_task`), so motion state and the shared SPI bus are now touched concurrently with no synchronization. These findings are all facets of that one root cause, touch the same files, and must be fixed together (fixing one in isolation risks half-measures). Deliberately slotted here, not done earlier, because they can only be *meaningfully* verified with the motor rig attached — the same session + safety review this phase already requires. **Re-verified against the repo 2026-07-25** (see `docs/review-2026-07-25.md`): #2 and #4 were previously marked done together with #8, but only the command-routing half of #8 and the counter half of #4 actually shipped — reopened below. Suggested internal order:
- [x] **#2 (command routing half of the foundation):** route ALL motion-affecting commands (run/stop/batch/profile/current — not just calibrate/home) through the deferred request-flag/queue pattern (`main/motion/motion_cmd.*`) so only `pump_task` touches the stepper/TMC. Also fixes the TOCTOU on the calibrate guard.
- [ ] **#8 (globals-snapshot half of the foundation):** collect the ~40 cross-task motion/endpoint/batch globals into one struct owned by `pump_task`, snapshot under a critical section. **Not done** — `main/globals.h:12-33` still exports free, mostly non-`volatile`/non-atomic globals (`currentTarget`, `rawUp/rawDown`, `endpointUp/Down`, `counter`, `runSGHigh/LowCount`, `batchActive/Count/Target`), with zero `portENTER_CRITICAL`/mutex/atomic in `globals.*` or `motion.cpp`. Confirmed cross-task readers: `main/net/web_server.cpp:74-78,209`, `main/ui/ui_touch.cpp:508`, `sse_task` (`main/app_main.cpp:69`) via `buildStateJson`.
- [x] **#6 + #7 (stepper stop/retarget semantics):** manage `s_running` inside `move_task` so `isRunning()` is true for the whole queued move; make `requestGracefulStop()` actually interrupt the in-flight move (`forceStop()` first, or live retargeting) instead of finishing the full stroke; serialize move submission so a second `moveTo()` can't clear a `s_stopRequested` a concurrent `forceStop()` just set.
- [x] **#3 (SPI bus lock):** wrap the TMC CS-toggle + transfer in `spi_device_acquire_bus()`/`release_bus()` so a StallGuard read can't interleave with the LVGL display flush on the shared bus (corrupting the safety-path SG value).
- [x] **#4a (StallCounter/ConfirmCounter):** the firmware routes through the already-host-tested `StallCounter` / `ConfirmCounter` modules (`lib/autolee_logic/`) instead of inline re-implementations.
- [ ] **#4b (motor_fsm not wired):** `motorTransition`/`canStart` (`lib/autolee_logic/motor_fsm.h`) are still never called from firmware — only reference outside the header is `host_test/test_motor_fsm/test_motor_fsm.cpp`. `main/motion/motion.cpp` assigns `runState` directly at lines 134, 157, 262, 274, 279, 295, 327, 497, 519, 534, 554, so the tested "can't Start from Stalled" rule etc. is not actually enforced. Route `handleMotion()` through the tested transition table, or delete `motor_fsm.h` if genuinely superseded.
- [x] **#5 (OTA handle leak):** on every OTA failure path call `esp_ota_abort()` and reset the handle/partition/flag (add a stale-OTA timeout). *(Self-contained + bench-verifiable without the motor — may be pulled forward into an earlier batch.)*
- [ ] **Watchdog config drift:** `config.h`'s `ENABLE_TASK_WDT` / `TASK_WDT_TIMEOUT_MS = 8000` are read by nothing (dead constants); `sdkconfig.defaults` never sets `CONFIG_ESP_TASK_WDT_TIMEOUT_S`, so the real budget is the 5s IDF default, not the documented 8s — and `motion.cpp`'s blocking calibration/homing loops are sized against the comment. Set the real Kconfig value explicitly and delete the dead constants.
- [ ] **`LogRing` thread-safety:** `lib/autolee_logic/log_ring.h` has plain (non-synchronized) `head_`/`serial_`; `webLog()` is called from `pump_task` *and* HTTP handlers, while `sse_task`/`/api/v1/state` read concurrently — exactly the jam/SG telemetry you'd want intact post-incident.

### Security hardening — TODO, needs a scope decision (from PR #4 review, findings #1, #20)
Not a blocker for the port itself, but must be decided before the press is used unattended on a shared network:
- [x] **#1a:** setup AP secured — WPA2 + per-device key + join QR (`b31fe49`), replacing the previous open (`WIFI_AUTH_OPEN`) AP.
- [x] **#1b:** Digest auth (`DIGEST_AUTH`) gates every state-changing route via a server-wide method-based middleware (`main/net/web_server.cpp:271-300`) — confirmed the middleware actually runs before dispatch, not just registration order (`lib/psychic_http/src/PsychicHttpServer.cpp:502-508`'s `runChain()` wraps `_process()`). Reads/SSE are intentionally open; `/save`+`/clear` are exempt only in unconfigured AP mode (WPA2 gates access there instead). Documented in `api/openapi.yaml:173-183`.
- [ ] **#1c (residual of #1b):** factory-default web password `"autolee"` (`main/config.h:166`) persists until manually changed, and OTA accepts any well-formed image with no identity/signature check (secure boot off) — so one leaked/default password is enough to flash arbitrary firmware. Decide (a) force-change-on-first-use + an OTA image identity check (`esp_ota_get_partition_description()` project-name compare, code-level, verifiable on the bench — also tracked in Phase 8), vs. (b) Secure Boot v2 / signed OTA (provisioning-level, involves **irreversible eFuse burns** — do NOT do casually on the dev board).
- [ ] **#20:** WiFi PSK is stored plaintext in NVS with flash encryption off — recoverable from a flash dump. Enable NVS/flash encryption as part of the (b) provisioning bundle above.

Note: PR #4 review finding #29 (squash the CI-debugging churn commits) is intentionally skipped — this branch will be **squash-merged**, which collapses the history anyway.

Deferred low-priority cleanups from the PR #4 review (not blocking):
- [ ] **#17:** split `buildUI()` (~510 lines) into per-screen `build_<name>_screen()` functions. Turned out NOT to be a mechanical extraction — a centralized event-wiring block at the tail references ~16 button handles created across many screen blocks, so a clean split forces either moving each screen's callback wiring inline or promoting those handles to globals (the latter conflicts with #8). Deferred: it's a cosmetic smell, and the result can't be visually verified without a display module attached. Revisit alongside the Phase 3 display bring-up.
- [ ] **#24:** the `feat!` platform migration currently ships under an unchanged `FW_VERSION "1.8"` with no changelog row — decide whether to bump the version / add a history entry (deferred pending that product decision).

## Phase 5 — Web UI + WiFi + OTA
- [x] **WiFi** (`main/wifi_mgr.*`): ported `wifi_ota.cpp`'s flow onto `esp_wifi` + `nvs` (STA with saved creds → AP fallback, network scan, credential save/clear). ArduinoOTA dropped (Arduino-only, contradicts staying off Arduino) - OTA is web-upload only, see below.
- [x] **Captive portal DNS**: vendored ESP-IDF's official `dns_server` component (`lib/dns_server/`, from `examples/protocols/http_server/captive_portal`) verbatim rather than hand-writing DNS parsing. **Found and fixed a real bug during hardware testing**: the vendored code always set the DNS reply's answer-count to the *question* count, even for questions it didn't actually answer (non-A queries like AAAA/HTTPS-SVCB, or names with no matching rule) — producing a malformed response (reproduced as `FORMERR` against a real client). Fixed to only count/advance past answers actually written; added `ESP_LOGI` for every query/answer/skip decision.
- [x] **ESPAsyncWebServer dropped** — verified (by reading its own README *and* its `idf_component.yml`, not the registry listing) that it hard-depends on `espressif/arduino-esp32` even in "ESP-IDF" mode; being on the ESP Component Registry doesn't imply ESP-IDF-native (the registry also hosts Arduino libraries). Same class of mistake as FastAccelStepper - ADR 0001 corrected. **Replaced by [PsychicHttp](https://github.com/hoeken/PsychicHttp)** (`lib/psychic_http/`, vendored, MIT) - a genuinely pure-ESP-IDF async server on top of `esp_http_server`, verified via its own `CMakeLists.txt` (auto-detects Arduino presence, doesn't require it) and confirmed actively maintained + independently adopted before vendoring. All `/api/v1/*` routes + `INDEX_HTML` + SSE (`PsychicEventSource`) ported in `main/web_server.cpp`; `main/index_html.h` is the original HTML/CSS/JS extracted verbatim (framework-agnostic).
- [x] Kept the shared JSON contract unchanged: `state_json.h`'s `buildStateJson()` reused as-is, just fed from ESP-IDF globals instead of Arduino ones.
- [x] **OTA**: web Firmware-page upload → `esp_ota_ops` (`esp_ota_begin/write/end/set_boot_partition`), wired through `PsychicUploadHandler`, replacing Arduino's `Update` class.
- [x] **AP-mode WiFi scan fixed**: `esp_wifi_scan_start()` requires the STA interface to be active; the fallback AP path used pure `WIFI_MODE_AP`, so every scan failed silently (WiFi setup page always showed "Scan failed"). Fixed by using `WIFI_MODE_APSTA` for the fallback AP (STA left unconnected, just enabled so scanning works) - matches what Arduino's `WiFi` library did implicitly.
- [x] **Captive-portal page dangling-pointer bug fixed**: `PsychicResponse::setContent(const char*)` stores the raw pointer rather than copying (`_body = content;`); the WiFi setup page was built via `res->setContent(wifiConfigPage().c_str())`, a temporary `std::string` whose buffer is freed before `res->send()` reads it. Reproduced on hardware as garbled/garbage bytes (stray accented characters, `@` symbols) served instead of the page. Fixed by keeping the string alive in a named local for the request handler's scope.
- [x] **Dashboard `position` field fixed**: `buildStateJSON()` hardcoded `st.position = 0` with a `// filled in below` comment that was never actually followed through - a genuine leftover from the port, not a hardware limitation (`stepper::getCurrentPosition()` reads the PCNT hardware counter and needs no motor attached). Wired up.
- [x] **OTA watchdog crash fixed, then fixed properly** - found via a real failed OTA upload + core dump decode: `pump_task`'s `broadcastState()` sent an SSE update every loop iteration, and that socket `send()` can block for the full default 5s timeout when a client's connection stalls (weak WiFi, a backgrounded browser tab, or - as first reproduced - a big OTA POST also in flight on the same HTTP server, so the uploading client isn't reading the SSE stream and the TCP send buffer backs up). That's long enough to starve the IDLE task and trip the task watchdog - crashing the device (`Panic reason: Task watchdog got triggered ... pump (CPU 0)`, confirmed via `idf.py coredump-info`'s thread backtrace showing `pump` blocked inside `PsychicEventSource::send`). First fix was narrow (skip SSE sends during OTA specifically). Generalized immediately after: `broadcastState()` moved off `pump_task` entirely onto its own `sse_task` (`main/app_main.cpp`), deliberately **not** watchdog-subscribed, since a slow web client blocking there is an expected network condition, not a bug that should panic the whole device. `pump_task` (which drives `handleMotion()`'s jam/homing dispatch) can no longer be affected by SSE client behavior at all. **Real-world evidence the generalization was necessary, not just theoretical**: between the narrow fix and the task-separation fix, the device crashed again via the exact same watchdog trip while sitting idle with no OTA in progress (confirmed via a stray core dump whose SHA matched that intermediate build) - almost certainly a stale/backgrounded browser SSE connection, not upload contention. Both fixes were verified via a full OTA upload cycle succeeding cleanly afterward.
- **DoD:** ✅ **Phase 5 complete - verified end-to-end on real hardware**, from a phone genuinely joining the AP and a laptop on the same home network (not just build/boot). Confirmed live: WiFi setup page loads with a real network scan list; saving credentials joins the home network (`wifiStatus: Connected`, real IP, confirmed via `/api/v1/state` and a live boot log); the DNS captive-portal redirect works against real Android traffic (`generate_204` correctly gets a `302`); the full SPA dashboard (`/`) loads and renders correctly over the network, including live click-through of all four sub-pages (Configuration, Log, Firmware, WiFi); a real profile-switch round-trip via the dashboard; a **full OTA upload cycle** (upload → success → clean reboot from the other OTA partition → WiFi reconnect) verified multiple times; **OTA rollback verified both halves** (see Phase 6); and the `webLog()` gap closed (see below). One item is out of Phase 5's own scope to close: SSE update cadence during an actual motor run needs a real run cycle, so it carries forward to Phase 4's bench session rather than being a Phase 5 blocker.
- **Gap found and fixed:** `main/motion.cpp`'s safety-relevant state machine (jam detection, calibration, homing) never called `webLog()` - only `showJamScreen()` (on-device LVGL) and `ESP_LOGx` (serial), so jam/stall/calibration/homing events never reached the web Log page. Root cause turned out to be a porting mistake, not a missing feature: `main`'s own `AutoLee/motion.h` (the Arduino single-translation-unit source this file was ported from) used `webLog()` **exclusively** for every diagnostic line in this logic - zero raw `Serial.print` calls anywhere in it. `webLog()` itself already calls `ESP_LOGI("weblog", ...)` internally, so one call was always meant to cover both serial and web. The port had silently substituted these with serial-only `ESP_LOGI`/`ESP_LOGW` calls instead. Fixed by converting every one of them back to a plain `webLog()` call (removing `ESP_LOGx`/the now-unused `TAG`/`esp_log.h` entirely from this file), restoring the original one-call-both-destinations design with no duplicated log lines.
- **Bug found + fixed during bring-up:** the pump task's `vTaskDelay(pdMS_TO_TICKS(1))` rounds down to 0 ticks at the default 100 Hz tick rate, starving the IDLE task and tripping the task watchdog (reproduced: boot to reboot in ~6.5s). Fixed by using 10ms in `app_main.cpp`'s pump loop and guaranteeing a minimum 1-tick delay in `motion.cpp`'s `delay()` helper (used by the calibration/homing polling loops - same failure mode, not yet triggered but same root cause).

## Phase 6 — Safety features (the `sdkconfig` payoff)
- [x] **OTA rollback: fully verified end-to-end, both halves.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (Phase 1). Added the missing `esp_ota_mark_app_valid_cancel_rollback()` self-check call in `app_main.cpp`, gated on `esp_ota_get_state_partition()` returning `ESP_OTA_IMG_PENDING_VERIFY`.
  - **Marks-valid half**: after a real OTA upload, the post-boot log shows `OTA: image marked valid (rollback canceled)`, confirming the image really was `PENDING_VERIFY` and the check fired correctly - this line correctly never appeared on any of the many direct-`idf.py flash` boots earlier in this session.
  - **Actual revert half**: tested with a deliberately-broken image - temporarily added an `abort()` right after the startup log line (before the app could ever reach the valid-mark call), built it, OTA-uploaded it via `curl`, and watched the full cycle on serial: the poison image booted from `ota_1`, logged its startup line, then `abort()`'d (full register dump + a fresh core dump saved to flash); on the *next* boot, the bootloader automatically reverted to `ota_0` with no intervention, booting the last confirmed-valid firmware completely cleanly (WiFi, web server, LVGL all up). The temporary `abort()` was reverted immediately afterward (confirmed via `git status` showing a clean tree) and the real firmware was reflashed over USB as a safety net regardless of the OTA result.
- [x] **Watchdog:** task + interrupt WDT with panic (Phase 1); `main/motion.cpp`'s `wdt_feed()` in the calibration/homing loops, `app_main.cpp`'s `pump_task` subscribes + resets every iteration. **Genuinely exercised twice during Phase 5 bring-up**: a real bug (0-tick `vTaskDelay` starving the idle task), and later a real bug where `broadcastState()`'s blocking SSE send starved `pump_task`'s watchdog reset (see Phase 5) - both tripped this exact watchdog and force-rebooted the device, confirming it isn't just configured but actually catches hangs. The second bug is also why `broadcastState()` now runs on its own non-watchdog-subscribed `sse_task`: `pump_task` is watchdog-critical and drives `handleMotion()`'s jam/homing dispatch, so nothing whose latency depends on a network client should ever share that task - a design rule worth keeping in mind for any future work in `app_main.cpp`/`web_server.cpp`.
- [x] **Core dump** to flash - **fully verified end-to-end**: deliberately crashed the device (null-pointer write), confirmed via serial log it panics ("Guru Meditation Error... Store access fault"), saves to the `coredump` partition, and survives reboot; then decoded it with `idf.py coredump-info`, which pinpointed the exact crash line (`app_main.cpp:64`) with a full backtrace and thread dump. Reproduced across 6+ crash/save/reboot cycles.
- [ ] **Brownout** threshold: `CONFIG_ESP_BROWNOUT_DET=y` (Phase 1, default threshold). Tuning it against the actual 24V→5V + stepper draw needs the physical press - not done.
- **DoD:** ✅ **core dump and OTA rollback both fully verified** (see above). Brownout tuning still needs real hardware (the press's power supply under load) - not done.

## Phase 7 — CI, release, docs, v1.9
- [x] **CI** (`.github/workflows/ci.yml`): three jobs, **confirmed green in real GitHub Actions runs on PR #4** (not just locally) - `idf.py build` (via `espressif/install-esp-idf-action`, pinned to ESP-IDF v5.3.2), host tests (`host_test/` ctest + gcovr coverage), and lint (pre-commit's clang-format/ruff/yamllint + Spectral on the API specs + a JSON Schema contract check). Actions pinned to commit SHAs (verified live via `gh api`, not from memory).
  - Getting host-tests green took 4 real iterations against actual CI: `install-esp-idf-action`'s cross-toolchain (Xtensa/RISC-V) leaked into `host_test/`'s native host build's `as`/`ld` resolution. Pinning `CC`/`CXX`, resetting `PATH`, and additionally resetting `COMPILER_PATH` all failed identically (`as: unrecognized option '--64'`), implying a system-level change (e.g. a gcc specs file), not a process-env one. Fixed by not using that action for this job at all - `host_test/` only needs one file from ESP-IDF (`components/unity/unity`, itself a `ThrowTheSwitch/Unity` submodule), fetched directly pinned to the exact commit ESP-IDF v5.3.2 references, verified in a fully clean (`env -i`) local simulation before trusting it in CI.
- [x] **Release** (`.github/workflows/release.yml`): `idf.py build` → `idf.py merge-bin -o ..._merged.bin --fill-flash-size 4MB` (verified by hand: produces exactly a 4 MB raw image, matching the README's "flash at offset 0x0" instructions) + the app-only image as `..._update.bin`, both attached to the GitHub Release. Kept the `FW_VERSION`-must-match-tag guard from the old workflow. **Not yet run** - only exercised on `release: published`, untested until an actual release is cut.
- [x] **Docs:** README/CONTRIBUTING/CLAUDE.md already rewritten for the ESP-IDF-only layout as part of the Phase 0-3 rebase (see the `refactor!` commit); nothing further pending here.
- [ ] **v1.9:** diff Karl's v1.8→v1.9 and fold the deltas into this port - blocked, v1.9 not available yet (per the original decision to build on v1.8 and re-diff later).
- **DoD:** ✅ **CI is green** (verified in real GitHub Actions, not just claimed). Release workflow's build steps verified by hand; the full release-on-publish flow untested (needs an actual release to exercise). v1.9 diff blocked on Karl.

## Phase 8 — Post-migration hardening
Non-bench-blocking follow-ups from the 2026-07-25 codebase review (`docs/review-2026-07-25.md`).
The safety-critical, bench-blocking items from that review are tracked in Phase 4 above, not here.
- [ ] Persist settings/calibration to NVS as a versioned struct (endpoints, per-profile
  `sg_trip`, `RUN_CURRENT_MA`, `SG_WORK_ZONE_STEPS`, active profile, counter) — currently
  RAM-only in `main/globals.cpp`, lost every reboot.
- [ ] Fake the `stepper::`/`tmc5160::` seams so `motion.cpp`'s jam/backoff/homing sequencing
  gets host-test coverage (highest-value test investment; narrow seams already exist).
- [ ] Add `/api/v1/info` diagnostics endpoint (`esp_app_get_description()`, reset reason, heap,
  uptime, running partition, coredump-present flag).
- [ ] OTA image identity check (`esp_ota_get_partition_description()` project-name compare)
  before `set_boot_partition` — one half of `#1c` above (the other half is the default-password fix).
- [ ] SSE diff-and-heartbeat instead of unconditional full-state every 250ms.
- [ ] Fix the stale `.pre-commit-config.yaml` exclude path (`^main/stepper_motor_encoder\.[ch]$`
  — the file now lives at `main/drivers/stepper_motor_encoder.c`).
- [ ] `FW_VERSION` still `"1.8"` for what is a `feat!` platform migration (see `#24` above).

Explicitly deferred (rationale in `docs/review-2026-07-25.md`'s Copilot-comparison section, not
repeated here): splitting `main/` into ESP-IDF `components/` (already native), `esp_event`
pub/sub (would undo the deliberate single-writer motion design), a diagnostics ZIP download (no
filesystem partition by design), a full HAL abstraction (already ~90% done via
`lib/autolee_logic/`), named-string profile enums in the API (breaking change for cosmetic gain).
- **DoD:** items tracked but not blocking; Phase 4's safety items are the actual bench-session
  blockers, not these.

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
ADR 0001 · [`docs/review-2026-07-25.md`](review-2026-07-25.md) (post-migration codebase review,
source of Phase 8 and the Phase 4 corrections above) ·
WaveShare ESP32‑C6‑Touch‑LCD‑1.47 ESP‑IDF demo (display + touch init) ·
[`TMC-API`](https://github.com/analogdevicesinc/TMC-API) · grblHAL Trinamic driver ·
[`esp_lvgl_port`](https://components.espressif.com/components/espressif/esp_lvgl_port) ·
[ESPAsyncWebServer (IDF component)](https://components.espressif.com/components/esp32async/espasyncwebserver) ·
ESP‑IDF `captive_portal` + `http_server` SSE examples.

### Phase 4 — what is now code-complete vs. still bench-gated

**Implemented (build-verified + network-verified, NOT motor-verified):**
- Deferred command layer (`main/motion/motion_cmd.*`): every motion-affecting
  request from the HTTP task and the LVGL task is queued and executed only on
  `pump_task`. Verified over the network that profile/current/batch changes are
  applied by `pump_task`.
- SPI bus lock around every TMC transfer (`spi_device_acquire_bus`).
- Stepper retargeting so a graceful stop interrupts the in-flight move, and the
  stop flag is owned by `move_task` rather than clobbered by `moveTo()`.
- Jam detection + calibration/homing confirmation now run through the
  host-tested `StallCounter` / `ConfirmCounter` (tested == shipped). **Not yet
  true for `motorTransition`/`canStart` (`motor_fsm.h`) — see #4b above.**
- Karl's TMC tuning (TOFF=4, TBL=1, INTPOL) applied.

**Still requires the motor/TMC5160 rig before it can be trusted:**
- [ ] Calibration finds both mechanical stops.
- [ ] A full run cycle between endpoints.
- [ ] **Jam detection actually triggers, backs off, and returns home safely.**
- [ ] `forceStop()` / retarget latency measured (design bound: one
      `kCruiseChunkMs` cruise chunk OR one accel/decel curve, whichever is in
      flight - curves transmit in a single shot and cannot be interrupted
      mid-curve). **Make this the first bench measurement** — the bound is
      currently design-asserted only, and it's the number that determines how
      far the press travels after a jam is confirmed.
- [ ] The TMC tuning values confirmed against the real press (torque/smoothness).
- [ ] A safety-focused review of the whole motion path before the first run.
