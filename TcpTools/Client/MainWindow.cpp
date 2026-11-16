#include "pch.h"
#include "MainWindow.h"
#include "ToolHost.h"
#include "Version.h"

namespace Client {

// ============================================================
//  Subclass proc for Send edit - intercepts Ctrl+Enter
// ============================================================
static LRESULT CALLBACK SendEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR /*idSubclass*/, DWORD_PTR /*refData*/)
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        HWND parent = GetParent(hWnd);
        PostMessageW(parent, WM_COMMAND, MAKEWPARAM(IDC_BTN_SEND, BN_CLICKED), 0);
        return 0;
    }
    // Suppress the WM_CHAR newline that follows Ctrl+Enter
    if (msg == WM_CHAR && wParam == L'\n')
        return 0;
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ============================================================
//  Helpers
// ============================================================

static constexpr int MARGIN    = 8;
static constexpr int ROW_H     = 24;
static constexpr int BTN_W     = 90;
static constexpr int LABEL_W   = 50;
static constexpr int GRP_TITLE = 18; // group-box title clearance

// Caps that keep memory bounded on a long-running session.
static constexpr size_t RECV_MAX        = 8u * 1024 * 1024;    // received bytes kept; newest half survives a trim
static constexpr size_t LAST_RECV_MAX   = 32u * 1024 * 1024;   // "Save last transmission"
static constexpr size_t LOG_MAX_ENTRIES = 20000;               // oldest 10 % dropped when exceeded
static constexpr size_t LOG_MAX_BYTES   = 64u * 1024 * 1024;   // payload bytes held by the log
static constexpr size_t FILE_MAX        = 64u * 1024 * 1024;   // Load File limit
static constexpr size_t HEX_EDIT_MAX    = 1u * 1024 * 1024;    // largest payload edited as hex text
static constexpr size_t TEXT_SHOW_MAX   = 8u * 1024 * 1024;    // largest payload shown in the text editor

// Drops the oldest half once `v` exceeds `maxBytes`. True when it trimmed.
static bool TrimNewestHalf(std::vector<BYTE>& v, size_t maxBytes)
{
    if (v.size() <= maxBytes) return false;
    v.erase(v.begin(), v.end() - (std::ptrdiff_t)(maxBytes / 2));
    return true;
}

static const wchar_t* ProtoLabel(bool udp) { return udp ? L"UDP" : L"TCP"; }

// ============================================================
//  RegisterClass / Create
// ============================================================

bool MainWindow::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbWndExtra    = sizeof(LONG_PTR);
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = ClassName();
    return RegisterClassExW(&wc) != 0;
}

HWND MainWindow::Create(HINSTANCE hInst, HWND parent,
                        const std::wstring& defaultIP, int defaultPort, bool autoConnect)
{
    struct Params { std::wstring ip; int port; bool autoConnect; };
    auto* p = new Params{ defaultIP, defaultPort, autoConnect };

    HWND hWnd = CreateWindowExW(
        TOOL_PAGE_EXSTYLE, ClassName(),
        L"TCP Client",
        TOOL_PAGE_STYLE,
        0, 0, 1180, 820,
        parent, nullptr, hInst, p);

    if (!hWnd) { delete p; return nullptr; }
    return hWnd;
}

MainWindow* MainWindow::FromHwnd(HWND hWnd)
{
    return reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, 0));
}

