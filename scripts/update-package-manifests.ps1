<#
.SYNOPSIS
    Generates the winget and Scoop manifests for a published portable release.

.DESCRIPTION
    Package managers pin each download by URL and SHA-256, so the manifests can
    only be produced from the exact ZIP files attached to a GitHub release. This
    script hashes those files, fills in the templates under packaging/winget and
    packaging/scoop, and writes ready-to-submit manifests to artifacts/packages.

    Nothing is uploaded or submitted: review the output, then open a pull
    request against microsoft/winget-pkgs or your Scoop bucket yourself.

.EXAMPLE
    ./scripts/update-package-manifests.ps1 -Version 1.3.0 `
        -X64Zip artifacts/notepad-colon-1.3.0-windows-x64-portable.zip `
        -Arm64Zip artifacts/notepad-colon-1.3.0-windows-arm64-portable.zip
#>
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory = $true)][string]$X64Zip,
    [string]$Arm64Zip,
    # Download URL of each ZIP. Defaults to the release asset with the file's own name.
    [string]$X64Url,
    [string]$Arm64Url,
    [string]$Repository = 'mwfl/notepad-colon',
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'artifacts/packages' }

function Get-Asset([string]$Path, [string]$Url) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "ZIP not found: $Path" }
    $item = Get-Item -LiteralPath $Path
    if (-not $Url) {
        $Url = "https://github.com/$Repository/releases/download/v$Version/$($item.Name)"
    }
    # The package must really be this application: a wrong file would be pinned forever.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($item.FullName)
    try {
        $names = $archive.Entries | ForEach-Object { $_.FullName }
        foreach ($required in 'notepad-colon.exe', 'notepad-colon-language-host.exe', 'Scintilla.dll', 'Lexilla.dll') {
            if ($names -notcontains $required) { throw "$($item.Name) does not contain $required at its root." }
        }
    } finally { $archive.Dispose() }
    return @{
        Url = $Url
        Hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}

function Expand-Template([string]$Template, [hashtable]$Values) {
    $text = [IO.File]::ReadAllText($Template)
    foreach ($key in $Values.Keys) { $text = $text.Replace("{{$key}}", [string]$Values[$key]) }
    if ($text -match '\{\{[A-Za-z0-9]+\}\}') { throw "Unfilled placeholder $($Matches[0]) in $Template" }
    return $text
}

$x64 = Get-Asset $X64Zip $X64Url
$values = @{
    Version = $Version
    ReleaseDate = (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd')
    Repository = $Repository
    X64Url = $x64.Url
    X64Hash = $x64.Hash
}
$arm64Block = ''
$scoopArm64 = ''
if ($Arm64Zip) {
    $arm64 = Get-Asset $Arm64Zip $Arm64Url
    $arm64Block = @"
- Architecture: arm64
  InstallerUrl: $($arm64.Url)
  InstallerSha256: $($arm64.Hash)
"@
    $scoopArm64 = @"
,
        "arm64": {
            "url": "$($arm64.Url)",
            "hash": "$($arm64.Hash.ToLowerInvariant())"
        }
"@
}
$values.Arm64Installer = $arm64Block
$values.ScoopArm64 = $scoopArm64
$values.X64HashLower = $x64.Hash.ToLowerInvariant()

$utf8 = [Text.UTF8Encoding]::new($false)
$wingetOut = Join-Path $OutputDirectory "winget/manifests/a/AtomSprout/NotepadColon/$Version"
New-Item -ItemType Directory -Force -Path $wingetOut | Out-Null
foreach ($template in Get-ChildItem -LiteralPath (Join-Path $root 'packaging/winget') -Filter '*.yaml') {
    $text = (Expand-Template $template.FullName $values).TrimEnd() + "`n"
    [IO.File]::WriteAllText((Join-Path $wingetOut $template.Name), $text, $utf8)
}
$scoopOut = Join-Path $OutputDirectory 'scoop'
New-Item -ItemType Directory -Force -Path $scoopOut | Out-Null
$scoop = Expand-Template (Join-Path $root 'packaging/scoop/notepad-colon.json') $values
# Fail here rather than in a user's terminal if the manifest is not valid JSON.
$parsed = $scoop | ConvertFrom-Json
if ($parsed.version -ne $Version) { throw 'Scoop manifest version mismatch.' }
[IO.File]::WriteAllText((Join-Path $scoopOut 'notepad-colon.json'), $scoop.TrimEnd() + "`n", $utf8)

Write-Output "winget manifests: $wingetOut"
Write-Output "Scoop manifest:   $(Join-Path $scoopOut 'notepad-colon.json')"
Write-Output "Validate with:    winget validate --manifest `"$wingetOut`""
