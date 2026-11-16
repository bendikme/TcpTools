# =============================================================================
#  RunTests.ps1  --  end-to-end tests of TcpTools.exe
#
#  Starts ONE TcpTools.exe and drives it the way a user would, through its
#  window (control IDs from each tool's resource.h) and through real sockets:
#
#    1. Server tab, echo mode      raw TCP client  <->  Server tab
#    2. Server log file            /logfile: holds exactly what was received
#    3. Relay tab                  raw TCP client  <->  Relay tab  <->  Server tab
#    4. Client tab, text           Client tab  ->  Relay tab  ->  Server tab (echo) -> back
#    5. Client tab, hex            "Hex" send of binary bytes arrives byte-exact
#    6. UDP                        raw UDP client  <->  second TcpTools.exe (UDP echo)
#    7. IPv6 and names             [::1], "[::1]:port" and localhost through relay and client
#    8. Start/stop cycles          relay and server stopped and started repeatedly; a waiting
#                                  second client; the port is exclusive; the client in
#                                  auto-connect / auto-disconnect cycles (replies kept)
#    9. Shutdown                   closing the window while everything is connected
#                                  ends the process cleanly
#
#  Usage:
#    .\Test\RunTests.ps1
#    .\Test\RunTests.ps1 -Config Release -Port 9901
#
#  Requirements: TcpTools built in the chosen Config (bin\<Config>\TcpTools.exe).
#  The window is shown without taking the focus; ports Port .. Port+2 must be free.
# =============================================================================

param(
    [int]    $Port     = 9901,
    [string] $Config   = "Debug",
    [string] $Exe      = "",       # test this TcpTools.exe instead of bin\<Config>\TcpTools.exe
    [int]    $MaxLagMs = 1500      # how long the window may take to react to a click, and to show
                                   # 512 KB of text, after a burst
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ROOT = Split-Path $PSScriptRoot -Parent
$EXE  = if ($Exe) { $Exe } else { Join-Path $ROOT "bin\$Config\TcpTools.exe" }
# The port is part of the names so that two test runs do not share their files.
$LOG  = Join-Path $env:TEMP "TcpTools_test_recv_$Port.bin"
$LOG2 = Join-Path $env:TEMP "TcpTools_test_recv_udp_$Port.bin"
$script:prevLen = -1
$script:PASS = 0
$script:FAIL = 0

$ServerPort = $Port          # Server tab listens here
$RelayPort  = $Port + 1      # Relay tab listens here, forwards to $ServerPort
$UdpPort    = $Port + 2      # second instance, UDP echo

if (!(Test-Path $EXE)) {
    Write-Error "TcpTools.exe not found at: $EXE`nBuild the solution first."
    exit 1
}

# -----------------------------------------------------------------------------
#  Win32 helpers (find the pages and their controls, click, type, read)
# -----------------------------------------------------------------------------
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W32 {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dlg, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, string lp);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, StringBuilder lp);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hwnd);
    public const uint WM_SETTEXT = 0x000C, WM_GETTEXT = 0x000D, WM_GETTEXTLENGTH = 0x000E,
                      WM_COMMAND = 0x0111, WM_CLOSE = 0x0010, BM_GETCHECK = 0x00F0, BM_CLICK = 0x00F5,
                      CB_SETCURSEL = 0x014E;
    public static string GetText(IntPtr hwnd) {
        int len = (int)SendMessage(hwnd, WM_GETTEXTLENGTH, IntPtr.Zero, IntPtr.Zero);
        var sb = new StringBuilder(len + 2);
        SendMessage(hwnd, WM_GETTEXT, (IntPtr)(len + 1), sb);
        return sb.ToString();
    }
}
"@