// ============================================================
//  WndProc dispatcher
// ============================================================

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = FromHwnd(hWnd);

    switch (msg)
    {
    case WM_CREATE:
    {
        struct Params { std::wstring ip; int port; bool autoConnect; };
        auto* cs = (CREATESTRUCT*)lParam;
        auto* p  = (Params*)cs->lpCreateParams;
        auto* mw = new MainWindow();
        mw->m_hWnd  = hWnd;
        mw->m_hInst = cs->hInstance;
        std::wstring ip  = p ? p->ip          : L"127.0.0.1";
        int          port = p ? p->port        : 9001;
        bool         ac   = p ? p->autoConnect : false;
        delete p;
        SetWindowLongPtrW(hWnd, 0, (LONG_PTR)mw);
        mw->OnCreate(hWnd, mw->m_hInst, ip, port, ac);
        return 0;
    }
    case WM_DESTROY:
        if (self) { self->OnDestroy(); delete self; SetWindowLongPtrW(hWnd, 0, 0); }
        return 0;

    case WM_SIZE:
        if (self && wParam != SIZE_MINIMIZED)
            self->OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_COMMAND:
        if (self) self->OnCommand(LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
        return 0;

    case WM_TOOL_HELP:
        {
            MessageBoxW(hWnd,
                L"TCP/UDP Client v" TCPTOOLS_VERSION L"\n"
                TCPTOOLS_COPYRIGHT L"\n"
                L"\n"
                L"\u2500\u2500\u2500 GETTING STARTED \u2500\u2500\u2500\n"
                L"\n"
                L"1. Enter the server address and port at the top.\n"
                L"You can type host:port (e.g. 192.168.1.10:502) "
                L"and both fields are filled automatically.\n"
                L"2. Choose TCP or UDP and set the timeout (IPv4 only).\n"
                L"3. Click Connect (or enable Auto-Connect). Disconnect "
                L"also cancels a connection attempt in progress.\n"
                L"4. Type data in the Send editor and click Send.\n"
                L"5. Incoming data appears in the Receive panel.\n"
                L"\n"
                L"\u2500\u2500\u2500 KEYBOARD SHORTCUTS \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Ctrl+Enter \u2014 Send data\n"
                L"\u2022 F1 \u2014 This help screen\n"
                L"\u2022 Ctrl+T / Ctrl+W / Ctrl+Tab \u2014 new tab / close tab / next tab\n"
                L"\n"
                L"\u2500\u2500\u2500 FEATURES \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Hex \u2014 Edit the data to send as hex byte pairs; "
                L"Hex View (Receive) shows received data as hex.\n"
                L"\u2022 Framing \u2014 Auto-wrap payloads with STX/ETX, "
                L"CR LF, LF, CR, NUL, or custom hex delimiters.\n"
                L"\u2022 Cyclic Send \u2014 Repeat the payload at a timed "
                L"interval (down to 1 ms) for stress testing.\n"
                L"\u2022 Auto-Connect \u2014 Connect on demand when sending.\n"
                L"\u2022 Auto-Disconnect \u2014 Drop the connection right after "
                L"each send (a response not yet received is lost).\n"
                L"\u2022 Keep-Alive \u2014 Detect broken TCP connections early.\n"
                L"\u2022 Load/Save \u2014 Load a file (up to 64 MB) as the data to "
                L"send, exactly as it is on disk; save the last received "
                L"transmission to disk.\n"
                L"\u2022 Connection History \u2014 Dropdown remembers recent "
                L"host:port entries across sessions.\n"
                L"\n"
                L"\u2500\u2500\u2500 BOTTOM PANEL \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Log \u2014 Full event history. Right-click to reload "
                L"data or copy previews.\n"
                L"\u2022 Diagnostics \u2014 Live socket info (buffers, Nagle, "
                L"MTU, keep-alive).\n"
                L"\u2022 Statistics \u2014 Timing, byte counts, latency, "
                L"and throughput.\n"
                L"\n"
                L"\u2500\u2500\u2500 TIPS \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Click the [?] button to enable help tooltips.\n"
                L"\u2022 Command line: TcpTools.exe /tab:client /ip:<host> "
                L"/port:<n> [/autoconnect]\n"
                L"\u2022 Config stored in: %APPDATA%\\TcpTools\\\n",
                L"TCP/UDP Client \u2014 Help", MB_OK | MB_ICONINFORMATION);
            return 0;
        }

    case WM_NOTIFY:
        if (self) self->OnNotify((NMHDR*)lParam);
        return 0;

    case WM_CONTEXTMENU:
        if (self && (HWND)wParam == self->m_hListLog) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            self->OnContextMenuLog(pt);
        }
        return 0;

    // ---- Network messages ------------------------------------
    case WM_NET_CONNECTED:
        if (self) self->OnNetConnected();
        return 0;
    case WM_NET_DISCONNECTED:
        if (self) self->OnNetDisconnected(wParam != 0);
        return 0;
    case WM_NET_DATA_RECEIVED:
        if (self) self->OnNetDataReceived((size_t)wParam, (BYTE*)lParam);
        return 0;
    case WM_NET_SEND_COMPLETE:
        if (self) self->OnNetSendComplete((size_t)wParam, (CycleStats*)lParam);
        return 0;
    case WM_NET_ERROR:
        if (self) self->OnNetError((int)wParam, (std::wstring*)lParam);
        return 0;
    case WM_NET_RECV_LATENCY:
        if (self) {
            // Reconstruct int64 latency (microseconds) from lo32/hi32
            int64_t latUs = (int64_t)(DWORD)wParam | ((int64_t)(DWORD)lParam << 32);
            self->OnNetLatency(latUs / 1000.0);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_CYCLIC_UPDATE:
        // lParam = (run id << 1) | final
        if (self) self->OnCyclicMessage((size_t)wParam, (lParam & 1) != 0, (uint32_t)((uint64_t)lParam >> 1));
        return 0;

    case WM_PARSE_HOST_COMBO:
        if (self) self->DoParseComboParts();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
//  OnCreate - build all controls
// ============================================================

HWND MainWindow::MakeLabel(const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
        x, y, w, h, m_hWnd, nullptr, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}

HWND MainWindow::MakeEdit(int id, int x, int y, int w, int h, DWORD extra)
{
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}

HWND MainWindow::MakeButton(int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}

HWND MainWindow::MakeCheck(int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}

HWND MainWindow::MakeGroupBox(int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}

void MainWindow::OnCreate(HWND hWnd, HINSTANCE hInst,
                          const std::wstring& defaultIP, int defaultPort, bool autoConnect)
{
    m_autoConnect = autoConnect;
    m_hFont     = Dpi::CreateUIFont();
    m_hFontMono = Dpi::CreateMonoFont(13);

    // --- Connection group (placeholder pos; real pos set in LayoutControls) ---
    m_hGrpConn     = MakeGroupBox(IDC_GRP_CONNECTION, L" Connection ", 0,0,10,10);
    m_hComboHost   = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_HOST, hInst, nullptr);
    SendMessage(m_hComboHost, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    m_hEditPort    = MakeEdit(IDC_EDIT_PORT, 0,0,10,10);
    m_hEditTimeout = MakeEdit(IDC_EDIT_TIMEOUT,0,0,10,10);
    m_hBtnConnect    = MakeButton(IDC_BTN_CONNECT,    L"Connect",    0,0,10,10);
    m_hBtnDisconnect = MakeButton(IDC_BTN_DISCONNECT, L"Disconnect", 0,0,10,10);
    m_hChkKeepAlive  = MakeCheck(IDC_CHK_KEEPALIVE, L"Keep Alive",  0,0,10,10);
    m_hChkAutoDisc   = MakeCheck(IDC_CHK_AUTODISCONNECT, L"Auto-Disconnect After Send", 0,0,10,10);
    m_hChkAutoConnect = MakeCheck(IDC_CHK_AUTOCONNECT, L"Auto-Connect Before Send", 0,0,10,10);
    m_hStaticStatus  = MakeLabel(L"\u25CF Disconnected", 0,0,10,10);

    // Load connection history and populate combo
    m_connHistory = Utils::LoadHistory(L"TcpClient");
    PopulateHostCombo();

    // Apply defaults from CLI args
    wchar_t portBuf[16]; swprintf_s(portBuf, L"%d", defaultPort);
    SetWindowTextW(m_hComboHost,   defaultIP.c_str());
    SetWindowTextW(m_hEditPort,    portBuf);
    SetWindowTextW(m_hEditTimeout, L"5");
    DoParseComboParts();
    Button_SetCheck(m_hChkKeepAlive, BST_CHECKED);
    EnableWindow(m_hBtnDisconnect, FALSE);

    // Labels (connection)
    m_hLblHost    = MakeLabel(L"Host:",       0,0,35,ROW_H);
    m_hLblPort    = MakeLabel(L"Port:",       0,0,32,ROW_H);
    m_hLblTimeout = MakeLabel(L"Timeout (s):",0,0,90,ROW_H);

    // Protocol combo (TCP / UDP)
    m_hComboProtocol = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_PROTOCOL, hInst, nullptr);
    SendMessage(m_hComboProtocol, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    SendMessage(m_hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"TCP");
    SendMessage(m_hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"UDP");
    SendMessage(m_hComboProtocol, CB_SETCURSEL, 0, 0);

    // --- Send group ---
    m_hGrpSend     = MakeGroupBox(IDC_GRP_SEND, L" Send ", 0,0,10,10);

    // Separator combo
    m_hComboSep = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_SEPARATOR, hInst, nullptr);
    SendMessage(m_hComboSep, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    const wchar_t* sepNames[] = {
        L"None", L"STX / ETX", L"CR LF", L"LF", L"CR", L"NUL", L"Custom"
    };
    for (auto* n : sepNames) SendMessage(m_hComboSep, CB_ADDSTRING, 0, (LPARAM)n);
    SendMessage(m_hComboSep, CB_SETCURSEL, 0, 0);

    m_hEditPreFix = MakeEdit(IDC_EDIT_CUSTOMPRE, 0,0,10,10);
    m_hEditSufFix = MakeEdit(IDC_EDIT_CUSTOMSUF, 0,0,10,10);
    m_hLblPre     = MakeLabel(L"Prefix (hex):", 0,0,90,ROW_H);
    m_hLblSuf     = MakeLabel(L"Suffix (hex):", 0,0,90,ROW_H);
    ShowWindow(m_hEditPreFix, SW_HIDE); ShowWindow(m_hEditSufFix, SW_HIDE);
    ShowWindow(m_hLblPre, SW_HIDE);    ShowWindow(m_hLblSuf, SW_HIDE);

    m_hChkHexSend   = MakeCheck(IDC_CHK_HEXVIEW,    L"Hex", 0,0,10,10);
    m_hEditSend     = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_EDIT_SEND, hInst, nullptr);
    SendMessage(m_hEditSend, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SetWindowSubclass(m_hEditSend, SendEditSubclassProc, 1, 0);
    SendMessage(m_hEditSend, EM_SETLIMITTEXT, 0, 0); // no character limit

    m_hBtnSend      = MakeButton(IDC_BTN_SEND,      L"Send",       0,0,10,10);
    m_hBtnLoadFile  = MakeButton(IDC_BTN_LOADFILE,  L"Load File",  0,0,10,10);
    m_hBtnClearSend = MakeButton(IDC_BTN_CLEARSEND, L"Clear",      0,0,10,10);
    m_hChkAutoClearSend = MakeCheck(IDC_CHK_AUTOCLEAR_SEND, L"Auto-clear after send", 0,0,10,10);

    // --- Receive group ---
    m_hGrpRecv    = MakeGroupBox(IDC_GRP_RECEIVE, L" Receive ", 0,0,10,10);
    m_hChkHexRecv = MakeCheck(IDC_CHK_HEXVIEW_RECV, L"Hex View", 0,0,10,10);
    m_hEditRecv   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_EDIT_RECV, hInst, nullptr);
    SendMessage(m_hEditRecv, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SendMessage(m_hEditRecv, EM_SETLIMITTEXT, 0, 0); // no character limit

    m_hHexRecv = HexView::Create(hWnd, 0,0,10,10, IDC_STATIC_HEXRECV, hInst);
    ShowWindow(m_hHexRecv, SW_HIDE);

    m_hBtnClearRecv = MakeButton(IDC_BTN_CLEARRECV, L"Clear",    0,0,10,10);
    m_hBtnCopyRecv  = MakeButton(IDC_BTN_COPYRECV,  L"Copy",     0,0,10,10);
    m_hBtnSaveRecv  = MakeButton(IDC_BTN_SAVERECV,  L"Save to file",0,0,10,10);
    m_hChkAutoClearRecv = MakeCheck(IDC_CHK_AUTOCLEAR_RECV, L"Auto-clear on recv", 0,0,10,10);

    // --- Statistics (created as tab 2 content, no group box) ---
    auto mkStat = [&](HWND& hw, const wchar_t* init) {
        hw = MakeLabel(init, 0,0,10,10);
    };
    mkStat(m_hStatCon,       L"Connect time: ---");
    mkStat(m_hStatSendStart, L"Send start: ---");
    mkStat(m_hStatSendEnd,   L"Send end: ---");
    mkStat(m_hStatRecvStart, L"Recv start: ---");
    mkStat(m_hStatRecvEnd,   L"Recv end: ---");
    mkStat(m_hStatBytesSent, L"Bytes sent: ---");
    mkStat(m_hStatBytesRecv, L"Bytes recv: ---");
    mkStat(m_hStatLatency,   L"Latency: ---");
    mkStat(m_hStatTotal,     L"Total time: ---");
    mkStat(m_hStatThrough,   L"Throughput: ---");
    mkStat(m_hStatTotalSent, L"Total sent: 0 B");
    mkStat(m_hStatTotalRecv, L"Total recv: 0 B");
    m_hBtnClearStats = MakeButton(IDC_BTN_CLEARSTATS, L"Clear", 0,0,10,10);
    // Hide stats controls initially (tab 2 not selected)
    HWND statsHwnds[] = { m_hStatCon, m_hStatSendStart, m_hStatSendEnd,
        m_hStatRecvStart, m_hStatRecvEnd, m_hStatBytesSent, m_hStatBytesRecv,
        m_hStatLatency, m_hStatTotal, m_hStatThrough,
        m_hStatTotalSent, m_hStatTotalRecv, m_hBtnClearStats };
    for (auto h : statsHwnds) ShowWindow(h, SW_HIDE);

    // --- Cyclic send controls ---
    m_hLblCyclic = MakeLabel(L"Interval (s):", 0,0,10,10);
    m_hEditCyclicInterval = MakeEdit(IDC_EDIT_CYCLIC_INTERVAL, 0,0,10,10, ES_CENTER);
    SetWindowTextW(m_hEditCyclicInterval, L"1.000");
    m_hBtnCyclicStart = MakeButton(IDC_BTN_CYCLIC_START, L"Start Cyclic", 0,0,10,10);
    m_hStaticCyclicCount = MakeLabel(L"", 0,0,10,10);

    // --- Bottom tab control (Log / Diagnostics / Statistics) ---
    m_hTabBottom = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_TAB_BOTTOM, hInst, nullptr);
    SendMessage(m_hTabBottom, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    {
        TCITEMW ti = {}; ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"History / Log";      TabCtrl_InsertItem(m_hTabBottom, 0, &ti);
        ti.pszText = (LPWSTR)L"Socket Diagnostics"; TabCtrl_InsertItem(m_hTabBottom, 1, &ti);
        ti.pszText = (LPWSTR)L"Statistics";          TabCtrl_InsertItem(m_hTabBottom, 2, &ti);
        TabCtrl_SetCurSel(m_hTabBottom, 0); // default to Log tab
    }

    // --- Diagnostics (tab 1 content) ---
    m_hStaticDiagInfo = CreateWindowExW(0, L"STATIC", L"Connect to view diagnostics",
        WS_CHILD | SS_LEFT | SS_NOPREFIX,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_STATIC_DIAG_INFO, hInst, nullptr);
    SendMessage(m_hStaticDiagInfo, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    m_hBtnRefreshDiag = MakeButton(IDC_BTN_REFRESHDIAG, L"Refresh", 0,0,10,10);
    ShowWindow(m_hStaticDiagInfo, SW_HIDE);
    ShowWindow(m_hBtnRefreshDiag, SW_HIDE);

    // --- Log (tab 0 content) ---
    m_hListLog = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | LVS_OWNERDATA,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_LIST_LOG, hInst, nullptr);
    SendMessage(m_hListLog, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    ListView_SetExtendedListViewStyle(m_hListLog,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Log icons (ImageList)
    {
        const int icon = Dpi::Scale(16);
        m_hImgListLog = ImageList_Create(icon, icon, ILC_COLOR32 | ILC_MASK, 8, 0);
        // Create icons for each LogType via GDI (colored circles)
        struct { COLORREF color; const wchar_t* glyph; } iconDefs[] = {
            { RGB(0,160,0),   L"\x25CF" },  // Connected  (green circle)
            { RGB(160,0,0),   L"\x25CF" },  // Disconnected (red circle)
            { RGB(0,100,200), L"\x25B2" },  // Sent (blue up triangle)
            { RGB(0,140,80),  L"\x25BC" },  // Received (teal down triangle)
            { RGB(200,0,0),   L"\x26A0" },  // Error (red warning)
            { RGB(128,128,128), L"\x2139" }, // Info (gray info)
        };
        HDC hdc = GetDC(hWnd);
        HFONT hIconFont = CreateFontW(Dpi::Scale(14), 0, 0, 0, FW_NORMAL, 0,0,0,
            DEFAULT_CHARSET, 0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI Symbol");
        for (auto& def : iconDefs) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, icon, icon);
            SelectObject(mem, bmp);
            RECT r = {0,0,icon,icon};
            HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(mem, &r, bg);
            DeleteObject(bg);
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, def.color);
            SelectObject(mem, hIconFont);
            DrawTextW(mem, def.glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DeleteDC(mem);
            ImageList_AddMasked(m_hImgListLog, bmp, GetSysColor(COLOR_WINDOW));
            DeleteObject(bmp);
        }
        DeleteObject(hIconFont);
        ReleaseDC(hWnd, hdc);
        ListView_SetImageList(m_hListLog, m_hImgListLog, LVSIL_SMALL);
    }

    // Log columns
    LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    auto addCol = [&](const wchar_t* hdr, int w) {
        col.pszText = (LPWSTR)hdr; col.cx = Dpi::Scale(w);
        ListView_InsertColumn(m_hListLog, 999, &col);
    };
    addCol(L"#",       75);
    addCol(L"Time",    80);
    addCol(L"Type",    90);
    addCol(L"Bytes",   60);
    addCol(L"Send ms", 70);
    addCol(L"Lat ms",  70);
    addCol(L"Preview", 500);

    m_hBtnClearLog = MakeButton(IDC_BTN_CLEARLOG, L"Clear Log", 0,0,10,10);

    // Log filter combo
    m_hComboLogFilter = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_LOGFILTER, hInst, nullptr);
    SendMessage(m_hComboLogFilter, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    const wchar_t* filterNames[] = { L"All", L"Data Only", L"Connection", L"Errors" };
    for (auto* n : filterNames) SendMessage(m_hComboLogFilter, CB_ADDSTRING, 0, (LPARAM)n);
    SendMessage(m_hComboLogFilter, CB_SETCURSEL, 0, 0);

    // Initial layout
    RECT rc; GetClientRect(hWnd, &rc);
    LayoutControls(rc.right, rc.bottom);
    LONG_PTR st = GetWindowLongPtrW(m_hStaticStatus, GWL_STYLE);
    SetWindowLongPtrW(m_hStaticStatus, GWL_STYLE, st | SS_ENDELLIPSIS);

    // Help toggle button (top-right "?")
    m_hBtnHelp = CreateWindowExW(0, L"BUTTON", L"?",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_BTN_HELP, hInst, nullptr);
    SendMessage(m_hBtnHelp, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    // Tooltips (start disabled)
    CreateTooltips();

    // Auto-connect after the window is visible (post so WM_CREATE returns first)
    if (m_autoConnect) PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_CONNECT, BN_CLICKED), 0);
}

// ============================================================
//  Layout
// ============================================================

void MainWindow::OnSize(int w, int h)
{
    LayoutControls(w, h);
    PositionHelpButton();
}

void MainWindow::PositionHelpButton()
{
    // Positioned inside LayoutControls
}

void MainWindow::OnGetMinMaxInfo(MINMAXINFO* mm)
{
    mm->ptMinTrackSize = { 900, 650 };
}

// wPx/hPx are client pixels. All arithmetic below is in 96-DPI units; Move()
// scales to pixels.
void MainWindow::LayoutControls(int wPx, int hPx)
{
    if (!m_hComboHost) return; // not yet created

    const int W = Dpi::Unscale(wPx), H = Dpi::Unscale(hPx);

    // A resized CBS_DROPDOWN combo re-selects the first history entry that starts
    // with its text; put back what the user had.
    const std::wstring hostText = Utils::GetWindowText(m_hComboHost);

    HDWP hdwp = BeginDeferWindowPos(96);
    auto Move = [&](HWND hw, int x, int y, int w, int h) {
        if (hw && hdwp)
            hdwp = DeferWindowPos(hdwp, hw, nullptr, Dpi::Scale(x), Dpi::Scale(y), Dpi::Scale(w), Dpi::Scale(h),
                                  SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    };

    int x = MARGIN, y = MARGIN;

    // ====== CONNECTION GROUP ======
    int connH = GRP_TITLE + ROW_H + MARGIN + ROW_H + MARGIN;
    Move(m_hGrpConn, x, y, W - 2*MARGIN, connH);
    int cx = x + MARGIN + 2;
    int cy = y + GRP_TITLE;

    // Row 1: IP | Port | Timeout | Connect | Disconnect | Status
    Move(m_hLblHost,    cx, cy, 35, ROW_H); cx += 35 + 2;
    Move(m_hComboHost, cx, cy, 180, 200); cx += 180 + MARGIN;
    Move(m_hLblPort,    cx, cy, 32, ROW_H); cx += 32 + 2;
    Move(m_hEditPort,    cx, cy, 55, ROW_H); cx += 55 + MARGIN;
    Move(m_hLblTimeout, cx, cy, 80, ROW_H); cx += 80 + 2;
    Move(m_hEditTimeout, cx, cy, 40, ROW_H); cx += 40 + MARGIN;
    Move(m_hComboProtocol, cx, cy, 55, 200); cx += 55 + MARGIN;
    Move(m_hBtnConnect,    cx, cy, BTN_W,   ROW_H); cx += BTN_W + 4;
    Move(m_hBtnDisconnect, cx, cy, BTN_W,   ROW_H); cx += BTN_W + 2*MARGIN;
    const int helpX = W - 2*MARGIN - 26;
    Move(m_hStaticStatus,  cx, cy, std::max(100, helpX - cx - 4), ROW_H);
    Move(m_hBtnHelp, helpX, cy, 26, ROW_H);

    // Row 2: checkboxes
    cx = x + MARGIN + 2;
    cy += ROW_H + 4;
    Move(m_hChkKeepAlive, cx, cy, 100, ROW_H); cx += 108;
    Move(m_hChkAutoDisc,  cx, cy, 200, ROW_H); cx += 204;
    Move(m_hChkAutoConnect, cx, cy, 190, ROW_H);

    y += connH + MARGIN;

    // ====== SEND + RECEIVE (side by side) ======
    // Bottom panel: Tab (Log/Diagnostics/Statistics)
    int tabMin  = 250; // min height for bottom tab control
    int midH = H - y - (tabMin + MARGIN);
    if (midH < 120) midH = 120;

    int sendW = (int)((W - 3*MARGIN) * m_splitRatio);
    int recvW = W - 3*MARGIN - sendW;

    // -- Send group --
    Move(m_hGrpSend, x, y, sendW, midH);
    int sx = x + MARGIN + 2, sy = y + GRP_TITLE;
    int sendCtrlW = sendW - 2*(MARGIN+2);

    // Separator controls row
    int comboX = sx;
    Move(m_hComboSep, comboX, sy, 120, 200); comboX += 124;
    Move(m_hChkHexSend, comboX, sy, 50, ROW_H); comboX += 54;
    Move(m_hLblPre, comboX, sy, 80, ROW_H);
    Move(m_hEditPreFix, comboX+82, sy, 80, ROW_H);
    Move(m_hLblSuf, comboX+170, sy, 80, ROW_H);
    Move(m_hEditSufFix, comboX+252, sy, 80, ROW_H);
    sy += ROW_H + 4;

    // Send text/hex area
    int sendAreaH = midH - GRP_TITLE - ROW_H - 4 - ROW_H - 6 - ROW_H - 4 - MARGIN;
    Move(m_hEditSend, sx, sy, sendCtrlW, sendAreaH);
    sy += sendAreaH + 4;

    // Send buttons row 1
    Move(m_hBtnSend,      sx, sy, BTN_W, ROW_H); sx += BTN_W + 4;
    Move(m_hBtnLoadFile,  sx, sy, BTN_W, ROW_H); sx += BTN_W + 4;
    Move(m_hBtnClearSend, sx, sy, 60,    ROW_H); sx += 64;
    Move(m_hChkAutoClearSend, sx, sy, 170, ROW_H);

    // Cyclic send row 2
    sx = x + MARGIN + 2; sy += ROW_H + 2;
    Move(m_hLblCyclic,          sx, sy, 75, ROW_H); sx += 77;
    Move(m_hEditCyclicInterval, sx, sy, 60, ROW_H); sx += 64;
    Move(m_hBtnCyclicStart,     sx, sy, 90, ROW_H); sx += 94;
    Move(m_hStaticCyclicCount,  sx, sy, 120, ROW_H);

    // -- Receive group --
    int rx = x + sendW + MARGIN;
    Move(m_hGrpRecv, rx, y, recvW, midH);
    int rsx = rx + MARGIN + 2, rsy = y + GRP_TITLE;
    int recvCtrlW = recvW - 2*(MARGIN+2);

    // Recv header row
    Move(m_hChkHexRecv,   rsx, rsy, 90, ROW_H); rsx += 94;
    Move(m_hBtnClearRecv, rsx, rsy, 60, ROW_H); rsx += 64;
    Move(m_hBtnCopyRecv,  rsx, rsy, 60, ROW_H); rsx += 64;
    Move(m_hBtnSaveRecv,  rsx, rsy, BTN_W, ROW_H); rsx += BTN_W + 4;
    Move(m_hChkAutoClearRecv, rsx, rsy, 155, ROW_H);
    rsx = rx + MARGIN + 2; rsy += ROW_H + 4;

    int recvAreaH = midH - GRP_TITLE - ROW_H - 4 - MARGIN;
    Move(m_hEditRecv, rsx, rsy, recvCtrlW, recvAreaH);
    Move(m_hHexRecv,  rsx, rsy, recvCtrlW, recvAreaH);

    y += midH + MARGIN;

    // ====== BOTTOM TAB (Log / Diagnostics / Statistics) ======
    int tabH = H - y - MARGIN;
    if (tabH < 80) tabH = 80;
    Move(m_hTabBottom, x, y, W - 2*MARGIN, tabH);
    // Tab client area: AdjustRect works in pixels, convert back to layout units
    RECT tabRc = { 0, 0, Dpi::Scale(W - 2*MARGIN), Dpi::Scale(tabH) };
    TabCtrl_AdjustRect(m_hTabBottom, FALSE, &tabRc);
    int tx = x + Dpi::Unscale(tabRc.left);
    int ty = y + Dpi::Unscale(tabRc.top);
    int tw = Dpi::Unscale(tabRc.right - tabRc.left);
    int th = Dpi::Unscale(tabRc.bottom - tabRc.top);

    // Log content (tab 0)
    Move(m_hComboLogFilter, tx + 2, ty + 2, 120, 200);
    Move(m_hBtnClearLog,    tx + 126, ty + 2, BTN_W, ROW_H);
    int listY = ty + ROW_H + 6;
    int listH = th - ROW_H - 8;
    Move(m_hListLog, tx + 2, listY, tw - 4, listH);

    // Diagnostics content (tab 1)
    Move(m_hStaticDiagInfo, tx + 2, ty + 2, tw - BTN_W - 10, th - 4);
    Move(m_hBtnRefreshDiag, tx + tw - BTN_W - 4, ty + 2, BTN_W, ROW_H);

    // Statistics content (tab 2)
    {
        int stx2 = tx + 2, sty2 = ty + 2;
        int statW2 = (tw - BTN_W - 12) / 5;
        // Row 1
        Move(m_hStatCon,       stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatSendStart, stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatSendEnd,   stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatBytesSent, stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatLatency,   stx2, sty2, statW2, ROW_H);
        stx2 = tx + 2; sty2 += ROW_H + 2;
        // Row 2
        Move(m_hStatRecvStart, stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatRecvEnd,   stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatBytesRecv, stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatTotal,     stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatThrough,   stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hBtnClearStats, stx2, sty2, BTN_W, ROW_H);
        stx2 = tx + 2; sty2 += ROW_H + 2;
        // Row 3 — cumulative totals
        Move(m_hStatTotalSent, stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatTotalRecv, stx2, sty2, statW2, ROW_H);
    }

    if (hdwp) EndDeferWindowPos(hdwp);
    if (Utils::GetWindowText(m_hComboHost) != hostText)
        SetWindowTextW(m_hComboHost, hostText.c_str());
    RedrawWindow(m_hWnd, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// ============================================================
//  SwitchBottomTab
// ============================================================

void MainWindow::SwitchBottomTab(int tab)
{
    m_bottomTab = tab;
    bool showLog   = (tab == 0);
    bool showDiag  = (tab == 1);
    bool showStats = (tab == 2);
    ShowWindow(m_hListLog,        showLog  ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnClearLog,    showLog  ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hComboLogFilter, showLog  ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStaticDiagInfo, showDiag ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnRefreshDiag, showDiag ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatCon,        showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatSendStart,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatSendEnd,    showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatBytesSent,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatLatency,    showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatRecvStart,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatRecvEnd,    showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatBytesRecv,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatTotal,      showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatThrough,    showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnClearStats,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatTotalSent,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatTotalRecv,  showStats ? SW_SHOW : SW_HIDE);
    if (showDiag) RefreshDiagnostics();
}

// ============================================================
//  OnDestroy
// ============================================================

void MainWindow::OnDestroy()
{
    // Stop the cyclic thread: the session is shut down first so a send blocked
    // on the socket returns, then the thread is joined before `this` goes away.
    m_cyclicStop = true;
    if (m_net) m_net->Disconnect();
    if (m_cyclicThread.joinable()) m_cyclicThread.join();
    m_cyclicRunning = false;
    m_net.reset();
    if (m_hImgListLog) ImageList_Destroy(m_hImgListLog);
    if (m_hTooltip) { DestroyWindow(m_hTooltip); m_hTooltip = nullptr; }
    if (m_hFont)     DeleteObject(m_hFont);
    if (m_hFontMono) DeleteObject(m_hFontMono);
}

// ============================================================
//  OnCommand
// ============================================================

void MainWindow::OnCommand(WORD id, WORD notify, HWND hCtrl)
{
    switch (id)
    {
    case IDC_BTN_CONNECT:    DoConnect();    break;
    case IDC_BTN_DISCONNECT: DoDisconnect(); break;
    case IDC_BTN_SEND:       DoSend();       break;
    case IDC_BTN_LOADFILE:   DoLoadFile();   break;

    case IDC_BTN_CLEARSEND:
        ClearSendEditor();
        break;

    case IDC_BTN_CLEARRECV:
        ClearRecv();
        break;

    case IDC_BTN_COPYRECV:
        if (!m_hexRecvMode) {
            std::wstring txt = Utils::GetWindowText(m_hEditRecv);
            Utils::CopyToClipboard(m_hWnd, txt);
        } else {
            // Copy hex representation
            std::string h = Utils::BytesToHexStr(m_recvData);
            Utils::CopyToClipboard(m_hWnd, Utils::ToWide(h));
        }
        break;

    case IDC_BTN_SAVERECV:
        DoSaveLastRecv();
        break;

    case IDC_BTN_CLEARSTATS:
        ClearStats();
        break;

    case IDC_BTN_CLEARLOG:
        ClearLog();
        break;

    case IDC_CHK_HEXVIEW:
        DoSendHexToggle();
        break;

    case IDC_CHK_HEXVIEW_RECV:
        m_hexRecvMode = Button_GetCheck(m_hChkHexRecv) == BST_CHECKED;
        RefreshRecvView();
        break;

    case IDC_COMBO_SEPARATOR:
        if (notify == CBN_SELCHANGE) {
            int sel = (int)SendMessage(m_hComboSep, CB_GETCURSEL, 0, 0);
            bool custom = (sel == (int)SeparatorMode::Custom);
            ShowWindow(m_hEditPreFix, custom ? SW_SHOW : SW_HIDE);
            ShowWindow(m_hEditSufFix, custom ? SW_SHOW : SW_HIDE);
            ShowWindow(m_hLblPre,     custom ? SW_SHOW : SW_HIDE);
            ShowWindow(m_hLblSuf,     custom ? SW_SHOW : SW_HIDE);
            // Relayout to reposition
            RECT rc; GetClientRect(m_hWnd, &rc);
            LayoutControls(rc.right, rc.bottom);
        }
        break;

    case IDC_CHK_KEEPALIVE:
        // Auto-uncheck autoDisconnect if keepAlive is checked
        if (Button_GetCheck(m_hChkKeepAlive) == BST_CHECKED)
            Button_SetCheck(m_hChkAutoDisc, BST_UNCHECKED);
        break;

    case IDC_CHK_AUTODISCONNECT:
        if (Button_GetCheck(m_hChkAutoDisc) == BST_CHECKED)
            Button_SetCheck(m_hChkKeepAlive, BST_UNCHECKED);
        break;

    case IDC_CHK_AUTOCONNECT:
        UpdateSendEnable();
        break;

    case IDC_EDIT_SEND:
        // Clear cached sendData when the user actually types so the hex view
        // re-syncs from the editor next time. Guard against programmatic SetWindowText.
        if (notify == EN_CHANGE && !m_ignoreEditChange) m_sendData.clear();
        break;

    case IDC_COMBO_HOST:
        if (notify == CBN_SELCHANGE) OnHostComboSelChange();
        break;

    case IDC_BTN_REFRESHDIAG:
        RefreshDiagnostics();
        break;

    case IDC_BTN_HELP:
        m_tooltipsEnabled = (Button_GetCheck(m_hBtnHelp) == BST_CHECKED);
        SendMessageW(m_hTooltip, TTM_ACTIVATE, m_tooltipsEnabled ? TRUE : FALSE, 0);
        break;

    case IDC_COMBO_LOGFILTER:
        if (notify == CBN_SELCHANGE) {
            m_logFilter = (int)SendMessage(m_hComboLogFilter, CB_GETCURSEL, 0, 0);
            RefreshLogView();
        }
        break;

    case IDC_BTN_CYCLIC_START:
        DoCyclicToggle();
        break;
    }
}

// ============================================================
//  Cyclic send
// ============================================================

void MainWindow::DoCyclicToggle()
{
    if (m_cyclicRunning) {
        // Stop without waiting: the thread may be inside a send that only ends with
        // its timeout. It reports back with WM_CYCLIC_UPDATE (final) and is joined there.
        if (!m_cyclicStop) {
            m_cyclicStop = true;
            SetWindowTextW(m_hBtnCyclicStart, L"Stopping...");
            UpdateSendEnable();
        }
        return;
    }

    if (!m_connected) {
        if (Button_GetCheck(m_hChkAutoConnect) == BST_CHECKED) {
            // Auto-connect then start cyclic - same pattern as DoSend.
            m_pendingCyclicStart = true;
            if (!m_sessionActive && !DoConnect()) m_pendingCyclicStart = false;
        } else {
            MessageBoxW(m_hWnd, L"Not connected.", L"Not connected", MB_ICONWARNING);
        }
        return;
    }

    // Interval in seconds; accept a decimal comma
    wchar_t buf[32] = {};
    GetWindowTextW(m_hEditCyclicInterval, buf, 32);
    for (wchar_t* c = buf; *c; ++c) if (*c == L',') *c = L'.';
    double sec = wcstod(buf, nullptr);
    if (!(sec >= 0.001)) sec = 0.001;
    if (sec > 86400.0)   sec = 86400.0;
    auto intervalUs = std::chrono::microseconds((long long)(sec * 1000000.0 + 0.5));

    std::vector<BYTE> data;
    if (!GetSendData(data)) return;
    if (data.empty()) {
        MessageBoxW(m_hWnd, L"Nothing to send.", L"Empty data", MB_ICONWARNING);
        return;
    }
    NetConfig cfg;
    if (!BuildNetConfig(cfg, true)) return;

    if (m_cyclicThread.joinable()) m_cyclicThread.join();   // finished (final message was handled)
    m_cyclicStop    = false;
    m_cyclicCount   = 0;
    m_cyclicRunning = true;
    const uint32_t gen = ++m_cyclicGen;
    SetWindowTextW(m_hBtnCyclicStart, L"Stop Cyclic");
    SetWindowTextW(m_hStaticCyclicCount, L"Sent: 0");

    LogEntry sle;
    sle.type = LogType::Info;
    sle.timestamp = Utils::NowTimestamp();
    {
        wchar_t ibuf[64];
        swprintf_s(ibuf, L"Cyclic send started (interval: %.3f s)", sec);
        sle.preview = ibuf;
    }
    AddLogEntry(std::move(sle));

    // The thread uses m_net and the members below; it is joined before either is
    // destroyed (OnCyclicMessage, OnDestroy).
    HWND hWnd = m_hWnd;
    m_cyclicThread = std::thread([this, data, cfg, intervalUs, hWnd, gen]() {
        using clock = std::chrono::steady_clock;
        const LPARAM tag = (LPARAM)((uint64_t)(gen & 0x7FFFFFFF) << 1);
        auto next = clock::now();
        auto lastUI = next;
        while (!m_cyclicStop.load(std::memory_order_relaxed)) {
            if (!m_net->IsConnected()) break;
            if (!m_net->Send(data, cfg)) break;   // send failed (disconnected)
            m_cyclicCount.fetch_add(1, std::memory_order_relaxed);
            auto now = clock::now();
            next += intervalUs;
            if (next < now) next = now;           // don't burst to catch up after a slow send

            // Throttled UI update every 100ms
            if (now - lastUI >= std::chrono::milliseconds(100)) {
                PostMessage(hWnd, WM_CYCLIC_UPDATE,
                            (WPARAM)m_cyclicCount.load(std::memory_order_relaxed), tag);
                lastUI = now;
            }

            // Sleep with periodic stop-check (max 10ms)
            while (!m_cyclicStop.load(std::memory_order_relaxed)) {
                auto remain = next - clock::now();
                if (remain <= std::chrono::microseconds(0)) break;
                if (remain > std::chrono::milliseconds(10))
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                else
                    std::this_thread::sleep_for(remain);
            }
        }
        PostMessage(hWnd, WM_CYCLIC_UPDATE,
                    (WPARAM)m_cyclicCount.load(std::memory_order_relaxed), tag | 1);
    });
}

void MainWindow::OnCyclicMessage(size_t count, bool final, uint32_t gen)
{
    if (gen != (m_cyclicGen & 0x7FFFFFFF)) return;   // message of an earlier run
    wchar_t cbuf[64];
    swprintf_s(cbuf, L"Sent: %u", (unsigned)count);
    SetWindowTextW(m_hStaticCyclicCount, cbuf);
    if (!final) return;

    // The thread has posted its last message and is returning.
    if (m_cyclicThread.joinable()) m_cyclicThread.join();
    const bool byUser = m_cyclicStop.load();
    m_cyclicRunning = false;
    m_cyclicStop = false;
    SetWindowTextW(m_hBtnCyclicStart, L"Start Cyclic");
    UpdateSendEnable();

    LogEntry sle;
    sle.type = LogType::Info;
    sle.timestamp = Utils::NowTimestamp();
    sle.preview = std::wstring(L"Cyclic send ") + (byUser ? L"stopped" : L"ended (connection lost or send failed)")
                  + L" (" + std::to_wstring(count) + L" sends)";
    AddLogEntry(std::move(sle));
}

// ============================================================
//  DoSaveLastRecv
// ============================================================

void MainWindow::DoSaveLastRecv()
{
    if (m_lastRecvData.empty()) {
        MessageBoxW(m_hWnd, L"No received data from the last transmission to save.",
                    L"Nothing to save", MB_ICONINFORMATION);
        return;
    }

    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = L"Binary Files\0*.bin\0Text Files\0*.txt\0All Files\0*.*\0\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"bin";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Save Last Received Transmission";
    if (!GetSaveFileNameW(&ofn)) return;

    std::ofstream f(buf, std::ios::binary);
    if (f) {
        f.write(reinterpret_cast<const char*>(m_lastRecvData.data()), (std::streamsize)m_lastRecvData.size());
        f.close();
    }
    if (!f) {
        MessageBoxW(m_hWnd, L"Cannot write to the selected file.", L"Save error", MB_ICONERROR);
        return;
    }

    wchar_t msg[MAX_PATH + 64];
    swprintf_s(msg, L"Saved %zu bytes to:\n%s", m_lastRecvData.size(), buf);
    MessageBoxW(m_hWnd, msg, L"Saved", MB_ICONINFORMATION);
}

// ============================================================
//  Connection history helpers
// ============================================================

void MainWindow::PopulateHostCombo()
{
    // CB_RESETCONTENT clears the edit text; keep what the user has typed.
    const std::wstring text = Utils::GetWindowText(m_hComboHost);
    SendMessage(m_hComboHost, CB_RESETCONTENT, 0, 0);
    for (auto& e : m_connHistory)
        SendMessage(m_hComboHost, CB_ADDSTRING, 0, (LPARAM)e.c_str());
    SetWindowTextW(m_hComboHost, text.c_str());
}

void MainWindow::OnHostComboSelChange()
{
    // Defer parsing: Windows overwrites the combo edit text AFTER
    // CBN_SELCHANGE returns, so we post a message to parse later.
    PostMessage(m_hWnd, WM_PARSE_HOST_COMBO, 0, 0);
}

void MainWindow::DoParseComboParts()
{
    std::wstring text = Utils::GetWindowText(m_hComboHost);
    auto pos = text.rfind(L':');
    if (pos != std::wstring::npos && pos > 0 && pos < text.size() - 1) {
        std::wstring host = text.substr(0, pos);
        std::wstring port = text.substr(pos + 1);
        int p = 0;
        if (Utils::IsValidPort(port, p)) {
            SetWindowTextW(m_hComboHost, host.c_str());
            SetWindowTextW(m_hEditPort, port.c_str());
        }
    }
}

// ============================================================
//  Hex text <-> bytes (the send editor in hex mode)
// ============================================================

// Whitespace-separated or contiguous hex digits. On failure `err` says what is wrong.
static bool ParseHexText(const std::wstring& text, std::vector<BYTE>& out, std::wstring& err)
{
    out.clear();
    int hi = -1;
    size_t digits = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        int v;
        if      (c >= L'0' && c <= L'9') v = c - L'0';
        else if (c >= L'a' && c <= L'f') v = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') v = c - L'A' + 10;
        else {
            err = L"'" + std::wstring(1, c) + L"' at character " + std::to_wstring(i + 1)
                  + L" is not a hex digit (use 0-9 and A-F).";
            return false;
        }
        ++digits;
        if (hi < 0) hi = v;
        else { out.push_back((BYTE)(hi << 4 | v)); hi = -1; }
    }
    if (hi >= 0) {
        err = L"The number of hex digits is odd (" + std::to_wstring(digits)
              + L"): every byte needs two digits, e.g. 48 65 6C 6C 6F.";
        return false;
    }
    return true;
}

// Index of the first byte of an incomplete UTF-8 sequence at the end, else size.
static size_t Utf8CompleteLength(const std::vector<BYTE>& v)
{
    size_t n = v.size();
    for (size_t k = 1; k <= 3 && k <= n; ++k) {
        BYTE b = v[n - k];
        if ((b & 0xC0) == 0x80) continue;               // continuation byte
        size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        return need > k ? n - k : n;
    }
    return n;
}

static std::wstring MakePreview(const BYTE* d, size_t n)
{
    std::wstring prev;
    for (size_t i = 0; i < std::min((size_t)80, n); ++i)
        prev += (d[i] >= 0x20 && d[i] < 0x7F) ? (wchar_t)d[i] : L'.';
    return prev;
}

// ============================================================
//  DoConnect / DoDisconnect
// ============================================================

bool MainWindow::DoConnect()
{
    if (m_sessionActive) return false;

    // Parse "host:port" from combo if the user typed/pasted a combined value
    std::wstring hostW = Utils::GetWindowText(m_hComboHost);
    auto colon = hostW.rfind(L':');
    if (colon != std::wstring::npos && colon > 0 && colon < hostW.size() - 1) {
        std::wstring maybePort = hostW.substr(colon + 1);
        int p = 0;
        if (Utils::IsValidPort(maybePort, p)) {
            hostW = hostW.substr(0, colon);
            SetWindowTextW(m_hComboHost, hostW.c_str());
            SetWindowTextW(m_hEditPort, maybePort.c_str());
        }
    }

    // Validate host/IP
    if (!Utils::IsValidHostOrIP(hostW)) {
        MessageBoxW(m_hWnd, L"Please enter a valid hostname or IPv4 address.", L"Invalid host", MB_ICONWARNING);
        SetFocus(m_hComboHost);
        return false;
    }

    // Validate port
    std::wstring portW = Utils::GetWindowText(m_hEditPort);
    int port = 0;
    if (!Utils::IsValidPort(portW, port)) {
        MessageBoxW(m_hWnd, L"Please enter a valid port number (1–65535).", L"Invalid port", MB_ICONWARNING);
        SetFocus(m_hEditPort);
        return false;
    }

    // Validate timeout: whole seconds, 0 = no limit
    std::wstring toW = Utils::GetWindowText(m_hEditTimeout);
    bool toOk = !toW.empty() && toW.size() <= 5;
    for (wchar_t c : toW) if (c < L'0' || c > L'9') toOk = false;
    if (toOk && _wtoi(toW.c_str()) > 3600) toOk = false;
    if (!toOk) {
        MessageBoxW(m_hWnd, L"The timeout must be a whole number of seconds from 0 to 3600 (0 = no limit).",
                    L"Invalid timeout", MB_ICONWARNING);
        SetFocus(m_hEditTimeout);
        return false;
    }

    NetConfig cfg;
    if (!BuildNetConfig(cfg, false)) return false;

    if (!m_net) m_net = std::make_unique<TcpNetwork>(m_hWnd);
    m_connectTime = Utils::Now();
    m_lastError.clear();
    if (!m_net->Connect(cfg)) {
        MessageBoxW(m_hWnd, L"A previous connection is still closing. Try again in a moment.",
                    L"Connect", MB_ICONWARNING);
        return false;
    }
    SetConnectionUI(CS_CONNECTING);
    return true;
}

void MainWindow::DoDisconnect()
{
    m_pendingAutoSend    = false;
    m_pendingCyclicStart = false;
    // The cyclic thread notices the stop flag / the closed session by itself and
    // reports back with WM_CYCLIC_UPDATE; nothing here waits for it.
    if (m_cyclicRunning) m_cyclicStop = true;
    if (m_net) m_net->Disconnect();
    if (m_sessionActive) {
        SetWindowTextW(m_hStaticStatus, L"◌ Disconnecting...");
        EnableWindow(m_hBtnDisconnect, FALSE);
    }
    // The UI is reset by WM_NET_DISCONNECTED.
}

// ============================================================
//  DoSend
// ============================================================

void MainWindow::DoSend()
{
    // Auto-connect if enabled and not connected. A flag instead of a retry message:
    // OnNetConnected() calls DoSend() exactly once when the connection is up.
    if (!m_connected && Button_GetCheck(m_hChkAutoConnect) == BST_CHECKED) {
        m_pendingAutoSend = true;
        if (!m_sessionActive && !DoConnect()) m_pendingAutoSend = false;
        return;
    }
    if (!m_connected) {
        MessageBoxW(m_hWnd, L"Not connected.", L"Not connected", MB_ICONWARNING);
        return;
    }
    std::vector<BYTE> data;
    if (!GetSendData(data)) return;
    if (data.empty()) {
        MessageBoxW(m_hWnd, L"Nothing to send.", L"Empty data", MB_ICONWARNING);
        return;
    }

    NetConfig cfg;
    if (!BuildNetConfig(cfg, true)) return;

    const wchar_t* proto = ProtoLabel(cfg.udpMode);

    // Save to log immediately as "Sent" entry
    LogEntry le;
    le.type      = LogType::Sent;
    le.timestamp = Utils::NowTimestamp();
    le.data      = data;
    le.byteCount = data.size();
    le.preview   = std::wstring(L"[") + proto + L"] " + MakePreview(data.data(), data.size())
                   + L" [" + std::to_wstring(data.size()) + L" B]";
    m_lastRecvData.clear();   // reset last-transmission tracker
    if (Button_GetCheck(m_hChkAutoClearRecv) == BST_CHECKED) ClearRecv();

    // Queue the send (non-blocking; send worker posts WM_NET_SEND_COMPLETE)
    if (m_net) m_net->QueueSend(std::move(data), cfg);

    AddLogEntry(std::move(le));

    // Auto-clear send window after sending
    if (Button_GetCheck(m_hChkAutoClearSend) == BST_CHECKED) ClearSendEditor();
}

// ============================================================
//  Send editor
// ============================================================

void MainWindow::SetSendEditorText(const std::wstring& text)
{
    // Programmatic change: must not drop the pinned bytes (see EN_CHANGE).
    m_ignoreEditChange = true;
    SetWindowTextW(m_hEditSend, text.c_str());
    m_ignoreEditChange = false;
}

void MainWindow::SetSendHexMode(bool hex)
{
    m_hexSendMode = hex;
    Button_SetCheck(m_hChkHexSend, hex ? BST_CHECKED : BST_UNCHECKED);
}

void MainWindow::ClearSendEditor()
{
    SetSendEditorText(L"");
    m_sendData.clear();
}

// Payload per the editor rules:
//  text mode - the pinned bytes if there are any, else the editor text as UTF-8
//  hex mode  - the hex pairs in the editor
bool MainWindow::GetSendData(std::vector<BYTE>& out)
{
    out.clear();
    std::wstring ws = Utils::GetWindowText(m_hEditSend);
    if (m_hexSendMode) {
        std::wstring err;
        if (!ParseHexText(ws, out, err)) {
            out.clear();
            MessageBoxW(m_hWnd, (L"The hex data cannot be sent.\n\n" + err).c_str(), L"Invalid hex", MB_ICONWARNING);
            SetFocus(m_hEditSend);
            return false;
        }
        return true;
    }
    if (!m_sendData.empty()) { out = m_sendData; return true; }
    std::string s = Utils::ToUTF8(ws);
    out.assign(s.begin(), s.end());
    return true;
}

// Shows `bytes` in the editor (Load File, log reload) and pins them so they are
// sent exactly as given until the user edits the text.
void MainWindow::LoadPayloadIntoEditor(const std::vector<BYTE>& bytes, const std::wstring& name)
{
    const bool text     = Utils::LooksLikeText(bytes);
    const bool fitsHex  = bytes.size() <= HEX_EDIT_MAX;
    const bool fitsText = text && bytes.size() <= TEXT_SHOW_MAX;

    std::wstring shown;
    if (m_hexSendMode && fitsHex) {
        shown = Utils::BytesToHexText(bytes);
    } else if (fitsText) {
        SetSendHexMode(false);
        shown = Utils::BytesToDisplayText(bytes);
    } else if (fitsHex) {                      // small binary: hex is the only readable view
        SetSendHexMode(true);
        shown = Utils::BytesToHexText(bytes);
    } else {
        SetSendHexMode(false);
        shown = L"[" + (name.empty() ? std::wstring(text ? L"Text data" : L"Binary data") : (text ? L"File " : L"Binary file ") + name)
                + L", " + std::to_wstring(bytes.size()) + L" bytes - sent exactly as loaded]";
    }
    SetSendEditorText(shown);
    m_sendData = bytes;
}

void MainWindow::DoSendHexToggle()
{
    const bool wantHex = Button_GetCheck(m_hChkHexSend) == BST_CHECKED;
    if (wantHex == m_hexSendMode) return;

    std::vector<BYTE> bytes;
    if (!GetSendData(bytes)) {             // hex -> text with invalid hex: stay in hex
        Button_SetCheck(m_hChkHexSend, m_hexSendMode ? BST_CHECKED : BST_UNCHECKED);
        return;
    }
    if (wantHex) {
        if (bytes.size() > HEX_EDIT_MAX) {
            Button_SetCheck(m_hChkHexSend, BST_UNCHECKED);
            MessageBoxW(m_hWnd, L"This data is larger than 1 MB and cannot be edited as hex. "
                        L"It can still be sent as it is.", L"Hex", MB_ICONINFORMATION);
            return;
        }
        m_hexSendMode = true;
        SetSendEditorText(Utils::BytesToHexText(bytes));
        m_sendData.clear();
    } else {
        m_hexSendMode = false;
        std::wstring shown = Utils::BytesToDisplayText(bytes);
        SetSendEditorText(shown);
        // Keep the exact bytes when the text view cannot reproduce them.
        std::string back = Utils::ToUTF8(shown);
        if (back.size() == bytes.size() && std::equal(back.begin(), back.end(), bytes.begin())) m_sendData.clear();
        else                                                                                  m_sendData = bytes;
    }
}

// ============================================================
//  DoLoadFile
// ============================================================

void MainWindow::DoLoadFile()
{
    std::wstring path;
    if (!Utils::OpenFileDialog(m_hWnd, path)) return;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { MessageBoxW(m_hWnd, L"Cannot open the file.", L"Load File", MB_ICONERROR); return; }

    std::streamoff sz = f.tellg();
    if (sz < 0) { MessageBoxW(m_hWnd, L"Cannot determine the size of the file.", L"Load File", MB_ICONERROR); return; }
    if ((unsigned long long)sz > FILE_MAX) {
        MessageBoxW(m_hWnd, L"The file is larger than 64 MB and cannot be loaded.", L"Load File", MB_ICONWARNING);
        return;
    }
    std::vector<BYTE> bytes((size_t)sz);
    f.seekg(0);
    if (sz > 0) f.read((char*)bytes.data(), sz);
    if (!f || f.gcount() != sz) {
        MessageBoxW(m_hWnd, L"The file could not be read completely.", L"Load File", MB_ICONERROR);
        return;
    }

    size_t slash = path.find_last_of(L"\\/");
    LoadPayloadIntoEditor(bytes, slash == std::wstring::npos ? path : path.substr(slash + 1));
    SetFocus(m_hEditSend);
}

// ============================================================
//  BuildNetConfig
// ============================================================

bool MainWindow::BuildNetConfig(NetConfig& cfg, bool forSend)
{
    cfg = NetConfig();
    cfg.host          = Utils::ToUTF8(Utils::GetWindowText(m_hComboHost));
    cfg.port          = Utils::GetDlgInt(m_hWnd, IDC_EDIT_PORT, 9000);
    cfg.timeoutSec    = std::clamp(Utils::GetDlgInt(m_hWnd, IDC_EDIT_TIMEOUT, 5), 0, 3600);
    cfg.keepAlive     = Button_GetCheck(m_hChkKeepAlive) == BST_CHECKED;
    cfg.autoDisconnect= Button_GetCheck(m_hChkAutoDisc)  == BST_CHECKED;
    cfg.udpMode       = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    cfg.separator     = GetSeparatorMode();

    if (forSend && cfg.separator == SeparatorMode::Custom) {
        std::wstring err;
        if (!ParseHexText(Utils::GetWindowText(m_hEditPreFix), cfg.customPrefix, err)) {
            MessageBoxW(m_hWnd, (L"Invalid custom prefix.\n\n" + err).c_str(), L"Invalid hex", MB_ICONWARNING);
            SetFocus(m_hEditPreFix);
            return false;
        }
        if (!ParseHexText(Utils::GetWindowText(m_hEditSufFix), cfg.customSuffix, err)) {
            MessageBoxW(m_hWnd, (L"Invalid custom suffix.\n\n" + err).c_str(), L"Invalid hex", MB_ICONWARNING);
            SetFocus(m_hEditSufFix);
            return false;
        }
    }
    return true;
}

SeparatorMode MainWindow::GetSeparatorMode()
{
    int sel = (int)SendMessage(m_hComboSep, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    return (SeparatorMode)sel;
}

// ============================================================
//  Receive view
// ============================================================

// Brings whichever receive view is now in front up to date with m_recvData.
void MainWindow::RefreshRecvView()
{
    if (m_hexRecvMode) {
        ShowWindow(m_hEditRecv, SW_HIDE);
        ShowWindow(m_hHexRecv,  SW_SHOW);
        if (m_recvHexStale || HexView::GetDataSize(m_hHexRecv) != m_recvData.size()) {
            HexView::SetData(m_hHexRecv, m_recvData);
            m_recvHexStale = false;
        }
    } else {
        ShowWindow(m_hHexRecv,  SW_HIDE);
        ShowWindow(m_hEditRecv, SW_SHOW);
        if (m_recvTextStale) RebuildRecvText();
    }
}

void MainWindow::RebuildRecvText()
{
    size_t complete = Utf8CompleteLength(m_recvData);
    m_recvPending.assign(m_recvData.begin() + complete, m_recvData.end());
    std::wstring text = Utils::BytesToDisplayText(m_recvData.data(), complete);
    m_recvPrevCR = complete > 0 && m_recvData[complete - 1] == '\r';
    SetWindowTextW(m_hEditRecv, text.c_str());
    m_recvTextStale = false;
}

void MainWindow::ClearRecv()
{
    m_recvData.clear();
    m_recvPending.clear();
    m_recvPrevCR = false;
    m_recvTextStale = m_recvHexStale = false;
    SetWindowTextW(m_hEditRecv, L"");
    HexView::ClearData(m_hHexRecv);
}

// Shows a UTF-8 tail that never got completed.
void MainWindow::FlushRecvPending()
{
    if (m_recvPending.empty() || m_hexRecvMode) return;
    std::wstring t = Utils::BytesToDisplayText(m_recvPending);
    m_recvPending.clear();
    SendMessage(m_hEditRecv, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessage(m_hEditRecv, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
}

// m_recvData already contains `data`; this brings the visible view along and
// marks the other one stale instead of rebuilding it for every chunk.
void MainWindow::AppendReceiveData(const std::vector<BYTE>& data)
{
    if (m_hexRecvMode) {
        m_recvTextStale = true;
        if (m_recvHexStale) return;                 // refreshed on the next view switch
        HexView::AppendData(m_hHexRecv, data.data(), data.size());
        return;
    }

    m_recvHexStale = true;
    if (m_recvTextStale) { RebuildRecvText(); return; }

    // Decode only complete UTF-8 sequences; an incomplete tail waits for the next chunk.
    std::vector<BYTE> bytes = std::move(m_recvPending);
    bytes.insert(bytes.end(), data.begin(), data.end());
    size_t complete = Utf8CompleteLength(bytes);
    m_recvPending.assign(bytes.begin() + complete, bytes.end());

    size_t off = 0;
    if (m_recvPrevCR && complete > 0 && bytes[0] == '\n') off = 1;   // CR LF split over two chunks
    if (complete > off) {
        std::wstring ws = Utils::BytesToDisplayText(bytes.data() + off, complete - off);
        SendMessage(m_hEditRecv, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessage(m_hEditRecv, EM_REPLACESEL, FALSE, (LPARAM)ws.c_str());
        m_recvPrevCR = bytes[complete - 1] == '\r';
    } else if (complete > 0) {
        m_recvPrevCR = false;
    }
}

// ============================================================
//  RefreshDiagnostics - nerdy socket info
// ============================================================

void MainWindow::RefreshDiagnostics()
{
    if (!m_net || !m_connected) {
        SetWindowTextW(m_hStaticDiagInfo, L"Not connected \x2014 connect to view diagnostics");
        return;
    }

    std::wstring info;
    bool ok = m_net->WithSocket([&](SOCKET sock) {

    // Local endpoint
    sockaddr_in localAddr = {};
    int localLen = sizeof(localAddr);
    if (getsockname(sock, (sockaddr*)&localAddr, &localLen) == 0) {
        wchar_t ip[64]; DWORD ipLen = 64;
        WSAAddressToStringW((sockaddr*)&localAddr, localLen, nullptr, ip, &ipLen);
        info += L"Local: " + std::wstring(ip) + L"  ";
    }

    // Remote endpoint
    sockaddr_in remoteAddr = {};
    int remoteLen = sizeof(remoteAddr);
    if (getpeername(sock, (sockaddr*)&remoteAddr, &remoteLen) == 0) {
        wchar_t ip[64]; DWORD ipLen = 64;
        WSAAddressToStringW((sockaddr*)&remoteAddr, remoteLen, nullptr, ip, &ipLen);
        info += L"Remote: " + std::wstring(ip) + L"\r\n";
    }

    // SO_SNDBUF / SO_RCVBUF
    int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
    optLen = sizeof(int);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);
    wchar_t buf[512];
    swprintf_s(buf, L"SO_SNDBUF: %d B  SO_RCVBUF: %d B  ", sndbuf, rcvbuf);
    info += buf;

    // TCP_NODELAY (Nagle)
    BOOL nodelay = FALSE; optLen = sizeof(BOOL);
    getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);
    info += nodelay ? L"Nagle: OFF (TCP_NODELAY)  " : L"Nagle: ON  ";

    // SO_KEEPALIVE
    BOOL keepAlive = FALSE; optLen = sizeof(BOOL);
    getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&keepAlive, &optLen);
    info += keepAlive ? L"KeepAlive: ON\r\n" : L"KeepAlive: OFF\r\n";

    // TCP_MAXSEG (MSS) - Windows may not support this, show if available
    int mss = 0; optLen = sizeof(int);
    if (getsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, &optLen) == 0 && mss > 0) {
        swprintf_s(buf, L"MSS: %d B  ", mss);
        info += buf;
    }

    // SO_TYPE
    int soType = 0; optLen = sizeof(int);
    getsockopt(sock, SOL_SOCKET, SO_TYPE, (char*)&soType, &optLen);
    info += (soType == SOCK_STREAM) ? L"Type: SOCK_STREAM  " : (soType == SOCK_DGRAM) ? L"Type: SOCK_DGRAM  " : L"Type: ?  ";

    // Estimated MTU (Ethernet): MSS + 40 (TCP+IP headers)
    if (mss > 0) {
        swprintf_s(buf, L"Est. MTU: %d B  Frame: ~%d B", mss + 40, mss + 40 + 14);
        info += buf;
    } else {
        info += L"MTU: (query N/A)  Est. Frame: ~1514 B (Ethernet)";
    }

    });
    SetWindowTextW(m_hStaticDiagInfo, ok ? info.c_str() : L"Socket not available");
}

// ============================================================
//  SetConnectionUI
// ============================================================

void MainWindow::UpdateSendEnable()
{
    const bool autoConn = Button_GetCheck(m_hChkAutoConnect) == BST_CHECKED;
    const bool canSend  = m_connected || autoConn;
    EnableWindow(m_hBtnSend, canSend);
    // While a cyclic run is winding down its button stays disabled.
    const bool stopping = m_cyclicRunning && m_cyclicStop;
    EnableWindow(m_hBtnCyclicStart, (canSend || m_cyclicRunning) && !stopping);
}

void MainWindow::SetConnectionUI(ConnState st)
{
    m_sessionActive = (st != CS_IDLE);
    m_connected     = (st == CS_CONNECTED);
    EnableWindow(m_hBtnConnect,    !m_sessionActive);
    EnableWindow(m_hBtnDisconnect,  m_sessionActive);
    UpdateSendEnable();
    EnableWindow(m_hComboHost,    !m_sessionActive);
    EnableWindow(m_hEditPort,      !m_sessionActive);
    EnableWindow(m_hEditTimeout,   !m_sessionActive);
    EnableWindow(m_hComboProtocol, !m_sessionActive);
    EnableWindow(m_hChkKeepAlive,  !m_sessionActive);
    EnableWindow(m_hChkAutoDisc,   !m_sessionActive);
    EnableWindow(m_hChkAutoConnect,!m_sessionActive);

    // Tab label: everything after the en dash is shown by the frame.
    std::wstring detail;
    if (m_sessionActive) {
        bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
        detail = std::wstring(isUdp ? L"UDP " : L"") + Utils::GetWindowText(m_hComboHost)
                 + L":" + Utils::GetWindowText(m_hEditPort);
    }
    switch (st) {
    case CS_CONNECTED:
        SetWindowTextW(m_hStaticStatus, L"● Connected");
        SetWindowTextW(m_hWnd, (L"TCP Client v" TCPTOOLS_VERSION L"  –  " + detail).c_str());
        NotifyToolState(m_hWnd, TOOL_ACTIVE);
        break;
    case CS_CONNECTING:
        SetWindowTextW(m_hStaticStatus, L"◌ Connecting...");
        SetWindowTextW(m_hWnd, (L"TCP Client v" TCPTOOLS_VERSION L"  –  " + detail + L"…").c_str());
        NotifyToolState(m_hWnd, TOOL_WAITING);
        break;
    default:
        SetWindowTextW(m_hStaticStatus,
            m_lastError.empty() ? L"● Disconnected" : (L"● Disconnected - " + m_lastError).c_str());
        SetWindowTextW(m_hWnd, L"TCP Client v" TCPTOOLS_VERSION);
        NotifyToolState(m_hWnd, TOOL_IDLE);
        break;
    }
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
}

// ============================================================
//  Network message handlers
// ============================================================

void MainWindow::OnNetConnected()
{
    m_connectTime = Utils::Now();
    SetConnectionUI(CS_CONNECTED);

    // Save connection to history. Other pages write the same file, so merge with
    // what is on disk instead of overwriting it with this page's older copy.
    {
        std::wstring host = Utils::GetWindowText(m_hComboHost);
        std::wstring port = Utils::GetWindowText(m_hEditPort);
        m_connHistory = Utils::LoadHistory(L"TcpClient");
        Utils::SaveHistory(L"TcpClient", host + L":" + port, m_connHistory);
        PopulateHostCombo();
    }

    bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    const wchar_t* proto = ProtoLabel(isUdp);

    LogEntry le;
    le.type      = LogType::Connected;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = std::wstring(L"[") + proto + L"] Connected to "
                   + Utils::GetWindowText(m_hComboHost)
                   + L":" + Utils::GetWindowText(m_hEditPort);
    le.byteCount = 0;
    AddLogEntry(std::move(le));

    // Socket info log
    if (m_net) m_net->WithSocket([this](SOCKET sock) {
        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);
        BOOL nodelay = FALSE; optLen = sizeof(BOOL);
        getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);

        wchar_t infoBuf[256];
        swprintf_s(infoBuf, L"Socket opts: SNDBUF=%dB RCVBUF=%dB TCP_NODELAY=%s",
                   sndbuf, rcvbuf, nodelay ? L"ON" : L"OFF");
        LogEntry li;
        li.type = LogType::Info;
        li.timestamp = Utils::NowTimestamp();
        li.preview = infoBuf;
        AddLogEntry(std::move(li));

        sockaddr_in localAddr = {};
        int localLen = sizeof(localAddr);
        if (getsockname(sock, (sockaddr*)&localAddr, &localLen) == 0) {
            wchar_t ip[64] = {}; DWORD ipLen = 64;
            WSAAddressToStringW((sockaddr*)&localAddr, localLen, nullptr, ip, &ipLen);
            LogEntry li2;
            li2.type = LogType::Info;
            li2.timestamp = Utils::NowTimestamp();
            li2.preview = L"Local endpoint: " + std::wstring(ip);
            AddLogEntry(std::move(li2));
        }
    });

    UpdateStats(CycleStats{});
    {
        wchar_t buf[64];
        swprintf_s(buf, L"Connect time: %s", Utils::NowTimestamp().c_str());
        SetWindowTextW(m_hStatCon, buf);
    }

    RefreshDiagnostics();

    // Connection triggered by auto-connect: do what the user asked for, now that
    // the connection (and its log lines) are in place.
    if (m_pendingAutoSend) {
        m_pendingAutoSend = false;
        DoSend();
    }
    if (m_pendingCyclicStart) {
        m_pendingCyclicStart = false;
        PostMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_CYCLIC_START, BN_CLICKED), 0);
    }
}

