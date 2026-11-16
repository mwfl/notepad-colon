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

Replace in Files scans at most 100,000 entries, skips binary files and files
over 8 MiB, and never rewrites a file whose result cannot be represented in
that file's own encoding.

## Folder behavior

Folder mode is optional. Users can continue to open one file or several tabs
without ever opening a folder. A folder supplies navigation and a search scope;
search starts only when the user invokes **Find in Files**. The dialog supports
semicolon-separated include and exclude globs and can instead search only the
currently open, named documents, including their unsaved contents.
