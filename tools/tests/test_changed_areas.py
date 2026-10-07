"""Guards on .github/scripts/changed_areas.py, which lets required jobs skip work.

A wrong `false` there turns a required check green without running it, so the
cases that matter are the ones that must NOT skip.
"""

from __future__ import annotations

import importlib.util
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location(
    "changed_areas", ROOT / ".github" / "scripts" / "changed_areas.py"
)
assert _spec and _spec.loader
changed_areas = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(changed_areas)

touched = changed_areas.touched


def test_docs_only_skips_everything() -> None:
    assert touched(["README.md", "docs/FLOWS.md", "docs/36V/AutoLeeWiringDiagram.png"]) == {
        "firmware": False,
        "contracts": False,
    }


def test_firmware_change_runs_firmware_only() -> None:
    assert touched(["main/motion/motion.cpp"]) == {"firmware": True, "contracts": False}
    assert touched(["main/idf_component.yml"]) == {"firmware": True, "contracts": False}
    assert touched(["host_test/test_step_ramp/test_step_ramp.cpp"])["firmware"]


@pytest.mark.parametrize(
    "path",
    [
        ".github/workflows/build.yml",
        ".github/scripts/changed_areas.py",
        "api/openapi.yaml",
        # host_test/ reads it, so an api/ change is a firmware-area change too.
        "api/schemas/state.example.json",
        # Not named by any rule.
        "some/new/dir/file.c",
        "Makefile",
    ],
)
def test_seams_and_unknown_paths_run_everything(path: str) -> None:
    assert touched([path]) == {"firmware": True, "contracts": True}


def test_prefix_rules_match_directories_not_name_prefixes() -> None:
    # "main/" must not match "mainline.txt"; unknown, so it runs everything.
    assert touched(["mainline.txt"]) == {"firmware": True, "contracts": True}


def test_any_path_that_runs_wins_over_docs() -> None:
    assert touched(["README.md", "lib/autolee_logic/command_gate.h"])["firmware"]


def _git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ("git", *args), cwd=repo, check=True, capture_output=True, text=True
    ).stdout


def _commit(repo: Path, path: str, msg: str) -> None:
    (repo / path).parent.mkdir(parents=True, exist_ok=True)
    (repo / path).write_text(msg)
    _git(repo, "add", path)
    _git(repo, "commit", "-qm", msg)


@pytest.fixture
def merge_repo(tmp_path: Path) -> Path:
    """A repo whose HEAD is a PR merge commit: base gained a firmware change,
    the PR changed only docs. Only the PR's side may count."""
    repo = tmp_path / "repo"
    repo.mkdir()
    _git(repo, "init", "-q", "-b", "main")
    _git(repo, "config", "user.email", "t@example.com")
    _git(repo, "config", "user.name", "T")
    # As in test_cliff_config.py: never inherit the maintainer's signing config.
    _git(repo, "config", "commit.gpgsign", "false")
    _commit(repo, "README.md", "init")
    _git(repo, "switch", "-qc", "pr")
    _commit(repo, "docs/x.md", "docs")
    _git(repo, "switch", "-q", "main")
    _commit(repo, "main/app_main.cpp", "firmware on base")
    _git(repo, "merge", "-q", "--no-ff", "pr", "-m", "m")
    return repo


def _run(repo: Path, event: str) -> dict[str, str]:
    out = subprocess.run(
        ["python3", str(ROOT / ".github" / "scripts" / "changed_areas.py")],
        cwd=repo,
        env={"GITHUB_EVENT_NAME": event, "PATH": "/usr/bin:/bin"},
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return dict(line.split("=", 1) for line in out.splitlines())


def test_merge_commit_diffs_against_its_base(merge_repo: Path) -> None:
    assert _run(merge_repo, "pull_request") == {"firmware": "false", "contracts": "false"}


def test_non_pull_request_events_run_everything(merge_repo: Path) -> None:
    for event in ("push", "workflow_call", "workflow_dispatch"):
        assert _run(merge_repo, event) == {"firmware": "true", "contracts": "true"}


def test_a_head_that_is_not_a_merge_runs_everything(merge_repo: Path) -> None:
    _git(merge_repo, "checkout", "-q", "HEAD^1")
    assert _run(merge_repo, "pull_request") == {"firmware": "true", "contracts": "true"}
