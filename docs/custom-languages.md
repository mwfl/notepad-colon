# Custom languages

Notepad Colon loads language definitions from:

```text
%LOCALAPPDATA%\mwfl\Notepad Colon\languages\*.json
```

Use **Language > Reload Language Definitions** after editing a definition.
Definitions select a bundled, sandboxed Tree-sitter grammar and supply query
files; they never load native DLLs from the user language directory.

```json
{
  "id": "acme-data",
  "name": "Acme Data",
  "extensions": [".adata"],
  "filenames": ["acme.lock"],
  "fallbackLexer": "json",
  "treeSitter": {
    "grammar": "wasm",
    "language": "json",
    "module": "tree-sitter-json.wasm",
    "highlights": "acme-highlights.scm",
    "symbols": "acme-symbols.scm"
  }
}
```

Available grammar IDs are `cpp`, `json`, and `wasm`. For `wasm`, `language` is
the exported Tree-sitter language name and `module` is a relative path to a
Tree-sitter Wasm grammar. Highlight captures may
include `comment`, `string`, `number`, `keyword`, `type`, `function`,
`property`, `variable`, `constant`, and `preprocessor`. Symbol captures use
names such as `symbol.function` and `symbol.class`.

Each definition and query is limited to 1 MiB and a Wasm module to 16 MiB. IDs,
extensions, duplicate definitions, UTF-8, the Wasm magic/version, and relative
paths are validated. Absolute paths and `..` escapes are rejected.

Wasm never runs in the GUI process. Notepad Colon starts a hidden language host
under a Windows Job Object with one-process, 256 MiB process-memory, and 25%
hard CPU-rate limits. Configure requests time out after 5 seconds and parse
requests after 1.5 seconds; a timeout terminates the entire job. The host does
not expose WASI, filesystem, network, or process imports. A host failure disables
that document's Wasm coloring while the editor and document remain available.

The extension path is staged:

1. Bundled grammar plus user queries and file associations (implemented).
2. More bundled grammars selected by stable IDs (C++ and JSON implemented).
3. WebAssembly grammars in an out-of-process, resource-limited host
   (implemented; native grammar DLLs are not a fallback).
