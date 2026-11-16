# Product scope

Notepad:: is a file-first native editor, not a small IDE.

## Performance contract

- Opening a file does not scan its parent folder, locate a repository root, or
  start a language process.
- Opening a folder enumerates only each visible directory level on demand.
  Quick Open and explicit search may perform bounded recursive scans, but no
  semantic symbol index is built.
- Tree-sitter is limited to ordinary documents of at most 8 MiB. Larger files
  use the bounded large-file path without a full syntax tree.
- Local completion runs only on explicit `Ctrl+Space`, examines at most 8 MiB,
  returns at most 256 candidates, and uses no background process.
- Folder search is cancellable, honors `.gitignore`, skips common generated
  directories and binary files, reads at most 8 MiB per file by default, scans
  at most 100,000 entries, and returns at most 5,000 matches.
- Search highlighting is limited to 10,000 visible-document matches.

## Code assistance

Tree-sitter provides precise coloring and current-document symbols for C++,
JSON, Python, JavaScript/JSX, and TypeScript/TSX. Local completion combines
language keywords, current-document identifiers, and those symbols.

Notepad:: intentionally does not implement LSP completion, cross-project
definitions, references, refactoring, or background diagnostics. The unused
LanguageService placeholder was removed so the architecture reflects that
product decision rather than advertising a future IDE subsystem.

## Encoding and disk generations

Opening, external reload, comparison and folder operations share a strict
decoder. UTF-8 and UTF-16 must be valid, legacy decoding must round-trip to the
same bytes, and embedded NUL text is refused rather than truncated by the editor
control. EditorConfig charset values are decoding fallbacks for BOMless UTF-16;
they do not override a detected BOM or convert an existing file implicitly.
Explicit per-document code-page and line-ending choices survive recovery.

Ordinary reads reject an active writer and verify file identity, size and write
time before publishing the text. Follow mode explicitly allows live UTF-8
reads; an invalid or changing update keeps the last usable view for a later
retry. Saves flush a temporary file and validate the original disk generation
before atomic replacement. File identity detects replacements that preserve
both the length and timestamp. Large-file snapshots hold independent reader
handles, so deleting or atomically replacing the original does not destroy the
source of a recoverable snapshot or Save As operation.

## Line tools

Rainbow columns and highlighted log lines decorate only the lines a view is
showing, at most 16 KiB per line, and are recomputed only when the visible
range or the text changes. The line filter reads the source in 4 MiB chunks,
keeps at most 100,000 lines or 16 MiB of text, and is cancellable. A large
file must be saved before it is filtered, because the scan reads the file on
disk rather than the in-memory edit list.

Replace in Files scans at most 100,000 entries per workspace root, honoring
include/exclude globs and `.gitignore` for both disk files and open buffers.
It skips binary and disk inputs over 8 MiB. Each reviewed batch contains at
most 256 files and 128 MiB of UTF-8 before/after snapshots, with a 64 MiB
UTF-16 output ceiling per file. The complete staged text is visible; navigation
records the first 256 matches per file and at most 4,096 matches per batch.
The preview reports skips and limits. Execution never rescans for new matches.

All selected disk identities, original contents and staged artifacts are checked
before the first write; each atomic write checks the identity again. Stale
previews abort before writes. A race or cancellation during application may
leave a partial batch; its completed files remain recorded in Replacement
History. Open buffers are checked again before their single undoable edit.
Unrepresentable legacy-encoding output is displayed but cannot be selected.

Replacement History always keeps checksummed originals independently of optional
adjacent `.bak` copies, plus a flushed journal and previous manifest generation.
Restore refuses later disk changes and currently open disk targets. Editor-buffer
originals can be recovered into new unnamed tabs. Interrupted writes can be
recovered when the current text still matches the staged output. History is
limited to 100 operations and approximately 512 MiB; it stops accepting new
batches rather than discarding recovery copies automatically. Review and delete
records explicitly from **Search > Replacement History**. Batches are atomic
per file, not transactions spanning all files.

## Folder behavior

Folder mode is optional. Users can continue to open one file or several tabs
without ever opening a folder. A folder supplies navigation and a search scope;
search starts only when the user invokes **Find in Files**. The dialog supports
semicolon-separated include and exclude globs and can instead search only the
currently open, named documents, including their unsaved contents.

## Interface languages and menus

English is the default interface language. Simplified Chinese, Traditional Chinese,
Spanish, French and Hindi each include the complete checked interface catalog.
Choose **Tools > Interface Language** to switch immediately; native names remain
visible in every language and the choice is saved for the next launch. Existing
Simplified Chinese preferences migrate without changing back to English.

The seven top-level menus are File, Edit, Search, View, Document, Tools and Help.
**Document > Encoding**, **Document > Line Endings** and **Document > Syntax
Highlighting** configure the current file. Syntax highlighting chooses the code
language; it is separate from the application's interface language. The status
bar continues to display the current encoding and line endings. Existing command
IDs and shortcuts remain compatible with saved configurations.

Every catalog is checked for complete key coverage, duplicate entries, menu
mnemonics, dialog ellipses and line breaks by `scripts/extract-strings.py --check`.
