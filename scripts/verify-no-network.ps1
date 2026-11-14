<#
.SYNOPSIS
    Proves from the binaries themselves that the editor cannot use the network.

.DESCRIPTION
    A Windows program can only call a system API it imports. This script reads
    the import and delay-import tables of each given PE file and fails if any
    of them references a networking library. It needs no SDK tools, so anyone
    can run it against a downloaded release:

        ./scripts/verify-no-network.ps1 notepad-colon.exe Scintilla.dll Lexilla.dll

    -List prints every imported DLL instead of only the verdict.
#>
param(
    [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Path,
    [switch]$List
)

$ErrorActionPreference = 'Stop'

# Winsock, WinHTTP, WinINet, URL monikers, HTTP server, IP helper, DNS, RPC
# over HTTP, WebSocket, BITS, RAS, WLAN and the WinRT networking stacks.
$networkLibraries = @(
    'ws2_32.dll', 'wsock32.dll', 'mswsock.dll', 'winhttp.dll', 'wininet.dll', 'urlmon.dll',
    'httpapi.dll', 'iphlpapi.dll', 'dnsapi.dll', 'webio.dll', 'websocket.dll', 'rasapi32.dll',
    'wlanapi.dll', 'netapi32.dll', 'qmgr.dll', 'msxml3.dll', 'msxml6.dll',
    'windows.networking.dll', 'windows.web.http.dll'
)

# An import table only lists libraries bound at load time. A program could
# still call LoadLibrary("winhttp.dll") at run time, so the file is also
# searched for those names as ASCII and UTF-16 text.
function Find-LibraryNames([string]$File) {
    $bytes = [IO.File]::ReadAllBytes($File)
    $latin = [Text.Encoding]::GetEncoding(28591).GetString($bytes).ToLowerInvariant()
    $found = @()
    foreach ($library in $networkLibraries) {
        $wide = ($library.ToCharArray() | ForEach-Object { "$_`0" }) -join ''
        if ($latin.Contains($library) -or $latin.Contains($wide)) { $found += $library }
    }
    return $found
}

function Get-ImportedLibraries([string]$File) {
    $bytes = [IO.File]::ReadAllBytes($File)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "$File is not a PE file." }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -le 0 -or $pe + 0x18 -ge $bytes.Length -or
        [BitConverter]::ToUInt32($bytes, $pe) -ne 0x00004550) { throw "$File has no PE header." }
    $sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
    $optional = $pe + 24
    $magic = [BitConverter]::ToUInt16($bytes, $optional)
    $directories = if ($magic -eq 0x20B) { $optional + 112 } elseif ($magic -eq 0x10B) { $optional + 96 }
                   else { throw "$File has an unknown optional header." }
    $sections = @()
    $table = $optional + $optionalSize
    for ($index = 0; $index -lt $sectionCount; $index++) {
        $entry = $table + 40 * $index
        $sections += [pscustomobject]@{
            VirtualSize = [BitConverter]::ToUInt32($bytes, $entry + 8)
            VirtualAddress = [BitConverter]::ToUInt32($bytes, $entry + 12)
            RawSize = [BitConverter]::ToUInt32($bytes, $entry + 16)
            RawPointer = [BitConverter]::ToUInt32($bytes, $entry + 20)
        }
    }
    $toOffset = {
        param([uint32]$rva)
        foreach ($section in $sections) {
            $size = [Math]::Max($section.VirtualSize, $section.RawSize)
            if ($rva -ge $section.VirtualAddress -and $rva -lt $section.VirtualAddress + $size) {
                return [int]($rva - $section.VirtualAddress + $section.RawPointer)
            }
        }
        return -1
    }
    $readName = {
        param([uint32]$rva)
        $offset = & $toOffset $rva
        if ($offset -lt 0) { return $null }
        $end = $offset
        while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
        return [Text.Encoding]::ASCII.GetString($bytes, $offset, $end - $offset)
    }
    $names = [Collections.Generic.List[string]]::new()
    # Directory 1: imports (20-byte descriptors, name RVA at +12).
    # Directory 13: delay imports (32-byte descriptors, name RVA at +4).
    foreach ($directory in @(@{ Index = 1; Size = 20; Name = 12 }, @{ Index = 13; Size = 32; Name = 4 })) {
        $rva = [BitConverter]::ToUInt32($bytes, $directories + 8 * $directory.Index)
        if ($rva -eq 0) { continue }
        $offset = & $toOffset $rva
        if ($offset -lt 0) { throw "$File has an unmapped import directory." }
        while ($offset + $directory.Size -le $bytes.Length) {
            $nameRva = [BitConverter]::ToUInt32($bytes, $offset + $directory.Name)
            if ($nameRva -eq 0) { break }
            $name = & $readName $nameRva
            if ($name) { $names.Add($name.ToLowerInvariant()) }
            $offset += $directory.Size
        }
    }
    return $names | Sort-Object -Unique
}

$failed = $false
foreach ($item in $Path) {
    $file = (Resolve-Path -LiteralPath $item).Path
    $imports = @(Get-ImportedLibraries $file)
    if ($imports.Count -eq 0) { throw "$file imports nothing; refusing to treat that as proof." }
    $network = @($imports | Where-Object { $networkLibraries -contains $_ })
    $named = @(Find-LibraryNames $file | Where-Object { $network -notcontains $_ })
    $name = Split-Path -Leaf $file
    if ($List) { Write-Output ("{0}: {1}" -f $name, ($imports -join ', ')) }
    if ($network.Count -ne 0) {
        Write-Output "FAIL  $name imports networking libraries: $($network -join ', ')"
        $failed = $true
    } elseif ($named.Count -ne 0) {
        Write-Output "FAIL  $name names networking libraries it could load at run time: $($named -join ', ')"
        $failed = $true
    } else {
        Write-Output "OK    $name imports $($imports.Count) libraries; no networking library is imported or named"
    }
}
if ($failed) { exit 1 }