void MainWindow::OnNetDisconnected(bool neverConnected)
{
    m_pendingAutoSend    = false; // cancel pending auto-send if connection dropped
    m_pendingCyclicStart = false;
    // A running cyclic thread sees the closed session by itself.
    FlushRecvPending();
    const bool hadError = !m_lastError.empty();
    SetConnectionUI(CS_IDLE);
    m_lastError.clear();

    const wchar_t* proto = ProtoLabel(SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);

    // A failed connect already has its Error entry.
    if (!neverConnected || !hadError) {
        LogEntry le;
        le.type      = neverConnected ? LogType::Info : LogType::Disconnected;
        le.timestamp = Utils::NowTimestamp();
        le.preview   = std::wstring(L"[") + proto + L"] "
                       + (neverConnected ? L"Connection attempt cancelled" : L"Disconnected (connection closed)");
        le.byteCount = 0;
        AddLogEntry(std::move(le));
    }

    RefreshDiagnostics();
}

void MainWindow::OnNetDataReceived(size_t len, BYTE* pData)
{
    // Takes ownership of pData
    std::vector<BYTE> chunk(pData, pData + len);
    delete[] pData;

    // Accumulate total bytes
    m_totalBytesRecv += len;

    // Auto-clear receive window before appending new data
    if (Button_GetCheck(m_hChkAutoClearRecv) == BST_CHECKED) {
        m_lastRecvData.clear();
        ClearRecv();
    }

    m_recvData.insert(m_recvData.end(), chunk.begin(), chunk.end());
    m_lastRecvData.insert(m_lastRecvData.end(), chunk.begin(), chunk.end()); // accumulate for Save Last
    TrimNewestHalf(m_lastRecvData, LAST_RECV_MAX);
    if (TrimNewestHalf(m_recvData, RECV_MAX)) {
        // Both views must show the same (shortened) buffer.
        if (m_hexRecvMode) { HexView::SetData(m_hHexRecv, m_recvData); m_recvTextStale = true; }
        else               { RebuildRecvText(); m_recvHexStale = true; }
    } else {
        AppendReceiveData(chunk);
    }

    const wchar_t* proto = ProtoLabel(SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);

    LogEntry le;
    le.type      = LogType::Received;
    le.timestamp = Utils::NowTimestamp();
    le.byteCount = len;
    le.preview   = std::wstring(L"[") + proto + L"] " + MakePreview(chunk.data(), chunk.size());
    le.data      = std::move(chunk);
    AddLogEntry(std::move(le));

    // Update total stats labels
    {
        wchar_t buf[80];
        swprintf_s(buf, L"Total recv: %s", Utils::FormatBytes(m_totalBytesRecv).c_str());
        SetWindowTextW(m_hStatTotalRecv, buf);
    }
}

