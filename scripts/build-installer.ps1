<#
.SYNOPSIS
    Builds the NSIS installer (setup.exe) for Notepad::.

.DESCRIPTION
    Stages the product files, either from a published portable ZIP (-Zip, so the
    installer carries exactly the released binaries) or from a Release build
    directory, and compiles packaging/nsis/installer.nsi with makensis.

    The installer is per-user and unsigned. Nothing is uploaded.

.EXAMPLE
    ./scripts/build-installer.ps1 -Zip notepad-colon-v1.3.2-windows-x64-portable.zip
#>
param(
    [string]$Zip,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot "..\build\vs2026-x64"),
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\artifacts\installer")
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $output "stage-$Architecture"

$makeNsis = Get-Command makensis.exe -ErrorAction SilentlyContinue
$makeNsisPath = if ($makeNsis) { $makeNsis.Source } else {
    @("${env:ProgramFiles(x86)}\NSIS\makensis.exe", "$env:ProgramFiles\NSIS\makensis.exe") |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $makeNsisPath) {
    throw "makensis.exe was not found. Install NSIS 3 (winget install NSIS.NSIS)."
}

New-Item -ItemType Directory -Path $output -Force | Out-Null
if (Test-Path -LiteralPath $stage) {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    if (-not $resolvedStage.StartsWith($output, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected staging directory: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

if ($Zip) {
    Expand-Archive -LiteralPath $Zip -DestinationPath $stage
} else {
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $cmake) {
        $cmake = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio" -Filter cmake.exe -Recurse |
            Where-Object { $_.FullName -match "CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake\.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
    }
    if (-not $cmake) { throw "cmake.exe was not found." }
    $cmakePath = if ($cmake.Source) { $cmake.Source } else { $cmake.FullName }
    $build = [System.IO.Path]::GetFullPath($BuildDirectory)
    & $cmakePath --install $build --config Release --component product --prefix $stage
    if ($LASTEXITCODE -ne 0) { throw "CMake install failed with exit code $LASTEXITCODE." }
}

$stage = (Resolve-Path -LiteralPath $stage).Path
$executable = Join-Path $stage "notepad-colon.exe"
foreach ($name in @("notepad-colon.exe", "notepad-colon-language-host.exe", "Scintilla.dll", "Lexilla.dll", "LICENSE")) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $name) -PathType Leaf)) {
        throw "The staged product is missing $name."
    }
}
# The installer must describe the binaries it carries, not the checkout.
$productVersion = (Get-Item -LiteralPath $executable).VersionInfo.ProductVersion
if (-not $Version) { $Version = $productVersion }
if ($Version -ne $productVersion) {
    throw "Requested version $Version, but the staged notepad-colon.exe is $productVersion."
}

# List every file explicitly so the uninstaller removes exactly what was
# installed and never deletes other files a user keeps in the same folder.
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
$installedKb = [int][Math]::Ceiling(($files | Measure-Object Length -Sum).Sum / 1024)
$install = [System.Collections.Generic.List[string]]::new()
$remove = [System.Collections.Generic.List[string]]::new()
$directories = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$current = $null
foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length + 1)
    if ($relative -match '[\$"]') { throw "Unsupported character in product file name: $relative" }
    $directory = [System.IO.Path]::GetDirectoryName($relative)
    if ($directory -ne $current) {
        $target = if ($directory) { "`$INSTDIR\$directory" } else { "`$INSTDIR" }
        $install.Add("    SetOutPath `"$target`"")
        $current = $directory
    }
    $install.Add("    File `"$($file.FullName)`"")
    $remove.Add("    Delete `"`$INSTDIR\$relative`"")
    while ($directory) {
        [void]$directories.Add($directory)
        $directory = [System.IO.Path]::GetDirectoryName($directory)
    }
}
# Deepest directories first; RMDir without /r only removes empty ones.
foreach ($directory in @($directories | Sort-Object { $_.Split('\').Count } -Descending)) {
    $remove.Add("    RMDir `"`$INSTDIR\$directory`"")
}
$list = Join-Path $output "files-$Architecture.nsh"
$content = @("!define INSTALLED_KB $installedKb", "!macro InstallFiles") + $install +
    @("!macroend", "!macro UninstallFiles") + $remove + @("!macroend")
[System.IO.File]::WriteAllLines($list, $content, [System.Text.UTF8Encoding]::new($true))

$package = Join-Path $output "notepad-colon-v$Version-windows-$Architecture-setup.exe"
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Force }
$script = Join-Path $repository "packaging\nsis\installer.nsi"
& $makeNsisPath /V2 /INPUTCHARSET UTF8 "/DVERSION=$Version" "/DVERSION4=$Version.0" "/DARCH=$Architecture" `
    "/DSTAGE=$stage" "/DFILELIST=$list" "/DOUTFILE=$package" $script
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $package -PathType Leaf)) {
    throw "makensis failed with exit code $LASTEXITCODE."
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $package).Hash.ToLowerInvariant()
"$hash  $([System.IO.Path]::GetFileName($package))" | Set-Content "$package.sha256" -Encoding ascii
Write-Host "Created $package"
Write-Host "SHA256 $hash"
