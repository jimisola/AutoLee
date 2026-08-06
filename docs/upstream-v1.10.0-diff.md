# Upstream v1.8 → v1.10.0 diff (Karl's Arduino source) — port follow-up

Karl bumped the Arduino firmware from **v1.8** (what `main` holds, and what this
ESP-IDF port was built against) to **v1.10.0**, delivered as a zip rather than a
release/tag. This is the analysis of what changed and what it means for the port.

Compared: `main`'s `AutoLee/*.{ino,h}` vs. the v1.10.0 zip contents.
Line deltas: `AutoLee.ino` +18, `web_server.h` +76, `motion.h` +5, `ui_touch.h` +5,
`wifi_ota.h` +1, `config.h` −1, `lv_conf.h` unchanged.

> **Headline:** v1.10.0 independently implements the same fix as this PR's review
> finding **#2** (defer all web-triggered work off the async task) for the same
> stated reason. That's strong independent validation that #2 is real — and it
> gives us a reference implementation plus an exact list of what needs deferring.
> Likewise v1.10.0's `jsonEscape()` matches review finding **#18**.

---

## Status legend

- ✅ **Ported** — already applied to this branch
- 🔨 **Port next** — accepted, not yet applied
- ⚙️ **Bench-gated** — needs the motor rig (Phase 4) before adopting
- ⛔ **N/A** — doesn't apply to the ESP-IDF port

---

## 1. Bugs the port inherited from v1.8

### ✅ Batch counting gated by the display counter cap
`motion.h` — v1.8 wrapped the whole block in `if (currentTarget == endpointDown && counter < 9999)`,
so once the lifetime counter saturated at 9999 the **batch counter stopped
incrementing too** and batches never completed. v1.10.0 separates them: always run
the batch logic, only cap the cosmetic display counter.

Ported in `main/motion.cpp` (`handleMotion`, RUNNING case).

### ✅ Decel-blank carry-through discarded jam evidence
`motion.h` — the decel-window skip tested `runSGHighCount < RUN_SG_HIGH_NEEDED`
and reset the count. But reaching `RUN_SG_HIGH_NEEDED` would already have fired
the jam earlier in the same pass, so that test was **always true** — meaning
partial jam evidence entering the decel window was silently thrown away. v1.10.0
keys the carry-through on `runSGHighCount == 0` and stops resetting the count.

Safety-relevant (jam detection sensitivity near the endpoints). Ported in
`main/motion.cpp`.

### ✅ `&` not HTML-escaped in the WiFi scan dropdown
`wifi_ota.h` — an SSID containing `&` produced broken markup. v1.10.0 adds
`&`→`&amp;` and notes it must be escaped *first*, because it uses sequential
`String::replace()` calls that would otherwise double-escape entities it just
inserted. Our port builds the string char-by-char, so it is immune to that
ordering hazard — but was missing `&` entirely. Ported in `main/wifi_mgr.cpp`.

---

## 2. Concurrency: v1.10.0 confirms review finding #2

`AutoLee.ino` + `web_server.h` — v1.10.0 moved **every** web-triggered state change
out of the AsyncTCP handler and into deferred flags processed from `loop()`,
with the rationale spelled out in-source:

> *"Async web handlers run in the AsyncTCP task. Anything touching SPI
> (TMC5160), the stepper, LVGL, or the log buffer is flagged here and processed
> from loop()."*

v1.8 deferred only calibrate + home. v1.10.0 additionally defers:

| Action | New flag | Why |
|---|---|---|
| toggle run | `webToggleRunRequested` | stepper + TMC SPI + LVGL |
| stop (from OTA upload) | `webStopRequested` | stepper calls must not run in the async task |
| batch start | `webBatchStartRequested` | stepper + TMC SPI + LVGL; validity re-checked in `loop()` |
| profile change | `webProfileRequested` (`-1` = none) | `setActiveProfile` touches stepper + LVGL |
| motor current | `webCurrentMaRequested` (`-1` = none) | `rms_current()` SPI write could collide with `read_sg()` on the shared bus |
| log clear | `webLogClearRequested` | clearing the ring races `webLog()` |
| any LVGL refresh | `webUiRefreshRequested` | LVGL is not thread-safe |