// Send-to-first-receive time measured by the network thread.
void MainWindow::OnNetLatency(double latMs)
{
    if (m_cyclicRunning) return;   // cyclic sends have no log entry of their own
    for (int i = (int)m_log.size() - 1; i >= 0; --i) {
        if (m_log[i].type != LogType::Sent) continue;
        if (m_log[i].latencyMs < 0) {
            m_log[i].latencyMs = latMs;
            for (int v = (int)m_logVisible.size() - 1; v >= 0; --v) {
                if (m_logVisible[v] == i) { ListView_RedrawItems(m_hListLog, v, v); break; }
            }
        }
        break;
    }
}

void MainWindow::OnNetSendComplete(size_t bytesSent, CycleStats* pStats)
{
    CycleStats s;
    if (pStats) { s = *pStats; delete pStats; }

    // Accumulate total bytes
    m_totalBytesSent += bytesSent;

    if (m_connected && !m_lastError.empty()) {   // a non-fatal error (UDP) is over
        m_lastError.clear();
        SetWindowTextW(m_hStaticStatus, L"● Connected");
    }

    // During cyclic mode, skip log entry updates (no individual log entries)
    if (!m_cyclicRunning) {
        // Update last log Sent entry with stats
        for (int i = (int)m_log.size() - 1; i >= 0; --i) {
            if (m_log[i].type == LogType::Sent) {
                m_log[i].stats = s;
                // Find visible row and invalidate it
                for (int v = (int)m_logVisible.size() - 1; v >= 0; --v) {
                    if (m_logVisible[v] == i) {
                        ListView_RedrawItems(m_hListLog, v, v);
                        break;
                    }
                }
                break;
            }
        }
    }

    UpdateStats(s);
}

