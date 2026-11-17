<#
.SYNOPSIS
    Installs the NSIS installer silently into a scratch directory, checks the
    result, and uninstalls it again.

.DESCRIPTION
    The installer is per-user, so this registers and then removes an
    "Apps & features" entry and a Start menu shortcut for the current user. It
    refuses to run when Notepad:: is already installed for this user.
#>
param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [string]$Version
)

$ErrorActionPreference = "Stop"
$installer = (Resolve-Path -LiteralPath $Installer).Path
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\NotepadColon"
$verbKey = "HKCU:\Software\Classes\Directory\shell\NotepadColon"
$shortcut = Join-Path ([Environment]::GetFolderPath("Programs")) "Notepad Colon.lnk"
if (Test-Path -LiteralPath $uninstallKey) {
    throw "Notepad:: is already installed for this user; not touching that installation."
}
if (Test-Path -LiteralPath $verbKey) {
    throw "An Explorer context menu entry already exists for this user; not touching it."
}

function Invoke-AndWait([string]$Path, [string[]]$Arguments) {
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(120000)) { $process.Kill(); throw "$Path timed out." }
    return $process.ExitCode
}

# A path with a space, as the default location has. /D must be last and unquoted.
$target = Join-Path ([System.IO.Path]::GetTempPath()) ("Notepad Colon test " + [guid]::NewGuid().ToString("N"))
$editor = $null
try {
    $code = Invoke-AndWait $installer @("/S", "/D=$target")
    if ($code -ne 0) { throw "Silent install exited $code." }
    foreach ($name in @("notepad-colon.exe", "notepad-colon-language-host.exe", "Scintilla.dll", "Lexilla.dll",
                        "vcruntime140.dll", "LICENSE", "uninstall.exe", "docs\distribution.md")) {
        if (-not (Test-Path -LiteralPath (Join-Path $target $name) -PathType Leaf)) { throw "Install is missing $name." }
    }
    $entry = Get-ItemProperty -LiteralPath $uninstallKey
    if ($entry.InstallLocation -ne $target) { throw "InstallLocation is '$($entry.InstallLocation)'." }
    if ($entry.Publisher -ne "AtomSprout") { throw "Publisher is '$($entry.Publisher)'." }
    $installed = (Get-Item -LiteralPath (Join-Path $target "notepad-colon.exe")).VersionInfo.ProductVersion
    if ($entry.DisplayVersion -ne $installed) { throw "Registered $($entry.DisplayVersion) but installed $installed." }
    if ($Version -and $installed -ne $Version) { throw "Installed $installed, expected $Version." }
    if (-not (Test-Path -LiteralPath $shortcut -PathType Leaf)) { throw "Start menu shortcut is missing." }

    # The installed copy must run on its own DLLs. The fixture is test-only and
    # doubles as a foreign file the uninstaller has to leave alone.
    $fixture = Join-Path $target "tree-sitter-json-test.wasm"
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "../tests/fixtures/tree-sitter-json.wasm") -Destination $fixture
    $originalPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $editor = Start-Process -FilePath (Join-Path $target "notepad-colon.exe") -ArgumentList "--self-test" `
            -WorkingDirectory $target -WindowStyle Hidden -PassThru
        # While the editor holds its files, a silent reinstall must fail, not corrupt them.
        Start-Sleep -Milliseconds 300
        if (-not $editor.HasExited) {
            $busy = Invoke-AndWait $installer @("/S", "/D=$target")
            if ($busy -eq 0 -and -not $editor.HasExited) { throw "Reinstall succeeded over a running editor." }
        }
        if (-not $editor.WaitForExit(30000)) { $editor.Kill(); throw "Installed self-test timed out." }
        if ($editor.ExitCode -ne 0) { throw "Installed self-test failed: $($editor.ExitCode)." }
    } finally { $env:PATH = $originalPath }

    # Upgrade in place, then plant an owned Explorer verb pointing at this install.
    $code = Invoke-AndWait $installer @("/S", "/D=$target")
    if ($code -ne 0) { throw "Reinstall exited $code." }
    New-Item -Path $verbKey -Force | Out-Null
    Set-ItemProperty -LiteralPath $verbKey -Name "NotepadColonOwner" -Value "mwfl.notepad-colon"
    Set-ItemProperty -LiteralPath $verbKey -Name "Icon" -Value "`"$target\notepad-colon.exe`",0"

    # The uninstaller re-launches itself from a temporary copy and returns at once.
    [void](Invoke-AndWait (Join-Path $target "uninstall.exe") @("/S"))
    for ($i = 0; $i -lt 100 -and (Test-Path -LiteralPath $uninstallKey); $i++) { Start-Sleep -Milliseconds 200 }
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $uninstallKey) { throw "Uninstall left the Apps & features entry." }
    if (Test-Path -LiteralPath $shortcut) { throw "Uninstall left the Start menu shortcut." }
    if (Test-Path -LiteralPath $verbKey) { throw "Uninstall left its Explorer context menu entry." }
    $left = @(Get-ChildItem -LiteralPath $target -Recurse -Force | ForEach-Object { $_.FullName.Substring($target.Length + 1) })
    if ($left.Count -ne 1 -or $left[0] -ne "tree-sitter-json-test.wasm") {
        throw "Uninstall should leave only the foreign file; left: $($left -join ', ')"
    }
    Write-Host "Installer installs, upgrades, refuses to overwrite a running editor, and uninstalls cleanly."
}
finally {
    if ($editor -and -not $editor.HasExited) { $editor.Kill() }
    # Never leave test registrations behind, whatever failed above.
    $registered = (Get-ItemProperty -LiteralPath $uninstallKey -ErrorAction SilentlyContinue).InstallLocation
    if ($registered -eq $target) { Remove-Item -LiteralPath $uninstallKey -Recurse -Force }
    $icon = (Get-ItemProperty -LiteralPath $verbKey -ErrorAction SilentlyContinue).Icon
    if ($icon -and $icon.Contains($target)) { Remove-Item -LiteralPath $verbKey -Recurse -Force }
    if ($registered -eq $target -and (Test-Path -LiteralPath $shortcut)) { Remove-Item -LiteralPath $shortcut -Force }
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
}
