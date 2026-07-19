# Contributing to AutoLee

Thanks for helping improve AutoLee. This guide covers the project layout and how
to build, test, and release the firmware.

## Repository layout

| Path | Purpose |
|---|---|
| `main/` | The firmware, grouped by concern (see below). |
| `lib/autolee_logic/` | Pure, hardware-independent logic (endpoint math, SG filter/blanking, stall FSM, batch, log ring, calibration, state JSON, motor FSM). Shared by the firmware **and** the host tests, so tested code == shipped code. |
| `host_test/` | Host unit tests — one folder per module, plus the CMake+CTest harness that builds them. |
| `api/` (`openapi.yaml`, `asyncapi.yaml`, `schemas/`) | API contract (shared JSON Schema + REST/SSE specs). |
| `include/lv_conf.h` | LVGL config (found via `-D LV_CONF_INCLUDE_SIMPLE`, wired project-wide in the root `CMakeLists.txt`). |
| `CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults` | ESP-IDF build config. |
| `tools/mock_server.py` | Run the web UI on your desktop without hardware. |
| `docs/` | [Wiring](docs/wiring.md), [Bill of Materials](docs/bill-of-materials.md), [PLAN.md](docs/PLAN.md) (migration checklist), `adr/` (architecture decision records), and the [upstream v1.10.0 diff](docs/upstream-v1.10.0-diff.md). |

### Inside `main/`

Grouped by concern. All groups are also on `INCLUDE_DIRS` (see `main/CMakeLists.txt`), so
`#include "motion.h"` resolves from anywhere — the grouping is navigational, not a
compiler-enforced module boundary.

| Path | Purpose |
|---|---|
| `main/` | `app_main.cpp` (entry point), `config.h` (pins, speed profiles, tuning constants), `globals.{h,cpp}` (cross-module mutable state). |
| `main/drivers/` | `display_touch`, `axs5106l_touch`, `tmc5160_hal`, `tmc5160_ctrl`, `stepper`, `stepper_motor_encoder`. |
| `main/motion/` | `motion.{h,cpp}` — the safety-critical run / jam-detection / calibration / homing state machine. |
| `main/net/` | `wifi_mgr`, `web_server`, `index_html.h` (the compiled-in web UI). |
| `main/ui/` | `ui_touch` — the on-device LVGL UI. |

### Why `host_test/`, not `test/` or `test_apps/`

ESP-IDF uses `host_test/` for tests that run on the build machine and `test_apps/` for
on-target applications you flash to a chip (77 components use `test_apps/`, 9 use
`host_test/` in v5.3.2). Ours are host tests, so `host_test/` it is — and `test_apps/`
stays free should real on-target tests ever be added.

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

> ESP32-C6 needs **ESP-IDF >= 5.3**, enforced by `main/idf_component.yml`'s `idf: ">=5.3"` —
> the version this port was built and tested against. (An earlier note credited
> FastAccelStepper's I2S-mux driver; that library was dropped for a native RMT+PCNT
> stepper — see ADR 0001.)

### Host tests

Pure logic goes in `lib/autolee_logic/` and is covered by a Unity suite in `host_test/test_<module>/`.
Run them (no hardware needed):

```bash
cd host_test
cmake -B build
cmake --build build -j
cd build && ctest --output-on-failure
```

For coverage: `cmake -B build -DAUTOLEE_COVERAGE=ON`, then run `gcovr --root .. --filter '../lib/autolee_logic/' build` from `host_test/`.

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
- The CI release pipeline (build + `idf.py merge-bin` + GitHub Release) lives in
  `.github/workflows/release.yml` and fires on `release: published`. A version guard fails the
  build if the tag doesn't match `FW_VERSION`.

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