// Errors come from the network thread, possibly while another tab is in front,
// so they go to the log and the status line, never to a modal box. The session
// end (WM_NET_DISCONNECTED) follows for every fatal one.
void MainWindow::OnNetError(int /*wsaErr*/, std::wstring* pMsg)
{
    m_pendingAutoSend    = false;
    m_pendingCyclicStart = false;
    std::wstring msg;
    if (pMsg) { msg = *pMsg; delete pMsg; }

    m_lastError = msg;
    if (m_sessionActive)
        SetWindowTextW(m_hStaticStatus,
            ((m_connected ? L"● Connected - " : L"◌ Connecting... - ") + msg).c_str());

    LogEntry le;
    le.type      = LogType::Error;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = msg;
    le.byteCount = 0;
    AddLogEntry(std::move(le));
}

// ============================================================
//  UpdateStats
// ============================================================

void MainWindow::UpdateStats(const CycleStats& s)
{
    auto set = [&](HWND hw, const wchar_t* label, double ms) {
        wchar_t buf[80];
        if (ms >= 0) swprintf_s(buf, L"%s %s", label, Utils::FormatMs(ms).c_str());
        else         swprintf_s(buf, L"%s ---", label);
        SetWindowTextW(hw, buf);
    };
    auto setBuf = [&](HWND hw, const wchar_t* text) { SetWindowTextW(hw, text); };

    if (!s.valid) return;

    set(m_hStatSendStart, L"Send start:", s.ConnectToSendMs());
    set(m_hStatSendEnd,   L"Send end:",   s.SendDurationMs());
    set(m_hStatRecvStart, L"Recv start:", s.SendToFirstRecvMs());
    set(m_hStatRecvEnd,   L"Recv end:",   s.RecvDurationMs());
    set(m_hStatLatency,   L"Latency:",    s.SendToFirstRecvMs());
    set(m_hStatTotal,     L"Total:",      s.TotalMs());

    wchar_t buf[80];
    swprintf_s(buf, L"Sent: %s", Utils::FormatBytes(s.bytesSent).c_str());
    SetWindowTextW(m_hStatBytesSent, buf);
    swprintf_s(buf, L"Recv: %s", Utils::FormatBytes(s.bytesReceived).c_str());
    SetWindowTextW(m_hStatBytesRecv, buf);

    double th = s.ThroughputKBps();
    if (th > 0) { swprintf_s(buf, L"Throughput: %.1f KB/s", th); SetWindowTextW(m_hStatThrough, buf); }
    else          SetWindowTextW(m_hStatThrough, L"Throughput: ---");

    // Update total byte labels (totals are accumulated in OnNetSendComplete/OnNetDataReceived)
    swprintf_s(buf, L"Total sent: %s", Utils::FormatBytes(m_totalBytesSent).c_str());
    SetWindowTextW(m_hStatTotalSent, buf);
    swprintf_s(buf, L"Total recv: %s", Utils::FormatBytes(m_totalBytesRecv).c_str());
    SetWindowTextW(m_hStatTotalRecv, buf);
}

