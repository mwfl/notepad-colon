# Wasm grammar fixture

`tree-sitter-json.wasm` is generated from the official
[`tree-sitter/tree-sitter-json`](https://github.com/tree-sitter/tree-sitter-json)
v0.24.8 parser source using the official Tree-sitter CLI v0.26.11:

```powershell
tree-sitter build --wasm --output tree-sitter-json.wasm path/to/tree-sitter-json
```

The grammar is MIT licensed. The fixture is test-only and is not installed in
the product package.
