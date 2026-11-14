param([Parameter(Mandatory = $true)][string]$Executable)

# Windows PowerShell 5.1 treats a missing method as a non-terminating error and
# would report success; make every failure fatal.
$ErrorActionPreference = 'Stop'

Add-Type -Namespace NotepadColonTest -Name Windows -MemberDefinition @'
    public delegate bool EnumProc(System.IntPtr window, System.IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, System.IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(System.IntPtr window, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(System.IntPtr window, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr window);
'@

function Get-ProcessWindows([uint32]$ProcessId) {
    $found = [Collections.Generic.List[string]]::new()
    $callback = [NotepadColonTest.Windows+EnumProc]{
        param($window, $parameter)
        [uint32]$owner = 0
        [void][NotepadColonTest.Windows]::GetWindowThreadProcessId($window, [ref]$owner)
        if ($owner -eq $ProcessId) {
            $title = [Text.StringBuilder]::new(256)
            $class = [Text.StringBuilder]::new(256)
            [void][NotepadColonTest.Windows]::GetWindowTextW($window, $title, 256)
            [void][NotepadColonTest.Windows]::GetClassNameW($window, $class, 256)
            $found.Add(("[{0}] '{1}' visible={2}" -f $class, $title,
                [NotepadColonTest.Windows]::IsWindowVisible($window)))
        }
        return $true
    }
    [void][NotepadColonTest.Windows]::EnumWindows($callback, [IntPtr]::Zero)
    return $found
}

function Format-Hex([byte[]]$Bytes) { return [BitConverter]::ToString($Bytes) }

$path = Join-Path $env:TEMP ("notepad-colon-large-self-test-{0}.cpp" -f $PID)
$line = [Text.Encoding]::UTF8.GetBytes("int original_value = 42; // streaming edit`r`n")
$target = [long]($line.Length * [Math]::Ceiling(34MB / $line.Length))
# Write whole megabyte-sized blocks: one write per line means some 800,000
# interpreted loop iterations, which occasionally took longer than the test's
# time limit on a busy machine.
$linesPerBlock = [int][Math]::Floor(1MB / $line.Length)
$block = [byte[]]::new($line.Length * $linesPerBlock)
for ($index = 0; $index -lt $linesPerBlock; $index++) {
    [Array]::Copy($line, 0, $block, $index * $line.Length, $line.Length)
}
$stream = [IO.File]::Open($path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
try {
    for ($remaining = $target; $remaining -gt 0; $remaining -= $count) {
        $count = [int][Math]::Min($block.Length, $remaining)
        $stream.Write($block, 0, $count)
    }
} finally {
    $stream.Dispose()
}

try {
    $process = Start-Process -FilePath $Executable `
        -ArgumentList @("--large-file-self-test", ('"{0}"' -f $path)) `
        -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(40000)) {
        # Report what the process is showing before killing it, so a hang is diagnosable.
        $windows = (Get-ProcessWindows ([uint32]$process.Id)) -join '; '
        $process.Kill()
        throw "large-file GUI self-test did not exit within 40 s; windows: $windows"
    }
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
        if ((Format-Hex $actualMarker) -ne (Format-Hex $expectedMarker)) {
            throw "saved marker mismatch"
        }
        $input.Seek(-$line.Length, [IO.SeekOrigin]::End) | Out-Null
        $tail = [byte[]]::new($line.Length)
        $input.Read($tail, 0, $tail.Length) | Out-Null
        if ((Format-Hex $tail) -ne (Format-Hex $line)) {
            throw "tail changed during streaming save"
        }
    } finally {
        $input.Dispose()
    }
} finally {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath ($path + '.bak') -Force -ErrorAction SilentlyContinue
}