function Get-Page([IntPtr]$Frame, [string]$Class) {
    # [NullString]::Value, not $null: PowerShell would pass "" and match only untitled windows
    $h = [W32]::FindWindowEx($Frame, [IntPtr]::Zero, $Class, [NullString]::Value)
    if ($h -eq [IntPtr]::Zero) { throw "page window '$Class' not found" }
    return $h
}
function Get-Ctl([IntPtr]$Page, [int]$Id) {
    $h = [W32]::GetDlgItem($Page, $Id)
    if ($h -eq [IntPtr]::Zero) { throw "control $Id not found" }
    return $h
}
function Set-Text([IntPtr]$Page, [int]$Id, [string]$Text) {
    [void][W32]::SendMessage((Get-Ctl $Page $Id), [W32]::WM_SETTEXT, [IntPtr]::Zero, $Text)
}
function Get-Text([IntPtr]$Page, [int]$Id) { return [W32]::GetText((Get-Ctl $Page $Id)) }
# A click is the button's own notification to its page (BN_CLICKED = 0): it
# works for a page that is not the tab in front, and never blocks this script.
function Click([IntPtr]$Page, [int]$Id) {
    [void][W32]::PostMessage($Page, [W32]::WM_COMMAND, [IntPtr]$Id, (Get-Ctl $Page $Id))
}
function Wait-Until([scriptblock]$Cond, [int]$TimeoutMs = 5000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        if (& $Cond) { return $true }
        Start-Sleep -Milliseconds 50
    }
    return $false
}

function Pass([string]$Name) { Write-Host "  [PASS] $Name" -ForegroundColor Green; $script:PASS++ }
function Fail([string]$Name, [string]$Why) { Write-Host "  [FAIL] $Name  ($Why)" -ForegroundColor Red; $script:FAIL++ }

function Assert-Equal {
    param($Actual, $Expected, [string]$TestName)
    if ($null -eq $Actual) { Fail $TestName "no data received"; return }
    if ($Actual.Length -ne $Expected.Length) { Fail $TestName "length: got $($Actual.Length) expected $($Expected.Length)"; return }
    for ($i = 0; $i -lt $Expected.Length; $i++) {
        if ($Actual[$i] -ne $Expected[$i]) {
            Fail $TestName ("byte[$i]: got 0x{0:X2} expected 0x{1:X2}" -f $Actual[$i], $Expected[$i]); return
        }
    }
    Pass $TestName
}
function Assert-True([bool]$Cond, [string]$TestName, [string]$Why = "condition is false") {
    if ($Cond) { Pass $TestName } else { Fail $TestName $Why }
}

# Sends $Data over a new TCP connection and reads the same number of bytes back.
function Send-Recv {
    param([byte[]]$Data, [int]$ToPort, [int]$ReadMs = 3000, [string]$ToHost = "127.0.0.1")
    $tcp = $null; $stream = $null
    try {
        $ip  = [System.Net.IPAddress]::Parse($ToHost)
        $tcp = New-Object System.Net.Sockets.TcpClient ($ip.AddressFamily)
        $tcp.Connect($ip, $ToPort)
        $stream = $tcp.GetStream()
        $stream.ReadTimeout  = $ReadMs
        $stream.WriteTimeout = 5000
        $stream.Write($Data, 0, $Data.Length)
        $stream.Flush()
        $buf   = New-Object byte[] 65536
        $recvd = New-Object System.Collections.Generic.List[byte]
        try {
            while ($recvd.Count -lt $Data.Length) {
                $n = $stream.Read($buf, 0, $buf.Length)
                if ($n -le 0) { break }
                $seg = New-Object byte[] $n
                [System.Array]::Copy($buf, 0, $seg, 0, $n)
                $recvd.AddRange($seg)
            }
        } catch { }   # timeout or closed: return what we have
        return ,[byte[]]$recvd.ToArray()
    } catch {
        Write-Host "  [WARN] Send-Recv error: $($_.Exception.Message)" -ForegroundColor Yellow
        return ,[byte[]]@()
    } finally {
        if ($stream) { try { $stream.Close() } catch {} }
        if ($tcp)    { try { $tcp.Close()    } catch {} }
        Start-Sleep -Milliseconds 150
    }
}