void MainWindow::ClearStats()
{
    auto names = std::initializer_list<std::pair<HWND, const wchar_t*>>{
        {m_hStatCon,       L"Connect time: ---"},
        {m_hStatSendStart, L"Send start: ---"},
        {m_hStatSendEnd,   L"Send end: ---"},
        {m_hStatRecvStart, L"Recv start: ---"},
        {m_hStatRecvEnd,   L"Recv end: ---"},
        {m_hStatBytesSent, L"Bytes sent: ---"},
        {m_hStatBytesRecv, L"Bytes recv: ---"},
        {m_hStatLatency,   L"Latency: ---"},
        {m_hStatTotal,     L"Total time: ---"},
        {m_hStatThrough,   L"Throughput: ---"},
        {m_hStatTotalSent, L"Total sent: 0 B"},
        {m_hStatTotalRecv, L"Total recv: 0 B"},
    };
    for (auto& [hw, txt] : names) SetWindowTextW(hw, txt);
    m_totalBytesSent = 0;
    m_totalBytesRecv = 0;
}

// ============================================================
//  Log management
// ============================================================

static bool PassesLogFilter(LogType type, int filter)
{
    // 0=All, 1=Data Only, 2=Connection, 3=Errors
    switch (filter) {
    case 1: return type == LogType::Sent || type == LogType::Received;
    case 2: return type == LogType::Connected || type == LogType::Disconnected || type == LogType::Info;
    case 3: return type == LogType::Error;
    default: return true;
    }
}

