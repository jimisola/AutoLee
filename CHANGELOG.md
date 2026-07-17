# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims to
follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- PlatformIO build (`esp32-c6`) and a host unit-test env (`native`, Unity) with
  the pure logic extracted into `lib/autolee_logic/` and shared with the firmware.
- Task watchdog (`ENABLE_TASK_WDT`) that resets the board on a stalled main loop.
- OpenAPI + AsyncAPI specs sharing one JSON Schema (`schemas/state.schema.json`),
  with a CI contract check and a `state_json` golden test.
- Hardware-free web-UI mock server (`tools/mock_server.py`).
- GitHub Releases pipeline (CI-built firmware binaries) and the vendored WaveShare
  touch driver under `third_party/`.
- Tooling: clang-format, ruff, yamllint, Spectral, AsyncAPI validate, commitlint,
  pre-commit config, Dependabot, PR/issue templates.

### Changed
- Restructured the firmware from a single Arduino sketch into a PlatformIO project
  with separate translation units under `src/` (globals centralized in
  `main.cpp`, declared `extern` in `globals.h`).
- Documentation split: developer/build docs moved from `README.md` into
  `CONTRIBUTING.md`.

### Removed
- Committed firmware binaries (moved to GitHub Releases).

[Unreleased]: https://github.com/jimisola/AutoLee/compare/main...HEAD
