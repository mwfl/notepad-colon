# Changelog

## Unreleased

- Finish the dark theme. The menu bar, document tabs, status bar and every
  dialog are now dark instead of white, toolbar icons are larger, drawn at the
  display scale and tinted so they stay
  visible, and the bright 3D frame around the editor and input fields is gone.

- Keep sustained log following bounded: install refreshed text without retaining
  old windows in undo history, decode only the captured UTF-8 tail, and use
  lightweight coloring while following. Preserve exact-state checks for saving
  and recovery even when live display tolerates a concurrent append.
- Compact deleted large-file insertion storage, release saved/closed buffers,
  and construct replacement piece tables before committing an edit.
- Write recovery history on a bounded, coalescing background queue. Check new
  snapshots for corruption, retain two loadable generations per document, and
  report a full history without deleting another document's only fallback.
- Archive failed or partial startup sessions with their recovery sidecars before
  new checkpoints can overwrite them. Retry temporary archive failures on close;
  keep unsaved text open if a safe checkpoint remains unavailable.
- Mark recovered and generated nonempty new tabs as unsaved, including after
  undoing an edit back to their initial text, so closing prompts to save them.
- Interrupt backtracking folder-search, replacement and line-filter regexes on
  cancellation or a two-second matching limit. Report engine failures explicitly;
  calculate dense search result positions in one forward pass.
- Add native sustained-run tests for log rotation, document/menu churn, search
  cancellation, memory and Windows resource growth, plus opt-in full 4 GiB
  streaming-save verification.

- Add complete Traditional Chinese, Spanish, French and Hindi interfaces alongside
  English (the default) and Simplified Chinese. Switch immediately from Tools >
  Interface Language; keep native language names visible, show the current choice,
  remember it on restart, and migrate the previous ChineseUi preference.
- Consolidate the menu bar into File, Edit, Search, View, Document, Tools and Help.
  Group encoding, line endings and syntax highlighting under Document; organize
  sessions, printing, text transformations, tabs, workspaces and macros into
  focused submenus. Preserve existing command IDs and keyboard shortcuts.

- Review Replace in Files with read-only before/after panes, match navigation
  and per-file checkboxes. Execute the frozen reviewed output; reject stale
  disk or buffer inputs instead of rescanning after confirmation.
- Always retain checked replacement recovery copies and durable per-file
  progress, including partial cancellations. Add Replacement History with
  conflict-aware disk restore and recovery into new tabs, bounded to 100
  records and approximately 512 MiB without silently deleting old originals.
- Support zero-width regex replacements and bound replacement expansion while
  formatting to keep `$'`/`$`` references from allocating unbounded results.

- Unify opening, external reload, comparisons and folder operations on a checked,
  lossless decoder. Preserve legacy code-page overrides, UTF-16 byte order and
  BOM presence; add automatic-encoding reopen and reject malformed text safely.
- Detect disk replacements even when the new file preserves the original size
  and timestamp. Refuse saves while another writer holds the destination, and
  retain edited large-file contents for Save As after deletion or replacement.
- Keep UTF-8 log following active across append, truncation to zero, rotation
  and regrowth. Prevent encoding or line-ending conversion from changing only
  a large file's loaded window, and keep typed newlines consistent with the file.
- Treat EditorConfig charset as a fallback instead of reinterpreting a detected
  encoding. Handle Unicode paths, BOM-tagged configs and inherited `unset`
  properties, and retain BOMless UTF-16 through search, replacement and recovery.

- Preserve unsaved empty documents and explicit ANSI code pages across session
  recovery. Restore the saved caret and scroll position for clean documents too.
- Checkpoint the complete contents of edited large files in a background worker,
  including edits outside the loaded window. Recovery copies are independent of
  the original file, checked for corruption, and reused while the text is unchanged.
- Add session integrity checks and a previous-generation fallback. Flush session
  contents before committing the manifest, and preserve the disk baseline so a
  restored document cannot silently overwrite a later external change.
- Report recovery write failures, keep the editor open if its closing checkpoint
  fails, and keep file monitoring active when closing is cancelled.

## 1.3.1 - 2026-11-14

- Add Review External Changes (F8). When another program, such as a coding
  agent, rewrites an open file, the editor still reloads it but now keeps the
  previous text, marks the changed lines, and lets each change be kept or
  rejected side by side. Nothing is sent anywhere: the review compares two
  local texts.
- Add column tools for CSV/TSV data: **Extract Column to New Tab** (streamed,
  so it also works on multi-gigabyte files) and **Sort Rows by Column**, which
  compares numbers as numbers and keeps a header row first.
- Line-filter results lead back to their source: double-click a row or press
  F4 to open the line it came from, including in large files.
- Compare long files that differ in a small region without building a table
  for the whole file.

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
