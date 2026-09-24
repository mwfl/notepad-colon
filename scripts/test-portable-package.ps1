param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory
)

$ErrorActionPreference = "Stop"
$archives = @(Get-ChildItem -LiteralPath $BuildDirectory -Filter "notepad-colon-*.zip")
if ($archives.Count -ne 1) {
    throw "Expected exactly one portable ZIP in $BuildDirectory; found $($archives.Count)."
}
# Use a fresh directory so stale DLLs cannot hide an incomplete package.
$stage = Join-Path $BuildDirectory ("package-check-" + [guid]::NewGuid().ToString("N"))
Expand-Archive -LiteralPath $archives[0].FullName -DestinationPath $stage
$stage = (Resolve-Path -LiteralPath $stage).Path
foreach ($name in @("notepad-colon.exe", "notepad-colon-language-host.exe",
                    "Scintilla.dll", "Lexilla.dll", "msvcp140.dll", "msvcp140_atomic_wait.dll",
                    "vcruntime140.dll", "vcruntime140_1.dll")) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $name) -PathType Leaf)) {
        throw "Portable ZIP is missing $name."
    }
}
if (Get-ChildItem -LiteralPath $stage -File | Where-Object {
    $_.Name -match '^(msvcp\d+.*d|vcruntime\d+.*d|ucrtbased)\.dll$'
}) {
    throw "Portable ZIP contains non-redistributable debug runtime DLLs."
}
$originalPath = $env:PATH
# The GUI self-test exercises Wasm with a test-only fixture, not a product file.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "../tests/fixtures/tree-sitter-json.wasm") `
    -Destination (Join-Path $stage "tree-sitter-json-test.wasm")
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = Start-Process -FilePath (Join-Path $stage "notepad-colon.exe") `
        -ArgumentList "--self-test" -WorkingDirectory $stage -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw "Packaged application self-test timed out."
    }
    if ($process.ExitCode -ne 0) {
        throw "Packaged application self-test failed: $($process.ExitCode)."
    }
}
finally {
    $env:PATH = $originalPath
}
Write-Host "Portable ZIP includes the runtime DLLs and passes the application self-test."
