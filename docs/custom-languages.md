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
    "grammar": "json",
    "highlights": "acme-highlights.scm",
    "symbols": "acme-symbols.scm"
  }
}
```

Available grammar IDs are currently `cpp` and `json`. Highlight captures may
include `comment`, `string`, `number`, `keyword`, `type`, `function`,
`property`, `variable`, `constant`, and `preprocessor`. Symbol captures use
names such as `symbol.function` and `symbol.class`.

Each definition and query is limited to 1 MiB. IDs, extensions, duplicate
definitions, UTF-8, and relative paths are validated. Absolute paths and `..`
escapes are rejected.

The extension path is staged:

1. Bundled grammar plus user queries and file associations (implemented).
2. More bundled grammars selected by stable IDs (C++ and JSON implemented).
3. Explicitly installed WebAssembly grammars with resource limits (planned;
   native grammar DLLs will not be a fallback).
