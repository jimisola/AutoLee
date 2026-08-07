# Releasing AutoLee

Releases are cut by the **Release** workflow
(`.github/workflows/release.yml`), not by pushing a tag by hand. Tagging
manually skips every check below and leaves the tag pushed with nothing built.

## Cutting a release

1. Make sure what you want to release is merged to `main`.
2. Actions → **Release** → **Run workflow**.
   - **Use workflow from**: the branch or commit to release. This is the ref
     that gets tagged *and* the ref the checks run against — they cannot
     disagree. It must be reachable from `main`.
   - **version**: **leave empty** to have it worked out from the Conventional
     Commits since the last tag (see "Auto-detected versions" below). If you do
     pass one, it must be bare, full three-part semver — `2.0.0`, not `2.0`, not
     `v2.0.0` (see [CONTRIBUTING.md](CONTRIBUTING.md#versioning--releases)) —
     and it has to *agree* with the auto-detected value unless you also tick
     **force**.
   - **force**: allow a version that disagrees with the auto-detected one. The
     run warns and records what it overrode.
   - **dry-run**: **defaults to on.** Resolves the version, checks the tag is
     free and the ref is on `main`, generates the release notes, and writes it
     all to the job summary without building or publishing anything. Uncheck it
     to actually release.
3. The full CI suite runs — firmware build, host tests, lint, API specs,
   workflow lint — so what you approve next is already green.
4. **The run pauses for approval.** The job that tags is bound to the `stable`
   environment, so it sits pending until a required reviewer approves it on the
   run page. There is no draft to publish by hand; this is the confirmation
   step.
5. On approval it tags the ref locally, builds, and **asserts the version baked
   into the firmware matches the tag** — for both `autolee.bin` and the merged
   image. Only then does it push the tag and create the GitHub Release **as a
   prerelease with its artifacts already attached**.
6. The published assets are downloaded back over the public path and re-checked:
   checksums against what was built, merged image exactly 4 MB, and the
   `esp_app_desc_t.version` inside each binary equal to the tag.
7. Only then is the prerelease promoted to the latest release — one flag flip,
   assets already in place.

## Auto-detected versions and release notes

Both come from [`git-cliff`](https://git-cliff.org/) and
[`cliff.toml`](cliff.toml), reading the Conventional Commit subjects since the
previous tag. Under squash-merge those subjects are the PR titles, which
`check-semantic-pr.yml` already validates — so the input to this is checked long
before a release runs.

| Highest-ranked commit since the last tag | Bump |
|---|---|
| any `!:` (e.g. `feat!:`) | major |
| `feat:` | minor |
| `fix:`, `docs:`, `refactor:`, … | patch |
| only `ci:` / `build:` / `tools:` | **none** |

That last row is deliberate: a range containing nothing but maintainer-facing
changes has nothing in it for whoever flashes the board, so auto-detect returns
the tag that already exists and the run fails on "tag already exists". Pass a
version explicitly (with **force**) if you really do want to ship it.

Two cases where you must pass the version yourself:

- **The first tag in the repository.** With no tags to bump from, git-cliff
  answers `0.1.0` regardless of history.
- **Any deliberate jump** — declaring `1.0.0`, skipping a number, or releasing a
  version the commit history does not justify.

The mapping above is pinned by `tools/tests/test_cliff_config.py`, which runs the
real git-cliff against throwaway repos on every PR (the `release-config` job in
`.github/workflows/lint.yml`) — otherwise a cliff.toml mistake would only
surface as a wrong release. Run it locally with git-cliff on `PATH`:

```bash
pytest tools/tests            # skipped, not failed, if git-cliff is missing
```

The notes are generated in the `prepare` job, shown in the job summary at the
approval gate, and handed to the release job as an artifact — so the notes that
ship are provably the ones that were approved, not a regenerated copy.

## Artifacts

| File | What it is |
|---|---|
| `autolee-X.Y.Z-merged.bin` | Full 4 MB flash image — bootloader, partition table, app. Fresh install, flash at `0x0`. |
| `autolee-X.Y.Z-update.bin` | App image only. OTA or re-flash of an already-provisioned board. |
| `SHA256SUMS.txt` | Checksums for both. |

The literal `v` in those filenames is prose. The tag, and the version the
firmware reports, never has one.

## Why a prerelease rather than a draft

The release has to be verifiable before anyone can reach it, and those two pull
in opposite directions.

A **draft**'s assets are not downloadable without an authenticated token, so the
verify job could not exercise the same path a user gets — it would be checking
something other than what ships.

A **prerelease** is publicly readable by exact tag, but `/releases/latest`
excludes it, and the release page does not badge it as Latest. So it is fully
testable while no one is being pointed at it.

If verification fails, the release stays a prerelease: nobody was served it. The
tag and prerelease are left in place for a human to delete or supersede —
fixing forward with a patch version is usually cleaner than deleting a pushed
tag.

This closes a window that used to exist. The old workflow fired on
`release: published`, so the release went public first and the firmware was
built afterwards: for the length of a cold ESP-IDF build, a live release
advertised binaries that did not exist, and a failed build left it public and
permanently empty.

## What the version assertion is for

There is no version constant in the source. `esp_app_desc_t.version` comes from
`git describe --always --tags --dirty`, evaluated by CMake at *configure* time,
and that is what the boot banner, `/api/v1/state` and
`/api/v1/diagnostics/info` all report. The artifact *filenames*, by contrast,
come from the workflow input. Three documented things can make those disagree
without anything looking wrong:

- `PROJECT_VER` is cached at configure time, not recomputed per build;
- `git describe` picks the nearest tag by commit-graph distance, not the highest
  semver;
- a dirty tree appends `-dirty`.

`tools/app_desc.py` reads the descriptor back out of the built image, and the
release fails rather than shipping a binary that misreports its own version.
CI runners are clean checkouts, so the first and third are unlikely there — the
assertion mostly earns its place as a backstop for the second, and for anything
that ever changes about how the version is derived.

## Repository setup this depends on

> [!IMPORTANT]
> **The `stable` environment must exist with a required reviewer.**
>
> GitHub auto-creates a named environment on first use with **no protection
> rules at all**. If nobody configures it, the approval gate in step 4 runs
> straight through and the release publishes unattended — the gate is there but
> does nothing, with no error to tell you.
>
> Settings → Environments → **New environment** → `stable` → **Required
> reviewers** → add a maintainer.
>
> Environment protection rules are free on public repositories; the paid tier
> applies to private and internal ones only.

Other settings worth applying by hand (this repo has no `settings.yml` and no
Settings app installed, so none of these are enforced as code):

- **Squash merges only**, with **squash commit title = PR title**. GitHub's
  default (`COMMIT_OR_PR_TITLE`) uses the commit subject when a PR has exactly
  one commit — so a PR whose title was validated by `check-semantic-pr.yml` can
  still land on `main` with an unchecked subject line.
- **Delete branch on merge** (currently off).
- **Branch protection on `main`**: require the CI checks and `Validate PR title`
  to pass. Note that requiring an *approving review* locks a solo maintainer out
  of merging their own PRs.
