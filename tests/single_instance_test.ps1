param([Parameter(Mandatory = $true)][string]$Executable)

$testFile = Join-Path ([IO.Path]::GetTempPath()) ("notepad-colon-activation-{0}.txt" -f $PID)
[IO.File]::WriteAllText($testFile, "forwarded activation`n", [Text.UTF8Encoding]::new($false))
$server = $null
try {
    $server = Start-Process -FilePath $Executable -ArgumentList '--activation-test-server' -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1200
    if ($server.HasExited) { throw "primary instance exited before activation: $($server.ExitCode)" }
    $quotedPath = '"' + $testFile + '"'
    $client = Start-Process -FilePath $Executable -ArgumentList @('--activation-test-client', $quotedPath) -WindowStyle Hidden -PassThru
    if (-not $client.WaitForExit(5000)) {
        $client.Kill()
        throw 'secondary instance did not finish forwarding activation'
    }
    if ($client.ExitCode -ne 0) { throw "secondary instance failed: $($client.ExitCode)" }
    if (-not $server.WaitForExit(5000)) { throw 'primary instance did not receive activation' }
    if ($server.ExitCode -ne 0) { throw "primary instance failed: $($server.ExitCode)" }
} finally {
    if ($server -and -not $server.HasExited) { $server.Kill() }
    [IO.File]::Delete($testFile)
}
