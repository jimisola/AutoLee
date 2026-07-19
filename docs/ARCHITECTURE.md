# Architecture

How the AutoLee firmware is put together: what runs where, which rules are
load-bearing for safety, and why the structure looks the way it does.

> ⚠️ This firmware drives a motorized press. The concurrency rules on this page
> are not stylistic — violating them can mean an uncontrolled reset mid-stroke or
> a corrupted jam-detection reading. See [PLAN.md](PLAN.md) for what is and isn't
> hardware-verified yet.

---

## 1. Layers

```mermaid
flowchart TB
    subgraph clients["Clients"]
        BROWSER["Browser<br/>(phone / desktop)"]
        TOUCH["On-device<br/>touchscreen"]
    end

    subgraph fw["Firmware — main/"]
        NET["net/<br/>wifi_mgr · web_server<br/>REST · SSE · OTA · captive portal"]
        UI["ui/<br/>ui_touch (LVGL screens)"]
        CMD["motion/motion_cmd<br/><b>deferred command queue</b>"]
        MOTION["motion/motion<br/>run · jam detect · calibrate · home"]
        DRV["drivers/<br/>stepper (RMT+PCNT) · tmc5160<br/>display_touch · axs5106l_touch"]
    end

    CORE["lib/autolee_logic/<br/><b>pure, host-tested logic</b><br/>endpoint math · SG filter/blanking<br/>stall FSM · batch · log ring · state JSON"]
    HW["Hardware<br/>TMC5160 · NEMA 23 · JD9853 LCD"]

    BROWSER -->|HTTP| NET
    TOUCH --> UI
    NET --> CMD
    UI --> CMD
    CMD --> MOTION
    MOTION --> DRV
    DRV --> HW

    MOTION -.uses.-> CORE
    NET -.uses.-> CORE

    style CMD fill:#fff3cd,stroke:#856404,stroke-width:2px
    style CORE fill:#d4edda,stroke:#155724,stroke-width:2px
    style MOTION fill:#f8d7da,stroke:#721c24,stroke-width:2px
```

`lib/autolee_logic/` is deliberately free of ESP-IDF and Arduino dependencies, so
the **same code** that ships is what the host tests in `host_test/` exercise.

---

## 2. Tasks and the one rule that matters

The Arduino original was cooperative — everything ran from `loop()`, so a web
callback could safely call motion code directly. This port has **real
preemption**, so that is no longer true.

```mermaid
flowchart LR
    subgraph tasks["FreeRTOS tasks"]
        HTTP["HTTP server task<br/><i>PsychicHttp</i>"]
        LVGL["LVGL task<br/><i>esp_lvgl_port</i>"]
        SSE["sse_task<br/><i>state + log broadcast</i>"]
        PUMP["<b>pump_task</b><br/>watchdog-subscribed<br/><i>owns all motion</i>"]
    end

    HTTP -->|request*| Q(["motion_cmd<br/>atomic flags"])
    LVGL -->|request*| Q
    Q -->|processPendingCommands| PUMP
    PUMP --> STEP["stepper / TMC5160 SPI"]
    PUMP --> FSM["motion FSM"]

    style PUMP fill:#f8d7da,stroke:#721c24,stroke-width:2px
    style Q fill:#fff3cd,stroke:#856404,stroke-width:2px
```

**The rule:** only `pump_task` touches the stepper, the TMC5160, or the motion
state machine. Everything else *requests*; nothing else acts.

Two independent sources confirm this is necessary, not theoretical:

