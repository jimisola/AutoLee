# AutoLee — ESP‑IDF migration plan (native `idf.py`)

Working checklist for porting AutoLee from Arduino to **native ESP‑IDF**, directly off `main`
(the PlatformIO/pioarduino intermediate step, PR #3, was researched but never merged — see ADR
0001's status note). Derived from [ADR 0001](adr/0001-build-tooling-and-platform.md) — read it
first for the *why* and the per‑subsystem analysis.

- **Base:** started from **v1.8** source (branch `feat/esp-idf`, off `main`). Karl's upstream
  release (**v1.10.0**) has since shipped and been diffed in - see Phase 7 - so this port now
  incorporates both v1.8 and the v1.10.0 delta.
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
- [x] Install ESP‑IDF **≥ 6.0**, set up `export.sh`. (Version pinned in `main/idf_component.yml`
  matches what this port was built/tested against - not tied to FastAccelStepper, which was
  dropped; see Phase 4 and ADR 0001.) Originally ≥ 5.3; moved to 6.0.2 after the port, which is
  now the floor - see Phase 9.
- [x] `idf.py set-target esp32c6`.
- [x] Confirm the VS Code **ESP‑IDF extension** works for Karl (Linux + Mac) — replaces the PlatformIO one.
- **DoD:** `idf.py --version` + target set; a hello‑world builds.

## Phase 1 — Project scaffold (no hardware)
- [x] Top‑level `CMakeLists.txt`, `main/` component (`CMakeLists.txt`, `app_main.c`), `sdkconfig.defaults`.
- [x] `partitions.csv`: `nvs`, `otadata`, `ota_0`/`ota_1` (~1.9 MB each), `coredump` — dual app
  slots enable OTA + rollback on 4 MB flash. No `storage`/SPIFFS partition: nothing in the
  firmware mounts a filesystem, so that space went to coredump instead (see CLAUDE.md).
- [x] `sdkconfig.defaults`: `esp32c6`, 4 MB / dio, size‑opt (`-Os`), log level, and the safety knobs (Phase 6): OTA rollback, task+interrupt WDT, core dump to flash, brownout.
- [x] Minimal `app_main`: NVS init, serial banner (git-derived `esp_app_get_description()->version`), boot.
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
  - **Update 2026-08-09** — a bench session on a **bare ESP32-C6 (still no TMC5160, still no
    motor)** verified the parts that do not need the mechanics: the control-route refusal
    contract end to end, the settings-blob v2 → v3 migration carrying counters forward, the SSE
    heartbeat at its designed 8 s cadence, and an operator abort interrupting a live
    `move_until_stall()` search and unwinding to IDLE with the position reference latched stale.
    None of that touches the list above: calibration finding real stops, a run cycle, jam
    detection + return-home and `forceStop()` latency all still require the motor rig, and this
    line stays ⚙️ until they are done.

### Concurrency/motion-safety rework — partially done, bench verification + two items still outstanding (from PR #4 review, findings #2-#8)
The Arduino cooperative `loop()` became several FreeRTOS tasks (`pump_task`, LVGL, HTTP, `sse_task`), so motion state and the shared SPI bus are now touched concurrently with no synchronization. These findings are all facets of that one root cause, touch the same files, and must be fixed together (fixing one in isolation risks half-measures). Deliberately slotted here, not done earlier, because they can only be *meaningfully* verified with the motor rig attached — the same session + safety review this phase already requires. **Re-verified against the repo 2026-07-25** (see `docs/review-2026-07-25.md`): #2 and #4 were previously marked done together with #8, but only the command-routing half of #8 and the counter half of #4 actually shipped — reopened below. Suggested internal order:
- [x] **#2 (command routing half of the foundation):** route ALL motion-affecting commands (run/stop/batch/profile/current — not just calibrate/home) through the deferred request-flag/queue pattern (`main/motion/motion_cmd.*`) so only `pump_task` touches the stepper/TMC. Also fixes the TOCTOU on the calibrate guard.
- [x] **#8 (globals-snapshot half of the foundation):** the cross-task motion/endpoint/batch/profile globals now live in one struct, `g_motion` (`main/motion/motion_state.h`), behind one `portMUX_TYPE` spinlock. `pump_task` owns it (unlocked reads, every write under `motion_state::Guard`, grouped so a partial update is never visible); every other task reads a coherent copy via `motion_state::snapshot()` — `buildStateJSON()` (HTTP + `sse_task`) takes one snapshot per payload, and each `ui_touch` label update takes one before touching LVGL. The handful of writes that still originate on the HTTP/LVGL tasks (endpoint offsets, SG trip, work zone, batch target, counter reset) are read-modify-writes under the same guard. Deliberately left outside the struct, unchanged: `rebootRequested`/`rebootRequestMs` (single `volatile` flag + timestamp, documented in `globals.h`), the LVGL widget pointers (written once at boot) and the log ring (its own item below). Pure synchronization change — no motion behavior, ordering or timing altered; build verified, **still needs the bench session below** like the rest of this batch.
- [x] **#6 + #7 (stepper stop/retarget semantics):** manage `s_running` inside `move_task` so `isRunning()` is true for the whole queued move; make `requestGracefulStop()` actually interrupt the in-flight move (`forceStop()` first, or live retargeting) instead of finishing the full stroke; serialize move submission so a second `moveTo()` can't clear a `s_stopRequested` a concurrent `forceStop()` just set.
- [x] **#3 (SPI bus lock):** wrap the TMC CS-toggle + transfer in `spi_device_acquire_bus()`/`release_bus()` so a StallGuard read can't interleave with the LVGL display flush on the shared bus (corrupting the safety-path SG value).
- [x] **#4a (StallCounter/ConfirmCounter):** the firmware routes through the already-host-tested `StallCounter` / `ConfirmCounter` modules (`lib/autolee_logic/`) instead of inline re-implementations.
- [x] **#4b (motor_fsm now wired):** every `runState` change in `main/motion/motion.cpp` now goes through `autolee::motorTransition()` via the `applyMotorEventLocked()` bridge in `main/motion/motion.h` (same `motion_state::Guard` transaction as before — `motorTransition()` is a pure switch, so it is safe inside the critical section), and `main/motion/motion_cmd.cpp`'s command gates ask the same table (`canStart()` / `motionEventAllowed()`) instead of re-hardcoding `runState == IDLE/RUNNING/STALLED`. The tested "can't Start from Stalled" rule is therefore enforced by the shipped firmware. **No transition-table/behavior mismatch was found**: motion.cpp's real graph is exactly the 9 transitions in `motor_fsm.h` (all four blocking entry points — run/stop/calibrate/return-home — were already gated on the matching state by `motion_cmd`), so nothing had to be added to or relaxed in the table. Two deliberate details: a rejected event now makes the entry point a logged no-op **before** it touches the TMC/stepper (previously it would have proceeded — e.g. a stop request from `STALLED` would have issued a fast `moveTo(endpointUp)` away from a jam), and the jam latch keeps a fail-safe `runState = STALLED` if the table were ever to reject `Jam` from `RUNNING` (STALLED is strictly the most restrictive state, so the fallback can only be more conservative). Behavior in every reachable state is otherwise unchanged. Build + host tests (9/9) verified; **still inside this phase's bench-verification scope** like the rest of the batch.
- [x] **#5 (OTA handle leak):** on every OTA failure path call `esp_ota_abort()` and reset the handle/partition/flag (add a stale-OTA timeout). *(Self-contained + bench-verifiable without the motor — may be pulled forward into an earlier batch.)*
- [x] **Watchdog config drift:** fixed — `CONFIG_ESP_TASK_WDT_TIMEOUT_S=8` set explicitly in `sdkconfig.defaults`, the dead `ENABLE_TASK_WDT`/`TASK_WDT_TIMEOUT_MS` constants deleted from `config.h`.
- [x] **`LogRing` thread-safety:** `lib/autolee_logic/log_ring.h` now guards every public method with a lock (`push`/`clear`/`serial`/`head`/`size`/`at`). On-target (`ESP_PLATFORM`) that's a `portMUX_TYPE` critical section (disables interrupts/preemption for the section); the host build uses a plain `std::atomic_flag` spin, which is only safe there because `host_test/` is single-threaded. A first pass used the `std::atomic_flag` spin unconditionally, including on-target — caught in review: on this single-core part, a busy-wait with no preemption-disable is a real hang, not just a race — if a lower-priority task (HTTP/`sse_task`) is preempted mid-section by `pump_task` calling `webLog()`, `pump_task` spins forever waiting for a task the scheduler can no longer run, tripping the task watchdog mid-motion. Fixed before commit. `motion_cmd.cpp`'s log-clear now calls the new `clear()` instead of racily reassigning the whole ring. Host tests (9/9) and `idf.py build` both verified after the fix.

### Security hardening — TODO, needs a scope decision (from PR #4 review, findings #1, #20)
Not a blocker for the port itself, but must be decided before the press is used unattended on a shared network:
- [x] **#1a:** setup AP secured — WPA2 + per-device key + join QR (`b31fe49`), replacing the previous open (`WIFI_AUTH_OPEN`) AP.
- [x] **#1b:** Digest auth (`DIGEST_AUTH`) gates every state-changing route via a server-wide method-based middleware (`main/net/web_server.cpp:271-300`) — confirmed the middleware actually runs before dispatch, not just registration order (`lib/psychic_http/src/PsychicHttpServer.cpp:502-508`'s `runChain()` wraps `_process()`). Reads/SSE are intentionally open; `/save`+`/clear` are exempt only in unconfigured AP mode (WPA2 gates access there instead). Documented in `api/openapi.yaml:173-183`.
- [x] **#1c (residual of #1b):** factory-default web password `"autolee"` (`main/config.h:166`) persisted until manually changed, and OTA accepted any well-formed image with no identity/signature check (secure boot off) — so one leaked/default password was enough to flash arbitrary firmware. Went with (a) over (b) Secure Boot v2 / signed OTA (provisioning-level, involves **irreversible eFuse burns** — not doing that casually on the dev board): the OTA image identity check (`esp_ota_get_partition_description()` project-name compare) landed first; the force-change-on-first-use half now also landed — while `defaultPassword` is true (`GET /api/v1/state`'s JSON / the SSE state event), `main/net/web_server.cpp`'s server-wide middleware refuses every state-changing route (run, calibrate, OTA upload, etc.) with 403 except `POST /api/v1/system/web_password` itself, and `main/net/index_html.h`'s dashboard shows a persistent "DEFAULT PASSWORD IN USE" banner until it's changed. Documented in `api/openapi.yaml`/`api/schemas/state.schema.json`.
- [ ] **#20:** WiFi PSK is stored plaintext in NVS with flash encryption off — recoverable from a flash dump. Enable NVS/flash encryption as part of the (b) provisioning bundle above.

Note: PR #4 review finding #29 (squash the CI-debugging churn commits) is intentionally skipped — this branch will be **squash-merged**, which collapses the history anyway.

Deferred low-priority cleanups from the PR #4 review (not blocking):
- [x] **#17:** split `buildUI()` (~510 lines) into 11 per-screen `build_<name>_screen()` functions (`main/ui/ui_touch.cpp:656-1259`): `build_main_screen()`, `build_settings_screen()`, `build_config_screen()`, `build_profile_screen()`, `build_tuning_screen()`, `build_endpoint_up_screen()`/`build_endpoint_down_screen()` (thin wrappers around the pre-existing shared `build_endpoint_screen(scr, title, isUp)` helper, which already wired its own widgets), `build_wifi_screen()`, `build_jam_screen()`, `build_stall_screen()`, `build_batch_screen()`. `buildUI()` itself is now ~25 lines: calls each `build_*_screen()` in the original construction order, then the unchanged tail (`lv_timer_create(counter_timer_cb, ...)` + the initial `ui_update_*()` calls). Revisited the original "conflicts with #8" blocker now that #8 (motion_state consolidation) is long done: re-reading the tail's event-wiring block found every one of its ~16 callbacks referenced only navigation targets (screen pointers like `main_scr`/`config_scr`, already `extern` in `globals.h` since app_main/motion.cpp/web_server.cpp also touch them) or widgets *already* promoted to file-scope statics for other reasons before this task started (`btn_calibrate`, `btn_reset_cal`, `profile_btns[]`, `btn_wifi_reset`/`skip`/`back`, `btn_run_global`) — no callback reached into a button handle local to a *different* screen's block. So the split was purely mechanical: each screen's wiring moved inline right after that screen's widget creation, with **zero new file-scope statics** needed. Verified with `idf.py build` (clean) plus a full manual diff review confirming every moved callback body is byte-identical to its original, just relocated — no widget, style, callback logic, or screen-navigation edge changed.
- [x] **#24:** decided — next release is **2.0.0** (bare tags, always full three-part semver:
  never `2.0`, and never a `v` prefix on the tag itself — semver.org's FAQ: "v1.2.3 is not a
  semantic version". ESP-IDF's own releases use a `v` prefix but are inconsistent on digit count
  (`v5.5`, then `v5.5.1`...`v5.5.5`); this project already rejects that precedent on digit count,
  so it isn't one to defer to on the prefix either — bare tags also match this project's other
  repos, `git-dyn-semver-gradle-plugin`/`reqstool-*`). `CHANGELOG.md`'s `[Unreleased]` section
  already covers this migration in full; it gets renamed to `## [2.0.0] - <date>` at actual
  release time per Keep a Changelog convention, once the release tag is cut. **Superseded
  resolution:** this was first done by bumping a hand-maintained `#define FW_VERSION "2.0"` in
  `main/config.h`; that macro has since been removed entirely in favour of the version ESP-IDF
  already derives from git — `esp_app_desc_t.version`, auto-populated at build time from
  `git describe --always --tags --dirty` (no `version.txt`, no `VERSION` in `project()`, no
  `PROJECT_VER`). The firmware reads it via `esp_app_get_description()->version` in the boot
  banner, in `buildStateJSON()` (`/api/v1/state`) and in `/api/v1/diagnostics/info`, so all three report the
  identical string. Verified on hardware: `"2.0.0"` with a bare `2.0.0` tag at HEAD, a commit hash
  (plus `-dirty`) otherwise. Two real bugs caught and fixed along the way: (1) the dashboard used
  to prepend its own `"v"` unconditionally, which double-prefixed a `v`-tagged release as
  `"vv2.0.0"` — fixed first by rendering verbatim, then, once tags were made bare, restored the
  `"v"` prefix as a **display-only** concern in `main/net/index_html.h`, safe now that the
  underlying value never carries one; (2) the web dashboard's `POSITION UNCONFIRMED` banner
  didn't gate on `calibrated`, unlike the touch UI — unrelated to versioning, caught live on
  hardware in the same session, fixed separately. Cutting 2.0.0 is now just
  `git tag 2.0.0 && git push --tags` + a GitHub Release — no source file to bump.

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

