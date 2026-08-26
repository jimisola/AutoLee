[![License: CC BY-NC 4.0](https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc/4.0/)

# AutoLee

**Automated Lee APP conversion — ESP32-C6 firmware with touchscreen UI and web control.**

AutoLee converts a manual Lee APP into a fully automated decapping machine using a stepper motor, sensorless homing, and stallguard jam detection. It runs on a tiny 1.47" touchscreen ESP32-C6 module and can also be controlled from any phone/computer via its built-in web interface.

[![AutoLee conversion kit](https://makerworld.bblmw.com/makerworld/model/US2e07ecebe9412d/design/d100da1ec8e05572.jpg?x-oss-process=image/resize,w_1000/format,webp)](https://makerworld.com/en/models/2529369-autolee-conversion-kit)

Find the 3D-printable parts here: https://makerworld.com/en/models/2529369-autolee-conversion-kit

Support my work: https://buymeacoffee.com/kl.design

> **By K.L Design**

---

## ⚠️ SAFETY WARNING — READ BEFORE BUILDING OR OPERATING

**This machine will crush fingers and hands without breaking a sweat.** It is a motorized press driven by a NEMA 23 stepper motor with significant torque. It does not know or care if something is in the way.

- **NEVER run this machine unattended.**
- **NEVER allow children near the machine, whether it is running or not.**
- **Keep your hands and fingers away from the machine at ALL times when it is powered on.**
- **Treat it like any industrial press — it can and will cause serious injury if misused.**

The stall detection and jam protection features are designed to detect brass getting stuck in the machine — nothing more. They will **not** detect or protect your fingers and hands. They were never designed for that. And even for brass jams, they can fail, be misconfigured, or react too slowly. **Do not rely on software to protect your body.**

**The Emergency Stop button is the only thing on this machine that protects people.** It is a normally-closed switch wired in series on the incoming DC rail, upstream of everything — so pressing it cuts power to the stepper driver *and* to the controller itself. It does not depend on the firmware running, responding, or being on the right screen. Fit it, wire it as shown in [docs/wiring.md](docs/wiring.md#power), test it before you first run the machine, and keep it within reach whenever the machine is powered.

**LIABILITY DISCLAIMER:** This project is provided as-is with absolutely no warranty of any kind. The author(s) accept no responsibility or liability for any injury, damage, or loss resulting from building, modifying, or operating this machine. You build and use it entirely at your own risk.

---

## Features

### Motion & Calibration
- **Sensorless calibration** — automatically finds the UP and DOWN mechanical stops using TMC5160 StallGuard, no limit switches needed
- **Adjustable endpoints** — fine-tune UP and DOWN positions in ±1/10/100 step increments after calibration
- **Fast ramp profile** — 800,000 steps/s² accel/decel (RUN_DECEL) for maximum cruise time and StallGuard coverage
- **Safe return-home** — after a jam or stall, creeps back to UP stop using calibration speed with stall detection and retries

### Speed Profiles
- **Three preset profiles** — Slow (15 kHz, SG=350), Normal (35 kHz, SG=15), Fast (45 kHz, SG=1)
- **Per-profile StallGuard threshold** — each speed has its own SG trip value, eliminating false jams when changing speed
- **One-tap switching** — change profile from the touchscreen or web UI; speed and SG update together instantly
- **Fine-tune SG per profile** — type a value directly into text inputs on the web Configuration page, or use ±1/±5 buttons on the touch screen

### Motor Current
- **Adjustable run current** — 1,000–4,500 mA via web slider (default 3,500 mA)
- **Overcurrent warning** — values above 4,000 mA show a warning (exceeds motor rating, ensure cooling)
- **Live adjustment** — takes effect immediately, no restart needed

### Jam Detection & Protection
- **Runtime StallGuard monitoring** — reads SG2 via median-of-5 filtered SPI during operation
- **Sliding counter stall detection** — requires multiple consecutive high-SG readings to trigger (rejects transient spikes)
- **Work zone blanking** — skips SG monitoring near the DOWN endpoint where primer seating resistance is normal
- **Accel/decel blanking** — position-based and time-based SG ignore windows during speed transitions
- **Automatic backoff** — on jam detection, motor stops and backs off in the opposite direction before showing the jam screen
- **Jam recovery screen** — one-button return-home using the same proven sensorless homing as calibration; the same button cancels a home in progress

### After an Emergency Stop
Pressing the E-stop cuts the whole DC rail, so the controller reboots along with the motor. On restart the stepper's position counter reads 0 while the carriage is wherever it stopped, so the firmware restores your calibration but marks the axis **unreferenced**:

- The main screen shows **POSITION UNCONFIRMED**, and the web dashboard says the same
- **RUN and Start Batch are refused** until you press **Return Home**, which re-establishes the reference against the UP hard stop
- Your calibration, offsets, StallGuard trips and counters are *not* lost

This is expected after every E-stop — it is the machine refusing to drive to stored positions it can no longer locate, not a fault.

### Batch Run
- **Set a target count** (1–9999) and the machine stops automatically when done
- **Progress display** — remaining count shown on the main screen during batch operation
- **Works with all profiles** — batch runs at whatever speed profile is active

### Counter
- **Stroke counter** — counts each completed down-up cycle, displayed large on the main screen. The panel's 4-digit field saturates at `9999+`; the web dashboard shows the exact figure
- **Resettable** from touch UI or web interface
- **Lifetime cycle count** — a separate, uncapped total that Reset Counter does not clear, persisted across reboots and shown on the web Diagnostics page

### Touch UI (172×320 LVGL)
- **Main screen** — Counter, active speed profile indicator, calibration warning, batch remaining, buttons for Batch Run and Settings
- **Settings screen** — Calibrate (doubles as Cancel while a calibration runs), Configuration, Reset Counter
- **Configuration screen** — Speed Profile, Endpoints, Stall Guard, WiFi Info
- **Speed Profile screen** — Three buttons with green highlight on active, info card showing Hz + SG
- **Endpoints (tuning) screen** — Raw and effective endpoint values, buttons to edit UP and DOWN
- **Stall Guard screen** — Adjust SG trip for the active profile with ±1/±5 buttons
- **Batch Run screen** — Set target count with ±1/10/100 buttons, start batch
- **Jam screen** — Warning display with one-button return home; the same button cancels a home in progress
- **WiFi screen** — Shows connected SSID and IP address, Reset WiFi button to clear credentials and reboot

### Web Interface (6-page layout)
- **Full control from any browser** — responsive dark-theme UI works on phone and desktop
- **Real-time updates** — Server-Sent Events (SSE). State is polled every 250 ms and pushed only when it actually changed, plus a heartbeat every 8 s so the page can tell "nothing changed" from "connection died" — a dropout greys the values out and says so
- **Main page** — status, counter, RUN/STOP, calibrate, reset counter, speed profile selector, batch run controls
- **Configuration page** — motor current slider (1,000–4,500 mA with overcurrent warning), endpoint tuning with collapsible offsets, per-profile StallGuard text inputs, work zone adjustment
- **Log page** — full-height 500-line scrollable log with real-time streaming, level filter and clear button
- **Diagnostics page** — build identity, uptime, reset reason, heap, and the persisted lifetime health counters
- **Firmware page** — drag-and-drop OTA .bin upload with progress bar
- **WiFi page** — shows connection status, SSID, and IP address; change credentials, reset to AP mode
- **Footer navigation** — blue text links on every page to jump between all six pages

### WiFi & Networking
- **Auto-connect** — attempts saved credentials on boot, falls back to AP if it fails
- **Captive portal** — WPA2 AP mode (`AutoLee-Setup`, per-device key shown on the LCD as a join QR) with DNS redirect so any joined device gets the setup page automatically
- **Works with no network at all** — press Skip on the setup screen and the press is fully usable from the touch UI and from a phone on its own AP, with no web password required; being able to read the AP key off the screen *is* the access control. A web password only becomes mandatory once the device has joined a WiFi network for the first time
- **Network scanner** — scans available WiFi networks and presents them in a dropdown
- **Locked-out recovery** — `Config → Reset Pwd` on the touch UI (two-tap confirm) restores the factory-default web password, so a forgotten one no longer means an `erase-flash` over USB that also discards the calibration. Pressing a button on the panel is the gate: it proves you are standing at the press. The restored default immediately re-arms the force-change rule, so the press still refuses to run, calibrate or accept firmware until a real password is set

---

## Build It

| | |
|---|---|
| **[Bill of Materials — 24V](docs/24V/bill-of-materials.md) · [36V](docs/36V/bill-of-materials.md)** | Every part needed, with links |
| **[Wiring](docs/wiring.md)** | Pin-by-pin connections + wiring diagrams (both variants) |

Read the safety warning above first.

---

## Software Setup

### Dependencies

Built with **ESP-IDF** (native `idf.py`). LVGL and
`esp_lvgl_port` are fetched automatically by the ESP-IDF Component Manager
(pinned in `main/idf_component.yml`); the TMC5160 and AXS5106L touch drivers
are implemented directly under `main/drivers/` against ESP-IDF's SPI/I2C APIs — no
offline/manually-installed libraries needed.

### Build & Flash

Requires **ESP-IDF >= 6.0** (CI and releases pin v6.0.2) — see Espressif's
[installation instructions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html#installation).

```bash
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # adjust the port for your OS
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the full toolchain setup, host-test
instructions, and repo layout, and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for how the firmware fits together (task model, shared SPI bus, motion FSM) —
with [docs/FLOWS.md](docs/FLOWS.md) for the sequence diagrams that go with it.

## Flash Pre-Compiled Binary

If you don't want to build the firmware yourself, you can flash a pre-built binary directly to the ESP32-C6 using a web browser.

### What You Need

- A **Chrome** or **Edge** browser (Web Serial is not supported in Firefox or Safari)
- A **USB-C cable** connected to the Waveshare ESP32-C6 board
- The factory firmware `.bin` file from the latest [GitHub Release](https://github.com/jimisola/AutoLee/releases)

### Steps

1. Download the latest `autolee-<version>-factory.bin` from [GitHub Releases](https://github.com/jimisola/AutoLee/releases)
2. Open the [**Espressif Web Flasher**](https://espressif.github.io/esptool-js/) in Chrome or Edge
3. Click **Connect** and select the port for your ESP32-C6
4. In the **Program** section, enter **`0x0`** in the Flash Address field
5. Click the file picker next to the address and select the downloaded `.bin` file
6. Set Flash Mode as **dio** and Flash Size as **4MB**
7. Click **Program**
8. Wait for flashing to complete — progress will show in the Console section at the bottom
9. After programming is complete click the **rst** button on the esp32 board to reboot
10. System should now be online

**Tip:** If the board doesn't show up as a COM port, hold the **BOOT** button on the Waveshare board while plugging in USB, then release after connecting. You may also need to install the [CH343 USB driver](https://www.wch-ic.com/downloads/CH343SER_ZIP.html) if your OS doesn't recognize the board.

### Updating Firmware Later

Once AutoLee is on your WiFi, go to the web UI → **Firmware** page and drag-and-drop `autolee-<version>-ota.bin` (not the factory binary). The factory binary is only needed for the initial USB flash — it also erases NVS, so it would wipe your calibration, WiFi credentials and web password.

### OTA Updates

After first flash, firmware can be updated two ways:

- **Web UI** — open the AutoLee web interface, go to the Firmware page, drag and drop a `.bin` file
---

## Configuration

Key constants are in `config.h`:

| Constant | Default | Description |
|---|---|---|
| `profiles[0]` (Slow) | 15,000 Hz / SG 350 | Low speed, high SG threshold — max torque for tough primers |
| `profiles[1]` (Normal) | 35,000 Hz / SG 15 | Balanced speed and sensitivity |
| `profiles[2]` (Fast) | 45,000 Hz / SG 1 | High speed, very sensitive stall detection |
| `RUN_CURRENT_MA` | 3,500 mA | Motor run current (adjustable 1,000–4,500 via web UI) |
| `RUN_DECEL` | 800,000 | Accel/decel rate (steps/s²) |
| `CAL_CURRENT_MA` | 3,200 | Calibration current (mA, fixed) |
| `CAL_SPEED_HZ` | 8,000 | Calibration speed (Hz) |
| `SG_WORK_ZONE_STEPS` | 5,500 | SG blanking zone near DOWN endpoint |
| `DOWN_OFFSET_DEFAULT` | −500 | Default DOWN endpoint offset after calibration |
| `CAL_SGT` | −1 | StallGuard sensitivity for calibration |

---

## API Reference

The HTTP + SSE contract lives in [`api/`](api/) and is validated in CI.

**Browse it rendered:**

| Spec | Source | Rendered |
|---|---|---|
| REST (`/api/v1/*`) | [`api/openapi.yaml`](api/openapi.yaml) | [Swagger UI](https://petstore.swagger.io/?url=https://raw.githubusercontent.com/jimisola/AutoLee/feat/esp-idf/api/openapi.yaml) · [Redoc](https://redocly.github.io/redoc/?url=https://raw.githubusercontent.com/jimisola/AutoLee/feat/esp-idf/api/openapi.yaml) |
| SSE (`/api/v1/events`) | [`api/asyncapi.yaml`](api/asyncapi.yaml) | [AsyncAPI Studio](https://studio.asyncapi.com/?url=https://raw.githubusercontent.com/jimisola/AutoLee/feat/esp-idf/api/asyncapi.yaml) |
| State object | [`api/schemas/state.schema.json`](api/schemas/state.schema.json) | (referenced by both) |

All state-changing endpoints and the OTA upload require **HTTP Digest auth**; reads
(`GET /api/v1/state`, the dashboard, the SSE stream) are open.

> The rendered links currently point at the `feat/esp-idf` branch so they can be
> checked before merge; switch them to `main` once this lands.

---

## Version History

See [CHANGELOG.md](CHANGELOG.md).

---

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License](https://creativecommons.org/licenses/by-nc/4.0/).

You are free to use, modify, and share this work for personal, non-commercial purposes, provided you give appropriate credit. Commercial use is not permitted without prior written permission.

Commercial use — including selling devices, kits, or services based on this project — is prohibited without prior written permission from the author.

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND. USE AT YOUR OWN RISK.

Copyright (c) 2025-2026 K.L Design