- **Our PR review** flagged it (findings #2, #3).
- **Karl's upstream v1.10.0** independently made the same fix in the Arduino
  build, for the same stated reason — see [upstream-v1.10.0-diff.md](upstream-v1.10.0-diff.md).

### Why `sse_task` is separate

`broadcastState()` does a blocking socket `send()`. It used to run inside
`pump_task`, which is watchdog-subscribed. A stalled SSE client (weak WiFi, a
backgrounded tab) could block long enough to starve the watchdog reset and force
a **hard reset mid-stroke**, bypassing every controlled-stop path in
`motion.cpp`. This actually happened on the bench, twice.

So SSE now runs on its own task, and that task is deliberately **not**
watchdog-subscribed: blocking there is an expected network condition, not a bug
that should panic the device.

```mermaid
flowchart TB
    A["Slow / stalled SSE client"] --> B["blocking send() stalls"]
    B --> C{"Which task?"}
    C -->|"pump_task<br/>(old)"| D["watchdog starved<br/>➜ hard reset mid-motion"]
    C -->|"sse_task<br/>(now)"| E["only SSE lags<br/>motion unaffected"]

    style D fill:#f8d7da,stroke:#721c24
    style E fill:#d4edda,stroke:#155724
```

---

## 3. Shared SPI bus

The display and the TMC5160 share one bus, with software chip-select:

```mermaid
flowchart LR
    SPI(["SPI2 bus<br/>SCK=1 · MOSI=2 · MISO=3"])
    SPI --> LCD["JD9853 LCD<br/>CS = GPIO 14"]
    SPI --> TMC["TMC5160<br/>CS = GPIO 8"]
    LCD -.driven by.-> LVGLT["LVGL task"]
    TMC -.driven by.-> PUMPT["pump_task"]

    style SPI fill:#fff3cd,stroke:#856404,stroke-width:2px
```

Two different tasks, one bus. Every TMC transfer therefore:

1. takes `spi_device_acquire_bus()` for the whole window,
2. forces the display CS **high**,
3. toggles TMC CS and transfers,
4. releases the bus.

Skipping the lock lets an LVGL flush interleave mid-transfer and corrupt a
StallGuard read — i.e. silently feed the jam detector garbage. `MISO` exists
only for these reads; the display never uses it.

---

## 4. Motion state machine

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> RUNNING: Start
    IDLE --> CALIBRATING: Calibrate
    RUNNING --> STOPPING: GracefulStop
    RUNNING --> STALLED: Jam detected
    STOPPING --> IDLE: ReachedHome / StopTimeout
    CALIBRATING --> IDLE: CalibrationDone
    STALLED --> HOMING: ReturnHome
    HOMING --> IDLE: HomeDone

    note right of STALLED
        Cannot Start from here —
        must home first.
    end note
```

The transition table is encoded in `lib/autolee_logic/motor_fsm.h` and is
host-tested exhaustively (every invalid `(state, event)` pair must be rejected).

> **Known gap:** the shipped firmware still implements this inline in
> `motion.cpp` rather than calling the tested module — review finding #4, tracked
> in [PLAN.md](PLAN.md).

### Jam detection

StallGuard is read over SPI (median-of-5 to reject glitches) and compared to a
per-profile trip threshold, with a sliding counter so one spike can't trigger a
stop. Monitoring is **blanked** in windows where high load is normal:

| Window | Why blanked |
|---|---|
| Acceleration | Load spikes while ramping |
| Deceleration | Same, on the way down — *unless* jam evidence is already accumulating |
| Work zone near DOWN | Primer seating is legitimately hard |

On a confirmed jam: stop → back off → `STALLED` → operator returns home.

---

## 5. Build-time layout

| Path | Role |
|---|---|
| `main/` | `app_main.cpp`, `config.h` (all tuning constants), `globals.*` |
| `main/drivers/` | Hardware: stepper, TMC5160, display, touch |
| `main/motion/` | `motion.*` (FSM) + `motion_cmd.*` (deferred commands) |
| `main/net/` | WiFi, web server, compiled-in web UI |
| `main/ui/` | LVGL screens |
| `lib/autolee_logic/` | Pure logic — no ESP-IDF, no Arduino |
| `host_test/` | Unity suites + CMake/CTest harness (ESP-IDF's `host_test/` convention) |
| `api/` | OpenAPI + AsyncAPI + JSON Schema — the external contract |

Subdirectories under `main/` are all on `INCLUDE_DIRS`, so `#include "motion.h"`
resolves from anywhere. The grouping is navigational, not a compiler-enforced
module boundary.

---

## 6. Safety mechanisms

| Mechanism | What it buys | Verified? |
|---|---|---|
| OTA rollback | An image that fails its post-boot self-check reverts automatically | ✅ both halves, on hardware |
| Task + interrupt watchdog | A hung task resets the board rather than leaving a powered stepper frozen | ✅ genuinely tripped by 3 real bugs |
| Core dump to flash | Post-crash backtrace instead of a silent reboot | ✅ used to root-cause real bugs |
| Digest auth on all writes | Nobody on the network can start the press or flash firmware | ✅ on hardware |
| Brownout detection | Clean reset when the rail sags | ⚙️ needs the press's PSU under load |
| Jam detection / controlled stop | Brass jams only — **never** a guard for hands | ⚙️ needs the motor rig |

Configuration lives in `sdkconfig.defaults`; see
[adr/0001-build-tooling-and-platform.md](adr/0001-build-tooling-and-platform.md)
for why ESP-IDF was chosen and what each option concretely buys.

---

## See also

- [PLAN.md](PLAN.md) — phased migration checklist and what remains unverified
- [upstream-v1.10.0-diff.md](upstream-v1.10.0-diff.md) — deltas from Karl's Arduino source
- [wiring.md](wiring.md) · [bill-of-materials.md](bill-of-materials.md)
- [../CONTRIBUTING.md](../CONTRIBUTING.md) — build, test, and release workflow
