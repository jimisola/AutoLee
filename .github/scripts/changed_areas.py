#!/usr/bin/env python3
"""Say which areas a pull request touches, so required jobs can skip their work.

    changed_areas.py >> "$GITHUB_OUTPUT"

Prints `firmware=` and `contracts=` as `true` or `false`. The required jobs
keep running and reporting under their names; only their steps are skipped, and
only on an explicit `false`.

Conservative by construction: a path no rule names counts as touching
everything, and so does any event other than `pull_request` (push to main and
release.yml's `workflow_call` always run the lot). For a pull request the diff
is the checked-out merge commit against its first parent - exactly what the PR
adds to the base it would merge into - so the checkout needs `fetch-depth: 2`.
"""

from __future__ import annotations

import os
import subprocess
import sys

AREAS = ("firmware", "contracts")
ALL = frozenset(AREAS)
NONE: frozenset[str] = frozenset()
FIRMWARE = frozenset({"firmware"})

# First match wins. A trailing `/` is a directory prefix, anything else an
# exact path.
RULES: list[tuple[str, frozenset[str]]] = [
    # CI itself, including this script.
    (".github/", ALL),
    # The contract, and host_test/ reads api/schemas/state.example.json.
    ("api/", ALL),
    (".spectral.yaml", frozenset({"contracts"})),
    # Everything the firmware build or the host tests compile or read.
    ("main/", FIRMWARE),
    ("lib/", FIRMWARE),
    ("include/", FIRMWARE),
    ("host_test/", FIRMWARE),
    ("CMakeLists.txt", FIRMWARE),
    ("dependencies.lock", FIRMWARE),
    ("partitions.csv", FIRMWARE),
    ("sdkconfig.defaults", FIRMWARE),
    ("tools/app_desc.py", FIRMWARE),
    # Checked by lint.yml, whose jobs always run, or by nothing.
    ("tools/", NONE),
    ("docs/", NONE),
    (".clang-format", NONE),
    (".commitlintrc.yml", NONE),
    (".gitignore", NONE),
    (".mcp.json", NONE),
    (".pre-commit-config.yaml", NONE),
    (".yamllint", NONE),
    ("CHANGELOG.md", NONE),
    ("CLAUDE.md", NONE),
    ("CONTRIBUTING.md", NONE),
    ("LICENSE", NONE),
    ("README.md", NONE),
    ("RELEASING.md", NONE),
    ("cliff.toml", NONE),
    ("renovate.json", NONE),
]


def areas_for(path: str) -> frozenset[str]:
    for rule, areas in RULES:
        if path == rule or (rule.endswith("/") and path.startswith(rule)):
            return areas
    return ALL


def touched(paths: list[str]) -> dict[str, bool]:
    hit: set[str] = set()
    for path in paths:
        hit |= areas_for(path)
    return {area: area in hit for area in AREAS}


def changed_paths() -> list[str] | None:
    """The PR's paths, or None when they cannot be known for certain."""
    if os.environ.get("GITHUB_EVENT_NAME") != "pull_request":
        return None
    # A checkout that is not the merge commit has no first parent to diff
    # against that means "the base".
    if subprocess.run(
        ["git", "rev-parse", "-q", "--verify", "HEAD^2"],
        capture_output=True,
        check=False,
    ).returncode:
        return None
    # --no-renames: a file moved out of main/ is a firmware change too.
    out = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "HEAD^1", "HEAD"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return out.splitlines()


def main() -> int:
    paths = changed_paths()
    if paths is None:
        result = dict.fromkeys(AREAS, True)
        print("Not a pull request merge commit - running everything.", file=sys.stderr)
    else:
        result = touched(paths)
        for path in paths:
            print(
                f"  {path}: {', '.join(sorted(areas_for(path))) or '-'}",
                file=sys.stderr,
            )
    for area, value in result.items():
        print(f"{area}={'true' if value else 'false'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
