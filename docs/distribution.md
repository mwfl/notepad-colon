# Distribution

## Packages

| Channel | Source | Notes |
|---|---|---|
| Portable ZIP (x64) | `cmake --build --preset vs2026-x64-release --target package` | Tested in CI, including a self-test of the extracted package. |
| Portable ZIP (ARM64) | `cmake --build --preset vs2026-arm64-release --target package` | Cross-compiled in CI. The binaries cannot run on the x64 runner, so the test suite runs for x64 only. |
| Installer (x64) | `scripts/build-installer.ps1` | NSIS, per-user, unsigned; see below. Round-trip tested in CI. |
| Microsoft Store (MSIX) | `scripts/build-store-msix.ps1` | Updated by the Store. |
| winget, Scoop | `scripts/update-package-manifests.ps1` | Generated from the published ZIPs; see below. |

Building for ARM64 needs the *MSVC ARM64 build tools* component of Visual
Studio. The ARM64 build fetches the matching prebuilt Wasmtime C API, pinned
by SHA-256 like the x64 one.

## Installer

`scripts/build-installer.ps1` wraps the product files in an NSIS installer
(`makensis` from NSIS 3 must be installed). Pass `-Zip` to build it from a
published portable ZIP, which is what the release workflow does so that both
downloads carry the same binaries; without `-Zip` it stages the local Release
build.

```powershell
./scripts/build-installer.ps1 -Zip path\to\notepad-colon-v1.3.2-windows-x64-portable.zip
./scripts/test-installer.ps1 -Installer artifacts\installer\notepad-colon-v1.3.2-windows-x64-setup.exe
```

The installer runs without administrator rights and changes only the current
user's profile: files go to `%LOCALAPPDATA%\Programs\Notepad Colon` (or the
folder chosen), with a Start menu shortcut, an optional desktop shortcut and
an *Apps & features* entry. `setup.exe /S` installs silently and
`/D=C:\some folder` (last, unquoted) picks the folder. Installing over an
existing copy upgrades it; setup waits while the editor is running, or exits
with code 2 when silent.

Uninstalling removes exactly the files that were installed, the shortcuts, and
any Explorer context menu entries that point into that folder. Preferences,
sessions and recovery data are kept, and so is an optional `.txt` association:
remove it from **Tools > Windows Integration** before uninstalling.

The installer is not code-signed, so Windows SmartScreen may warn on first
run. Check the published `.sha256` file if in doubt.

## winget and Scoop manifests

Package managers pin each download by URL and SHA-256, so manifests are
generated from the exact files attached to a GitHub release:

```powershell
./scripts/update-package-manifests.ps1 -Version 1.3.0 `
    -X64Zip path\to\notepad-colon-1.3.0-windows-x64-portable.zip `
    -Arm64Zip path\to\notepad-colon-1.3.0-windows-arm64-portable.zip
```

The script checks that each ZIP really contains the application, hashes it,
fills in the templates under `packaging/winget` and `packaging/scoop`, and
writes the result to `artifacts/packages`. By default the download URL is the
release asset with the ZIP's own file name; pass `-X64Url` / `-Arm64Url` if
the published name differs. `-Arm64Zip` is optional.

Nothing is submitted for you. Validate and submit by hand:

```powershell
winget validate --manifest artifacts\packages\winget\manifests\a\AtomSprout\NotepadColon\1.3.2
```

Then open a pull request against
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) with that
folder, and copy `artifacts/packages/scoop/notepad-colon.json` into your
Scoop bucket.

## Explorer context menu

**Tools > Add Explorer Context Menu** adds, for the current user only:

- **Edit with Notepad::** on files,
- **Open folder in Notepad::** on folders and on the background of a folder.

**Tools > Remove Explorer Context Menu** removes them. Like the optional
`.txt` association, the entries are explicit and reversible: launching the
editor never writes them, only entries carrying the application's owner
marker are replaced or removed, and an entry of the same name that belongs
to another program is left alone.

If you move the portable folder, choose **Add Explorer Context Menu** again
to update the path. Remove the entries before deleting the folder.

The Microsoft Store build runs with package identity, where per-user registry
writes are virtualised, so these commands have no effect on Explorer there.

A folder passed on the command line, dropped on the window or sent from the
context menu is opened as a workspace root rather than as a document.
