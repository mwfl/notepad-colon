# Agent guide

Notes for coding agents (and people) working in this repository. `README.md`
describes the product and `docs/` the details; this file covers how to build,
test and release.

## Build and test

Windows only: Visual Studio 2026 (MSVC, C++20) and CMake presets.

```powershell
cmake --preset vs2026-x64 -DMWFL_SOURCE_DIR=
cmake --build --preset vs2026-x64-debug --parallel
ctest --preset vs2026-x64-debug
cmake --build --preset vs2026-x64-release --parallel
ctest --preset vs2026-x64-release
```

- `-DMWFL_SOURCE_DIR=` fetches the pinned mwfl framework. Without it the
  configure step looks for a sibling `../mwfl` checkout and fails when there is
  none. A failed configure leaves the old binaries in place, so tests that
  "pass" afterwards prove nothing: check the configure exit code.
- mwfl is a pinned dependency under `build/*/_deps/mwfl-src`. Do not edit it;
  fix things on the application side.
- The suite has 12 tests. `sustained-run` and `performance` have timing
  budgets and occasionally fail on a slow or busy machine, including CI
  runners. Rerun before concluding anything, and do not loosen the budgets to
  make a run pass.
- User-visible strings go through `Tr()` and need an entry in every
  `src/strings_*.inc` (six interface languages). The `translations` test fails
  on a missing one.
- Dark-theme chrome that Windows does not darken itself lives in
  `src/app/dark_chrome.*`. Check both themes with real screenshots after UI
  changes.

## Releasing

Every new version is published to **three channels**:

| Channel | What | How |
|---|---|---|
| GitHub Release | NSIS installer `notepad-colon-vX.Y.Z-windows-x64-setup.exe`, plus the portable ZIP it is built from, each with a `.sha256` | Pushing the tag runs `.github/workflows/release.yml` |
| Microsoft Store | MSIX `notepad-colon-X.Y.Z.0-x64.msix` | `scripts/build-store-msix.ps1`, then submit in Partner Center |
| winget | Manifests for `AtomSprout.NotepadColon` | `scripts/update-package-manifests.ps1`, then a pull request to `microsoft/winget-pkgs` |

The publisher name is **AtomSprout** in all three.

### Steps

1. Bump the version everywhere. `git grep -n "1\.3\.2\|1,3,2"` (with the
   current version) lists the places: `CMakeLists.txt`, `app.rc`,
   `app.manifest`, the About string in `src/app/main_window_commands.cpp` and
   every `src/strings_*.inc`, `scripts/build-store-msix.ps1`, `README.md`,
   `docs/distribution.md`, `site/index.html`.
2. In `CHANGELOG.md`, rename `## Unreleased` to `## X.Y.Z - YYYY-MM-DD`.
3. Build Release and run the Release tests locally.
4. Commit as `release: prepare Notepad Colon X.Y.Z`, tag `vX.Y.Z`, push both.
5. Create the release page first, with that version's changelog section as the
   notes:
   `gh release create vX.Y.Z --verify-tag --title "Notepad Colon vX.Y.Z" --notes-file <notes>`.
   The shared release workflow uploads to an existing release but has failed
   to create one itself ("no matches found for `vX.Y.Z`"). If the workflow
   already failed at *Publish GitHub Release*, create the page and rerun the
   failed job.
6. The workflow's `release` job attaches the portable ZIP; its `installer`
   job then builds the NSIS installer from that ZIP, runs
   `scripts/test-installer.ps1`, and attaches it. Verify the four assets and
   their hashes on the release page.
7. Microsoft Store: `./scripts/build-store-msix.ps1`, then replace the package
   in a new Partner Center submission and update "What's new".
8. winget: download the published ZIP, then
   `./scripts/update-package-manifests.ps1 -Version X.Y.Z -X64Zip <zip>` and
   `winget validate --manifest artifacts\packages\winget\manifests\a\AtomSprout\NotepadColon\X.Y.Z`.
   Open the pull request with that folder.

Store submissions and winget pull requests are public and slow to undo:
confirm with the maintainer before the final submit. Check for a submission
still in certification or an unmerged winget pull request for the previous
version first, because replacing either restarts its review.

### Installer

`packaging/nsis/installer.nsi` is compiled by `scripts/build-installer.ps1`
(needs NSIS 3: `winget install NSIS.NSIS`). It is per-user, unsigned, and
removes only the files it installed. `scripts/test-installer.ps1` does a real
silent install and uninstall for the current user, and refuses to run when
Notepad:: is already installed. See `docs/distribution.md`.

## Status

As of 2026-11-17. Update this section when a release or submission changes.

- **GitHub Release**: v1.3.2 is the latest, with the portable ZIP and the NSIS
  installer. That installer was built and uploaded by hand; the workflow's
  `installer` job was added afterwards and has not yet run on a tag.
- **Microsoft Store**: a submission with package 1.3.1.0 was in certification
  when last checked (2026-11-15). 1.3.2 has not been submitted.
- **winget**: [microsoft/winget-pkgs#480510](https://github.com/microsoft/winget-pkgs/pull/480510)
  adds `AtomSprout.NotepadColon` 1.3.1. It has passed validation and is
  waiting for a moderator. 1.3.2 has not been submitted.
- **Known gaps**: the About and encoding-information message boxes stay light
  in the dark theme (they are system message boxes); the installer does not
  remove an optional `.txt` association; the installer is not code-signed.
