<div align="center">
  <img src="assets/notepad-colon-128.png" width="96" height="96" alt="Notepad:: icon">
  <h1>Notepad::</h1>
  <p><strong>A fast, focused text and code editor for Windows.</strong></p>
  <p>Native C++ · No plugins · No telemetry · No background indexing</p>
</div>

Notepad:: brings the everyday editing strengths people value in Notepad++
to a focused native Windows application. It starts as an editor—not an IDE—and
keeps expensive language servers, accounts, cloud services, and project-wide
code indexing out of the process.

> **Project status:** early preview. Core editing, search, recovery, syntax
> highlighting, folder browsing, and large-file workflows are implemented and
> covered by automated tests. Interfaces may still change during this early preview period.

## Why Notepad::?

| | Notepad:: approach |
|---|---|
| **Fast by default** | Native Win32 UI built with [mwfl](https://github.com/mwfl/mwfl) and Scintilla |
| **Focused** | Editing and search without plugins, LSP processes, or IDE project systems |
| **Safe** | Atomic saves, external-change detection, backups, session recovery, and encoding checks |
| **Large-file aware** | Editable fixed-memory windows for UTF-8 files through exactly 4 GiB |
| **Private** | No accounts, telemetry, analytics, or cloud services — and no networking code: [verify it yourself](docs/security-model.md) |
| **Safely extensible** | Grammars run as sandboxed WebAssembly in a separate process; no native plugin DLLs |

## Highlights

### Everyday code editing

- Native multi-document tabs, recent files, drag and drop, bookmarks, folding,
  multiple and rectangular selections, brace matching, line operations,
  whitespace controls, wrapping, and zoom.
- Split view: a second, independently scrolled view of the same document,
  side by side or stacked (`Ctrl+\`, `F6` to switch views).
- Syntax highlighting for more than 60 languages through Scintilla and Lexilla,
  from C++, Python and TypeScript to Go, Kotlin, Swift, PHP, Ruby, Lua, shell,
  Dockerfile, TOML, Zig and Verilog.
- Incremental Tree-sitter parsing for C++, JSON, Python, JavaScript/JSX, and
  TypeScript/TSX, including document symbols and visible-range coloring.
- Explicit `Ctrl+Space` completion from language keywords, current-document
  identifiers, and current-document symbols—with no language server or folder
  index.

### Search that stays useful

- Incremental find, match case, whole word, regular expressions, selection
  scope, wraparound, replace, history, and bounded **Mark All**.
- Cancellable folder and open-document search with previews, include/exclude
  globs, `.gitignore`, multiline patterns, binary detection, and safety limits.
- **Replace in Files** with a count-first preview. Every file keeps its
  encoding, byte order mark and line endings, is replaced atomically, and can
  keep a `.bak` copy; documents open in the editor are changed in the editor
  (undoable) instead of behind your back.
- A lazy workspace tree and fuzzy Quick Open. Opening a folder does not trigger
  background analysis.

### Files and encodings

- UTF-8/BOM, UTF-16 LE/BE, checked ANSI code pages, CRLF/LF conversion, and
  `.editorconfig`.
- Automatic detection of legacy CJK encodings — GB18030, Big5, Shift-JIS,
  EUC-JP and EUC-KR — independent of the system code page, with **Reopen as**
  and **Convert to** commands for each.
- Lossless conversion checks, mixed-EOL reporting, and warnings for bidi and
  zero-width characters.
- Safe atomic replacement, optional backup-before-save, conflict detection, and
  crash/session recovery—including unsaved documents.
- Recovery checkpoints preserve empty edits and full large-file contents, check
  data integrity, and can fall back to the previous generation. Named sessions
  containing large-file edits have a companion `.data` folder; keep it alongside
  the session file when moving or copying the session.

### Working next to a coding agent

- **Review External Changes** (`F8`): when another program rewrites a file you
  have open, Notepad:: reloads it, keeps the text from before, marks every
  changed line in the margin, and shows the changes side by side so each one
  can be kept or rejected. Rejected changes go back into the editor as a
  normal, undoable edit.
- The editor never talks to a model or a network. It only compares the two
  versions of your own file, so it works with any agent, formatter or script.

### Large files without large memory use

- Files through 32 MiB open in the regular editor.
- UTF-8 files above 32 MiB through exactly 4 GiB use an editable 8 MiB window
  backed by a piece table.
- Windowed scrolling, insert/delete, fixed-memory full-file literal search,
  conflict-safe streaming save, and follow-tail are supported.
- Other large encodings remain read-only until byte-to-character mapping can be
  guaranteed lossless.

### Logs and tables

- **Filter Lines** (`Ctrl+Shift+G`) collects the lines that match — or do not
  match — a text or regular expression into a new tab with their original line
  numbers. Large files are streamed from disk in fixed-size chunks.
  Double-click a result row or press `F4` to jump back to that line in the
  source, even in a multi-gigabyte file.
- **Highlight Log Lines** colours whole lines by rule (`red=ERROR|FATAL;
  orange=WARN; gray=DEBUG|TRACE` by default, editable). It turns on
  automatically for `.log` files and when following a growing file.
- **Rainbow Columns** colours each field of CSV/TSV data and shows the field
  number of the caret. Only visible lines are decorated, so it costs the same
  in a 4 GiB file as in a small one.
- **Extract Column to New Tab** copies the column under the caret, streaming
  large files the same way. **Sort Rows by Column** compares numbers as
  numbers and keeps a header row first.

See [Large-file testing](docs/large-file-testing.md) for the reproducible 100 MiB
and 4 GiB verification workflow.

## Supported systems

- Windows 10 or newer
- x64 (tested packaged build); ARM64 (cross-compiled in CI)
- English and Simplified Chinese interface (**Tools > 简体中文界面**)
- MSVC C++20 with Visual Studio 2026

## Build from source

Prerequisites: Visual Studio 2026 with Desktop development with C++ and CMake.
Standalone builds fetch the pinned MWFL `v0.1.1` release; set
`MWFL_SOURCE_DIR` when developing against a local mwfl checkout.

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug --parallel
ctest --preset vs2026-x64-debug
```

Create the portable ZIP:

```powershell
cmake --build --preset vs2026-x64-release --target package
```

The Release package contains `notepad-colon.exe`, its language host, Scintilla,
Lexilla, the MSVC runtime DLLs, notices, and this guide. Extract the entire ZIP
to one folder before launching; copying only the EXE leaves required DLLs behind.
Debug builds are for development and cannot be packaged for distribution.
Launching it makes no registry changes. The optional `.txt` association
is explicit, reversible, and available under **Tools**.

## Project scope

Notepad:: deliberately excludes plugins, LSP processes, semantic project
indexing, IDE project systems, accounts, telemetry, and cloud services. Opening
a folder provides bounded navigation and on-demand search; it does not turn the
folder into a project or analyze its code in the background.

Read the detailed [product scope](docs/product-scope.md), the
[security and privacy model](docs/security-model.md), the guide for
[custom languages](docs/custom-languages.md), and the notes on
[distribution](docs/distribution.md) (winget, Scoop, ARM64, Explorer menu).

## Validation

CTest covers the model layer, real native-window GUI behavior, single-instance
forwarding, isolated Shell registration, a 34 MiB edit/save integration path,
Tree-sitter and Wasm syntax handling, an exact-4-GiB piece-table/search
performance guard, translation-catalog completeness, and a check that no
shipped binary imports a networking library. GitHub Actions builds, tests, packages, and uploads the x64
portable artifact.

## License

Notepad:: is released under the [MIT License](LICENSE). Third-party
components and grammar licenses are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Releases

Notepad:: never checks the network for updates. Microsoft Store installations
are updated by the Store, while Portable users can choose when to download a
new version from the GitHub Releases page. Tag releases publish a versioned
`windows-x64-portable.zip` plus SHA-256 checksums.

Build the Store MSIX from an existing Release configuration with:

```powershell
./scripts/build-store-msix.ps1
```

The script stages the product files, applies the Partner Center package identity,
generates the required logo sizes from the repository icon, and validates the
manifest while packing `artifacts/store/notepad-colon-1.3.1.0-x64.msix`.
