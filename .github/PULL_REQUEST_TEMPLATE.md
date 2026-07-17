<!-- Use a Conventional Commits style title, e.g. "feat: ..." / "fix: ..." -->

## Summary

<!-- What does this change and why? -->

## Checklist

- [ ] `pio run -e esp32-c6` builds
- [ ] `pio test -e native` passes
- [ ] Lint/format clean (`clang-format`, `ruff`) — or `pre-commit run --all-files`
- [ ] Docs updated if behavior/paths changed (README / CONTRIBUTING)
- [ ] If the API state payload changed: updated `state_json`, `schemas/state.example.json`, and `schemas/state.schema.json` together

## Hardware validation

<!-- Firmware changes can't be fully verified in CI. For anything touching
     motion / UI / web / init, confirm on the board: -->

- [ ] Flashed and smoke-tested on hardware (calibration, a run cycle, jam/return-home, UI speed-profile change, web UI) — **or** N/A (tooling/docs only)
