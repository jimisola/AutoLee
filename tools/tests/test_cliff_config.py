"""Guards on cliff.toml — the file that decides the released version number.

`release.yml` calls `git-cliff --bumped-version` when its `version` input is
left empty, and that answer becomes the tag, which becomes the string the
firmware reports as `esp_app_desc_t.version`. Releases are immutable (the
workflow refuses a tag that already exists), so a wrong answer here ships a
permanently wrong version — and cliff.toml is otherwise only exercised by
cutting a release.

These run the *real* git-cliff binary against throwaway repos rather than
reading cliff.toml as text: the failure being guarded against is a config that
is correct in theory and wrong in fact, which only executing it can catch.

Skipped when git-cliff is not installed. CI installs the pinned one via
`.github/actions/setup-git-cliff`; a contributor without it still gets the rest
of the suite.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
CLIFF_CONFIG = ROOT / "cliff.toml"

pytestmark = pytest.mark.skipif(shutil.which("git-cliff") is None, reason="git-cliff not installed")


def _git(repo: Path, *args: str) -> None:
    subprocess.run(("git", *args), cwd=repo, check=True, capture_output=True)


def _repo(tmp_path: Path, base_tag: str, commits: list[str]) -> Path:
    """A throwaway git repo tagged ``base_tag`` with ``commits`` on top."""
    repo = tmp_path / "repo"
    repo.mkdir()
    _git(repo, "init", "-q", "-b", "main", ".")
    _git(repo, "config", "user.email", "t@example.com")
    _git(repo, "config", "user.name", "T")
    # Never inherit the maintainer's signing config — it makes every commit
    # here fail, while CI runners (which configure no signing) would pass.
    _git(repo, "config", "commit.gpgsign", "false")
    _git(repo, "config", "tag.gpgsign", "false")

    (repo / "base.txt").write_text("base\n", encoding="utf-8")
    _git(repo, "add", "-A")
    _git(repo, "commit", "-q", "-m", "chore: base")
    _git(repo, "tag", base_tag)

    for i, subject in enumerate(commits):
        (repo / f"f{i}.txt").write_text("x\n", encoding="utf-8")
        _git(repo, "add", "-A")
        _git(repo, "commit", "-q", "-m", subject)
    return repo


def _cliff(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git-cliff", "--config", str(CLIFF_CONFIG), *args],
        cwd=repo,
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def _bumped_version(repo: Path) -> str:
    return _cliff(repo, "--bumped-version")


def _changelog(repo: Path) -> str:
    return _cliff(repo, "--unreleased", "--offline")


@pytest.mark.parametrize(
    ("base", "subject", "expected"),
    [
        ("1.2.3", "fix: a bug", "1.2.4"),
        ("1.2.3", "feat: a feature", "1.3.0"),
        ("1.2.3", "feat!: a breaking change", "2.0.0"),
        ("1.2.3", "docs: a doc", "1.2.4"),
        ("1.2.3", "refactor: move a thing", "1.2.4"),
        ("2.0.0", "feat!: a breaking change", "3.0.0"),
        ("1.9.0", "feat: a feature", "1.10.0"),
        # ci/build/tools are `skip = true` in cliff.toml, so a range of only
        # those bumps to nothing: git-cliff returns the tag that already
        # exists and release.yml's "tag is free" check then rejects the run.
        # Deliberate — such a release holds nothing for whoever flashes the
        # board. Pass a version explicitly to override.
        ("1.2.3", "ci: tweak a workflow", "1.2.3"),
        ("1.2.3", "build: bump a dependency", "1.2.3"),
        ("1.2.3", "tools: tidy a script", "1.2.3"),
    ],
)
def test_bump_mapping_is_what_the_docs_claim(
    tmp_path: Path, base: str, subject: str, expected: str
) -> None:
    """RELEASING.md publishes this table and a maintainer picks the release
    version from it.
    """
    assert _bumped_version(_repo(tmp_path, base, [subject])) == expected


def test_breaking_always_bump_major_is_on(tmp_path: Path) -> None:
    """`breaking_always_bump_major = true` in cliff.toml's `[bump]` only has
    an observable effect below 1.0.0 — from any 1.x base a `feat!:` takes the
    major either way, so none of the rows above can detect the flag being
    flipped. This asserts it directly, from a 0.x base AutoLee will never see
    again: with the flag off, git-cliff applies the 0.x carve-out and answers
    0.10.0, keeping the project pre-1.0 forever.
    """
    assert _bumped_version(_repo(tmp_path, "0.9.0", ["feat!: a breaking change"])) == "1.0.0"


@pytest.mark.parametrize("subject", ["ci!: drop a runner", "build!: require ESP-IDF 6.0"])
def test_a_breaking_skipped_type_still_bumps_and_still_ships(tmp_path: Path, subject: str) -> None:
    """`protect_breaking_commits = true` overrides the ci/build/tools skip for
    `!:`. Without it a change that breaks a user's ability to build would be
    both invisible in the notes and unable to trigger a release at all.
    """
    repo = _repo(tmp_path, "1.2.3", [subject])
    assert _bumped_version(repo) == "2.0.0"
    assert "**BREAKING**" in _changelog(repo)


def test_the_highest_ranked_commit_in_a_range_wins(tmp_path: Path) -> None:
    """The bump is decided by the whole range, not the newest commit — a
    `feat!:` early in a release cycle must still take the major however many
    ordinary fixes land after it.
    """
    repo = _repo(
        tmp_path,
        "1.2.3",
        ["feat!: a breaking change", "fix: a bug", "docs: a doc", "ci: a workflow"],
    )
    assert _bumped_version(repo) == "2.0.0"


@pytest.mark.parametrize("stray", ["v2.0.0", "2.0", "release-3.0.0"])
def test_tag_pattern_ignores_a_tag_that_is_not_bare_semver(tmp_path: Path, stray: str) -> None:
    """cliff.toml's `tag_pattern` is anchored and fully specified for this
    reason. git-cliff matches it unanchored, so the obvious `[0-9].*` would
    also match any tag merely *containing* a digit — including the `v2.0.0`
    and `2.0` forms release.yml rejects. A stray one would silently become
    the release boundary and the next version would be computed from it.
    """
    repo = _repo(tmp_path, "1.2.3", ["feat: a feature"])
    _git(repo, "tag", stray)

    assert _bumped_version(repo) == "1.3.0"


def test_ci_and_build_are_absent_from_the_notes(tmp_path: Path) -> None:
    """The skipped types are for maintainers, not for whoever flashes the
    board. They must not reach the release page even when a releasable commit
    drags them into the range.
    """
    changelog = _changelog(
        _repo(tmp_path, "1.2.3", ["feat: a feature", "ci: tweak a workflow", "build: bump dep"])
    )
    assert "A feature" in changelog
    assert "Tweak a workflow" not in changelog
    assert "Bump dep" not in changelog


def test_features_sort_above_bug_fixes(tmp_path: Path) -> None:
    """The `<!-- N -->` prefixes on the group names exist only to force this
    order (and are stripped by `striptags` before rendering). Drop them and
    the groups sort alphabetically, burying Features under Bug Fixes —
    cliff.toml orders them for someone deciding whether to re-flash a press.
    """
    changelog = _changelog(_repo(tmp_path, "1.2.3", ["fix: a bug", "feat: a feature"]))
    assert "<!--" not in changelog
    assert changelog.index("### Features") < changelog.index("### Bug Fixes")


def test_changelog_line_degrades_gracefully_without_a_github_remote(tmp_path: Path) -> None:
    """The `by @user in #N` attribution reads `commit.remote.username` and
    `commit.remote.pr_number`, both populated only by git-cliff's GitHub
    integration (a token, network access, and — for pr_number — a squash
    merge; see the comment above `body` in cliff.toml). None of that exists
    for these throwaway repos, which have no remote at all. The line must
    still render cleanly rather than leaving a dangling "by @" or "in #".
    """
    changelog = _changelog(_repo(tmp_path, "1.2.3", ["fix: a bug fix"]))
    assert "- A bug fix" in changelog
    assert "by @" not in changelog
    assert " in #" not in changelog


def test_an_unrelated_parenthesized_number_in_the_message_is_left_alone(tmp_path: Path) -> None:
    """The `(#N)` strip — which removes GitHub's auto-appended PR suffix from
    a squashed subject — is guarded behind `commit.remote.pr_number` so it
    cannot fire on a coincidence. A subject that legitimately ends in `(#N)`
    must render untouched when there is no real PR association.
    """
    changelog = _changelog(_repo(tmp_path, "1.2.3", ["fix: close issue (#5) properly"]))
    assert "Close issue (#5) properly" in changelog


def test_an_unconventional_subject_is_dropped(tmp_path: Path) -> None:
    """A subject that is not a conventional commit is not a release note.
    check-semantic-pr.yml keeps these out of main under squash-merge, so this
    is the second line of defence. (Two settings would each have to change for
    this to regress: `filter_unconventional = true` *and* the absence of a
    catch-all `commit_parsers` entry — flipping either alone leaves the commit
    ungrouped and still dropped.)
    """
    changelog = _changelog(_repo(tmp_path, "1.2.3", ["feat: a feature", "wip, will fix later"]))
    assert "A feature" in changelog
    assert "will fix later" not in changelog
