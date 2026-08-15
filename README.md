# Notepad Colon

Notepad Colon is a fast, native Windows text and code editor built with
[mwfl](https://github.com/mwfl/mwfl) and Scintilla.

The product targets the everyday editing capabilities people rely on in
Notepad++ while deliberately excluding plugins, IDE features, accounts,
telemetry, and cloud services.

## Current capabilities

- Native multi-document tabs, safe atomic saves, recent files, drag and drop,
  and crash/session recovery including unsaved documents.
- UTF-8/BOM, UTF-16 LE/BE and checked ANSI reopening/saving, CRLF/LF
  conversion, `.editorconfig`, external-change detection, backup-before-save,
  and atomic replacement.
- Scintilla/Lexilla editing for 19 common languages with syntax color, folding,
  bookmarks, multiple/rectangular selections, auto-indent, brace matching,
  line operations, whitespace, wrapping, and zoom.
- Strict UTF-8/BOM/UTF-16/ANSI analysis, selectable ANSI code-page reopening,
  lossless conversion checks, mixed-EOL reporting, and bidi/zero-width warnings.
- Incremental Tree-sitter parsing and visible-range coloring for C++ and JSON,
  with document symbols and JSON-defined languages. Third-party Tree-sitter
  Wasm grammars run in a time- and memory-limited helper process, never in the
  GUI process.
- Workspace tree, fuzzy Quick Open, and cancellable background folder search
  with literal, multiline, whole-word, regular-expression, preview, persistent
  history, and `.gitignore` support.
- Explicit, reversible per-user `.txt` association under **Tools**. Existing
  associations are never silently overwritten.
- A native Preferences dialog persists System/Light/Dark theme, editor font,
  font size, and tab width. A single running instance accepts multiple file
  paths forwarded from later command-line launches.
- Compact English and Simplified Chinese core navigation can be switched at
  runtime and persists with the local UI settings.
- Files through 32 MiB open normally. UTF-8 files above 32 MiB through exactly
  4 GiB use an editable 8 MiB window backed by a piece table: scrolling between
  windows, inserts/deletes, fixed-memory full-file literal search, conflict-safe
  streaming save, and follow-tail are supported. Other large encodings remain
  read-only until their byte/character mapping is made lossless.

Plugins, IDE project systems, accounts, telemetry, and cloud services are
deliberately out of scope.

See [Custom languages](docs/custom-languages.md) and
[Large-file testing](docs/large-file-testing.md) for extension and verification
details.

## Supported systems

Windows 10 or newer on x64. Builds and CI use MSVC C++20 with Visual Studio
2026. CI runs the GUI, model, single-instance, and performance tests and creates
the x64 portable package.

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
registry lifecycle coverage, a 34 MiB edit/save integration test, and a sparse
exact-4-GiB piece-table/search performance guard.
