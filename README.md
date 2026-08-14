# Notepad Colon

Notepad Colon is a fast, native Windows text and code editor built with
[mwfl](https://github.com/mwfl/mwfl) and Scintilla.

The product targets the everyday editing capabilities people rely on in
Notepad++ while deliberately excluding plugins, IDE features, accounts,
telemetry, and cloud services.

## Status

Active development. The first supported release will target Windows 10 and
newer on x64 and ARM64 with Visual Studio 2022 and Visual Studio 2026.

## Local development

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug --parallel
ctest --preset vs2026-x64-debug
```

Set `MWFL_SOURCE_DIR` when developing against a local mwfl checkout.