void MainWindow::RefreshLogView()
{
    m_logVisible.clear();
    for (int i = 0; i < (int)m_log.size(); ++i) {
        if (PassesLogFilter(m_log[i].type, m_logFilter))
            m_logVisible.push_back(i);
    }
    ListView_SetItemCountEx(m_hListLog, (int)m_logVisible.size(), 0);
    InvalidateRect(m_hListLog, nullptr, TRUE);
}

void MainWindow::ClearLog()
{
    m_log.clear();
    m_logVisible.clear();
    m_logBase  = 0;
    m_logBytes = 0;
    ListView_SetItemCountEx(m_hListLog, 0, 0);
    InvalidateRect(m_hListLog, nullptr, TRUE);
}

void MainWindow::AddLogEntry(LogEntry&& e)
{
    m_logBytes += e.data.size();
    m_log.push_back(std::move(e));
    NotifyToolEvent(m_hWnd);   // every entry counts, also ones the log filter hides

    if (m_log.size() > LOG_MAX_ENTRIES || m_logBytes > LOG_MAX_BYTES) {
        // Drop the oldest 10 %; m_logBase keeps the "#" column counting from the first ever entry.
        size_t drop = std::max<size_t>(m_log.size() / 10, 1);
        for (size_t i = 0; i < drop; ++i) m_logBytes -= m_log[i].data.size();
        m_log.erase(m_log.begin(), m_log.begin() + (std::ptrdiff_t)drop);
        m_logBase += drop;
        RefreshLogView();
        if (!m_logVisible.empty()) ListView_EnsureVisible(m_hListLog, (int)m_logVisible.size() - 1, FALSE);
        return;
    }

    const LogEntry& le = m_log.back();
    int origIdx = (int)(m_log.size() - 1);

    // If this entry doesn't pass current filter, don't add to visible list
    if (!PassesLogFilter(le.type, m_logFilter)) return;

    m_logVisible.push_back(origIdx);
    int count = (int)m_logVisible.size();
    ListView_SetItemCountEx(m_hListLog, count, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    ListView_RedrawItems(m_hListLog, count - 1, count - 1);
    ListView_EnsureVisible(m_hListLog, count - 1, FALSE);
}

// ============================================================
//  OnNotify (ListView double-click)
// ============================================================

void MainWindow::OnNotify(NMHDR* nm)
{
    if (nm->idFrom == IDC_TAB_BOTTOM && nm->code == TCN_SELCHANGE) {
        int sel = TabCtrl_GetCurSel(m_hTabBottom);
        SwitchBottomTab(sel);
        return;
    }
    if (nm->idFrom == IDC_LIST_LOG) {
        if (nm->code == LVN_GETDISPINFOW) {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
            int vIdx = di->item.iItem;
            if (vIdx < 0 || vIdx >= (int)m_logVisible.size()) return;
            int logIdx = m_logVisible[vIdx];
            if (logIdx < 0 || logIdx >= (int)m_log.size()) return;
            const LogEntry& le = m_log[logIdx];

            static wchar_t buf[1024]; // static buffer for text callbacks
            static const wchar_t* typeNames[] = {
                L"Connected", L"Disconnected", L"Sent", L"Received", L"Error", L"Info"
            };

            if (di->item.mask & LVIF_TEXT) {
                switch (di->item.iSubItem) {
                case 0: swprintf_s(buf, L"%zu", m_logBase + (size_t)logIdx + 1); break;
                case 1: wcsncpy_s(buf, le.timestamp.c_str(), _TRUNCATE); break;
                case 2: { int t = (int)le.type; wcsncpy_s(buf, (t >= 0 && t < 6) ? typeNames[t] : L"?", _TRUNCATE); break; }
                case 3: if (le.byteCount > 0) swprintf_s(buf, L"%zu", le.byteCount); else buf[0] = 0; break;
                case 4: { double sms = le.stats.SendDurationMs(); if (sms >= 0) swprintf_s(buf, L"%.1f", sms); else buf[0] = 0; break; }
                case 5: { if (le.latencyMs >= 0) swprintf_s(buf, L"%.1f", le.latencyMs); else buf[0] = 0; break; }
                case 6: wcsncpy_s(buf, 1024, le.preview.c_str(), _TRUNCATE); break;
                default: buf[0] = 0; break;
                }
                di->item.pszText = buf;
            }
            if (di->item.mask & LVIF_IMAGE) {
                di->item.iImage = (int)le.type;
            }
            return;
        }
        if (nm->code == NM_DBLCLK) {
            NMITEMACTIVATE* nma = (NMITEMACTIVATE*)nm;
            if (nma->iItem >= 0 && nma->iItem < (int)m_logVisible.size()) {
                DoReloadLogEntry(m_logVisible[nma->iItem]);
            }
        }
    }
}

// ============================================================
//  Context menu for log
// ============================================================

void MainWindow::OnContextMenuLog(POINT pt)
{
    int sel = ListView_GetNextItem(m_hListLog, -1, LVNI_SELECTED);
    int origIdx = -1;
    if (sel >= 0 && sel < (int)m_logVisible.size())
        origIdx = m_logVisible[sel];

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | (origIdx >= 0 ? 0 : MF_GRAYED), IDM_LOG_RELOAD, L"Reload to Send/Receive");
    AppendMenuW(hMenu, MF_STRING | (origIdx >= 0 ? 0 : MF_GRAYED), IDM_LOG_COPY,   L"Copy Preview");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_LOG_CLEAR, L"Clear Log");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == IDM_LOG_RELOAD && origIdx >= 0) DoReloadLogEntry(origIdx);
    if (cmd == IDM_LOG_COPY   && origIdx >= 0) Utils::CopyToClipboard(m_hWnd, m_log[origIdx].preview);
    if (cmd == IDM_LOG_CLEAR) ClearLog();
}

// ============================================================
//  DoReloadLogEntry
// ============================================================

void MainWindow::DoReloadLogEntry(int idx)
{
    if (idx < 0 || idx >= (int)m_log.size()) return;
    const LogEntry le = m_log[idx];   // copy: reloading must not depend on the log staying put

    switch (le.type)
    {
    case LogType::Sent:
    {
        LoadPayloadIntoEditor(le.data, L"");
        SetFocus(m_hEditSend);
        break;
    }
    case LogType::Received:
    {
        m_recvData = le.data;
        m_recvTextStale = m_recvHexStale = true;
        RefreshRecvView();
        break;
    }
    default:
        break;
    }

    // Flash stats if available
    if (le.stats.valid) UpdateStats(le.stats);
}

// ============================================================
//  CreateTooltips
// ============================================================

