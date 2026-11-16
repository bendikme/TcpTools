# TcpTools

A TCP/UDP **client**, **server** and **relay** in one native Windows program. One window, one tab per tool —
open as many clients, servers and relays as a test needs and watch them side by side.

No installer and no runtime to install: `TcpTools.exe` is a single file (Win32, C++17, Winsock 2).

![TcpTools icon](TcpTools/app.png)

---

## Download

The newest release is on the DYNATEC Portal's Applications page (For Windows ▸ TcpTools) and on this
repository's Releases page: `TcpTools-<version>-windows-x64.zip`. Unzip it anywhere and run `TcpTools.exe`.

Settings and history are kept in `%APPDATA%\TcpTools\`.

---

## The window

```
┌ TcpTools v26.11.0 – Client – 192.168.1.10:502 ─────────────────────────────┐
│ ● Client – 192.168.1.10:502 │ ● Server – Listening :9001 │ ● Relay │  +  │  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│                    the tool of the tab in front                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

- **Tabs** — the program starts with one Client, one Server and one Relay tab. The **+** tab (or Ctrl+T) adds
  another one of any kind; a right-click on a tab, a middle-click or Ctrl+W closes it.
- **State dot** on every tab — grey: idle, amber: waiting (listening, or connecting), green: connected.
  The tab also shows the tool's endpoint, so a tool that is not in front still tells what it is doing.
- **Keyboard** — Ctrl+Tab / Ctrl+Shift+Tab switch tabs, F1 opens the help of the tab in front, Ctrl+Enter sends.
- The **?** button of a tool turns its explanatory tooltips on.

### Client

Connects to a TCP server or sends UDP datagrams.

- Host / port (type `host:port` in the host field and both are filled in), protocol, connect timeout;
  recent connections in the drop-down.
- Send editor in text or **hex** (`48 65 6C 6C 6F`); load a file (binary files are sent byte-exact).
- Framing: none, STX/ETX, CR LF, LF, CR, NUL, or a custom prefix/suffix in hex.
- Auto-connect before a send, auto-disconnect after it, keep-alive, cyclic send down to 1 ms.
- Receive view as text or hex dump, copy, save to file.
- Log of every event with timing, socket diagnostics, statistics (latency, throughput, totals).

### Server

Listens on a port (TCP: one client at a time; UDP: answers the last sender).

- **Echo mode** returns everything it receives.
- The same send editor, framing, receive view, log, diagnostics and statistics as the client.

### Relay

Sits between a client and a target server and shows what passes in both directions.

- Listens on a port and connects to the target; forwards client → target and target → client.
- **Auto-forward** per direction; switched off, the packets wait until you press **Forward** or **Drop** —
  to hold back, delay or discard traffic by hand.
- Both directions as text or hex dump, a log of every packet, statistics and socket diagnostics.

---

## Command line

```
TcpTools.exe [/tab:client|server|relay] [switches]

  /tab:<tool>        the tab in front at start (default: client)

  Client:  /ip:<host>  /port:<n>  /autoconnect
  Server:  /port:<n>   /autostart  /echo  /logfile:<path>
```

`/port:` belongs to the tab named by `/tab:`. `/logfile:` makes the server append every received byte to a
file (used by the tests). Example — an echo server on port 9001, started at once:

```
TcpTools.exe /tab:server /port:9001 /autostart /echo
```

---

## Build

| Tool         | Version                          |
| ------------ | -------------------------------- |
| Visual Studio / Build Tools | 2026 (toolset `v145`) |
| Windows SDK  | 10.0                             |
| Target       | x64                              |

Open `TcpTools.sln` and build **Release | x64**, or from a command prompt:

```bat
msbuild TcpTools.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
```

The result is `bin\Release\TcpTools.exe`. The release build links the C++ runtime statically.

### Tests

```powershell
.\Test\RunTests.ps1 -Config Release
```

starts the built `TcpTools.exe` and drives it end to end: server echo, the server's log file, the relay,
the client tab in text and hex, UDP, and a clean exit while everything is connected.

### Version

The version is in `Version.h` only (`year.month.patch`); a release is tagged `v<version>`.

---

## Source layout

| Path                      | What                                                                 |
| ------------------------- | -------------------------------------------------------------------- |
| `TcpTools/main.cpp`       | entry point, command line, message loop                              |
| `TcpTools/AppFrame.*`     | the main window: tab strip, pages, shortcuts, window placement       |
| `TcpTools/Tools.h`        | how the frame creates a tool page                                    |
| `TcpTools/Common/`        | shared code: `HexView` (hex dump control), `Utils.h` (strings, hex, DPI, history), `ToolHost.h` (page ↔ frame contract) |
| `TcpTools/Client/`        | the client page and its network class (namespace `Client`)           |
| `TcpTools/Server/`        | the server page and its network class (namespace `Server`)           |
| `TcpTools/Relay/`         | the relay page and its network class                                 |
| `TcpTools/app.ico`, `app.png` | the icon; drawn by `scripts/make-icon.ps1`                       |
| `Test/RunTests.ps1`       | end-to-end tests                                                     |

Each tool page is a child window of the frame and owns its network object; all socket work runs on worker
threads that post their results to the page, so the window never blocks.

---

## Issues

Bugs, features and tasks are tracked in DYNATEC Project (product `TCPTOOLS`), see `AGENTS.md`.
