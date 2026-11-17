param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot "..\build\vs2026-x64"),
    [ValidateSet("Release")]
    [string]$Configuration = "Release",
    [ValidatePattern('^[1-9][0-9]*\.[0-9]+\.[0-9]+\.0$')]
    [string]$PackageVersion = "1.3.2.0",
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\artifacts\store")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$build = [System.IO.Path]::GetFullPath($BuildDirectory)
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $output "stage-$Architecture"
$package = Join-Path $output "notepad-colon-$PackageVersion-$Architecture.msix"

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) {
    $cmake = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio" -Filter cmake.exe -Recurse |
        Where-Object { $_.FullName -match "CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake\.exe$" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
}
if (-not $cmake) {
    throw "cmake.exe was not found."
}
$makeAppx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter makeappx.exe -Recurse |
    Where-Object { $_.FullName -match "\\x64\\makeappx\.exe$" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $makeAppx) {
    throw "makeappx.exe was not found in the Windows 10 SDK."
}
$makePri = Join-Path $makeAppx.DirectoryName "makepri.exe"
if (-not (Test-Path -LiteralPath $makePri -PathType Leaf)) {
    throw "makepri.exe was not found alongside makeappx.exe."
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

$cmakePath = if ($cmake.Source) { $cmake.Source } else { $cmake.FullName }
& $cmakePath --install $build --config $Configuration --component product --prefix $stage
if ($LASTEXITCODE -ne 0) {
    throw "CMake install failed with exit code $LASTEXITCODE."
}

$assets = Join-Path $stage "Assets"
New-Item -ItemType Directory -Path $assets -Force | Out-Null
$sourceLogo = Join-Path $repository "assets\notepad-colon-256.png"

function Write-ScaledPng([string]$Destination, [int]$Width, [int]$Height, [int]$LogoSize) {
    $canvas = [System.Drawing.Bitmap]::new($Width, $Height)
    $graphics = [System.Drawing.Graphics]::FromImage($canvas)
    $source = [System.Drawing.Image]::FromFile($sourceLogo)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $x = [int](($Width - $LogoSize) / 2)
        $y = [int](($Height - $LogoSize) / 2)
        $graphics.DrawImage($source, $x, $y, $LogoSize, $LogoSize)
        $canvas.Save($Destination, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $source.Dispose()
        $graphics.Dispose()
        $canvas.Dispose()
    }
}

Write-ScaledPng (Join-Path $assets "StoreLogo.png") 50 50 50
Write-ScaledPng (Join-Path $assets "Square44x44Logo.png") 44 44 44
Write-ScaledPng (Join-Path $assets "Square150x150Logo.png") 150 150 150
Write-ScaledPng (Join-Path $assets "Wide310x150Logo.png") 310 150 120

# Match the manifest's Square44x44Logo resource name. Windows needs all three
# variants even when their artwork is identical, otherwise it may add a plate.
foreach ($size in @(16, 20, 24, 30, 32, 36, 40, 44, 48, 60, 64, 72, 80, 96, 256)) {
    foreach ($variant in @("", "_altform-unplated", "_altform-lightunplated")) {
        Write-ScaledPng (Join-Path $assets "Square44x44Logo.targetsize-$size$variant.png") $size $size $size
    }
}

$manifestTemplate = Get-Content -Raw (Join-Path $repository "packaging\msix\AppxManifest.xml.in")
$manifest = $manifestTemplate.Replace("@PACKAGE_VERSION@", $PackageVersion).Replace("@ARCHITECTURE@", $Architecture)
[System.IO.File]::WriteAllText((Join-Path $stage "AppxManifest.xml"), $manifest, [System.Text.UTF8Encoding]::new($false))

# Keep build configuration outside the package. Index the qualified image names
# before MakeAppx runs so Windows can select their target size and theme.
$priConfig = Join-Path $output "priconfig-$Architecture.xml"
& $makePri createconfig /cf $priConfig /dq en-US /o
if ($LASTEXITCODE -ne 0) {
    throw "MakePri createconfig failed with exit code $LASTEXITCODE."
}
# Deliver one self-contained package, not separate language/scale resource packs.
[xml]$priConfiguration = Get-Content -Raw -LiteralPath $priConfig
foreach ($node in @($priConfiguration.SelectNodes("//packaging"))) {
    [void]$node.ParentNode.RemoveChild($node)
}
$priConfiguration.Save($priConfig)
$priFile = Join-Path $stage "resources.pri"
& $makePri new /pr $stage /cf $priConfig /of $priFile /o
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $priFile -PathType Leaf)) {
    throw "MakePri did not generate resources.pri successfully (exit code $LASTEXITCODE)."
}

if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Force
}
& $makeAppx.FullName pack /d $stage /p $package /o
if ($LASTEXITCODE -ne 0) {
    throw "MakeAppx failed with exit code $LASTEXITCODE."
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $package).Hash.ToLowerInvariant()
Write-Host "Created $package"
Write-Host "SHA256 $hash"