function Start-App([string]$Arguments) {
    # Minimized: the window exists (MainWindowHandle) without covering the desktop
    # or taking the focus; the pages work the same hidden or shown.
    $p = Start-Process -FilePath $EXE -ArgumentList $Arguments -WindowStyle Minimized -PassThru
    if (-not (Wait-Until { $p.Refresh(); $p.MainWindowHandle -ne [IntPtr]::Zero } 8000)) {
        throw "TcpTools.exe did not show its window"
    }
    return $p
}

# Control IDs (TcpTools\Client\resource.h, Server\resource.h, Relay\resource.h)
$C_HOST = 1001; $C_PORT = 1002; $C_CONNECT = 1004; $C_DISCONNECT = 1005
$C_EDIT_SEND = 1010; $C_BTN_SEND = 1011; $C_CHK_HEX = 1014; $C_EDIT_RECV = 1020; $C_BTN_CLEARRECV = 1021
$S_BTN_STOP = 1005
$R_LISTEN = 1001; $R_TARGET = 1002; $R_TARGET_PORT = 1003; $R_START = 1006; $R_STOP = 1007

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  TcpTools end-to-end tests" -ForegroundColor Cyan
Write-Host "  Config: $Config   Ports: $ServerPort (server) $RelayPort (relay) $UdpPort (udp)" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

foreach ($f in $LOG, $LOG2) { if (Test-Path $f) { Remove-Item $f -Force } }

