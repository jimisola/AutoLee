# Contributing to AutoLee

Thanks for helping improve AutoLee. This guide covers the project layout and how
to build, test, and release the firmware.

## Repository layout

| Path | Purpose |
|---|---|
| `main/` | The firmware: `app_main.cpp` (entry point), `config.h` (pins, speed profiles, tuning constants), and the hardware modules (`display_touch.*`, `axs5106l_touch.*`, and more as they're ported — see `docs/PLAN.md`). |
| `lib/autolee_logic/` | Pure, hardware-independent logic (endpoint math, SG filter/blanking, stall FSM, batch, log ring, calibration, state JSON, motor FSM). Shared by the firmware **and** the host tests, so tested code == shipped code. |
| `test/` | Host unit tests — one folder per module, plus the CMake+CTest harness that builds them. |
| `api/` (`openapi.yaml`, `asyncapi.yaml`, `schemas/`) | API contract (shared JSON Schema + REST/SSE specs). |
| `include/lv_conf.h` | LVGL config (found via `-D LV_CONF_INCLUDE_SIMPLE`, wired project-wide in the root `CMakeLists.txt`). |
| `CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults` | ESP-IDF build config. |
| `tools/mock_server.py` | Run the web UI on your desktop without hardware (once the web server is ported — see `docs/PLAN.md` Phase 5). |
| `docs/adr/` | Architecture decision records — why ESP-IDF, what was considered, what it costs. |
| `docs/PLAN.md` | The active migration checklist. |

## Toolchain

Targets **ESP32-C6** with a custom partition table (`partitions.csv`): nvs + otadata + dual OTA
app slots (~1.9 MB each) + a coredump partition.

```bash
# Install ESP-IDF >= 5.3: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/
. $HOME/esp/esp-idf/export.sh   # or wherever you installed it; adjust the path

idf.py set-target esp32c6   # once, per clone
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # adjust the port for your OS
```

Dependencies (LVGL, `esp_lvgl_port`) are pinned in `main/idf_component.yml` and fetched
automatically by the ESP-IDF Component Manager on first build; `dependencies.lock` is committed
for reproducibility. `managed_components/` (the downloaded source) is gitignored.

> ESP32-C6 needs **ESP-IDF >= 5.3** (FastAccelStepper's I2S-mux step driver requirement).

### Host tests

Pure logic goes in `lib/autolee_logic/` and is covered by a Unity suite in `test/test_<module>/`.
Run them (no hardware needed):

```bash
cd test
cmake -B build
cmake --build build -j
cd build && ctest --output-on-failure
```

For coverage: `cmake -B build -DAUTOLEE_COVERAGE=ON`, then run `gcovr --root .. --filter '../lib/autolee_logic/' build` from `test/`.

Keep new algorithmic logic in `lib/autolee_logic/` (not inline in `main/`) so it can be tested on
the host and reused by the firmware.

### VS Code

Install the official **ESP-IDF** extension (publisher: Espressif Systems) — it bundles the
toolchain setup, build/flash/monitor commands, and a serial port picker.

## Safety features

`sdkconfig.defaults` turns on, from the first boot:

- **OTA rollback** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) — an update that doesn't self-validate
  reverts to the last-good app automatically.
- **Task + interrupt watchdog with panic** (`CONFIG_ESP_TASK_WDT_*`, `CONFIG_ESP_INT_WDT*`) — a
  hung loop/ISR forces a reset rather than leaving a powered stepper frozen mid-stroke.
- **Core dump to flash** (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`) — a crash snapshot you can pull
  and decode instead of a silent reboot.
- **Brownout detection** (`CONFIG_ESP_BROWNOUT_DET`).

See `docs/adr/0001-build-tooling-and-platform.md` for what each of these concretely buys over the
old Arduino/PlatformIO setup, and honest calibration of which are genuinely new vs. just more
configurable.

## Versioning & releases

- The firmware version lives in `main/config.h` as `FW_VERSION` (single source of truth; read by
  the serial banner). Keep the README version history consistent when bumping.
- The CI release pipeline (build + `idf.py merge-bin` + GitHub Release) is not yet ported to
  ESP-IDF — see `docs/PLAN.md` Phase 7.

## Local hooks (optional)

Run the same formatters/linters CI uses (clang-format, ruff, yamllint, basic checks)
automatically before each commit:

```bash
pip install pre-commit
pre-commit install          # one-time, per clone
pre-commit run --all-files  # run against everything on demand
```

## Design decisions

Why ESP-IDF (and what it costs vs. the earlier PlatformIO/Arduino attempt) is recorded in
[`docs/adr/0001-build-tooling-and-platform.md`](docs/adr/0001-build-tooling-and-platform.md).

## Conventions

- [Conventional Commits](https://www.conventionalcommits.org/) for commits and PR titles.
- Work on a branch and open a PR; don't push directly to `main`.
- Don't hardcode the version anywhere except `FW_VERSION`; keep the README version history
  consistent when bumping.
