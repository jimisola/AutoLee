# Contributing to AutoLee

Thanks for helping improve AutoLee. This guide covers the project layout and how
to build, test, and release the firmware.

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | The firmware: `main.cpp` (composition root — globals, `setup()`, `loop()`) plus the hardware modules `motion.cpp`, `ui_touch.cpp`, `web_server.cpp`, `wifi_ota.cpp`, and the shared headers `config.h` / `globals.h`. Compiled as separate translation units; every shared global is `extern` in `globals.h` and defined once in `main.cpp`. |
| `include/lv_conf.h` | LVGL config (found via `-D LV_CONF_INCLUDE_SIMPLE`). |
| `lib/autolee_logic/` | Pure, hardware-independent logic (endpoint math, SG filter/blanking, stall FSM, batch, log ring, calibration, state JSON, motor FSM). Shared by the firmware **and** the host tests, so tested code == shipped code. |
| `test/` | Host (native) unit tests — one folder per module, run with `pio test -e native`. |
| `api/` (`openapi.yaml`, `asyncapi.yaml`, `schemas/`) | API contract (shared JSON Schema + REST/SSE specs). |
| `third_party/esp_lcd_touch_axs5106l/` | Vendored WaveShare touch driver (not in the Library Manager). See its README for licensing. |
| `tools/mock_server.py` | Run the web UI on your desktop without hardware. |
| `platformio.ini` | Build config: `esp32-c6` firmware env + `native` test env. |
| `.github/workflows/` | CI (build + tests + API contract) and the release pipeline. |