## Phase 7 — CI, release, docs, v1.10.0
- [x] **CI**: **confirmed green in real GitHub Actions runs on PR #4** (not just locally) - `idf.py build` (via `espressif/install-esp-idf-action`, pinned to ESP-IDF v6.0.2 - originally v5.3.2), host tests (`host_test/` ctest + gcovr coverage), and lint (pre-commit's clang-format/ruff + Spectral on the API specs + a JSON Schema contract check). Actions pinned to commit SHAs (verified live via `gh api`, not from memory). Originally one `ci.yml`; since split into `build.yml` (firmware + host tests) and `lint.yml` (pre-commit, API specs, workflow lint, cliff.toml's bump mapping) so a formatting nit and a broken build report as separate checks and can be re-run separately.
  - Getting host-tests green took 4 real iterations against actual CI: `install-esp-idf-action`'s cross-toolchain (Xtensa/RISC-V) leaked into `host_test/`'s native host build's `as`/`ld` resolution. Pinning `CC`/`CXX`, resetting `PATH`, and additionally resetting `COMPILER_PATH` all failed identically (`as: unrecognized option '--64'`), implying a system-level change (e.g. a gcc specs file), not a process-env one. Fixed by not using that action for this job at all - `host_test/` only needs one file from ESP-IDF (`components/unity/unity`, itself a `ThrowTheSwitch/Unity` submodule), fetched directly pinned to the exact commit ESP-IDF references, verified in a fully clean (`env -i`) local simulation before trusting it in CI. (v5.3.2 and v6.0.2 point the submodule at the same Unity commit, so the 6.0 bump needed no change here.)
