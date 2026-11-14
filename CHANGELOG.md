# Changelog

## Unreleased

## 1.3.0 - 2026-11-14

- Add built-in syntax highlighting for more than 60 languages (was 19),
  driven by one language table with extensions, file names and comment
  prefixes. `CMakeLists.txt`, `Dockerfile` and `Makefile` are now recognised
  by name.
- Add a split view: a second view of the same document, side by side or
  stacked.
- Detect GB18030, Big5, Shift-JIS, EUC-JP and EUC-KR automatically, and add
  Reopen as / Convert to commands for each. Fix saving documents as GB18030,
  which always failed.
- Add Replace in Files with a preview, per-file encoding preservation, atomic
  writes and optional backups. Find in Files now decodes legacy and UTF-16
  files the same way the editor does.
- Add Filter Lines, Highlight Log Lines with editable rules, and Rainbow
  Columns for CSV/TSV data.
- Translate the whole interface into Simplified Chinese; previously only
  about twenty commands were translated.
- Add an optional, reversible Explorer context menu, and open folders passed
  on the command line or dropped on the window as workspaces.
- Add winget and Scoop manifest generation and an ARM64 build configuration.
- Keep the WebAssembly engine out of the editor process entirely and verify in
  the test suite that no shipped binary imports a networking library.
- Fix editor focus and change notifications being mistaken for menu commands
  once a session had opened about a hundred documents.
- Split the 4,600-line main.cpp into per-area files under `src/app`.
- Show each command's keyboard shortcut in the menus, and name keys such as
  Space and the brackets instead of showing `Key32`.
- Move Quick Open to Ctrl+E. It shared Ctrl+P with Print, which made Quick
  Open unreachable from the keyboard and made the shortcut editor reject every
  change.
- Size dialog buttons to their captions so Chinese captions are no longer
  cramped, and stop leaving an empty Untitled tab when files are opened from
  the command line.

- Include target-size icons for default, dark, and light themes in MSIX packages
  and generate resources.pri so Windows can select icons without a system plate.
- Bundle the MSVC runtime DLLs with portable ZIP and Store MSIX packages so
  machines without Visual Studio can launch the editor. Distribute Release
  builds only and verify the extracted portable package in CI.

## 1.2.0 - 2026-08-28

- Remove automatic and manual GitHub update checks from every distribution;
  Store updates are managed by Microsoft Store and Portable updates remain an
  explicit user choice.
- Add a repeatable Microsoft Store MSIX packaging workflow using the product
  identity assigned by Partner Center.
- Align Portable and Microsoft Store releases on version 1.2.0 from the same
  source commit.

## 0.1.2 - 2026-08-27

- Make the native menu, status, tree, result, and editor chrome follow Windows
  dark mode, including live system-theme changes.
- Give Scintilla line-number, fold, and bookmark margins coherent light and
  dark palettes.
- Reject directory targets before attempting an atomic session save.

## 0.1.1 - 2026-08-23

- Prevent an older cancelled Find in Files operation from replacing the result
  of a newer search.
- Apply the same generation-safe completion handoff to workspace scans.
- Add deterministic model coverage for stale, current, cancelled, and
  single-delivery background-operation results.
- Update standalone builds to the maintained MWFL 0.1.1 baseline.

## 0.1.0 - 2026-08-22

- First public preview of Notepad::.
