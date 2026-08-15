param([Parameter(Mandatory = $true)][string]$Executable)

$path = Join-Path $env:TEMP ("notepad-colon-large-self-test-{0}.cpp" -f $PID)
$line = [Text.Encoding]::UTF8.GetBytes("int original_value = 42; // streaming edit`r`n")
$target = [long]($line.Length * [Math]::Ceiling(34MB / $line.Length))
$stream = [IO.File]::Open($path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
try {
    while ($stream.Length -lt $target) {
        $count = [Math]::Min($line.Length, $target - $stream.Length)
        $stream.Write($line, 0, $count)
    }
} finally {
    $stream.Dispose()
}

try {
    $process = Start-Process -FilePath $Executable `
        -ArgumentList @("--large-file-self-test", ('"{0}"' -f $path)) `
        -PassThru -Wait -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "large-file GUI self-test exited $($process.ExitCode)" }
    $expectedMarker = [Text.Encoding]::UTF8.GetBytes("/*NPC-LARGE-EDIT*/")
    $input = [IO.File]::OpenRead($path)
    try {
        if ($input.Length -ne $target + $expectedMarker.Length) {
            throw "saved length was $($input.Length), expected $($target + $expectedMarker.Length)"
        }
        $actualMarker = [byte[]]::new($expectedMarker.Length)
        if ($input.Read($actualMarker, 0, $actualMarker.Length) -ne $actualMarker.Length) {
            throw "could not read saved marker"
        }
        if ([Convert]::ToHexString($actualMarker) -ne [Convert]::ToHexString($expectedMarker)) {
            throw "saved marker mismatch"
        }
        $input.Seek(-$line.Length, [IO.SeekOrigin]::End) | Out-Null
        $tail = [byte[]]::new($line.Length)
        $input.Read($tail, 0, $tail.Length) | Out-Null
        if ([Convert]::ToHexString($tail) -ne [Convert]::ToHexString($line)) {
            throw "tail changed during streaming save"
        }
    } finally {
        $input.Dispose()
    }
} finally {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}