void MainWindow::CreateTooltips()
{
    m_hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_hWnd, nullptr, m_hInst, nullptr);
    if (!m_hTooltip) return;
    SendMessageW(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, Dpi::Scale(420));
    SendMessageW(m_hTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    SendMessageW(m_hTooltip, TTM_SETDELAYTIME, TTDT_INITIAL,  1200);
    SendMessageW(m_hTooltip, TTM_SETDELAYTIME, TTDT_RESHOW,    200);
    SendMessageW(m_hTooltip, TTM_ACTIVATE, FALSE, 0);  // start disabled

    auto tip = [&](HWND ctrl, const wchar_t* text) {
        if (!ctrl) return;
        TOOLINFOW ti = {};
        ti.cbSize   = sizeof(TOOLINFOW);
        ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd     = m_hWnd;
        ti.uId      = (UINT_PTR)ctrl;
        ti.lpszText = (LPWSTR)text;
        SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    };

    // ================================================================
    //  CONNECTION GROUP
    // ================================================================
    tip(m_hComboHost,
        L"Server address \u2014 IP or hostname of the remote server.\n\n"
        L"You can type a new address or pick one from the dropdown, "
        L"which stores your recent connections automatically.\n\n"
        L"Supported formats:\n"
        L"\u2022 IPv4 address: 192.168.1.10 (IPv6 is not supported)\n"
        L"\u2022 Hostname: myserver.local\n"
        L"\u2022 host:port shortcut: 192.168.1.10:9001\n"
        L"   (auto-fills both Host and Port fields)");

    tip(m_hEditPort,
        L"Server port number (1\u201365535).\n\n"
        L"Common ports:\n"
        L"\u2022 80 / 443 \u2014 HTTP / HTTPS\n"
        L"\u2022 23 \u2014 Telnet\n"
        L"\u2022 502 \u2014 Modbus TCP\n"
        L"\u2022 9001\u201365535 \u2014 typical custom services");

    tip(m_hEditTimeout,
        L"Connection timeout in seconds.\n\n"
        L"How long to wait for the TCP connection to be "
        L"established, and how long a single send may block "
        L"before it fails. (UDP has no handshake.)\n\n"
        L"\u2022 Whole seconds, 0\u20133600. Enter 0 to wait without limit "
        L"(Disconnect still cancels).\n"
        L"\u2022 Typical values: 3\u201310 seconds.\n"
        L"\u2022 Use a longer timeout for remote/VPN hosts.");

    tip(m_hComboProtocol,
        L"Network protocol to use for this connection.\n\n"
        L"TCP \u2014 Transmission Control Protocol.\n"
        L"Reliable, ordered, byte-stream delivery. "
        L"Best for most application protocols.\n\n"
        L"UDP \u2014 User Datagram Protocol.\n"
        L"Fast, connectionless datagrams. "
        L"No delivery guarantee or ordering. "
        L"Use for DNS, SNMP, Syslog, game traffic, etc.");

    tip(m_hBtnConnect,
        L"Open a connection to the server.\n\n"
        L"Uses the Host, Port, Protocol, and Timeout fields. "
        L"The connection is established in the background; "
        L"the status bar shows progress and result.\n\n"
        L"TIP: If \u2018Auto-Connect\u2019 is checked, you don\u2019t need "
        L"to click this \u2014 it connects when you send data.");

    tip(m_hBtnDisconnect,
        L"Close the current connection, or cancel a connection "
        L"attempt that is still in progress.\n\n"
        L"Closes the TCP connection (or discards the UDP socket) "
        L"and returns to the idle state.");

    tip(m_hChkKeepAlive,
        L"Enable TCP keep-alive probes (TCP only).\n\n"
        L"The OS sends probe packets on a connection that has been idle "
        L"for 10 seconds, so that a broken link (cable unplugged, "
        L"remote crash) is detected after roughly 20 seconds instead of "
        L"waiting for the next send to fail.\n\n"
        L"Recommended for long-lived connections.");

    tip(m_hChkAutoDisc,
        L"Auto-Disconnect after every send.\n\n"
        L"The connection is closed immediately after each payload "
        L"is transmitted; a response that has not arrived yet is lost. Useful for protocols that expect a fresh "
        L"connection per request (e.g. some HTTP/1.0 or industrial "
        L"protocols).\n\n"
        L"Can be combined with Auto-Connect for fully automatic "
        L"connect \u2192 send \u2192 disconnect cycles.");

    tip(m_hChkAutoConnect,
        L"Auto-Connect before sending.\n\n"
        L"When checked, clicking Send (or pressing Ctrl+Enter) "
        L"will automatically establish the connection first if "
        L"the client is not already connected. The data is "
        L"queued and transmitted as soon as the link is up.\n\n"
        L"Combine with Auto-Disconnect for one-shot send cycles.");

    tip(m_hStaticStatus,
        L"Connection status indicator.\n\n"
        L"Shows the current state of the connection:\n"
        L"\u2022 Disconnected \u2014 no active connection\n"
        L"\u2022 Connecting... \u2014 TCP handshake in progress\n"
        L"\u2022 Connected \u2014 link is active and ready");

    // ================================================================
    //  SEND GROUP
    // ================================================================
    tip(m_hEditSend,
        L"Send editor \u2014 type or paste the data to transmit.\n\n"
        L"In text mode, type plain text; it is sent as UTF-8. "
        L"With Hex checked, the editor holds hex pairs: "
        L"48 65 6C 6C 6F (= \"Hello\").\n\n"
        L"Keyboard shortcuts:\n"
        L"\u2022 Ctrl+Enter \u2014 Send immediately\n"
        L"\u2022 Ctrl+A \u2014 Select all text\n\n"
        L"You can also load binary data from a file.");

    tip(m_hBtnSend,
        L"Send the current editor contents to the server.\n\n"
        L"The payload is framed according to the Separator "
        L"setting (prefix/suffix are added automatically).\n\n"
        L"Shortcut: Ctrl+Enter");

    tip(m_hBtnLoadFile,
        L"Load a file from disk into the send editor.\n\n"
        L"The file (up to 64 MB) is read as raw bytes and sent exactly as it is on disk. "
        L"A text file is shown as text (or hex when Hex is checked). A binary "
        L"file up to 1 MB switches the editor to hex; a larger one is "
        L"represented by a one-line note.\n\n"
        L"Useful for sending binary protocols, captured packets, "
        L"or template request files.");

    tip(m_hBtnClearSend,
        L"Clear the send editor and any loaded data.");

    tip(m_hChkHexSend,
        L"Edit the data to send as hex.\n\n"
        L"When checked, the editor shows the data as hexadecimal "
        L"byte pairs (16 per line) that you can edit; what is sent is "
        L"the bytes those pairs describe. Spaces and line breaks are ignored.\n"
        L"Example: 48 65 6C 6C 6F = \"Hello\"\n\n"
        L"Switching converts the current data. Back to text, invalid hex "
        L"is refused with a message, and bytes that are not text are still "
        L"sent exactly as they are. Data over 1 MB cannot be edited as hex.");

    tip(m_hChkAutoClearSend,
        L"Auto-clear the send editor after each send.\n\n"
        L"When checked, the editor is emptied automatically "
        L"after every successful transmission so you can "
        L"immediately type a new message.");

    tip(m_hComboSep,
        L"Payload framing / delimiter.\n\n"
        L"Wraps the data with a protocol-specific delimiter "
        L"before it is transmitted:\n\n"
        L"\u2022 None \u2014 raw data, no extra bytes\n"
        L"\u2022 STX/ETX \u2014 0x02 ... 0x03 (industrial/POS)\n"
        L"\u2022 CR LF \u2014 0x0D 0x0A (HTTP, SMTP, Telnet)\n"
        L"\u2022 LF \u2014 0x0A (Unix-style)\n"
        L"\u2022 CR \u2014 0x0D\n"
        L"\u2022 NUL \u2014 0x00 (null-terminated)\n"
        L"\u2022 Custom \u2014 your own prefix/suffix as hex");

    tip(m_hEditPreFix,
        L"Custom framing prefix (hex bytes).\n\n"
        L"These bytes are prepended before every payload. "
        L"Enter space-separated hex pairs, e.g. AA BB CC.\n\n"
        L"Only active when Separator is set to \"Custom\".");

    tip(m_hEditSufFix,
        L"Custom framing suffix (hex bytes).\n\n"
        L"These bytes are appended after every payload. "
        L"Enter space-separated hex pairs, e.g. 0D 0A.\n\n"
        L"Only active when Separator is set to \"Custom\".");

    // ================================================================
    //  CYCLIC SEND
    // ================================================================
    tip(m_hEditCyclicInterval,
        L"Cyclic send interval in seconds.\n\n"
        L"The current payload is sent repeatedly at this "
        L"interval until you stop it.\n\n"
        L"Minimum: 0.001 s (1 ms).\n"
        L"Examples: 1.0, 0.5, 0.1, 0.01.\n\n"
        L"WARNING: Very short intervals can flood the server.");

    tip(m_hBtnCyclicStart,
        L"Start / Stop cyclic (periodic) sending.\n\n"
        L"When started, the current send-editor payload is "
        L"transmitted repeatedly at the configured interval. "
        L"The counter shows how many sends have been executed.\n\n"
        L"Click again to stop the cycle.\n\n"
        L"The cycle ends by itself if the connection is lost or a send "
        L"fails. With Auto-Disconnect the connection is closed after the first "
        L"send, which ends the cycle.");

    tip(m_hStaticCyclicCount,
        L"Cyclic send counter.\n\n"
        L"Shows the number of payloads sent during the "
        L"current cyclic send session. Resets to zero "
        L"when a new cycle starts.");

    // ================================================================
    //  RECEIVE GROUP
    // ================================================================
    tip(m_hEditRecv,
        L"Receive display (read-only).\n\n"
        L"Incoming data from the server appears here in real time. "
        L"New data is appended as it arrives.\n\n"
        L"Switch between text and hex view with the Hex View "
        L"checkbox. Use Copy or Save to export the data.");

    tip(m_hBtnClearRecv,
        L"Clear the receive display and buffer.\n\n"
        L"Both the text view and the hex view are reset. "
        L"The log history is NOT affected.");

    tip(m_hBtnCopyRecv,
        L"Copy all received data to the clipboard.\n\n"
        L"Copies the entire contents of the receive display "
        L"(text or hex, whichever is active).");

    tip(m_hBtnSaveRecv,
        L"Save received data to a file.\n\n"
        L"Writes the raw bytes from the most recent send/receive "
        L"cycle to a file on disk. Only the last transmission is "
        L"kept \u2014 earlier data is overwritten when new data arrives.\n\n"
        L"Useful for capturing binary responses or saving server "
        L"output for offline analysis.");

    tip(m_hChkHexRecv,
        L"Toggle Hex View for the receive display.\n\n"
        L"When checked, incoming data is shown as hexadecimal "
        L"bytes with an ASCII sidebar. When unchecked, data is "
        L"displayed as text.\n\n"
        L"Both views show the same buffer; only the newest part "
        L"(up to 8 MB) of a long session is kept.");

    tip(m_hChkAutoClearRecv,
        L"Auto-clear the receive display on new data.\n\n"
        L"When checked, the display is cleared automatically "
        L"each time new data arrives, so you only see the latest "
        L"message. Useful for request/response testing.");

    // ================================================================
    //  BOTTOM PANEL
    // ================================================================
    tip(m_hTabBottom,
        L"Bottom panel tabs.\n\n"
        L"\u2022 Log \u2014 chronological history of all events "
        L"(connections, sends, receives, errors). "
        L"Right-click entries to reload or copy.\n\n"
        L"\u2022 Diagnostics \u2014 live socket information: buffer sizes, "
        L"Nagle state, keep-alive, MTU estimate.\n\n"
        L"\u2022 Statistics \u2014 timing and byte counters for the "
        L"current session.\n\n"
        L"Press F1 for a complete help guide.");

    tip(m_hBtnClearLog,
        L"Clear all log entries.\n\n"
        L"Removes every entry from the history log. "
        L"This cannot be undone.");

    tip(m_hComboLogFilter,
        L"Filter which log entries are displayed.\n\n"
        L"\u2022 All \u2014 show everything\n"
        L"\u2022 Data only \u2014 only sent/received payloads\n"
        L"\u2022 Connection \u2014 connect/disconnect events\n"
        L"\u2022 Errors \u2014 only error messages\n\n"
        L"The underlying log data is preserved; switching "
        L"filters does not delete entries.");

    tip(m_hBtnRefreshDiag,
        L"Refresh socket diagnostics.\n\n"
        L"Queries the active socket for live information:\n"
        L"\u2022 Send/receive buffer sizes (SO_SNDBUF/SO_RCVBUF)\n"
        L"\u2022 Nagle algorithm (TCP_NODELAY) state\n"
        L"\u2022 Keep-alive probe status\n"
        L"\u2022 Estimated path MTU (if available)\n\n"
        L"Only available while a connection is active.");

    tip(m_hBtnClearStats,
        L"Reset all statistics to zero.\n\n"
        L"Clears all timing values and byte counters:\n"
        L"\u2022 Connection time\n"
        L"\u2022 Send/Receive timestamps\n"
        L"\u2022 Bytes sent/received\n"
        L"\u2022 Latency and throughput\n"
        L"\u2022 Cumulative totals");

    tip(m_hListLog,
        L"Event log \u2014 chronological history of all activity.\n\n"
        L"Each row shows timestamp, direction, size, and preview.\n\n"
        L"Right-click an entry for options:\n"
        L"\u2022 Reload to Send/Receive\n"
        L"\u2022 Copy Preview\n"
        L"\u2022 Clear Log");

    tip(m_hBtnHelp,
        L"Toggle help tooltips on/off.\n\n"
        L"When active, hover over any control to see a detailed "
        L"description of its function.\n\n"
        L"Press F1 for the full help guide.");
}

} // namespace Client

// ---- Tools factory (see Tools.h) ---------------------------------
#include "Tools.h"
bool Tools::RegisterClientClass(HINSTANCE hInst) { return Client::MainWindow::RegisterClass(hInst); }
HWND Tools::CreateClient(HINSTANCE hInst, HWND parent, const std::wstring& host, int port, bool autoConnect)
{
    return Client::MainWindow::Create(hInst, parent, host, port, autoConnect);
}
