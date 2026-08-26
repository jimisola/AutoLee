## Description

<!-- Summary of the change and which issue it addresses. -->

Fixes #

## Safety

<!-- Delete this section if the change cannot affect motion.

This firmware drives a press that can crush hands. If this PR touches
main/motion/, main/drivers/stepper*, main/drivers/tmc5160*, or any stop /
backoff / homing / StallGuard path, say what you changed and how you convinced
yourself the safety semantics still hold. "No behaviour change" is a fine
answer — state it. -->

## Checklist

- [ ] PR title follows [Conventional Commits](https://www.conventionalcommits.org/)
      (enforced by `check-semantic-pr.yml`; it becomes the squash commit subject)
- [ ] I have read [CONTRIBUTING.md](../CONTRIBUTING.md)
- [ ] Host tests pass locally (`cd host_test && cmake -B build && cmake --build build -j && cd build && ctest`)
- [ ] `idf.py build` succeeds
- [ ] Verified on real hardware, or marked below as not hardware-affecting
- [ ] I have updated `CLAUDE.md` / `docs/ARCHITECTURE.md` if this changes
      architecture, a module boundary, or a subsystem described there
- [ ] `api/openapi.yaml`, `api/asyncapi.yaml` and `api/schemas/state.schema.json`
      are in sync if this changes the `/api/v1/*` contract or `state_json`