Also: `reset_counter` no longer calls `lv_label_set_text()` directly — a plain
32-bit write is safe, and the 100 ms `counter_timer_cb` refreshes the label.

**Relevance:** this *is* review finding #2, and the same shared-SPI concern as
finding #3. Our port has the same hazard with a different shape (FreeRTOS tasks:
HTTP task + LVGL task + `pump_task`, rather than AsyncTCP + `loop()`), so the
fix must be adapted rather than copied — but Karl's flag list is effectively a
checklist of everything that needs routing through `pump_task`.

**Do this as part of the Phase 4 concurrency cluster (#2 + #8).**

---

## 3. TMC5160 tuning — ⚙️ bench-gated, do NOT adopt blind

`AutoLee.ino` `setup()`:

```cpp
driver.toff(4);        // was 5 — shorter slow-decay phase per cycle,
                       // "tuned for low-speed high-load torque"
driver.tbl(1);         // TBL=1 -> 24 tCLK comparator blank time
driver.intpol(true);   // MicroPlyer: interpolate 16 usteps -> 256 internally
```

Karl's note is worth keeping — a genuine library gotcha:

> *"TMCStepper's `blank_time()` expects 16/24/36/54 (clock counts);
> `blank_time(1)` is a silent no-op — write the register field directly."*

Our port uses TMC-API (`tmc5160_fieldWrite`) rather than TMCStepper, so we write
register fields directly and are not exposed to that specific no-op — but we
must confirm our `TBL`/`TOFF`/`intpol` values match whatever is decided.

These change torque and smoothness characteristics on real hardware. **Defer to
the Phase 4 bench session** and verify against the press before adopting.

---

## 4. Smaller items

### 🔨 `lv_refr_now(NULL)` before blocking calibration
`ui_touch.h` — forces a synchronous redraw so the "Calibrating..." label
actually renders, because the handler runs inside `lv_timer_handler()` and
LVGL's re-entrancy guard makes the nested calls inside calibration no-op.

This is **exactly the known limitation already documented** in our Phase 3 notes
(`on_calibrate()`'s status text never reaching the screen). Needs adapting to
`esp_lvgl_port`'s task-based model rather than a copy-paste, and is only
meaningfully verifiable once a display module is attached.

### 🔨 `jsonEscape()` also drops control characters
`web_server.h` — v1.10.0 escapes `"` and `\` and drops bytes `< 0x20`. Our
`lib/autolee_logic/state_json.h` (added for review finding #18) handles `"` and
`\` but does **not** drop control chars. Small hardening to align.

Also: v1.10.0 grew the state buffer 900 → 1024 bytes. Ours is 768 with a ~497-byte
observed payload — enough headroom even for a fully-escaped 32-char SSID, but
worth revisiting if fields are added.

### 🔨 Dead `sg_trip_default` field
`config.h` — v1.10.0 removed `SpeedProfile::sg_trip_default`. Our port still
declares it in `main/config.h` and references it **nowhere**. Safe to delete.

### 🔨 Batch button contrast
`ui_touch.h` — black text on the green "Start Batch" button (white-on-green was
unreadable). Cosmetic, trivially portable.

### ⛔ ArduinoOTA progress division-by-zero guard
`wifi_ota.h` — `if (t > 0)` plus 64-bit percentage math. Not applicable: this
port dropped ArduinoOTA entirely (OTA is web-upload only).

### ⛔ DIAG1 comment
`AutoLee.ino` — clarifies that DIAG1 on GPIO7 is configured as a push-pull stall
output but stall detection is polled over SPI; the pin config is kept so the pin
is actively driven and reserved for a future interrupt implementation. No
functional change; our port already carries the equivalent note.

---

## Versioning note — why this doc says "v1.10.0"

Karl's zip sets `FW_VERSION "2.0"`, but the release is a feature/bugfix drop
over 1.8 with **no breaking change**, so a major bump isn't semver-meaningful.
We therefore refer to it as **v1.10.0** throughout this repo (docs, code
comments, PLAN) to keep our own versioning honest.

⚠️ Note the mismatch when reading upstream source: the file itself still says
`2.0`. If this release is ever ingested or its version surfaced anywhere, decide
explicitly which label wins rather than letting both float (see also review
finding #24, the deferred version/changelog decision for this port).
