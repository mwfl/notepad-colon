# Security and privacy model

Notepad:: makes two claims that you do not have to take on trust:

1. **The editor cannot use the network.** No shipped binary imports, or even
   names, a Windows networking library.
2. **Language extensions cannot harm the editor or your files.** Grammars run
   as WebAssembly in a separate, resource-limited process with no system
   access. Native plugin DLLs are not supported at all.

This page explains what backs each claim and how to check it yourself.

## 1. No network access — verifiable

A Windows program can only call a system API from a library it imports, or
one it loads by name at run time. `scripts/verify-no-network.ps1` reads the
import and delay-import tables of each binary directly (no SDK tools needed)
and also searches the file for the names of networking libraries, in ASCII
and UTF-16, to catch run-time loading.

Run it against a downloaded release:

```powershell
./scripts/verify-no-network.ps1 notepad-colon.exe notepad-colon-language-host.exe Scintilla.dll Lexilla.dll
```

Expected output:

```text
OK    notepad-colon.exe imports 15 libraries; no networking library is imported or named
OK    notepad-colon-language-host.exe imports 13 libraries; no networking library is imported or named
OK    Scintilla.dll imports 7 libraries; no networking library is imported or named
OK    Lexilla.dll imports 1 libraries; no networking library is imported or named
```

Add `-List` to print every imported library. The libraries checked are
Winsock (`ws2_32`, `wsock32`, `mswsock`), WinHTTP, WinINet, URL monikers,
the HTTP server API, IP helper, DNS, WebSocket, RAS, WLAN, NetAPI, BITS,
MSXML and the WinRT networking stacks. For comparison, the same script
reports `FAIL` for `C:\Windows\System32\curl.exe` and for Windows Notepad.

The check runs in the test suite (`ctest -R no-network`) for every build, so
a change that links networking code fails CI before it can ship.

### What this does and does not prove

- It proves the four shipped binaries have no way to open a socket or make an
  HTTP request themselves.
- It does not cover programs the editor starts **at your request**: **Open
  Terminal Here** starts your terminal, and **Refresh Git Change Markers**
  runs your installed `git diff` on the current file. Neither is started
  automatically, and Notepad:: passes them no network-related arguments.
- Files you open from a network share are read through the Windows file
  system, like any other path.
- Microsoft Store updates are performed by the Store, not by the editor. The
  portable build never checks for updates.

### How the build keeps it true

- The editor links a Tree-sitter runtime compiled **without** the WebAssembly
  feature. The Wasm engine exists only in the language host.
- The language host is linked with unreferenced code removed in every
  configuration, so the parts of the Wasm runtime it never enables (WASI
  sockets, for example) are not in the binary.

## 2. Sandboxed language extensions

Custom languages are data: a JSON definition, Tree-sitter query files and,
optionally, a Tree-sitter grammar compiled to WebAssembly. See
[custom languages](custom-languages.md) for the format.

| Layer | Limit |
|---|---|
| Format | Definitions and queries ≤ 1 MiB, Wasm modules ≤ 16 MiB, at most 256 files. Paths must be relative; absolute paths and `..` are rejected. The Wasm magic and version are checked before use. |
| Process | Wasm never runs in the editor process. A hidden `notepad-colon-language-host.exe` is started per document that needs it. |
| Job Object | One process (the host cannot spawn children), 256 MiB process memory, 25% hard CPU rate. The job is terminated with the editor. |
| Time | Configuration requests time out after 5 s, parse requests after 1.5 s. A timeout terminates the whole job. |
| Capabilities | The host exposes **no** WASI, file-system, network or process imports to the module. A grammar can only compute over the bytes the editor sends it. |
| Protocol | A fixed binary protocol with a magic number, version and a 32 MiB payload cap. Malformed replies stop the host. |
| Failure | If the host is rejected, crashes or times out, that document loses Wasm highlighting. The editor and the document stay open and editable. |

There is deliberately no fallback to native grammar DLLs: a DLL in the user
language directory is never loaded.

The sandbox limits are exercised by `ctest -L security`.

## Reporting a problem

If you find a way around either claim, please open an issue at
<https://github.com/mwfl/notepad-colon/issues>.
