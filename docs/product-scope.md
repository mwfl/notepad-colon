# Product scope

Notepad Colon is a file-first native editor, not a small IDE.

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

Notepad Colon intentionally does not implement LSP completion, cross-project
definitions, references, refactoring, or background diagnostics. The unused
LanguageService placeholder was removed so the architecture reflects that
product decision rather than advertising a future IDE subsystem.

## Folder behavior

Folder mode is optional. Users can continue to open one file or several tabs
without ever opening a folder. A folder supplies navigation and a search scope;
search starts only when the user invokes **Find in Files**. The dialog supports
semicolon-separated include and exclude globs and can instead search only the
currently open, named documents, including their unsaved contents.
