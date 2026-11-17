# Distribution

## Packages

| Channel | Source | Notes |
|---|---|---|
| Portable ZIP (x64) | `cmake --build --preset vs2026-x64-release --target package` | Tested in CI, including a self-test of the extracted package. |
| Portable ZIP (ARM64) | `cmake --build --preset vs2026-arm64-release --target package` | Cross-compiled in CI. The binaries cannot run on the x64 runner, so the test suite runs for x64 only. |
| Microsoft Store (MSIX) | `scripts/build-store-msix.ps1` | Updated by the Store. |
| winget, Scoop | `scripts/update-package-manifests.ps1` | Generated from the published ZIPs; see below. |

Building for ARM64 needs the *MSVC ARM64 build tools* component of Visual
Studio. The ARM64 build fetches the matching prebuilt Wasmtime C API, pinned
by SHA-256 like the x64 one.

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
