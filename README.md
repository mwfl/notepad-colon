# Notepad Colon

Notepad Colon is a fast, native Windows text and code editor built with
[mwfl](https://github.com/mwfl/mwfl) and Scintilla.

The product targets the everyday editing capabilities people rely on in
Notepad++ while deliberately excluding plugins, IDE features, accounts,
telemetry, and cloud services.

## Current capabilities

- Native multi-document tabs, safe atomic saves, recent files, drag and drop,
  and crash/session recovery including unsaved documents.
- UTF-8/BOM and UTF-16 LE/BE, CRLF/LF conversion, find/replace, and external
  file-change detection.
- Scintilla/Lexilla editing for 19 common languages with syntax color, folding,
  bookmarks, multiple/rectangular selections, auto-indent, brace matching,
  line operations, whitespace, wrapping, and zoom.
- Workspace tree and cancellable background folder search with ignored build,
  VCS, cache, and dependency directories.
- Explicit, reversible per-user `.txt` association under **Tools**. Existing
  associations are never silently overwritten.
- Files through 32 MiB open normally; files above 32 MiB through 256 MiB open
  in protected read-only mode. Larger files are rejected before allocation.

Plugins, IDE project systems, accounts, telemetry, and cloud services are
deliberately out of scope.

## Supported systems

The currently runnable package supports Windows 10 or newer on x64. Builds use
MSVC C++20 with Visual Studio 2022 or Visual Studio 2026. CI also compiles the
application on native Windows ARM64, but ARM64 distribution remains blocked
until matching ARM64 Scintilla and Lexilla runtime DLLs replace the upstream
x64-only binaries; ARM64 is therefore not claimed as a runnable release yet.

## Local development

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug --parallel
ctest --preset vs2026-x64-debug
```

Set `MWFL_SOURCE_DIR` when developing against a local mwfl checkout.

## Portable package

```powershell
cmake --build --preset vs2026-x64-release --target package
```

The ZIP contains `notepad-colon.exe`, `Scintilla.dll`, `Lexilla.dll`, the
license, and this guide. It makes no registry changes on launch; Shell
registration happens only when explicitly selected from **Tools** and can be
removed there.

## Validation

CTest runs model coverage, a real native-window GUI self-test, isolated Shell
registry lifecycle coverage, and a deterministic workspace performance guard.
