# =============================================================================
#  RunTests.ps1  --  Automated test for TcpServer (echo mode)
#
#  Usage:
#   .\Test\RunTests.ps1
#   .\Test\RunTests.ps1 -Port 9902 -Config Release
#
#  Requirements:  Both TcpClient and TcpServer built in the chosen Config.
# =============================================================================

param(
    [int]    $Port   = 9901,
    [string] $Config = "Debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ROOT  = Split-Path $PSScriptRoot -Parent
$SRV   = Join-Path $ROOT "bin\$Config\TcpServer.exe"
$LOG   = Join-Path $env:TEMP "TcpServer_test_recv.bin"
$PASS  = 0
$FAIL  = 0

if (!(Test-Path $SRV)) {
    Write-Error "TcpServer.exe not found at: $SRV`nBuild the solution first."
    exit 1
}

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#  Helpers
# -----------------------------------------------------------------------

function Assert-Equal {
    param($Actual, $Expected, [string]$TestName)
    if ($null -eq $Actual) {
        Write-Host "  [FAIL] $TestName  (no data received)" -ForegroundColor Red
        $script:FAIL++
        return
    }
    $aLen = $Actual.Length
    $eLen = $Expected.Length
    if ($aLen -ne $eLen) {
        Write-Host "  [FAIL] $TestName  (length: got $aLen expected $eLen)" -ForegroundColor Red
        $script:FAIL++
        return
    }
    for ($i = 0; $i -lt $eLen; $i++) {
        if ($Actual[$i] -ne $Expected[$i]) {
            Write-Host "  [FAIL] $TestName  (byte[$i]: got 0x$($Actual[$i].ToString('X2')) expected 0x$($Expected[$i].ToString('X2')))" -ForegroundColor Red
            $script:FAIL++
            return
        }
    }
    Write-Host "  [PASS] $TestName" -ForegroundColor Green
    $script:PASS++
}

function Send-Recv {
    param([byte[]]$Data, [int]$ReadMs = 3000)
    $tcp    = $null
    $stream = $null
    try {
        $tcp    = New-Object System.Net.Sockets.TcpClient
        $tcp.Connect("127.0.0.1", $Port)
        $stream = $tcp.GetStream()
        $stream.ReadTimeout  = $ReadMs
        $stream.WriteTimeout = 5000
        $stream.Write($Data, 0, $Data.Length)
        $stream.Flush()

        $buf    = New-Object byte[] 65536
        $recvd  = New-Object System.Collections.Generic.List[byte]
        $target = $Data.Length

        try {
            while ($recvd.Count -lt $target) {
                $n = $stream.Read($buf, 0, $buf.Length)
                if ($n -le 0) { break }
                $seg = New-Object byte[] $n
                [System.Array]::Copy($buf, 0, $seg, 0, $n)
                $recvd.AddRange($seg)
            }
        } catch {
            # timeout or connection closed - return what we have
        }
        return ,[byte[]]$recvd.ToArray()
    } catch {
        Write-Host "  [WARN] Send-Recv error: $($_.Exception.Message)" -ForegroundColor Yellow
        return ,[byte[]]@()
    } finally {
        if ($stream) { try { $stream.Close() } catch {} }
        if ($tcp)    { try { $tcp.Close()    } catch {} }
        Start-Sleep -Milliseconds 100
    }
}

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#  Start server
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  TcpServer / TcpClient automated tests" -ForegroundColor Cyan
Write-Host "  Config: $Config   Port: $Port" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# Remove stale log
if (Test-Path $LOG) { Remove-Item $LOG -Force }

Write-Host "Starting TcpServer.exe ..." -NoNewline
$srvProc = Start-Process -FilePath $SRV `
    -ArgumentList "/port:$Port /autostart /echo /logfile:$LOG" `
    -WindowStyle Minimized -PassThru

# Wait for server to bind (listen() is synchronous in the network thread)
Start-Sleep -Milliseconds 800

if ($srvProc.HasExited) {
    Write-Host " [ERROR] Server process exited immediately." -ForegroundColor Red
    exit 1
}
Write-Host " OK (PID $($srvProc.Id))" -ForegroundColor Green

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#  Tests
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Running tests..." -ForegroundColor Cyan

# Test 1 -- plain ASCII text
$t1 = [System.Text.Encoding]::UTF8.GetBytes("HELLO TEST")
$r1 = Send-Recv -Data $t1
Assert-Equal -Actual $r1 -Expected $t1 -TestName "Echo: ASCII text 'HELLO TEST'"

# Test 2 -- all bytes 0-255 round-trip
$t2 = [byte[]](0..255)
$r2 = Send-Recv -Data $t2
Assert-Equal -Actual $r2 -Expected $t2 -TestName "Echo: all bytes 0-255 (256 bytes)"

# Test 3 -- larger payload (4 KB)
$t3 = New-Object byte[] 4096
for ($i = 0; $i -lt $t3.Length; $i++) { $t3[$i] = [byte]($i % 251) }
$r3 = Send-Recv -Data $t3 -ReadMs 5000
Assert-Equal -Actual $r3 -Expected $t3 -TestName "Echo: 4 KB payload"

# Test 4 -- binary zeros
$t4 = [byte[]](0,0,0,0,0,0,0,0)
$r4 = Send-Recv -Data $t4
Assert-Equal -Actual $r4 -Expected $t4 -TestName "Echo: 8 zero bytes"

# Test 5 -- single byte
$t5 = [byte[]](0xAB)
$r5 = Send-Recv -Data $t5
Assert-Equal -Actual $r5 -Expected $t5 -TestName "Echo: single byte 0xAB"

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#  Verify log file (server should have written received data)
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Checking log file ..." -NoNewline
if (Test-Path $LOG) {
    $logSize = (Get-Item $LOG).Length
    Write-Host " $logSize bytes logged" -ForegroundColor Green
    # The log should contain at least the bytes from all tests
    $minExpected = $t1.Length + $t2.Length + $t3.Length + $t4.Length + $t5.Length
    if ($logSize -ge $minExpected) {
        Write-Host "  [PASS] Log file size ($logSize) >= expected ($minExpected)" -ForegroundColor Green
        $script:PASS++
    } else {
        Write-Host "  [WARN] Log file size ($logSize) < expected ($minExpected) --- partial write or test failed earlier" -ForegroundColor Yellow
    }
} else {
    Write-Host " not found (logfile writes only on receive; may be OK if tests failed)" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#  Stop server
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "Stopping server ..." -NoNewline
try {
    $srvProc | Stop-Process -Force
    $srvProc.WaitForExit(3000) | Out-Null
    Write-Host " done" -ForegroundColor Green
} catch {
    Write-Host " already exited" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
#  Summary
# -----------------------------------------------------------------------

$total = $PASS + $FAIL
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
if ($FAIL -eq 0) {
    Write-Host "  RESULT: ALL $total TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host "  RESULT: $FAIL/$total TESTS FAILED" -ForegroundColor Red
}
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

exit $FAIL