Firmware binaries are **not** committed — they are built by CI and attached to
each [GitHub Release](https://github.com/jimisola/AutoLee/releases).

## Toolchain

Targets **ESP32-C6** with the **Minimal SPIFFS** partition. The platform and all
library versions are pinned in [`platformio.ini`](platformio.ini) — the single
source of truth, used by local builds, CI, and releases. Don't duplicate versions
elsewhere.

> ⚠️ Use the **ESP32Async** forks of the async libraries (`ESP Async WebServer`,
> `Async TCP`) — other forks with similar names won't compile against a recent
> esp32 core (this is already encoded in `platformio.ini`'s `lib_deps`).

## Build & test with PlatformIO (recommended)

All versions and board settings are pinned in `platformio.ini`; the vendored
driver and `lv_conf.h` are wired in automatically — no manual library copying.

### Command line

```bash
# Install PlatformIO Core: https://docs.platformio.org/en/latest/core/installation/
pio run  -e esp32-c6      # build the firmware (app + merged .bin)
pio test -e native        # run the host unit tests (no hardware needed)
pio run  -e esp32-c6 -t upload   # flash over USB
pio device monitor -b 115200     # serial monitor
```

### VS Code + PlatformIO (GUI)

If you prefer a GUI (or don't want to touch the command line):

1. Install **[VS Code](https://code.visualstudio.com/)**.
2. Open the **Extensions** panel (square icon in the sidebar) and install
   **"PlatformIO IDE"** (publisher: *PlatformIO*). It bundles its own toolchain —
   you do **not** need the Arduino IDE or a separate PlatformIO install. (When you
   open this repo, VS Code also offers it automatically via
   `.vscode/extensions.json`.)
3. **File → Open Folder…** and choose the repository root (the folder that
   contains `platformio.ini`).
4. *First open only:* PlatformIO reads `platformio.ini` and downloads the ESP32
   platform + the pinned libraries. Progress shows in the bottom status bar; it
   takes a few minutes and is cached afterwards.
5. Use the **PlatformIO toolbar** in the blue status bar at the bottom — ✓ **Build**,
   → **Upload** (flash over USB), 🔌 **Serial Monitor** — or open the PlatformIO
   sidebar (the alien-head icon) → **Project Tasks**:
   - `esp32-c6` → *Build* / *Upload* / *Monitor*
   - `native` → *Test* (runs the host unit tests)
6. To flash, connect the WaveShare board over **USB-C**, then press **Upload**.

If the board doesn't show up as a serial port, pick the right port in the
PlatformIO toolbar, and by platform:

- **Linux (Ubuntu):** the `ch341` driver is in the kernel, so it usually works.
  Add yourself to the serial group once — `sudo usermod -aG dialout $USER` — then
  log out/in. If the port keeps disconnecting, Ubuntu's `brltty` may be grabbing
  it: `sudo apt remove brltty`.
- **macOS / Windows:** install the WCH **CH34x** USB driver.

Build outputs are under `.pio/build/esp32-c6/`:

- `firmware.merged.bin` — full-flash image, flashed at offset `0x0`
  (the `..._merged.bin` release asset).
- `firmware.bin` — app-only image, used for OTA updates
  (the `..._update.bin` release asset).

### Writing tests

Pure logic goes in `lib/autolee_logic/` and is covered by a Unity suite in
`test/test_<module>/`. Keep new algorithmic logic in that library (not inline in
the sketch) so it can be tested on the host and reused by the firmware.

### Web UI without hardware

Preview and develop the embedded web UI on your desktop — it serves the real HTML
(extracted from `src/web_server.cpp`) and fakes the API + SSE with schema-valid
state:

```bash
python tools/mock_server.py    # http://localhost:8080
```

### API specs & contract

The HTTP/SSE API is described by [`api/openapi.yaml`](api/openapi.yaml) (REST) and
[`api/asyncapi.yaml`](api/asyncapi.yaml) (the `/events` SSE stream). Both reference one
JSON Schema, [`api/schemas/state.schema.json`](api/schemas/state.schema.json), which is
the single source of truth for the state payload. The `state_json` module emits
exactly that shape; the `test_state_json` golden test and the CI contract check
(`api/schemas/state.example.json` vs the schema) keep firmware and spec in sync — so
**if you change the state payload, update the module, the example, and the schema
together.**

> **Note on the Arduino IDE:** the firmware is now a **PlatformIO project**
> (`src/` compilation units), not a single-file Arduino sketch, so it no longer
> opens directly in the Arduino IDE. Use PlatformIO (CLI or the VS Code extension)
> as shown above — `platformio.ini` pins everything and wires in the vendored
> driver and `lv_conf.h` automatically.

## Safety features

- **Task watchdog** (`ENABLE_TASK_WDT` in `config.h`): resets the board if the
  main loop stalls. The blocking calibration/homing loops feed it via
  `wdt_feed()`. Set `ENABLE_TASK_WDT 0` to disable during bring-up.
- **OTA rollback** (planned): true bootloader-level auto-rollback needs an
  `sdkconfig` option not available with the precompiled Arduino libraries; it is
  a follow-up that would come with an ESP-IDF-component build.

## Versioning & releases

- The firmware version lives in `src/config.h` as `FW_VERSION` (single source
  of truth; read by the serial banner and the CI release guard).
- To cut a release: bump `FW_VERSION`, merge it, then publish a GitHub Release
  with a tag of the form `vX.Y` **matching** `FW_VERSION`. CI validates the match
  and fails the build otherwise, then attaches `AutoLee_vX.Y_merged.bin` and
  `AutoLee_vX.Y_update.bin`.

## Local hooks (optional)

Run the same formatters/linters CI uses (clang-format, ruff, yamllint, basic
checks) automatically before each commit:

```bash
pip install pre-commit
pre-commit install          # one-time, per clone
pre-commit run --all-files  # run against everything on demand
```

Optional — CI enforces the same checks — but it catches issues before you push.

## Design decisions

Why PlatformIO + the pioarduino platform (and the ESP‑IDF future direction, exit ramps,
and library‑support analysis) are recorded in
[`docs/adr/0001-build-tooling-and-platform.md`](docs/adr/0001-build-tooling-and-platform.md).

## Conventions

- [Conventional Commits](https://www.conventionalcommits.org/) for commits and PR titles.
- Work on a branch and open a PR; don't push directly to `main`.
- Don't hardcode the version anywhere except `FW_VERSION`; keep the README
  version history consistent when bumping.
