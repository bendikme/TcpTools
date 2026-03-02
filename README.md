# TCP Client — Native Windows C++ Application

A fast, native Win32 TCP client with a clean, feature-rich UI. No dependencies beyond the Windows SDK and Winsock2.

---

## Requirements

| Tool         | Version                   |
| ------------ | ------------------------- |
| MSVC Toolset | `v142` (VS 2019) or newer |
| Windows SDK  | 10.0                      |
| Target       | x64                       |

---

## Build Instructions

### Option A — Visual Studio IDE

1. Open `TcpClient.sln` in Visual Studio 2019 or later.
2. Select **x64 | Debug** or **x64 | Release**.
3. Press **Ctrl+Shift+B** (Build Solution).
4. Output binary: `bin\Debug\TcpClient.exe` or `bin\Release\TcpClient.exe`.

### Option B — Command Line (MSBuild)

```bat
set MSBUILD="C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
%MSBUILD% TcpClient.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
```

The compiled executable will be at `bin\Release\TcpClient.exe`.

---

## Source Files

| File                  | Purpose                                                               |
| --------------------- | --------------------------------------------------------------------- |
| `pch.h / pch.cpp`     | Precompiled header (Windows, Winsock2, STL)                           |
| `resource.h`          | Control IDs and custom window messages                                |
| `Utils.h`             | Utility functions (string conversion, timing, hex, file dialog)       |
| `Network.h / .cpp`    | Winsock2 TCP client with background thread, framing/separator support |
| `HexView.h / .cpp`    | Custom hex-editor-style viewer control (owner-draw, scrollable)       |
| `MainWindow.h / .cpp` | Main Win32 window: all UI controls, layout, event handling            |
| `main.cpp`            | `wWinMain` entry point                                                |

---

## Features

### Connection

- Host / IP address and port entry fields
- Connect / Disconnect buttons
- Configurable connection timeout (seconds)
- **Keep Alive** — keep socket open after sending (receive responses)
- **Auto-Disconnect** — close socket immediately after sending

### Send

- Multi-line text editor for entering data to send
- **Load File** — load any binary or text file; content appears in both plain text and hex view simultaneously
- **Hex View toggle** — switch between plain text and a live hex-editor view (classic `offset | bytes | ASCII` layout)
- **Send File Content** — send the loaded file's raw bytes over the socket

#### Framing / Separator Options

| Mode      | Description                                                         |
| --------- | ------------------------------------------------------------------- |
| None      | Raw bytes, no wrapping                                              |
| STX / ETX | Prepend `0x02`, append `0x03`                                       |
| CR LF     | Append `\r\n`                                                       |
| LF        | Append `\n`                                                         |
| CR        | Append `\r`                                                         |
| NUL       | Append `0x00`                                                       |
| Custom    | User-defined prefix and suffix (entered as hex pairs, e.g. `02 A1`) |

### Receive

- Scrollable multi-line text output for received data
- **Hex View toggle** — same hex-editor view for binary-safe inspection
- Copy to clipboard button

### Statistics (per send/receive cycle)

| Metric          | Description                                      |
| --------------- | ------------------------------------------------ |
| Connect time    | Absolute timestamp of successful connection      |
| Send start      | Time from "Send" button press to first byte sent |
| Send duration   | Time to transmit all bytes                       |
| Network latency | Time from last byte sent to first byte received  |
| Recv duration   | Time to receive all data in the response         |
| Total time      | Send start → last recv byte                      |
| Bytes sent      | Number of raw bytes sent (including framing)     |
| Bytes received  | Number of bytes received                         |
| Throughput      | Receive throughput in KB/s                       |

### History / Log

- Every connection event, send, receive, and error is recorded.
- **Double-click** a `Sent` row → reloads that payload into the Send panel.
- **Double-click** a `Received` row → reloads that data into the Receive panel.
- Right-click context menu: Reload, Copy Preview, Clear Log.
- Columns: Time, Type, Bytes, Send ms, Latency ms, Preview.

---

## Layout

```
┌─ Connection ──────────────────────────────────────────────────────────────┐
│ Host: [___________] Port: [_____] Timeout: [__]s  [Connect] [Disconnect]  │
│ [x] Keep Alive   [ ] Auto-Disconnect After Send      ● Disconnected        │
└───────────────────────────────────────────────────────────────────────────┘
┌─ Send ──────────────────────────┐ ┌─ Receive ─────────────────────────────┐
│ Separator: [None ▼] [x] Hex View│ │ [x] Hex View  [Clear]  [Copy]         │
│                                  │ │                                        │
│   [text / hex editor area]       │ │   [text / hex editor area]             │
│                                  │ │                                        │
│  [Send]  [Load File]  [Clear]    │ │                                        │
└──────────────────────────────────┘ └────────────────────────────────────────┘
┌─ Statistics ──────────────────────────────────────────────────────────────┐
│ Connect time: ...   Send start: ...   Send end: ...   Bytes sent: ...     │
│ Recv start: ...     Recv end: ...     Bytes recv: ...  Total: ...         │
└───────────────────────────────────────────────────────────────────────────┘
┌─ History / Log ───────────────────────────────────────────────────────────┐
│ Time      │ Type       │ Bytes │ Send ms │ Lat ms │ Preview               │
│ ...       │            │       │         │        │                       │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## Notes

- All networking runs on a background thread; the UI is never blocked.
- File sends support arbitrarily large files (streamed, no memory limit beyond available RAM).
- The hex editor view updates live as data arrives.
- Window is resizable; controls reflow proportionally.