- [x] **Release** (`.github/workflows/release.yml`): `idf.py build` → `idf.py merge-bin -o ...-factory.bin --pad-to-size 4MB` (verified by hand: produces exactly a 4 MB raw image, matching the README's "flash at offset 0x0" instructions) + the app-only image as `...-ota.bin`, both attached to the GitHub Release. The old `FW_VERSION`-must-match-tag guard has been dropped: the version is now derived from `git describe` at build time, so a clean checkout of tag `X.Y.Z` (bare, always full three-part semver) reports exactly that tag by construction and there is nothing left to cross-check — the workflow uses the tag only to name its artifacts. Since rewritten as a `workflow_dispatch` pipeline (validate → checks → approval gate → tag/build/prerelease → verify the published assets → promote to latest), with the version and the release notes derived from the Conventional Commits by git-cliff (`cliff.toml`) and the version read back out of the built image by `tools/app_desc.py`. **Not yet run** - untested until an actual release is cut; `dry-run` (the default) exercises everything up to the checks.
- [x] **Docs:** README/CONTRIBUTING/CLAUDE.md already rewritten for the ESP-IDF-only layout as part of the Phase 0-3 rebase (see the `refactor!` commit); nothing further pending here.
- [x] **v1.10.0 (was "v1.9"):** Karl's upstream release shipped, diffed, and folded into this port - see [`docs/upstream-v1.10.0-diff.md`](upstream-v1.10.0-diff.md) and the PR description's "Upstream v1.10.0 merge" section. Confirmed incorporated: three inherited bugs fixed (batch counting gated by the display counter cap, decel-blank carry-through discarding jam evidence, unescaped `&` in the WiFi scan dropdown), plus the TMC tuning delta (`TOFF` 5→4, `TBL`=1, `INTPOL`) - flagged unverified on hardware, still a Phase 4 bench item below.
- **DoD:** ✅ **CI is green** (verified in real GitHub Actions, not just claimed). Release workflow's build steps verified by hand; the end-to-end release flow untested (needs an actual release to exercise). v1.10.0 diff incorporated.

## Phase 8 — Post-migration hardening
Non-bench-blocking follow-ups from the 2026-07-25 codebase review (`docs/review-2026-07-25.md`).
The safety-critical, bench-blocking items from that review are tracked in Phase 4 above, not here.
- [x] **UX/UI/bug review follow-ups (2026-08-08/09).** Five PRs: the batch-start gate on a stale
  position reference; the dashboard's silent `SG=0` write, invisible SSE dropout, per-pixel slider
  POSTs and counter-parity; control routes reporting `400`/`409` instead of a blanket `200 ok`
  (new `lib/autolee_logic/command_gate.h`); a true persisted lifetime cycle count (settings blob
  v3); and operator-abortable calibration/creep-home (new `MotorEvent::Abort`). Findings judged
  not worth fixing now were filed as issues rather than left implicit — command coalescing,
  confirmation-pattern consistency, web-UI cleanups, and STOP reachability from every screen.
  Sequence diagrams for the resulting flows are in [`FLOWS.md`](FLOWS.md).
- [x] **Persist settings/calibration to NVS as a versioned struct** (`main/settings_store.{h,cpp}`,
  registered in `main/CMakeLists.txt:6`). One fixed-layout `Persisted` blob
  (`PersistedV1`, `main/settings_blob.h:74`) in NVS namespace `autolee`, key `settings`, `uint16_t version`
  first: `rawUp`/`rawDown`/`endpointsCalibrated`/`upOffsetSteps`/`downOffsetSteps`, the
  per-profile `sg_trip[]`, `runCurrentMa`, `sgWorkZoneSteps`, `activeProfile`, `counter`.
  Deliberately **not** persisted: `runState`, `currentTarget`, `batch*` and the SG telemetry
  counters (must always come up at safe defaults after any reset), `SpeedProfile::name`/`speed_hz`
  (a `const char *` is meaningless across a firmware update, and `speed_hz` has no runtime writer),
  and the derived `endpointUp`/`endpointDown` (recomputed from the restored raws).
  - **Load** (`main/settings_store.cpp:162`) runs from `app_main.cpp:93`, before `motion_init()`,
    so the restored `runCurrentMa` is what `motion_init()` pushes to the TMC5160; it finishes with
    `recomputeEffectiveEndpoints()`.
  - **Fail-safe, all-or-nothing**: a missing blob, a size mismatch, a `version` mismatch, or *any*
    field outside the bounds the live setters already enforce (`OFFSET_MIN/MAX`,
    `RUN_CURRENT_MIN/MAX`, `SG_WORK_ZONE_MIN/MAX`, `RUN_SG_TRIP_MIN/MAX`, `NUM_PROFILES`, plus a
    geometry check that travel is positive and within `CAL_SEARCH_STEPS`) discards the **whole**
    blob (`validateV1()`, `main/settings_blob.h:111`), keeps `MotionState`'s compiled-in defaults
    and explicitly forces `endpointsCalibrated = false` + zeroed effective endpoints
    (`fallbackToDefaults()`, `main/settings_store.cpp:117`), logging the reason via `webLog()`. Nothing is partially
    trusted — the press must never believe it knows its endpoints from data that failed a check.
  - **Save** (`main/settings_store.cpp:247`) is a dirty-check hook at the end of
    `processPendingCommands()` (`main/motion/motion_cmd.cpp:156`), i.e. pump_task only: `memcmp`
    against the last-saved copy, at most one write per 5 s (`kSaveIntervalMs`), so a held-down UI
    knob costs one commit rather than one per step. An NVS commit is a few ms — three orders of
    magnitude inside the 8 s task-WDT budget, and pump_task resets the watchdog on the next
    iteration.
  - **Residual closed: a restored calibration now *requires* a Return Home before it will run.**
    The stepper's position counter starts at 0 every boot while the carriage sits wherever power
    was lost. 0 *is* the UP endpoint in the normal case (a finished run, a stop and a home all park
    there), but after a mid-stroke power cut or watchdog reset the restored endpoints are offset
    from reality — so the restore is no longer merely logged, it is gated:
    - `MotionState::positionReferenceStale` (`main/motion/motion_state.h:62`) is latched by
      `settings_store::apply()` (`main/settings_store.cpp:61`) whenever a calibration is restored,
      and by `fallbackToDefaults()` (`:117`) for good measure. It is cleared *only* by something
      that re-establishes ground truth against the UP hard stop: a successful `safeCreepHome()`
      (`main/motion/motion.cpp:458`, inside the same guard as the `HomeDone` transition, and **not**
      on the "FAILED to find stop!" path) or a fresh `calibrateEndpointsSensorless()`
      (`main/motion/motion.cpp:708`, in the same transaction as `endpointsCalibrated = true`).
    - `startRunBetweenEndpoints()` refuses while it is set (`main/motion/motion.cpp:155`), logging
      "Start refused: position reference unconfirmed - return home first" and returning before it
      touches the TMC or the stepper — same shape as the existing `!endpointsCalibrated` and
      rejected-transition early-outs.
    - The tested FSM gained a second source state for `ReturnHome`:
      `Idle + ReturnHome -> Homing` (`lib/autolee_logic/motor_fsm.h:46`, alongside
      `Stalled + ReturnHome -> Homing` at `:60`), so re-referencing does not require a full
      recalibration — the DOWN search stays valid as long as UP is correctly re-zeroed.
      `motion_cmd`'s `s_returnHome` handler already gates on
      `motionEventAllowed(MotorEvent::ReturnHome)` (`main/motion/motion_cmd.cpp:73`), so it picked
      the new source state up with no change.
    - Operator affordance: the main-screen warning banner (`ui_update_main_warning()`,
      `main/ui/ui_touch.cpp:188`) now shows "TAP: RETURN HOME" and becomes clickable in that state
      (same object/style as "NOT CALIBRATED"; the tap defers to pump_task via
      `motion_cmd::requestReturnHome()` exactly like the jam screen's button, which is otherwise the
      only Return Home in the UI and is unreachable from IDLE). The state JSON gained
      `positionStale` (`lib/autolee_logic/state_json.h:30`/`:74`,
      `buildStateJSON()` `main/net/web_server.cpp:124`, `api/schemas/state.schema.json` +
      `state.example.json`), and the web dashboard reuses the jam-alert panel to surface it
      (`main/net/index_html.h:368`).
    - Covered by `host_test/test_motor_fsm` (the exhaustive transition sweep knows the new valid
      pair) and 4 new `host_test/test_motion_seq` cases: Start refused while stale, Return Home from
      IDLE clears it and unblocks Start, a failed home keeps it, a fresh calibration clears it.
- [x] **Versioned-settings migration chain** (README-Copilot review item #4, "Version your settings",
  `docs/review-2026-07-25.md:60`) — the persisted blob was *versioned* but had no *migration*: a
  `version` mismatch took the same all-or-nothing reject path as corruption, so the first field
  addition after deployment would have silently wiped every device's calibration. There is now a
  real forward-migration mechanism.
  - **New pure header `main/settings_blob.h`** — the on-flash layouts, the per-version validators
    and the version dispatch, split out of `settings_store.cpp` and deliberately free of NVS,
    `MotionState` and logging so the host tests exercise exactly what the firmware runs (same
    split as `lib/autolee_logic/`, but it stays under `main/` because it depends on `main/config.h`
    for `NUM_PROFILES` and the validation bounds). `settings_store.cpp` keeps the NVS I/O and the
    `MotionState` apply halves only.
  - **Layouts:** `struct PersistedV1` (`main/settings_blob.h:74`) is the byte-for-byte 36-byte
    layout that shipped, `static_assert`'d at `:87` and frozen — old-version structs are never
    edited, they describe bytes that exist on deployed devices. `using Persisted = PersistedV1`
    (`:93`) is what the firmware works with in RAM; a new version repoints *that*, not `PersistedV1`.
    `kMaxBlobBytes` (`:98`) sizes the `nvs_get_blob()` read buffer against the largest known version,
    so `load()` reads raw bytes and lets the dispatch decide what they mean rather than reading
    straight into today's struct (`main/settings_store.cpp:176`).
  - **Dispatch:** `peekBlobVersion()` (`:172`) reads only the leading `uint16_t` — `version` is the
    first member of every layout by construction, `static_assert`'d at `:173` — and
    `parseAndMigrate()` (`:185`) then switches on it: current version → direct deserialize, older
    version → deserialize with *that version's* struct and run the chained
    `migrate_vN_to_vN+1()` steps, anything else (too old, or from the future after an OTA rollback)
    → `ParseResult::UnknownVersion`. Multi-version jumps chain single steps (v1→v2→v3); no
    shortcut functions.
  - **Corrupt old blobs cannot be laundered forward:** each version has its own validator
    (`validateV1()`, `main/settings_blob.h:111` — the old `validate()` moved verbatim, same bounds
    the live setters enforce) and every step validates its input on its *own* version's terms
    **before** migrating, so a bad v1 blob is rejected rather than turned into a v2 struct that
    passes every v2 check. `out` is left untouched on every failure path.
  - **`kVersion` deliberately stays 1.** Nothing is being added to the persisted set here — the
    mechanism is what was missing. The bump to 2 belongs to the health-monitoring item (stall
    count, cycle stats), which will be its first real user. To stop that bump shipping without its
    migration, `static_assert(kVersion == 1, ...)` (`main/settings_blob.h:218`) fails the build if
    `kVersion` moves without `parseAndMigrate()`'s switch being extended — verified by temporarily
    bumping it and watching the build stop.
  - **Recipe is documented where it will be found** (`main/settings_blob.h:20`, the file header):
    the seven steps to add a persisted field, from "never edit the old struct" through the new
    validator, the one migration function, the new `case`, and the test.
  - **Load-path behaviour is otherwise unchanged.** `load()` (`main/settings_store.cpp:162`) maps
    `ParseResult` back onto the same `webLog()` reasons as before (size mismatch / version
    mismatch / failed validation → `fallbackToDefaults()`), so a current-version blob loads exactly
    as it did. New: on a successful migration it logs `Settings: migrated from vN to vM` (`:211`)
    and immediately rewrites the blob in the current format (`:243`) — after `s_lastSaved` is
    baselined, so a failed rewrite just leaves the old bytes and `tick()` retries.
  - **Tests:** new `host_test/test_settings_blob` (9 cases, suite wired via
    `host_test/test_settings_blob/suite.cmake`). Every "stored blob" is assembled as raw
    little-endian bytes at hand-written offsets rather than by `memcpy`-ing a struct, so the suite
    is an independent description of what is on flash and catches a layout edit the struct's own
    `sizeof` assert would miss (`test_v1_layout_is_frozen`, `:104`). Covers the unchanged common
    case (`test_v1_blob_loads_unchanged`, `:141`), the uncalibrated blob, version 0/2/0xFFFF
    rejection, truncated/short/long blobs, and 11 out-of-range field rejections that each assert
    `out` was left untouched. Because there is no real v2 yet, `test_recipe_migrates_an_old_blob_forward`
    (`:387`) and `test_recipe_rejects_a_corrupt_old_blob_before_migrating` (`:403`) rehearse the
    documented recipe on stand-in structs (`namespace recipe`, `:279`) with a genuine two-step
    v1→v2→v3 chain, proving carried-over fields survive, new fields get explicit defaults and a
    corrupt old blob is rejected before any migration runs. Replace that pair with the real
    migration's test when the health-monitoring bump lands.
  - **Verified:** `idf.py build` clean; all 11 host suites pass; and on the real board a v1 blob
    written by the *previous* firmware (`runCurrentMa` set to 2800 mA over
    `POST /api/v1/motion/current`) was restored correctly after flashing the new build —
    `I (592) weblog: Settings: restored (cal=no up=0 dn=0 cur=2800mA prof=1 cnt=0)`, no migration
    line, no fallback.
- [x] **Reset calibration/settings action** — a deliberate way to discard a stored calibration
  (press moved, brass changed, a clean recalibration wanted) without an NVS erase over serial.
  - **Core:** `settings_store::resetToDefaults()` (`main/settings_store.h:72`,
    `main/settings_store.cpp:264`). Erases the blob with **`nvs_erase_key`, never
    `nvs_erase_all`** — the `autolee` namespace is *shared*: `wifi_mgr.cpp` keeps `ssid`/`pass`/
    `apkey` and `web_server.cpp` keeps `webpass` in it, so only the `settings` key is ours to
    delete and a reset can never lock the device off the network (the misleading "own namespace,
    not wifi_mgr's" comment at `main/settings_store.cpp:16` was corrected to say this).
  - **Defaults come from `MotionState` itself**, not from literals: `applyCompiledDefaults()`
    (`main/settings_store.cpp:96`) copies the persisted subset out of a default-constructed
    `MotionState`, so `motion_state.h` stays the single source of truth and a new persisted field
    cannot be silently left behind. `fallbackToDefaults()` (`:117`) now just calls it plus logs —
    the rejected-load and the deliberate-reset paths share one field list. Calibration is forced
    off, the effective endpoints zeroed and `positionReferenceStale` latched, then
    `recomputeEffectiveEndpoints()` runs, exactly as at the end of `load()`.
  - **Dirty-check bookkeeping is defined, not accidental** (`main/settings_store.cpp:264`): on a
    successful erase, `s_lastSaved` is re-baselined to the *defaults* and `s_lastCheckMs` reset,
    so the `tick()` call at the very end of the same `processPendingCommands()` pass sees no diff
    and does **not** immediately re-create the blob — the next boot takes the same "no stored
    settings" path as a factory-fresh device. If the erase fails, the defaults are written over
    the stale blob instead (RAM and flash must not disagree, or the old calibration would come
    back on the next boot); if that write also fails, `s_lastSaved` is deliberately left at the
    pre-reset copy so `tick()`'s dirty check keeps retrying.
  - **Routing:** `motion_cmd::requestResetSettings()` (`main/motion/motion_cmd.h:37`,
    `main/motion/motion_cmd.cpp:53`) — one more atomic flag, executed on pump_task only
    (`:144`). Gated on the literal `g_motion.runState == IDLE` rather than
    `canStart()`/`motionEventAllowed()`: this is not a motion command, and coupling it to the
    motion-permission table would silently widen the gate if that table ever gains a state.
    pump_task then re-programs the hardware from the restored defaults (`tmc5160::rms_current()`
    on the display-shared SPI bus, `setActiveProfile()` for the stepper speed) and refreshes the
    UI. A non-IDLE request is logged and dropped.
  - **API:** `POST /api/v1/settings/reset` (`main/net/web_server.cpp:654`), already
    covered by the existing Digest-auth middleware like every other write; documented with the
    other action values in `api/openapi.yaml:104`.
  - **Web UI:** a "Reset Calibration" section at the end of the Configuration page
    (`main/net/index_html.h:171`) with a `confirm()` guard in `resetSettings()` (`:401`), the
    same client-side pattern `resetWifi()` uses.
  - **Touch UI:** a red "Reset Cal" button, last on the (scrollable) Config screen
    (`main/ui/ui_touch.cpp:746`) — deliberately off the main/settings screens and out of the way
    of normal operation. **Confirm step: two-tap arming** (`on_reset_cal()`, `:599`), because
    this UI has no modal-dialog pattern to reuse (the existing destructive buttons, "Reset WiFi"
    and "Reset Count", fire on the first tap) and a dedicated confirm *screen* would be a lot of
    machinery plus another navigation dead-end on a 172x320 display. The first tap turns the
    button amber and relabels it "Sure? Tap" and starts a one-shot `UI_CONFIRM_ARM_MS` (5 s,
    `main/config.h:141`) timer; only a second tap inside that window fires. The timeout, entering
    the screen and leaving it all disarm (`:1107`, `:1166`), so a stray tap can never wipe a
    calibration and the operator is told what the next tap will do.
  - Verified: `idf.py build` clean, and all 10 `host_test/` suites still pass (this touches no
    `lib/autolee_logic/` code; `main/motion/motion.cpp`'s host-tested sequencing is unchanged).
- [x] **Faked the `stepper::`/`tmc5160::` seams — `motion.cpp`'s sequencing now has host-test
  coverage** (`host_test/fakes/` + `host_test/test_motion_seq/`, 34 Unity tests, **100% line
  coverage of `main/motion/motion.cpp`** per gcovr — note CI's coverage *gate* still filters on
  `lib/autolee_logic/` only, so this figure is informational, not enforced). `main/motion/`'s
  `motion.cpp` and `motion_state.cpp` are compiled **verbatim** into the new suite (not extracted,
  not reimplemented — a behaviour-preserving change by construction, since the firmware sources are
  byte-identical and `idf.py build` is unaffected): what is replaced is everything *below* them —
  fake `stepper::`/`tmc5160::` bodies behind the **real** `main/drivers/*.h` headers (so a seam
  signature change breaks the test build instead of silently drifting), fake `webLog()`/`ui_touch.h`
  hooks, and minimal `esp_timer`/`esp_task_wdt`/`vTaskDelay`/`portMUX`/`lvgl.h` stubs under
  `host_test/fakes/include/`. The stepper fake models the two things the sequencing depends on:
  moves take time (`forceStop()` only takes effect on the next poll, matching the one-cruise-chunk
  latency `stepper.cpp` documents) and position is a **pulse count** (PCNT keeps counting while the
  carriage sits against a hard stop — which is exactly what StallGuard sees as a stall), so
  calibration/homing hit detection is driven by a simulated press with real mechanical stops rather
  than by canned SG values. `host_test/CMakeLists.txt` grew one generic extension point (a suite may
  add a `suite.cmake` for extra sources/includes); the other nine suites are unchanged.
  - **Now covered:** the jam path end-to-end (SG trip → `forceStop` → creep speed → backoff by
    exactly ±`RUN_BACKOFF_STEPS` *away* from the target endpoint → `STALLED` latch → jam screen,
    plus "no further move is commanded after a jam" and `handleMotion()` being inert in
    `STALLED`/`HOMING`/`CALIBRATING`); accel-blank, work-zone and single-high-reading debounce
    (no false jam); endpoint-arrival cycle counting, target flip, batch completion → graceful stop,
    and the counter-saturation-must-not-stall-a-batch regression; graceful stop reaching UP vs.
    timing out after `STOP_TIMEOUT_MS`; the full `calibrateEndpointsSensorless()` order
    (cal current → pre-move → UP search → back off → re-zero → DOWN search → back off → restore run
    current → park at UP) and its clean-failure path when no stop is found; `safeCreepHome()`
    find-stop/re-zero/return-to-IDLE and its failure path; `return_home_up_safe()`'s retry counting
    (3 stalls → `HOME_MAX_RETRIES` give-up) and `HOME_TIMEOUT_MS`; every FSM-rejection guard
    (start/stop/calibrate/return-home from a state that forbids it must touch **no** hardware);
    and — asserted in `tearDown()` for every test — that no stepper/TMC/`webLog` call ever happens
    inside a `motion_state::Guard` critical section, the rule `motion_state.h` documents.
  - **Still NOT covered:** anything below the seam (`main/drivers/stepper.cpp`'s RMT/PCNT pulse
    generation, `tmc5160_hal.cpp`'s SPI/CS handling, `axs5106l_touch`, `display_touch`), real
    concurrency (the host build is single-threaded: `portENTER_CRITICAL` only counts nesting, so
    these tests prove the *discipline*, not the absence of races), real timing/accel behaviour
    (the fake ramps instantly and advances the clock in 10 ms `vTaskDelay` steps), and the actual
    SG signal a jammed press produces. Bench verification of calibration/jam/homing on the real
    press (Phase 4) is still required — this suite catches sequencing regressions, it does not
    replace the bench session.
- [x] Added `GET /api/v1/diagnostics/info` diagnostics endpoint (`main/net/web_server.cpp:444-472`):
  `esp_app_get_description()` (version, idf_ver, compile date/time, ELF SHA-256 truncated to
  the first 8 bytes/16 hex chars — matching ESP-IDF's own boot-log short SHA), `esp_reset_reason()`
  mapped to a string via a local `resetReasonStr()` (`web_server.cpp:70-90`), free/min-free heap
  (`esp_get_free_heap_size()`/`esp_get_minimum_free_heap_size()`), uptime
  (`esp_timer_get_time()/1000`), the running OTA partition's label
  (`esp_ota_get_running_partition()`), and a `coredumpPresent` bool
  (`esp_core_dump_image_check() == ESP_OK`). It's a GET, so — like every other read in this file —
  it's left open, not Digest-gated (matches the existing convention: only non-GET routes go through
  `s_auth`). Documented in `api/openapi.yaml`.
- [x] Added `GET /api/v1/diagnostics/coredump` (`main/net/web_server.cpp:475-512`) — streams the raw `coredump`
  partition (`partitions.csv`, verified end-to-end in Phase 6) as a chunked file download via
  PsychicHttp's `sendChunk`/`finishChunking`, reading through `esp_partition_read` in 512-byte
  chunks after `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP)`.
  Presence/integrity is checked with `esp_core_dump_image_check()` (validates the stored checksum)
  before anything is streamed — returns a clean 404 instead of any bytes if no valid coredump is
  present, and the actual byte count comes from `esp_core_dump_image_get()`. **Digest-gated**,
  unlike `/api/v1/diagnostics/info` and every other GET in this file — caught in review before commit: a
  coredump is a raw RAM snapshot and can contain the WiFi or web password in plaintext (both are
  held in local C buffers at runtime — `wifi_mgr.cpp`'s `wifi_config.sta/ap.password`, `s_auth`'s
  stored password). The shared auth middleware (`web_server.cpp`'s `addMiddleware`) special-cases
  this one route to require auth despite being a GET. Added a "Download Core Dump" link next to
  the OTA upload control on
  the web UI's Firmware page (`main/net/index_html.h`) — a plain `<a href>`, so the browser handles
  the download natively. `espcoredump` and `esp_partition` added to `main/CMakeLists.txt`'s
  `REQUIRES`. Documented in `api/openapi.yaml`.
- [x] OTA image identity check (`main/net/web_server.cpp`'s `handleOtaUpload()`, lines 260-300) —
  one half of `#1c` above (the other half, force-change-on-first-use for the default password, has
  since also landed — see `#1c` above). Before
  `esp_ota_set_boot_partition()`, and while the OTA handle is still open (i.e. before
  `esp_ota_end()`), `esp_ota_get_partition_description()` reads the just-written partition's
  `esp_app_desc_t` and compares its `project_name` against the running app's own
  (`esp_app_get_description()`). A mismatch is rejected via the existing `otaAbort()` helper
  (handle still open, so `esp_ota_abort()` — not `esp_ota_end()` — is correct) and logged both to
  `ESP_LOGE` and `webLog()`; boot partition is never set. A same-project image proceeds through the
  existing `esp_ota_end()` → `esp_ota_set_boot_partition()` path unchanged.
- [x] SSE diff-and-heartbeat instead of unconditional full-state every 250ms
  (`main/net/web_server.cpp`'s `broadcastState()`, lines 714-740). `buildStateJSON()`'s output is
  now compared against `s_lastSentState`; the default `message` SSE event is only sent when it
  differs. When it doesn't, a lightweight named `heartbeat` event (`{}`) is sent instead once
  `SSE_HEARTBEAT_MS` (8s, new constant in `main/config.h`) has elapsed since the last send of
  either kind — so a client can tell "still connected, nothing changed" apart from a dead
  connection without a full-state payload every 250ms regardless of whether anything moved. The
  dashboard's `onmessage` handler only fires for the unnamed event, so the heartbeat is inert to
  existing JS. The `MotionState`/`buildStateJSON()` data model is unchanged. `api/asyncapi.yaml`
  updated with the new `heartbeat` message and the revised send-cadence description.
- [x] Fixed the stale `.pre-commit-config.yaml` exclude path — was
  `^main/stepper_motor_encoder\.[ch]$`, now `^main/drivers/stepper_motor_encoder\.[ch]$` matching
  where the file actually lives.
- [x] Firmware version switched to the git-derived `esp_app_desc_t.version` (`git describe`), replacing the hand-maintained `FW_VERSION` macro that was previously bumped to `"2.0"` (see `#24` above).
- [x] **REST API regrouped from flat `/api/v1/*` into nested paths** — `motion/*`, `settings/*`,
  `wifi/*`, `diagnostics/*`, `system/*`; `GET /api/v1/state` and the SSE `/api/v1/events` stay
  top-level. The old `POST /api/v1/action?do=<verb>` dispatcher is gone, split into four standalone
  routes (`motion/calibrate`, `motion/return_home`, `motion/reset_counter`, and `settings/reset`,
  which moved out of motion into its own group). Started as "just add OpenAPI `tags:` so Swagger
  UI/Redoc group cleanly", but on rubber-ducking it, the two things that made a full regroup look
  risky both turned out to be defensible: the `system` bucket (password/log/OTA are genuinely
  device administration, not motion) and the `action` split (each verb is its own resource; the
  `do=` query param was only ever a way to save URI handlers). Safe to do now because no release
  has been cut — zero git tags, zero GitHub releases, so there is no external consumer to break.
  The one real risk was `main/net/web_server.cpp`'s server-wide auth middleware, which does **exact
  string equality** on two literal paths: the `#1c` write-gate exemption for the password endpoint
  (get it wrong and a device on the factory-default password can never have it changed over the
  network again) and the Digest-despite-being-a-GET gate on the coredump download (get it wrong and
  the coredump silently becomes unauthenticated). Both are testable with two curls, and both were
  verified on hardware after the rename. Changed in lockstep: `main/net/web_server.cpp`,
  `main/net/index_html.h` (dashboard fetches, OTA target, coredump `href`; `doAct()` now takes a
  path instead of a verb), `api/openapi.yaml` (paths + per-group `tags:`, `action` split into four
  documented operations), `api/schemas/state.schema.json`, `lib/autolee_logic/state_json.h`,
  `tools/mock_server.py` and the doc references. `api/asyncapi.yaml` is unaffected (only
  `/api/v1/events`). `max_uri_handlers` raised 26 → 32 to cover the three extra routes.

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
  host-tested `StallCounter` / `ConfirmCounter` (tested == shipped). The same is
  now true for the run-state transitions themselves: `motorTransition()` /
  `canStart()` (`motor_fsm.h`) are called from `motion.cpp` and `motion_cmd.cpp`
  — see #4b above.
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

## Phase 9 — ESP-IDF 6.0 upgrade
- [x] **Toolchain moved 5.3.2 → 6.0.2**, floor raised to `idf: ">=6.0"` in `main/idf_component.yml`;
  CI (`build.yml`) and `release.yml` pin `v6.0.2`. `dependencies.lock` regenerated (LVGL 8.4.0 and
  `esp_lvgl_port` 2.8.0 resolve unchanged - no component bumps were needed).
- [x] **Three source changes, all that 6.0 required:**
  - `main/drivers/display_touch.cpp` — explicit `freertos/FreeRTOS.h` + `freertos/task.h`; 6.0
    stopped re-exporting them through the `esp_lcd`/`driver` headers, so `vTaskDelay()` /
    `pdMS_TO_TICKS()` in `jd9853_send_init_sequence()` no longer resolved.
  - `main/drivers/display_touch.cpp` — `LCD_DC/CS/RST_GPIO` are `GPIO_NUM_*` rather than bare ints;
    6.0 typed `esp_lcd`'s cs/dc/reset config members as `gpio_num_t`, which C++ will not implicitly
    narrow an int into.
  - `main/net/wifi_mgr.cpp` — a scoped `-Wmissing-field-initializers` pragma around
    `DNS_SERVER_CONFIG_SINGLE`, which 6.0 rejects under `-Werror`. Kept at the call site so
    `lib/dns_server/` stays byte-identical to Espressif's example, where the macro is unchanged in
    6.0.2.
- [x] **Notably NOT needed:** ST7789 stayed in `esp_lcd` (only NT35510 moved to the registry in
  6.0), `esp_private/esp_clk.h` still exists, and the vendored `lib/psychic_http` + `lib/tmc_api`
  needed nothing.
- [x] **Verified on hardware** (WaveShare ESP32-C6, `/dev/ttyACM0`): clean boot on 6.0.2 — NVS
  settings restored across the upgrade (`boots=14`), AXS5106L probed over I²C, LVGL UI built,
  stepper driver enabled, WiFi associated, web server up, `app_main()` returned. `/api/v1/state`
  and `/api/v1/diagnostics/info` both answer and report `idfVersion: v6.0.2`.
  **The screen itself is a visual check the maintainer still has to make.**
- [x] Host tests: 11/11 pass against 6.0.2's Unity (same submodule commit as 5.3.2, so CI's pin
  did not move).
- **Cost:** app image grew **1,570,384 → 1,648,352 bytes (+78 KB, +5.0%)**, leaving 16% of the OTA
  slot free (was 20%). Attributed by diffing `idf.py size-components` between the two builds:
  - **mbedTLS 4.x + the PSA Crypto migration, ~+45 KB** — `libmbedcrypto.a` (72,208 B) is replaced
    by `libtfpsacrypto.a` (117,201 B). This is the dominant cause and it is called out in 6.0's own
    release notes ("For `http_server/simple`, the flash impact increases about 41KB (~4.97%)").
  - **WiFi/PHY blobs, ~+28 KB** — `libnet80211` +14.0 KB, `libwpa_supplicant` +8.6 KB,
    `libphy` +2.9 KB, `libpp` +2.5 KB.
  - **libc, ~+5.6 KB** — Picolibc (6.0's new default) is advertised as a size *reduction*; measured
    here it is a small *increase* (`libnewlib`+`libc` 10,246 B → `libesp_libc`+`libc` 15,858 B),
    presumably because `CONFIG_LIBC_PICOLIBC_NEWLIB_COMPATIBILITY` defaults on.
  So the lever if the slot ever gets tight is mbedTLS, not libc. Worth knowing: this firmware
  serves plain HTTP and uses Digest (MD5) auth — mbedTLS is pulled in because `lib/psychic_http`
  requires `esp_https_server`, not because anything here terminates TLS.