$app = $null; $app2 = $null; $app3 = $null
try {
    # -------------------------------------------------------------------------
    Write-Host "`n1. Server tab, echo mode" -ForegroundColor Cyan
    $app   = Start-App "/tab:server /port:$ServerPort /autostart /echo `"/logfile:$LOG`""
    $frame = $app.MainWindowHandle
    $pgClient = Get-Page $frame "TcpClientMainWnd"
    $pgServer = Get-Page $frame "TcpServerMainWnd"
    $pgRelay  = Get-Page $frame "TcpRelayMainWindow"

    $listening = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgServer $S_BTN_STOP)) } 5000
    Assert-True $listening "Server: /autostart is listening" "Stop button never became enabled"

    $expectedLog = New-Object System.Collections.Generic.List[byte]

    $t1 = [System.Text.Encoding]::UTF8.GetBytes("HELLO TEST")
    Assert-Equal (Send-Recv -Data $t1 -ToPort $ServerPort) $t1 "Echo: ASCII text 'HELLO TEST'";   $expectedLog.AddRange($t1)
    $t2 = [byte[]](0..255)
    Assert-Equal (Send-Recv -Data $t2 -ToPort $ServerPort) $t2 "Echo: all bytes 0-255";          $expectedLog.AddRange($t2)
    $t3 = New-Object byte[] 4096
    for ($i = 0; $i -lt $t3.Length; $i++) { $t3[$i] = [byte]($i % 251) }
    Assert-Equal (Send-Recv -Data $t3 -ToPort $ServerPort -ReadMs 5000) $t3 "Echo: 4 KB payload"; $expectedLog.AddRange($t3)
    $t4 = [byte[]](0,0,0,0,0,0,0,0)
    Assert-Equal (Send-Recv -Data $t4 -ToPort $ServerPort) $t4 "Echo: 8 zero bytes";             $expectedLog.AddRange($t4)
    $t5 = [byte[]](0xAB)
    Assert-Equal (Send-Recv -Data $t5 -ToPort $ServerPort) $t5 "Echo: single byte 0xAB";         $expectedLog.AddRange($t5)
    $t6 = New-Object byte[] (1024 * 1024)
    (New-Object System.Random 42).NextBytes($t6)
    Assert-Equal (Send-Recv -Data $t6 -ToPort $ServerPort -ReadMs 15000) $t6 "Echo: 1 MB random payload"; $expectedLog.AddRange($t6)

    # The megabyte is still being shown. A click must not wait behind it: the
    # posted click on the "Hex View" box has to come through within $MaxLagMs.
    $S_CHK_HEX_RECV = 1023
    $chk = Get-Ctl $pgServer $S_CHK_HEX_RECV
    $sw  = [Diagnostics.Stopwatch]::StartNew()
    [void][W32]::PostMessage($chk, [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    $reacted = Wait-Until { [int][W32]::SendMessage($chk, [W32]::BM_GETCHECK, [IntPtr]::Zero, [IntPtr]::Zero) -eq 1 } 30000
    $lag = $sw.ElapsedMilliseconds
    Assert-True ($reacted -and $lag -le $MaxLagMs) "Server: window reacts within $MaxLagMs ms after a 1 MB burst" "took $lag ms"
    [void][W32]::PostMessage($chk, [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    [void](Wait-Until { [int][W32]::SendMessage($chk, [W32]::BM_GETCHECK, [IntPtr]::Zero, [IntPtr]::Zero) -eq 0 } 30000)

    # The text view must show a burst at once, not seconds later. 512 KB of text
    # lines; the view shows each LF as CR LF, so its length is known exactly.
    $S_BTN_CLEARRECV = 1021
    $recvView = Get-Ctl $pgServer $C_EDIT_RECV
    function Get-ViewLength { [int][W32]::SendMessage($recvView, [W32]::WM_GETTEXTLENGTH, [IntPtr]::Zero, [IntPtr]::Zero) }
    $script:prevLen = -1                                 # let the megabyte above settle first
    [void](Wait-Until { $n = Get-ViewLength; $same = ($n -eq $script:prevLen); $script:prevLen = $n; $same } 30000)
    Click $pgServer $S_BTN_CLEARRECV
    [void](Wait-Until { (Get-ViewLength) -eq 0 } 10000)
    $t7 = New-Object byte[] (512 * 1024)
    for ($i = 0; $i -lt $t7.Length; $i++) { $t7[$i] = if ($i % 80 -eq 79) { 10 } else { 65 + ($i % 26) } }
    $wantLen = $t7.Length + [int][Math]::Floor($t7.Length / 80)
    Assert-Equal (Send-Recv -Data $t7 -ToPort $ServerPort -ReadMs 15000) $t7 "Echo: 512 KB of text lines"; $expectedLog.AddRange($t7)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $shown = Wait-Until { (Get-ViewLength) -eq $wantLen } 30000
    $took = $sw.ElapsedMilliseconds
    Assert-True ($shown -and $took -le $MaxLagMs) "Server: 512 KB of text is in the Receive view within $MaxLagMs ms" ("took $took ms, view length " + (Get-ViewLength) + " of $wantLen")

    # -------------------------------------------------------------------------
    Write-Host "`n2. Server log file" -ForegroundColor Cyan
    $want = [byte[]]$expectedLog.ToArray()
    $ok = Wait-Until { (Test-Path $LOG) -and ((Get-Item $LOG).Length -ge $want.Length) } 5000
    if ($ok) { Assert-Equal ([System.IO.File]::ReadAllBytes($LOG)) $want "Log file holds exactly the received bytes" }
    else     { Fail "Log file holds exactly the received bytes" "file missing or too short" }

    # -------------------------------------------------------------------------
    Write-Host "`n3. Relay tab (raw client -> relay -> server)" -ForegroundColor Cyan
    Set-Text $pgRelay $R_LISTEN      "$RelayPort"
    Set-Text $pgRelay $R_TARGET      "127.0.0.1"
    Set-Text $pgRelay $R_TARGET_PORT "$ServerPort"
    Click    $pgRelay $R_START
    # generous: the window may still be showing the megabyte it just received
    $relayUp = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgRelay $R_STOP)) } 30000
    Assert-True $relayUp "Relay: started" "Stop button never became enabled"
    Start-Sleep -Milliseconds 500

    $r1 = [System.Text.Encoding]::UTF8.GetBytes("THROUGH THE RELAY")
    Assert-Equal (Send-Recv -Data $r1 -ToPort $RelayPort -ReadMs 5000) $r1 "Relay: ASCII round trip"
    Assert-Equal (Send-Recv -Data $t2 -ToPort $RelayPort -ReadMs 5000) $t2 "Relay: all bytes 0-255 round trip"
    $r3 = New-Object byte[] (256 * 1024)
    (New-Object System.Random 7).NextBytes($r3)
    Assert-Equal (Send-Recv -Data $r3 -ToPort $RelayPort -ReadMs 15000) $r3 "Relay: 256 KB random round trip"

    # -------------------------------------------------------------------------
    Write-Host "`n4. Client tab, text (client -> relay -> server echo -> client)" -ForegroundColor Cyan
    Set-Text $pgClient $C_HOST "127.0.0.1"
    Set-Text $pgClient $C_PORT "$RelayPort"
    Click    $pgClient $C_CONNECT
    $connected = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_BTN_SEND)) -and
                              [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_DISCONNECT)) -and
                              -not [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_CONNECT)) } 30000
    Assert-True $connected "Client: connected to the relay" "Send button never became enabled"
    Start-Sleep -Milliseconds 300

    $msg = "Hello from the Client tab 123"
    Set-Text $pgClient $C_EDIT_SEND $msg
    Click    $pgClient $C_BTN_SEND
    $echoed = Wait-Until { (Get-Text $pgClient $C_EDIT_RECV) -eq $msg } 15000
    Assert-True $echoed "Client: text sent and echoed back into the Receive view" ("receive view shows: '" + (Get-Text $pgClient $C_EDIT_RECV) + "'")

    # -------------------------------------------------------------------------
    Write-Host "`n5. Client tab, hex send (byte-exact)" -ForegroundColor Cyan
    $before = (Get-Item $LOG).Length
    $hexBytes = [byte[]](0x00, 0x01, 0x02, 0x7F, 0x80, 0xFE, 0xFF, 0x0D, 0x0A, 0x00)
    Click $pgClient $C_BTN_CLEARRECV
    Set-Text $pgClient $C_EDIT_SEND ""
    [void][W32]::SendMessage((Get-Ctl $pgClient $C_CHK_HEX), [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    Set-Text $pgClient $C_EDIT_SEND "00 01 02 7f 80 FE FF 0D 0A 00"
    Click    $pgClient $C_BTN_SEND
    $ok = Wait-Until { (Get-Item $LOG).Length -ge ($before + $hexBytes.Length) } 15000
    if ($ok) {
        $all  = [System.IO.File]::ReadAllBytes($LOG)
        $tail = New-Object byte[] ($all.Length - $before)
        [System.Array]::Copy($all, $before, $tail, 0, $tail.Length)
        Assert-Equal $tail $hexBytes "Client: hex payload reached the server byte-exact"
    } else {
        Fail "Client: hex payload reached the server byte-exact" "the server log did not grow"
    }
    # back to text mode for whoever looks at the window afterwards
    [void][W32]::SendMessage((Get-Ctl $pgClient $C_CHK_HEX), [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)

    # -------------------------------------------------------------------------
    Write-Host "`n6. UDP echo (second instance)" -ForegroundColor Cyan
    # The protocol combo has no command-line switch: start idle, pick UDP, press Start.
    $app2 = Start-App "/tab:server /port:$UdpPort /echo `"/logfile:$LOG2`""
    $pgServer2 = Get-Page $app2.MainWindowHandle "TcpServerMainWnd"
    $S_PROTOCOL = 1100; $S_BTN_START = 1004
    $combo = Get-Ctl $pgServer2 $S_PROTOCOL
    [void][W32]::SendMessage($combo, [W32]::CB_SETCURSEL, [IntPtr]1, [IntPtr]::Zero)
    Click $pgServer2 $S_BTN_START
    $udpUp = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgServer2 $S_BTN_STOP)) } 5000
    Assert-True $udpUp "UDP server: started" "Stop button never became enabled"
    $udp = New-Object System.Net.Sockets.UdpClient
    try {
        $udp.Client.ReceiveTimeout = 3000
        $udp.Connect("127.0.0.1", $UdpPort)
        $d1 = [byte[]](0..255)
        [void]$udp.Send($d1, $d1.Length)
        $ep = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Any), 0
        $back = $null
        try { $back = $udp.Receive([ref]$ep) } catch { }
        Assert-Equal $back $d1 "UDP: datagram of all bytes 0-255 echoed"
    } finally { $udp.Close() }

    # -------------------------------------------------------------------------
    Write-Host "`n7. IPv6 and names" -ForegroundColor Cyan
    # The server and the relay listen on IPv4 and IPv6 with one socket.
    Click $pgClient $C_DISCONNECT
    $idle = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_CONNECT)) } 10000
    Assert-True $idle "Client: disconnected" "Connect button stayed disabled"
    Start-Sleep -Milliseconds 500
    $v6 = [System.Text.Encoding]::UTF8.GetBytes("OVER IPV6 THROUGH THE RELAY")
    Assert-Equal (Send-Recv -Data $v6 -ToPort $RelayPort -ReadMs 5000 -ToHost "::1") $v6 "Relay: raw client over IPv6 ([::1])"

    # Client tab: "[::1]:port" typed into the host field is split into host and port.
    foreach ($case in @(@("[::1]:$RelayPort", "IPv6 literal with port"), @("localhost", "host name"))) {
        Click $pgClient $C_BTN_CLEARRECV
        Click $pgClient 1013
        Set-Text $pgClient $C_PORT "$RelayPort"
        Set-Text $pgClient $C_HOST $case[0]
        Click    $pgClient $C_CONNECT
        $up = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_DISCONNECT)) -and
                           [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_BTN_SEND)) -and
                           -not [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_CONNECT)) } 10000
        $text = "hello via " + $case[1]
        if ($up) {
            Start-Sleep -Milliseconds 300
            Set-Text $pgClient $C_EDIT_SEND $text
            Click    $pgClient $C_BTN_SEND
            $up = Wait-Until { (Get-Text $pgClient $C_EDIT_RECV) -eq $text } 8000
        }
        Assert-True $up ("Client: connect and echo with " + $case[1] + " '" + $case[0] + "'") ("receive view shows: '" + (Get-Text $pgClient $C_EDIT_RECV) + "'")
        Click $pgClient $C_DISCONNECT
        [void](Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_CONNECT)) } 10000)
        Start-Sleep -Milliseconds 400
    }
    Set-Text $pgClient $C_HOST "127.0.0.1"
    Click    $pgClient $C_CONNECT
    [void](Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_DISCONNECT)) } 10000)

    # -------------------------------------------------------------------------
    Write-Host "`n8. Start/stop cycles" -ForegroundColor Cyan
    # Relay off and on, several times in a row, then it must still relay.
    $cyclesOk = $true
    for ($i = 0; $i -lt 5 -and $cyclesOk; $i++) {
        Click $pgRelay $R_STOP
        $cyclesOk = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgRelay $R_START)) } 10000
        if ($cyclesOk) {
            Click $pgRelay $R_START
            $cyclesOk = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgRelay $R_STOP)) } 10000
        }
    }
    Assert-True $cyclesOk "Relay: 5 stop/start cycles" "a cycle did not complete"
    Start-Sleep -Milliseconds 800
    $r4 = [System.Text.Encoding]::UTF8.GetBytes("STILL RELAYING AFTER RESTARTS")
    Assert-Equal (Send-Recv -Data $r4 -ToPort $RelayPort -ReadMs 5000) $r4 "Relay: round trip after the restarts"

    # Relay off for good: the server's one client slot is free for the client tab.
    Click $pgRelay $R_STOP
    [void](Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgRelay $R_START)) } 10000)
    $clientIdle = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_CONNECT)) } 10000
    Assert-True $clientIdle "Client: back to idle when the relay stops" "Connect button stayed disabled"

    # Server off and on, several times in a row, then it must still echo.
    $cyclesOk = $true
    for ($i = 0; $i -lt 5 -and $cyclesOk; $i++) {
        Click $pgServer $S_BTN_STOP
        $cyclesOk = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgServer 1004)) } 10000
        if ($cyclesOk) {
            Click $pgServer 1004
            $cyclesOk = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgServer $S_BTN_STOP)) } 10000
        }
    }
    Assert-True $cyclesOk "Server: 5 stop/start cycles" "a cycle did not complete"
    $s1 = [System.Text.Encoding]::UTF8.GetBytes("STILL ECHOING AFTER RESTARTS")
    Assert-Equal (Send-Recv -Data $s1 -ToPort $ServerPort -ReadMs 5000) $s1 "Server: echo after the restarts"

    # A second client while one is connected: the server says that it waits, and
    # serves it when the first one has gone.
    $S_CLIENTINFO = 1009
    $tcpA = New-Object System.Net.Sockets.TcpClient; $tcpA.Connect("127.0.0.1", $ServerPort)
    Start-Sleep -Milliseconds 600
    $tcpB = New-Object System.Net.Sockets.TcpClient; $tcpB.Connect("127.0.0.1", $ServerPort)
    try {
        $shown = Wait-Until { (Get-Text $pgServer $S_CLIENTINFO) -match "waiting" } 5000
        Assert-True $shown "Server: a second client is shown as waiting" ("client label: '" + (Get-Text $pgServer $S_CLIENTINFO) + "'")
        $sB = $tcpB.GetStream(); $sB.ReadTimeout = 6000
        $msgB = [System.Text.Encoding]::UTF8.GetBytes("FROM THE WAITING CLIENT")
        $sB.Write($msgB, 0, $msgB.Length); $sB.Flush()
        Start-Sleep -Milliseconds 400
        $tcpA.Close()
        $got = New-Object byte[] $msgB.Length; $n = 0
        try { while ($n -lt $got.Length) { $r = $sB.Read($got, $n, $got.Length - $n); if ($r -le 0) { break }; $n += $r } } catch { }
        if ($n -lt $got.Length) { $got = $got[0..([Math]::Max($n - 1, 0))]; if ($n -eq 0) { $got = [byte[]]@() } }
        Assert-Equal $got $msgB "Server: the waiting client is served when the first one leaves"
        $expectedWaiting = $msgB.Length
    } finally {
        try { $tcpA.Close() } catch {}
        try { $tcpB.Close() } catch {}
        Start-Sleep -Milliseconds 400
    }

    # The port belongs to this server alone: a second program cannot listen on it too.
    $app3 = Start-App "/tab:server /port:$ServerPort /autostart"
    $pgServer3 = Get-Page $app3.MainWindowHandle "TcpServerMainWnd"
    $stole = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgServer3 $S_BTN_STOP)) } 2500
    Assert-True (-not $stole) "Server: a second instance cannot listen on the same port" "it is listening"
    [void][W32]::PostMessage($app3.MainWindowHandle, [W32]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $app3.WaitForExit(8000)) { $app3 | Stop-Process -Force }
    Assert-Equal (Send-Recv -Data $s1 -ToPort $ServerPort -ReadMs 5000) $s1 "Server: still echoing after the port was contested"

    # Client: auto-connect + auto-disconnect, 15 sends in quick succession. Every
    # send is its own connect - send - disconnect; all must arrive, in order.
    $C_CHK_AUTODISC = 1007; $C_CHK_AUTOCONNECT = 1072
    Set-Text $pgClient $C_PORT "$ServerPort"
    Click $pgClient $C_BTN_CLEARRECV
    # Clear drops the bytes the hex test left pinned in the editor. (Typing would
    # do the same; a text set from another process does not count as typing.)
    Click $pgClient 1013
    [void][W32]::SendMessage((Get-Ctl $pgClient $C_CHK_AUTODISC),    [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][W32]::SendMessage((Get-Ctl $pgClient $C_CHK_AUTOCONNECT), [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    $before   = (Get-Item $LOG).Length
    $expected = New-Object System.Collections.Generic.List[byte]
    $cycleOk  = $true
    for ($i = 1; $i -le 15 -and $cycleOk; $i++) {
        $text = "cycle-$i;"
        Set-Text $pgClient $C_EDIT_SEND $text
        Click    $pgClient $C_BTN_SEND
        $expected.AddRange([System.Text.Encoding]::UTF8.GetBytes($text))
        $want = $before + $expected.Count
        # the next send only when this one has arrived and the client is idle again
        $cycleOk = Wait-Until { ((Get-Item $LOG).Length -ge $want) -and
                                [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_CONNECT)) } 10000
    }
    if ($cycleOk) {
        $all  = [System.IO.File]::ReadAllBytes($LOG)
        $tail = New-Object byte[] ($all.Length - $before)
        [System.Array]::Copy($all, $before, $tail, 0, $tail.Length)
        Assert-Equal $tail ([byte[]]$expected.ToArray()) "Client: 15 auto-connect / send / auto-disconnect cycles arrive complete and in order"
        # Auto-Disconnect must not cut off the reply: every echo is in the Receive view.
        $wantText = [System.Text.Encoding]::UTF8.GetString([byte[]]$expected.ToArray())
        $allBack  = Wait-Until { (Get-Text $pgClient $C_EDIT_RECV) -eq $wantText } 5000
        Assert-True $allBack "Client: every reply arrived although Auto-Disconnect is on" ("receive view shows: '" + (Get-Text $pgClient $C_EDIT_RECV) + "'")
        if ($tail.Length -ne $expected.Count) {
            Write-Host ("         server received: " + [System.Text.Encoding]::ASCII.GetString($tail)) -ForegroundColor Yellow
        }
    } else {
        Fail "Client: 15 auto-connect / send / auto-disconnect cycles arrive complete and in order" "a cycle did not complete"
    }

    # Leave the main instance busy for the shutdown test: relay on, client connected through it.
    [void][W32]::SendMessage((Get-Ctl $pgClient $C_CHK_AUTODISC),    [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][W32]::SendMessage((Get-Ctl $pgClient $C_CHK_AUTOCONNECT), [W32]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Click $pgRelay $R_START
    [void](Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgRelay $R_STOP)) } 10000)
    Start-Sleep -Milliseconds 800
    Set-Text $pgClient $C_PORT "$RelayPort"
    Click    $pgClient $C_CONNECT
    $busy = Wait-Until { [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_DISCONNECT)) -and
                         -not [W32]::IsWindowEnabled((Get-Ctl $pgClient $C_CONNECT)) } 10000
    Assert-True $busy "Client: reconnected through the relay" "not connected"

    # -------------------------------------------------------------------------
    Write-Host "`n9. Shutdown while connected" -ForegroundColor Cyan
    # Client connected, relay relaying, server with a client: closing the window
    # must stop every network thread and end the process by itself.
    foreach ($pair in @(@($app, "main instance (client + relay + server connected)"), @($app2, "UDP instance"))) {
        $p = $pair[0]
        [void][W32]::PostMessage($p.MainWindowHandle, [W32]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
        $exited = $p.WaitForExit(20000)
        if ($exited) { Assert-True ($p.ExitCode -eq 0) "Clean exit: $($pair[1])" "exit code $($p.ExitCode)" }
        else         { Fail "Clean exit: $($pair[1])" "still running 20 s after WM_CLOSE" }
    }
}
catch {
    Fail "Test run aborted" $_.Exception.Message
}
finally {
    foreach ($p in $app, $app2, $app3) {
        if ($p -and -not $p.HasExited) { try { $p | Stop-Process -Force } catch {} }
    }
}

# -----------------------------------------------------------------------------
$total = $script:PASS + $script:FAIL
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
if ($script:FAIL -eq 0) { Write-Host "  RESULT: ALL $total TESTS PASSED" -ForegroundColor Green }
else                    { Write-Host "  RESULT: $($script:FAIL)/$total TESTS FAILED" -ForegroundColor Red }
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

exit $script:FAIL
