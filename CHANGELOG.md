# Changelog

## Unreleased

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
