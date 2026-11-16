#include "pch.h"
#include "ServerMainWindow.h"
#include "ToolHost.h"
#include "Version.h"

namespace Server {

// ============================================================
//  Subclass proc for Send edit - intercepts Ctrl+Enter
// ============================================================
static LRESULT CALLBACK SendEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR /*idSubclass*/, DWORD_PTR /*refData*/)
{
    // The frame runs IsDialogMessage; make sure it hands Ctrl+Enter to the edit
    // instead of treating it as a dialog key.
    if (msg == WM_GETDLGCODE) {
        LRESULT code = DefSubclassProc(hWnd, msg, wParam, lParam);
        const MSG* m = (const MSG*)lParam;
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000))
            code |= DLGC_WANTMESSAGE;
        return code;
    }
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
//  Constants (layout numbers are 96-DPI units; Move() scales them)
// ============================================================
static constexpr int MARGIN    = 8;
static constexpr int ROW_H     = 24;
static constexpr int BTN_W     = 90;
static constexpr int GRP_TITLE = 18;

static constexpr size_t RECV_CAP        = 8u * 1024 * 1024;    // received bytes kept; newest half survives a trim
static constexpr size_t LOG_MAX_ENTRIES = 20000;
static constexpr size_t LOG_MAX_BYTES   = 64u * 1024 * 1024;   // payload bytes held by the log
static constexpr size_t FILE_MAX        = 64u * 1024 * 1024;   // Load File limit
static constexpr size_t HEX_EDIT_MAX    = 1u * 1024 * 1024;    // largest payload shown / edited as hex text
static constexpr size_t TEXT_SHOW_MAX   = 4u * 1024 * 1024;    // largest payload shown as text in the editor

// ============================================================
//  RegisterClass / Create
// ============================================================

bool ServerMainWindow::RegisterClass(HINSTANCE hInst)
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

HWND ServerMainWindow::Create(HINSTANCE hInst, HWND parent,
                               int defaultPort, bool autoStart, bool echoMode,
                               const std::wstring& logFile)
{
    // Pack startup params into a heap struct; WM_CREATE reads it
    struct Params { int port; bool autoStart; bool echoMode; std::wstring logFile; };
    auto* p = new Params{ defaultPort, autoStart, echoMode, logFile };

    HWND hWnd = CreateWindowExW(
        TOOL_PAGE_EXSTYLE, ClassName(), L"TCP Server",
        TOOL_PAGE_STYLE,
        0, 0, 1180, 820,
        parent, nullptr, hInst, p); // pass params via lpCreateParams

    if (!hWnd) { delete p; return nullptr; }
    return hWnd;
}

ServerMainWindow* ServerMainWindow::FromHwnd(HWND hWnd)
{
    return reinterpret_cast<ServerMainWindow*>(GetWindowLongPtrW(hWnd, 0));
}

// ============================================================
//  WndProc
// ============================================================

LRESULT CALLBACK ServerMainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ServerMainWindow* self = FromHwnd(hWnd);

    switch (msg)
    {
    case WM_CREATE:
    {
        auto* cs   = (CREATESTRUCT*)lParam;
        struct Params { int port; bool autoStart; bool echoMode; std::wstring logFile; };
        auto* p    = (Params*)cs->lpCreateParams;
        auto* mw   = new ServerMainWindow();
        mw->m_hWnd  = hWnd;
        mw->m_hInst = cs->hInstance;
        SetWindowLongPtrW(hWnd, 0, (LONG_PTR)mw);
        int port = p ? p->port : 9001;
        bool as  = p ? p->autoStart : false;
        bool em  = p ? p->echoMode  : false;
        std::wstring lf = p ? p->logFile : L"";
        delete p;
        mw->OnCreate(hWnd, cs->hInstance, port, as, em, lf);
        return 0;
    }
    case WM_DESTROY:
        if (self) { SetWindowLongPtrW(hWnd, 0, 0); self->OnDestroy(); delete self; }
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
                L"TCP/UDP Server v" TCPTOOLS_VERSION L"\n"
                TCPTOOLS_COPYRIGHT L"\n"
                L"\n"
                L"─── GETTING STARTED ───\n"
                L"\n"
                L"1. Enter the port to listen on at the top.\n"
                L"Select from recent ports in the dropdown, "
                L"or type a new port number.\n"
                L"2. Choose TCP or UDP and set the send timeout.\n"
                L"3. Click Start Server to begin listening (IPv4 and IPv6, all local addresses; the port is taken exclusively).\n"
                L"4. Wait for a client to connect (UDP: for a datagram to arrive).\n"
                L"5. Received data appears in the Receive panel.\n"
                L"6. Type a response in Send and click Send.\n"
                L"\n"
                L"─── KEYBOARD SHORTCUTS ───\n"
                L"\n"
                L"• Ctrl+Enter — Send data to the connected client\n"
                L"• F1 — This help screen\n"
                L"• Ctrl+Tab — Next tab, Ctrl+T — new tab, Ctrl+W — close tab\n"
                L"\n"
                L"─── FEATURES ───\n"
                L"\n"
                L"• Echo Mode — Automatically echoes received "
                L"data back to the client. Great for testing.\n"
                L"• Hex — Edit the data to send as hex byte pairs "
                L"instead of text.\n"
                L"• Hex View (Receive) — Show received data as a hex dump.\n"
                L"• Framing — Auto-wrap payloads with STX/ETX, "
                L"CR LF, LF, CR, NUL, or custom hex delimiters.\n"
                L"• Load/Save — Load files into the send editor "
                L"(sent byte-exact), save received data to disk.\n"
                L"• Port History — Dropdown remembers recent "
                L"listen ports across sessions.\n"
                L"• Waiting clients — TCP serves one client at a time. Further clients that connect "
                L"are parked in arrival order (up to 16; the Client line shows \"(n waiting)\") and are "
                L"served when the current client leaves. Data they send meanwhile is received then.\n"
                L"• IPv4 and IPv6 — The server listens on both; IPv6 clients are shown as [addr]:port, "
                L"IPv4 clients as addr:port.\n"
                L"• UDP — There is no connection: the \"client\" is the sender of "
                L"the last datagram, and Send goes to that sender.\n"
                L"\n"
                L"─── BOTTOM PANEL ───\n"
                L"\n"
                L"• Log — Event history (newest 20,000 entries). Right-click to reload "
                L"data or copy previews.\n"
                L"• Diagnostics — Live socket info (buffer sizes, Nagle, "
                L"MSS/MTU estimate).\n"
                L"• Statistics — Timing, byte counts, latency, "
                L"and throughput.\n"
                L"\n"
                L"─── TIPS ───\n"
                L"\n"
                L"• Click the [?] button to enable help tooltips.\n"
                L"• Command line: TcpTools.exe /tab:server /port:<n> /autostart "
                L"/echo /logfile:<path>\n"
                L"• Config stored in: %APPDATA%\\TcpTools\\\n",
                L"TCP/UDP Server — Help", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    case WM_NOTIFY:
        if (self) self->OnNotify((NMHDR*)lParam);
        return 0;
    case WM_CONTEXTMENU:
        if (self && (HWND)wParam == self->m_hListLog) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pt.x == -1 && pt.y == -1) {          // keyboard invocation
                RECT r; GetWindowRect(self->m_hListLog, &r);
                pt = { r.left + 16, r.top + 16 };
            }
            self->OnContextMenuLog(pt);
            return 0;
        }
        break;

    // Network messages
    case WM_SERVER_EVENTS:
        if (self) self->OnNetEvents(); return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
//  Control factory helpers
// ============================================================

HWND ServerMainWindow::MakeLabel(const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE|SS_NOPREFIX,
        x, y, w, h, m_hWnd, nullptr, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}
HWND ServerMainWindow::MakeEdit(int id, int x, int y, int w, int h, DWORD extra)
{
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|extra,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}
HWND ServerMainWindow::MakeButton(int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}
HWND ServerMainWindow::MakeCheck(int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}
HWND ServerMainWindow::MakeGroupBox(int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", text,
        WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
        x, y, w, h, m_hWnd, (HMENU)(UINT_PTR)id, m_hInst, nullptr);
    SendMessage(hw, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    return hw;
}

// ============================================================
//  OnCreate
// ============================================================

void ServerMainWindow::OnCreate(HWND hWnd, HINSTANCE hInst,
                                 int defaultPort, bool autoStart, bool echoMode,
                                 const std::wstring& logFile)
{
    m_hFont     = Dpi::CreateUIFont();
    m_hFontMono = Dpi::CreateMonoFont(13);

    // --- Server group ---
    m_hGrpServer    = MakeGroupBox(IDC_GRP_SERVER, L" Server ", 0,0,10,10);
    m_hLblPort      = MakeLabel(L"Port:", 0,0,35,ROW_H);
    m_hComboPort    = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_PORT, hInst, nullptr);
    SendMessage(m_hComboPort, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    m_hLblTimeout   = MakeLabel(L"Send timeout (s):", 0,0,100,ROW_H);
    m_hEditTimeout  = MakeEdit(IDC_EDIT_TIMEOUT, 0,0,10,10);
    m_hBtnStart     = MakeButton(IDC_BTN_STARTSERVER, L"Start Server", 0,0,10,10);
    m_hBtnStop      = MakeButton(IDC_BTN_STOPSERVER,  L"Stop Server",  0,0,10,10);
    m_hChkEcho      = MakeCheck(IDC_CHK_ECHO, L"Echo Mode", 0,0,10,10);
    m_hStaticStatus = MakeLabel(L"● Not listening", 0,0,10,10);
    m_hStaticClient = MakeLabel(L"No client", 0,0,10,10);
    // Ids so that the status lines can be found (and read) from outside.
    SetWindowLongPtrW(m_hStaticStatus, GWLP_ID, IDC_STATIC_CONNSTATUS);
    SetWindowLongPtrW(m_hStaticClient, GWLP_ID, IDC_STATIC_CLIENTINFO);
    for (HWND h : { m_hStaticStatus, m_hStaticClient })   // long text gets "...", and tooltips can see the mouse
        SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | SS_ENDELLIPSIS | SS_NOTIFY);

    m_hComboProtocol = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_PROTOCOL, hInst, nullptr);
    SendMessage(m_hComboProtocol, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    SendMessage(m_hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"TCP");
    SendMessage(m_hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"UDP");
    SendMessage(m_hComboProtocol, CB_SETCURSEL, 0, 0);

    // Load port history and populate combo
    m_portHistory = Utils::LoadHistory(L"TcpServer");
    PopulatePortCombo();

    wchar_t portBuf[16]; swprintf_s(portBuf, L"%d", defaultPort);
    SetWindowTextW(m_hComboPort, portBuf);
    SetWindowTextW(m_hEditTimeout, L"5");
    if (echoMode) Button_SetCheck(m_hChkEcho, BST_CHECKED);
    EnableWindow(m_hBtnStop, FALSE);

    // --- Send group ---
    m_hGrpSend  = MakeGroupBox(IDC_GRP_SEND, L" Send ", 0,0,10,10);
    m_hComboSep = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_SEPARATOR, hInst, nullptr);
    SendMessage(m_hComboSep, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    const wchar_t* sepNames[] = { L"None", L"STX / ETX", L"CR LF", L"LF", L"CR", L"NUL", L"Custom" };
    for (auto* n : sepNames) SendMessage(m_hComboSep, CB_ADDSTRING, 0, (LPARAM)n);
    SendMessage(m_hComboSep, CB_SETCURSEL, 0, 0);

    m_hEditPreFix = MakeEdit(IDC_EDIT_CUSTOMPRE, 0,0,10,10);
    m_hEditSufFix = MakeEdit(IDC_EDIT_CUSTOMSUF, 0,0,10,10);
    m_hLblPre     = MakeLabel(L"Prefix (hex):", 0,0,90,ROW_H);
    m_hLblSuf     = MakeLabel(L"Suffix (hex):", 0,0,90,ROW_H);
    ShowWindow(m_hEditPreFix, SW_HIDE); ShowWindow(m_hEditSufFix, SW_HIDE);
    ShowWindow(m_hLblPre, SW_HIDE);    ShowWindow(m_hLblSuf, SW_HIDE);

    m_hChkHexSend = MakeCheck(IDC_CHK_HEXVIEW, L"Hex", 0,0,10,10);
    m_hEditSend   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_EDIT_SEND, hInst, nullptr);
    SendMessage(m_hEditSend, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SetWindowSubclass(m_hEditSend, SendEditSubclassProc, 1, 0);
    SendMessage(m_hEditSend, EM_SETLIMITTEXT, 0, 0); // no character limit

    m_hBtnSend      = MakeButton(IDC_BTN_SEND,      L"Send",      0,0,10,10);
    m_hBtnLoadFile  = MakeButton(IDC_BTN_LOADFILE,  L"Load File", 0,0,10,10);
    m_hBtnClearSend = MakeButton(IDC_BTN_CLEARSEND, L"Clear",     0,0,10,10);
    m_hChkAutoClearSend = MakeCheck(IDC_CHK_AUTOCLEAR_SEND, L"Auto-clear after send", 0,0,10,10);
    EnableWindow(m_hBtnSend, FALSE);

    // --- Receive group ---
    m_hGrpRecv    = MakeGroupBox(IDC_GRP_RECEIVE, L" Receive ", 0,0,10,10);
    m_hChkHexRecv = MakeCheck(IDC_CHK_HEXVIEW_RECV, L"Hex View", 0,0,10,10);
    m_hEditRecv   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_EDIT_RECV, hInst, nullptr);
    SendMessage(m_hEditRecv, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SendMessage(m_hEditRecv, EM_SETLIMITTEXT, 0, 0); // no character limit
    m_hHexRecv = HexView::Create(hWnd, 0,0,10,10, IDC_STATIC_HEXRECV, hInst);
    ShowWindow(m_hHexRecv, SW_HIDE);
    m_hBtnClearRecv = MakeButton(IDC_BTN_CLEARRECV, L"Clear",    0,0,10,10);
    m_hBtnCopyRecv  = MakeButton(IDC_BTN_COPYRECV,  L"Copy",     0,0,10,10);
    m_hBtnSaveRecv  = MakeButton(IDC_BTN_SAVERECV,  L"Save to file",0,0,10,10);
    m_hChkAutoClearRecv = MakeCheck(IDC_CHK_AUTOCLEAR_RECV, L"Auto-clear on recv", 0,0,10,10);

    // --- Statistics (tab 2 content, initially hidden) ---
    auto mkStat = [&](HWND& hw, const wchar_t* init) {
        hw = MakeLabel(init, 0,0,10,10);
        ShowWindow(hw, SW_HIDE);
    };
    mkStat(m_hStatCon,       L"Client time: ---");
    mkStat(m_hStatSendStart, L"Send start: ---");
    mkStat(m_hStatSendEnd,   L"Send end: ---");
    mkStat(m_hStatRecvStart, L"Recv start: ---");
    mkStat(m_hStatRecvEnd,   L"Recv end: ---");
    mkStat(m_hStatBytesSent, L"Bytes sent: ---");
    mkStat(m_hStatBytesRecv, L"Bytes recv: ---");
    mkStat(m_hStatLatency,   L"Latency: ---");
    mkStat(m_hStatTotal,     L"Total time: ---");
    mkStat(m_hStatThrough,   L"Throughput: ---");
    mkStat(m_hStatTotalSent,  L"Total sent: 0 B");
    mkStat(m_hStatTotalRecv,  L"Total recv: 0 B");
    m_hBtnClearStats = MakeButton(IDC_BTN_CLEARSTATS, L"Clear", 0,0,10,10);
    ShowWindow(m_hBtnClearStats, SW_HIDE);

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
    m_hStaticDiagInfo = CreateWindowExW(0, L"STATIC", L"Start server to view diagnostics",
        WS_CHILD | SS_LEFT | SS_NOPREFIX,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_STATIC_DIAG_INFO, hInst, nullptr);
    SendMessage(m_hStaticDiagInfo, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    m_hBtnRefreshDiag = MakeButton(IDC_BTN_REFRESHDIAG, L"Refresh", 0,0,10,10);
    ShowWindow(m_hStaticDiagInfo, SW_HIDE);
    ShowWindow(m_hBtnRefreshDiag, SW_HIDE);

    // --- Log (tab 0 content) ---
    m_hListLog = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOSORTHEADER|LVS_OWNERDATA,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_LIST_LOG, hInst, nullptr);
    SendMessage(m_hListLog, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    ListView_SetExtendedListViewStyle(m_hListLog,
        LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);

    // Log icons (ImageList)
    {
        const int isz = Dpi::Scale(16);
        m_hImgListLog = ImageList_Create(isz, isz, ILC_COLOR32 | ILC_MASK, 10, 0);
        // Icons for each server LogType: Listening, ClientConn, ClientDisconn, Sent, Received, Error, Stopped, Info
        struct { COLORREF color; const wchar_t* glyph; } iconDefs[] = {
            { RGB(0,100,200), L"\x25CF" },  // Listening (blue circle)
            { RGB(0,160,0),   L"\x25CF" },  // ClientConn (green circle)
            { RGB(160,0,0),   L"\x25CF" },  // ClientDisconn (red circle)
            { RGB(0,100,200), L"\x25B2" },  // Sent (blue up triangle)
            { RGB(0,140,80),  L"\x25BC" },  // Received (teal down triangle)
            { RGB(200,0,0),   L"\x26A0" },  // Error (red warning)
            { RGB(160,80,0),  L"\x25A0" },  // Stopped (orange square)
            { RGB(128,128,128), L"\x2139" }, // Info (gray info)
        };
        HDC hdc = GetDC(hWnd);
        HFONT hIconFont = CreateFontW(Dpi::Scale(14), 0, 0, 0, FW_NORMAL, 0,0,0,
            DEFAULT_CHARSET, 0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI Symbol");
        for (auto& def : iconDefs) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, isz, isz);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            RECT r = {0,0,isz,isz};
            HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(mem, &r, bg);
            DeleteObject(bg);
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, def.color);
            HGDIOBJ oldFont = SelectObject(mem, hIconFont);
            DrawTextW(mem, def.glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(mem, oldFont);
            SelectObject(mem, oldBmp);
            DeleteDC(mem);
            ImageList_AddMasked(m_hImgListLog, bmp, GetSysColor(COLOR_WINDOW));
            DeleteObject(bmp);
        }
        DeleteObject(hIconFont);
        ReleaseDC(hWnd, hdc);
        ListView_SetImageList(m_hListLog, m_hImgListLog, LVSIL_SMALL);
    }

    LVCOLUMNW col = {}; col.mask = LVCF_TEXT|LVCF_WIDTH|LVCF_FMT; col.fmt = LVCFMT_LEFT;
    auto addCol = [&](const wchar_t* h, int w) { col.pszText=(LPWSTR)h; col.cx=Dpi::Scale(w); ListView_InsertColumn(m_hListLog,999,&col); };
    addCol(L"#",75); addCol(L"Time",80); addCol(L"Type",110); addCol(L"Bytes",60); addCol(L"Send ms",70); addCol(L"Lat ms",70); addCol(L"Preview",500);
    m_hBtnClearLog = MakeButton(IDC_BTN_CLEARLOG, L"Clear Log", 0,0,10,10);

    // Log filter combo
    m_hComboLogFilter = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_LOGFILTER, hInst, nullptr);
    SendMessage(m_hComboLogFilter, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    const wchar_t* filterNames[] = { L"All", L"Data Only", L"Connection", L"Errors" };
    for (auto* n : filterNames) SendMessage(m_hComboLogFilter, CB_ADDSTRING, 0, (LPARAM)n);
    SendMessage(m_hComboLogFilter, CB_SETCURSEL, 0, 0);

    // Help toggle button (top-right "?")
    m_hBtnHelp = CreateWindowExW(0, L"BUTTON", L"?",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_BTN_HELP, hInst, nullptr);
    SendMessage(m_hBtnHelp, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    // Initialize network object (pass notify window)
    m_net = std::make_unique<ServerNetwork>(hWnd, WM_SERVER_EVENTS, echoMode);
    if (!logFile.empty()) m_net->SetLogFile(logFile);

    // Initial layout
    RelayoutToClient();

    // Tooltips (start disabled)
    CreateTooltips();
    UpdateToolState();

    // Auto-start if requested (command-line test mode): TCP, 5 s send timeout
    if (autoStart) {
        int port = 0;
        if (!Utils::IsValidPort(std::to_wstring(defaultPort), port)) {
            LogEntry le;
            le.type      = LogType::Error;
            le.timestamp = Utils::NowTimestamp();
            le.preview   = L"Auto-start: invalid port " + std::to_wstring(defaultPort);
            AddLogEntry(std::move(le));
            ShowStatusNote(L"Auto-start: invalid port " + std::to_wstring(defaultPort));
        }
        else {
            StartServer(port, 5, false, echoMode);
        }
    }
}

// ============================================================
//  Layout
// ============================================================

void ServerMainWindow::OnSize(int w, int h) { LayoutControls(Dpi::Unscale(w), Dpi::Unscale(h)); }

void ServerMainWindow::RelayoutToClient()
{
    RECT rc; GetClientRect(m_hWnd, &rc);
    LayoutControls(Dpi::Unscale(rc.right), Dpi::Unscale(rc.bottom));
}

void ServerMainWindow::LayoutControls(int W, int H)
{
    if (!m_hComboPort) return;

    // A drop-down combo re-selects the first history entry that starts with its
    // text when it is resized; put the typed text back afterwards.
    std::wstring portText = Utils::GetWindowText(m_hComboPort);

    HDWP hdwp = BeginDeferWindowPos(60);
    auto Move = [&](HWND hw, int x, int y, int w, int h) {
        if (hw && hdwp)
            hdwp = DeferWindowPos(hdwp, hw, nullptr,
                                  Dpi::Scale(x), Dpi::Scale(y), Dpi::Scale(w), Dpi::Scale(h),
                                  SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    };

    int x = MARGIN, y = MARGIN;

    // ====== SERVER GROUP ======
    int srvH = GRP_TITLE + ROW_H + MARGIN + ROW_H + MARGIN;
    Move(m_hGrpServer, x, y, W - 2*MARGIN, srvH);
    int cx = x + MARGIN + 2, cy = y + GRP_TITLE;

    // Row 1: Port | Timeout | Protocol | Start | Stop | Status
    Move(m_hLblPort,    cx, cy, 35, ROW_H); cx += 37;
    Move(m_hComboPort, cx, cy, 80, 200); cx += 84;
    Move(m_hLblTimeout, cx, cy, 100, ROW_H); cx += 102;
    Move(m_hEditTimeout,cx, cy, 40, ROW_H); cx += 44;
    Move(m_hComboProtocol, cx, cy, 55, 200); cx += 59;
    Move(m_hBtnStart,   cx, cy, 100, ROW_H); cx += 104;
    Move(m_hBtnStop,    cx, cy, 100, ROW_H); cx += 108;
    int helpX = W - 2*MARGIN - 26;
    Move(m_hStaticStatus, cx, cy, std::max(60, helpX - 6 - cx), ROW_H);
    Move(m_hBtnHelp, helpX, cy, 26, ROW_H);

    cx = x + MARGIN + 2; cy += ROW_H + 4;
    Move(m_hChkEcho,      cx, cy, 100, ROW_H); cx += 108;
    Move(m_hStaticClient, cx, cy, std::max(60, W - 2*MARGIN - MARGIN - cx), ROW_H);

    y += srvH + MARGIN;

    // ====== SEND + RECEIVE ======
    int tabMin  = 250; // min height for bottom tab control
    int midH = H - y - (tabMin + MARGIN);
    if (midH < 120) midH = 120;

    int sendW = (int)((W - 3*MARGIN) * m_splitRatio);
    int recvW = W - 3*MARGIN - sendW;

    // -- Send group --
    Move(m_hGrpSend, x, y, sendW, midH);
    int sx = x + MARGIN + 2, sy = y + GRP_TITLE;
    int sendCtrlW = sendW - 2*(MARGIN+2);

    // Separator row
    int comboX = sx;
    Move(m_hComboSep,   comboX, sy, 120, 200); comboX += 124;
    Move(m_hChkHexSend, comboX, sy,  50, ROW_H); comboX += 54;
    Move(m_hLblPre, comboX, sy, 80, ROW_H);
    Move(m_hEditPreFix, comboX+82, sy, 80, ROW_H);
    Move(m_hLblSuf, comboX+170, sy, 80, ROW_H);
    Move(m_hEditSufFix, comboX+252, sy, 80, ROW_H);
    sy += ROW_H + 4;

    int sendAreaH = midH - GRP_TITLE - ROW_H - 4 - ROW_H - 6 - MARGIN;
    Move(m_hEditSend, sx, sy, sendCtrlW, sendAreaH);
    sy += sendAreaH + 4;

    Move(m_hBtnSend,      sx, sy, BTN_W, ROW_H); sx += BTN_W + 4;
    Move(m_hBtnLoadFile,  sx, sy, BTN_W, ROW_H); sx += BTN_W + 4;
    Move(m_hBtnClearSend, sx, sy,    60, ROW_H); sx += 64;
    Move(m_hChkAutoClearSend, sx, sy, 170, ROW_H);

    // -- Receive group --
    int rx = x + sendW + MARGIN;
    Move(m_hGrpRecv, rx, y, recvW, midH);
    int rsx = rx + MARGIN + 2, rsy = y + GRP_TITLE;
    int recvCtrlW = recvW - 2*(MARGIN+2);

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
    // AdjustRect works in pixels; convert its result back to layout units.
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
        // Row 3 - cumulative totals
        Move(m_hStatTotalSent, stx2, sty2, statW2, ROW_H); stx2 += statW2 + 2;
        Move(m_hStatTotalRecv, stx2, sty2, statW2, ROW_H);
    }

    if (hdwp) EndDeferWindowPos(hdwp);

    if (Utils::GetWindowText(m_hComboPort) != portText)
        SetWindowTextW(m_hComboPort, portText.c_str());

    RedrawWindow(m_hWnd, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// ============================================================
//  SwitchBottomTab
// ============================================================

void ServerMainWindow::SwitchBottomTab(int tab)
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

void ServerMainWindow::OnDestroy()
{
    // Joins the accept and send threads and closes the hand-off queue, so nothing
    // touches this object (or posts to the window) after this call.
    if (m_net) m_net->Shutdown();
    if (m_hImgListLog) ImageList_Destroy(m_hImgListLog);
    if (m_hTooltip) { DestroyWindow(m_hTooltip); m_hTooltip = nullptr; }
    if (m_hFont)     DeleteObject(m_hFont);
    if (m_hFontMono) DeleteObject(m_hFontMono);
}

// ============================================================
//  OnCommand
// ============================================================

void ServerMainWindow::OnCommand(WORD id, WORD notify, HWND /*hCtrl*/)
{
    switch (id)
    {
    case IDC_BTN_STARTSERVER: DoStartServer(); break;
    case IDC_BTN_STOPSERVER:  DoStopServer();  break;
    case IDC_BTN_SEND:        DoSend();        break;
    case IDC_BTN_LOADFILE:    DoLoadFile();    break;

    case IDC_BTN_CLEARSEND:
        SetSendEditText(L"");
        m_sendData.clear();
        break;

    case IDC_BTN_CLEARRECV:
        m_recvData.clear();
        m_lastRecvData.clear();
        ResetRecvTextState();
        SetWindowTextW(m_hEditRecv, L"");
        HexView::ClearData(m_hHexRecv);
        m_textStale = m_hexStale = false;
        break;

    case IDC_BTN_COPYRECV:
        if (!m_hexRecvMode)
            Utils::CopyToClipboard(m_hWnd, Utils::GetWindowText(m_hEditRecv));
        else
            Utils::CopyToClipboard(m_hWnd, Utils::BytesToHexText(m_recvData));
        break;

    case IDC_BTN_SAVERECV:
        DoSaveLastRecv();
        break;

    case IDC_BTN_CLEARSTATS:
        ClearStats();
        break;

    case IDC_BTN_CLEARLOG:
        m_log.clear();
        m_logBytes = 0;
        RefreshLogView();
        break;

    case IDC_CHK_HEXVIEW:
        DoToggleSendHex();
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
            RelayoutToClient();
        }
        break;

    case IDC_CHK_ECHO:
        if (m_net) m_net->SetEchoMode(Button_GetCheck(m_hChkEcho) == BST_CHECKED);
        break;

    case IDC_EDIT_SEND:
        // The user edited the text: the pinned raw bytes (file / log reload) no longer
        // match it. Programmatic SetWindowText is guarded by m_ignoreEditChange.
        if (notify == EN_CHANGE && !m_ignoreEditChange) m_sendData.clear();
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
    }
}

// ============================================================
//  Connection history helpers
// ============================================================

void ServerMainWindow::PopulatePortCombo()
{
    // CB_RESETCONTENT clears the edit text; keep what the user has typed.
    std::wstring text = Utils::GetWindowText(m_hComboPort);
    SendMessage(m_hComboPort, CB_RESETCONTENT, 0, 0);
    for (auto& e : m_portHistory)
        SendMessage(m_hComboPort, CB_ADDSTRING, 0, (LPARAM)e.c_str());
    SetWindowTextW(m_hComboPort, text.c_str());
}

// ============================================================
//  DoStartServer / DoStopServer
// ============================================================

static bool ParseTimeoutSec(const std::wstring& s, int& out)
{
    if (s.empty() || s.size() > 4) return false;
    for (wchar_t c : s) if (c < L'0' || c > L'9') return false;
    out = _wtoi(s.c_str());
    return out <= 3600;
}

bool ServerMainWindow::StartServer(int port, int timeoutSec, bool udp, bool echo)
{
    if (!m_net) m_net = std::make_unique<ServerNetwork>(m_hWnd, WM_SERVER_EVENTS);
    m_net->SetEchoMode(echo);
    m_udpActive = udp;
    m_port      = port;
    if (!m_net->StartListening(port, timeoutSec, udp)) {
        ShowStatusNote(L"The server is still shutting down - try again in a moment.");
        return false;
    }
    // Until WM_NET_LISTENING (or WM_NET_STOPPED after a failed start) arrives the
    // controls are locked so a second Start cannot race the first.
    m_starting = true;
    SetWindowTextW(m_hStaticStatus, L"◌ Starting...");
    SetServerUI(false, false);
    return true;
}

void ServerMainWindow::DoStartServer()
{
    if (m_listening || m_starting) return;

    // Validate port
    std::wstring portW = Utils::GetWindowText(m_hComboPort);
    int port = 0;
    if (!Utils::IsValidPort(portW, port)) {
        MessageBoxW(m_hWnd, L"Please enter a valid port number (1–65535).", L"Invalid port", MB_ICONWARNING);
        SetFocus(m_hComboPort);
        return;
    }

    int timeout = 5;
    if (!ParseTimeoutSec(Utils::GetWindowText(m_hEditTimeout), timeout)) {
        MessageBoxW(m_hWnd, L"The send timeout must be a whole number of seconds from 0 to 3600 (0 = no limit).",
                    L"Invalid timeout", MB_ICONWARNING);
        SetFocus(m_hEditTimeout);
        return;
    }

    bool udp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);

    // A busy port is reported by the start itself (log + status line), the same way
    // for a click and for /autostart.
    StartServer(port, timeout, udp, Button_GetCheck(m_hChkEcho) == BST_CHECKED);
}

void ServerMainWindow::DoStopServer()
{
    // Waits for the accept thread (it polls every 100-200 ms); WM_NET_STOPPED then
    // brings the UI back to idle.
    if (m_net) m_net->StopServer();
}

// ============================================================
//  DoSend
// ============================================================

void ServerMainWindow::DoSend()
{
    if (!m_clientConn) {
        MessageBoxW(m_hWnd,
            m_udpActive ? L"No UDP sender yet - Send goes to the sender of the last received datagram."
                        : L"No client connected.",
            L"Not connected", MB_ICONWARNING);
        return;
    }
    std::vector<BYTE> data;
    if (!GetSendData(data)) return;
    if (data.empty()) {
        MessageBoxW(m_hWnd, L"Nothing to send.", L"Empty data", MB_ICONWARNING);
        return;
    }
    NetConfig cfg;
    if (!BuildNetConfig(cfg)) return;

    const wchar_t* proto = m_udpActive ? L"UDP" : L"TCP";

    // The completion message carries this id back (= the log entry's number).
    const unsigned long long id = m_nextSeq;
    if (!m_net || !m_net->QueueSend(data, cfg, id)) {
        MessageBoxW(m_hWnd, L"The data could not be queued: the client is gone or too many sends are waiting.",
                    L"Not sent", MB_ICONWARNING);
        return;
    }

    LogEntry le;
    le.type      = LogType::Sent;
    le.timestamp = Utils::NowTimestamp();
    le.data      = data;
    le.byteCount = data.size();
    std::wstring prev;
    for (size_t i = 0; i < std::min((size_t)80, data.size()); ++i) {
        BYTE b = data[i];
        prev += (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
    }
    le.preview = std::wstring(L"[") + proto + L"] " + prev;

    // Reset last recv for new transmission
    m_lastRecvData.clear();

    AddLogEntry(std::move(le));

    // Auto-clear send window after sending
    if (Button_GetCheck(m_hChkAutoClearSend) == BST_CHECKED) {
        SetSendEditText(L"");
        m_sendData.clear();
    }
}

// ============================================================
//  Send editor: text / hex payload
// ============================================================

void ServerMainWindow::SetSendEditText(const std::wstring& text)
{
    m_ignoreEditChange = true;
    SetWindowTextW(m_hEditSend, text.c_str());
    m_ignoreEditChange = false;
}

// Parses hex pairs (whitespace ignored). Shows what is wrong and returns false on error.
bool ServerMainWindow::ParseHexField(const std::wstring& text, const wchar_t* what, std::vector<BYTE>& out)
{
    out.clear();
    int hi = -1;
    size_t digits = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        int v;
        if (c >= L'0' && c <= L'9')      v = c - L'0';
        else if (c >= L'a' && c <= L'f') v = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') v = c - L'A' + 10;
        else {
            wchar_t msg[200];
            swprintf_s(msg, L"%s: '%c' is not a hex digit (character %zu). Use pairs like 48 65 6C 6C 6F.",
                       what, c, i + 1);
            MessageBoxW(m_hWnd, msg, L"Invalid hex", MB_ICONWARNING);
            out.clear();
            return false;
        }
        ++digits;
        if (hi < 0) hi = v;
        else { out.push_back((BYTE)(hi * 16 + v)); hi = -1; }
    }
    if (hi >= 0) {
        wchar_t msg[200];
        swprintf_s(msg, L"%s: odd number of hex digits (%zu) - every byte needs two digits.", what, digits);
        MessageBoxW(m_hWnd, msg, L"Invalid hex", MB_ICONWARNING);
        out.clear();
        return false;
    }
    return true;
}

// Text mode: the pinned raw bytes if there are any, else the editor text as UTF-8.
// Hex mode: the editor parsed as hex.
bool ServerMainWindow::GetSendData(std::vector<BYTE>& out)
{
    if (m_hexSendMode)
        return ParseHexField(Utils::GetWindowText(m_hEditSend), L"Send data", out);
    if (!m_sendData.empty()) { out = m_sendData; return true; }
    std::string s = Utils::ToUTF8(Utils::GetWindowText(m_hEditSend));
    out.assign(s.begin(), s.end());
    return true;
}

void ServerMainWindow::DoToggleSendHex()
{
    const bool wantHex = Button_GetCheck(m_hChkHexSend) == BST_CHECKED;
    if (wantHex == m_hexSendMode) return;

    if (wantHex) {
        std::vector<BYTE> payload;
        GetSendData(payload);                       // text mode: cannot fail
        if (payload.size() > HEX_EDIT_MAX) {
            MessageBoxW(m_hWnd, L"This data is larger than 1 MB and cannot be edited as hex. "
                                L"It can still be sent as it is.", L"Hex", MB_ICONINFORMATION);
            Button_SetCheck(m_hChkHexSend, BST_UNCHECKED);
            return;
        }
        m_hexSendMode = true;
        SetSendEditText(Utils::BytesToHexText(payload));
    }
    else {
        std::vector<BYTE> bytes;
        if (!ParseHexField(Utils::GetWindowText(m_hEditSend), L"Send data", bytes)) {
            Button_SetCheck(m_hChkHexSend, BST_CHECKED);   // stay in hex mode
            return;
        }
        m_hexSendMode = false;
        std::wstring shown = Utils::BytesToDisplayText(bytes);
        std::string back = Utils::ToUTF8(shown);
        bool exact = Utils::LooksLikeText(bytes) && back.size() == bytes.size()
                  && (bytes.empty() || memcmp(back.data(), bytes.data(), bytes.size()) == 0);
        SetSendEditText(shown);
        // Bytes the text view cannot reproduce exactly stay pinned so Send still transmits them.
        if (exact) m_sendData.clear(); else m_sendData = bytes;
    }
}

// Pins `bytes` as the payload and shows them in the editor the best way the
// current mode allows. `note` names the source (file name) for the placeholder.
void ServerMainWindow::ShowSendBytes(const std::vector<BYTE>& bytes, const std::wstring& note)
{
    m_sendData = bytes;
    const bool text = Utils::LooksLikeText(bytes);

    if (m_hexSendMode) {
        if (bytes.size() <= HEX_EDIT_MAX) { SetSendEditText(Utils::BytesToHexText(bytes)); return; }
        m_hexSendMode = false;                      // too large to edit as hex
        Button_SetCheck(m_hChkHexSend, BST_UNCHECKED);
    }
    if (text && bytes.size() <= TEXT_SHOW_MAX) {
        SetSendEditText(Utils::BytesToDisplayText(bytes));
    }
    else if (!text && bytes.size() <= HEX_EDIT_MAX) {
        m_hexSendMode = true;
        Button_SetCheck(m_hChkHexSend, BST_CHECKED);
        SetSendEditText(Utils::BytesToHexText(bytes));
    }
    else {
        std::wstring n = L"[" + std::wstring(text ? L"Large text " : L"Binary ")
                       + (note.empty() ? L"data" : L"file " + note)
                       + L", " + std::to_wstring(bytes.size()) + L" bytes - sent exactly as loaded]";
        SetSendEditText(n);
    }
}

// ============================================================
//  DoLoadFile
// ============================================================

void ServerMainWindow::DoLoadFile()
{
    std::wstring path;
    if (!Utils::OpenFileDialog(m_hWnd, path)) return;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { MessageBoxW(m_hWnd, L"Cannot open file.", L"Error", MB_ICONERROR); return; }
    std::streamoff sz = f.tellg();
    if (sz < 0) { MessageBoxW(m_hWnd, L"Cannot read the file size.", L"Error", MB_ICONERROR); return; }
    if ((unsigned long long)sz > FILE_MAX) {
        MessageBoxW(m_hWnd, L"The file is larger than the 64 MB limit.", L"File too large", MB_ICONWARNING);
        return;
    }
    f.seekg(0);
    std::vector<BYTE> bytes((size_t)sz);
    if (sz > 0) f.read((char*)bytes.data(), sz);
    if (!f && sz > 0) { MessageBoxW(m_hWnd, L"The file could not be read completely.", L"Error", MB_ICONERROR); return; }
    f.close();

    ShowSendBytes(bytes, PathFindFileNameW(path.c_str()));
}

// ============================================================
//  DoSaveLastRecv
// ============================================================

void ServerMainWindow::DoSaveLastRecv()
{
    if (m_lastRecvData.empty()) {
        MessageBoxW(m_hWnd, L"No data received since the last Send or Clear to save.",
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
    ofn.lpstrTitle  = L"Save Received Data";
    if (!GetSaveFileNameW(&ofn)) return;

    std::ofstream out(buf, std::ios::binary);
    if (!out) { MessageBoxW(m_hWnd, L"Cannot write file.", L"Save error", MB_ICONERROR); return; }
    out.write(reinterpret_cast<const char*>(m_lastRecvData.data()), (std::streamsize)m_lastRecvData.size());
    out.close();

    wchar_t msg[MAX_PATH + 64];
    swprintf_s(msg, L"Saved %zu bytes to:\n%s", m_lastRecvData.size(), buf);
    MessageBoxW(m_hWnd, msg, L"Saved", MB_ICONINFORMATION);
}

// ============================================================
//  BuildNetConfig / GetSeparatorMode
// ============================================================

bool ServerMainWindow::BuildNetConfig(NetConfig& cfg)
{
    cfg = NetConfig();
    cfg.port       = m_port;
    cfg.separator  = GetSeparatorMode();
    cfg.udpMode    = m_udpActive;
    if (cfg.separator == SeparatorMode::Custom) {
        if (!ParseHexField(Utils::GetWindowText(m_hEditPreFix), L"Custom prefix", cfg.customPrefix)) {
            SetFocus(m_hEditPreFix); return false;
        }
        if (!ParseHexField(Utils::GetWindowText(m_hEditSufFix), L"Custom suffix", cfg.customSuffix)) {
            SetFocus(m_hEditSufFix); return false;
        }
    }
    return true;
}

SeparatorMode ServerMainWindow::GetSeparatorMode()
{
    int sel = (int)SendMessage(m_hComboSep, CB_GETCURSEL, 0, 0);
    return (sel < 0) ? SeparatorMode::None : (SeparatorMode)sel;
}

// ============================================================
//  Receive view
//
//  The text view and the hex view always hold the same buffer
//  (m_recvData); the checkbox only chooses which one is visible.
// ============================================================

// Only the visible view is kept up to date while data arrives; the hidden one is
// flagged stale and rebuilt here, when it is shown.
void ServerMainWindow::RefreshRecvView()
{
    if (m_hexRecvMode && m_hexStale) {
        HexView::SetData(m_hHexRecv, m_recvData);
        m_hexStale = false;
    }
    else if (!m_hexRecvMode && m_textStale) {
        RebuildTextView();
        m_textStale = false;
    }
    ShowWindow(m_hEditRecv, m_hexRecvMode ? SW_HIDE : SW_SHOW);
    ShowWindow(m_hHexRecv,  m_hexRecvMode ? SW_SHOW : SW_HIDE);
}

void ServerMainWindow::ResetRecvTextState()
{
    m_recvTail.clear();
    m_recvLastCR = false;
}

// Bytes at the end of `b` that start a UTF-8 sequence which is not complete yet.
static size_t IncompleteUtf8Tail(const std::vector<BYTE>& b)
{
    const size_t n = b.size();
    for (size_t k = 1; k <= 3 && k <= n; ++k) {
        BYTE c = b[n - k];
        if ((c & 0xC0) == 0x80) continue;               // continuation byte: look further back
        size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        return need > k ? k : 0;
    }
    return 0;
}

// Appends to the text view only. A UTF-8 character split across two chunks is held
// back until its rest arrives, and a CR LF split across chunks shows as one break.
void ServerMainWindow::AppendReceiveText(const std::vector<BYTE>& data, bool flush)
{
    std::vector<BYTE> bytes = std::move(m_recvTail);
    m_recvTail.clear();
    bytes.insert(bytes.end(), data.begin(), data.end());
    if (!flush) {
        size_t t = IncompleteUtf8Tail(bytes);
        if (t) {
            m_recvTail.assign(bytes.end() - t, bytes.end());
            bytes.resize(bytes.size() - t);
        }
    }
    size_t off = 0;
    if (m_recvLastCR && !bytes.empty() && bytes[0] == '\n') { off = 1; m_recvLastCR = false; }
    if (bytes.size() <= off) return;
    m_recvLastCR = (bytes.back() == '\r');

    std::wstring ws = Utils::BytesToDisplayText(bytes.data() + off, bytes.size() - off);
    int len = GetWindowTextLengthW(m_hEditRecv);
    // No repaint while the text changes, and no undo record: a read-only view has no
    // use for it, and it makes every append slower as the text grows.
    SendMessage(m_hEditRecv, WM_SETREDRAW, FALSE, 0);
    SendMessage(m_hEditRecv, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(m_hEditRecv, EM_REPLACESEL, FALSE, (LPARAM)ws.c_str());
    SendMessage(m_hEditRecv, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessage(m_hEditRecv, WM_SETREDRAW, TRUE, 0);
    SendMessage(m_hEditRecv, EM_SCROLLCARET, 0, 0);
    InvalidateRect(m_hEditRecv, nullptr, FALSE);
}

void ServerMainWindow::RebuildTextView()
{
    ResetRecvTextState();
    SetWindowTextW(m_hEditRecv, Utils::BytesToDisplayText(m_recvData).c_str());
    m_recvLastCR = !m_recvData.empty() && m_recvData.back() == '\r';
    int len = GetWindowTextLengthW(m_hEditRecv);
    SendMessage(m_hEditRecv, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(m_hEditRecv, EM_SCROLLCARET, 0, 0);
}

// Replaces the visible view with the whole of m_recvData; the other one is rebuilt when shown.
void ServerMainWindow::RebuildReceiveView()
{
    if (m_hexRecvMode) {
        HexView::SetData(m_hHexRecv, m_recvData);
        m_hexStale = false;  m_textStale = true;
    }
    else {
        RebuildTextView();
        m_textStale = false; m_hexStale = true;
    }
}

// Keeps the newest half of a buffer that outgrew RECV_CAP, starting on a UTF-8 boundary.
static bool TrimToNewestHalf(std::vector<BYTE>& v)
{
    if (v.size() <= RECV_CAP) return false;
    size_t cut = v.size() - RECV_CAP / 2;
    while (cut < v.size() && (v[cut] & 0xC0) == 0x80) ++cut;
    v.erase(v.begin(), v.begin() + cut);
    return true;
}

// ============================================================
//  RefreshDiagnostics - nerdy socket info
// ============================================================

void ServerMainWindow::RefreshDiagnostics()
{
    if (!m_net) {
        SetWindowTextW(m_hStaticDiagInfo, L"Server not started");
        return;
    }

    std::wstring info;

    std::wstring listenEp;
    m_net->WithListenSocket([&](SOCKET s) { listenEp = NetAddr::LocalName(s); });
    if (listenEp.empty()) {
        SetWindowTextW(m_hStaticDiagInfo, L"Server not listening — start the server for diagnostics");
        return;
    }
    info += L"Listen: " + listenEp + (m_udpActive ? L" (UDP)  " : L" (TCP)  ");
    info += L"Echo: ";
    info += m_net->GetEchoMode() ? L"ON  " : L"OFF  ";

    auto sockOpts = [&](SOCKET s, bool tcp) {
        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);
        wchar_t buf[256];
        swprintf_s(buf, L"SO_SNDBUF: %dB  SO_RCVBUF: %dB  ", sndbuf, rcvbuf);
        info += buf;
        if (!tcp) return;
        BOOL nodelay = FALSE; optLen = sizeof(BOOL);
        getsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);
        swprintf_s(buf, L"Nagle: %s  ", nodelay ? L"OFF" : L"ON");
        info += buf;
        int mss = 0; optLen = sizeof(int);
        if (getsockopt(s, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, &optLen) == 0 && mss > 0)
            swprintf_s(buf, L"MSS: %dB  MTU: ~%dB  Frame: ~%dB", mss, mss + 40, mss + 40 + 14);
        else
            swprintf_s(buf, L"Est. Frame: ~1514B (Ethernet)");
        info += buf;
    };

    if (m_udpActive) {
        info += m_clientConn ? L"Last sender: " + m_peerText : std::wstring(L"No datagram received yet");
        info += L"\r\n";
        m_net->WithListenSocket([&](SOCKET s) { sockOpts(s, false); });
    }
    else {
        bool haveClient = false;
        std::wstring clientEp;
        std::wstring opts;
        haveClient = m_net->WithClientSocket([&](SOCKET s) {
            clientEp = NetAddr::PeerName(s);
            std::wstring saved = std::move(info);
            info.clear();
            sockOpts(s, true);
            opts = std::move(info);
            info = std::move(saved);
        });
        if (haveClient) {
            info += L"Client: " + clientEp;
            if (m_waiting > 0) info += L"  (" + std::to_wstring(m_waiting) + L" waiting)";
            info += L"\r\n" + opts;
        }
        else {
            info += L"\r\nNo client — connect a client for full diagnostics";
        }
    }

    SetWindowTextW(m_hStaticDiagInfo, info.c_str());
}

// ============================================================
//  SetServerUI / tool state
// ============================================================

void ServerMainWindow::SetServerUI(bool listening, bool clientConnected)
{
    m_listening  = listening;
    m_clientConn = clientConnected;
    const bool busy = listening || m_starting;
    EnableWindow(m_hBtnStart,      !busy);
    EnableWindow(m_hBtnStop,        busy);
    EnableWindow(m_hComboPort,     !busy);
    EnableWindow(m_hEditTimeout,   !busy);
    EnableWindow(m_hComboProtocol, !busy);
    // Echo mode can always be toggled (SetEchoMode propagates to running network)
    EnableWindow(m_hChkEcho,       TRUE);
    EnableWindow(m_hBtnSend,        clientConnected);
    UpdateToolState();
}

void ServerMainWindow::SetPageTitle(const std::wstring& detail)
{
    std::wstring t = L"TCP Server v" TCPTOOLS_VERSION;
    if (!detail.empty()) t += L"  –  " + detail;     // the frame shows what follows the dash
    SetWindowTextW(m_hWnd, t.c_str());
}

void ServerMainWindow::UpdateToolState()
{
    ToolState st = TOOL_IDLE;
    std::wstring d;
    const std::wstring port = std::to_wstring(m_port);
    if (m_clientConn) {
        st = TOOL_ACTIVE;
        d  = L":" + port + L" ● " + m_peerText;
    }
    else if (m_listening || m_starting) {
        st = TOOL_WAITING;
        d  = std::wstring(m_listening ? L"Listening :" : L"Starting :") + port + (m_udpActive ? L" (UDP)" : L"");
    }
    SetPageTitle(d);
    NotifyToolState(m_hWnd, st);
}

void ServerMainWindow::ShowStatusNote(const std::wstring& text)
{
    SetWindowTextW(m_hStaticClient, text.c_str());
    InvalidateRect(m_hStaticClient, nullptr, TRUE);
}

// ============================================================
//  Network message handlers
// ============================================================

// Largest payload handled per wake-up; the queue re-posts itself for the rest, so
// clicks and paints get in between two slices.
static constexpr size_t DRAIN_SLICE_BYTES = 64u * 1024;

// The line under the status: the current client, and how many more wait behind it.
void ServerMainWindow::UpdateClientLabel()
{
    std::wstring t;
    if (m_clientConn) t = (m_udpActive ? L"Last sender: " : L"Client: ") + m_peerText;
    if (m_waiting > 0) {
        if (!t.empty()) t += L"  ";
        t += L"(" + std::to_wstring(m_waiting) + L" waiting)";
    }
    SetWindowTextW(m_hStaticClient, t.c_str());
    InvalidateRect(m_hStaticClient, nullptr, TRUE);
}

// One wake-up from the network threads: handles what has gathered, in order.
// Consecutive data chunks are shown with one append, the log list is updated once.
void ServerMainWindow::OnNetEvents()
{
    if (!m_net) return;
    std::vector<NetEvent> evs = m_net->Events().Drain(DRAIN_SLICE_BYTES);

    ++m_logBatch;
    std::vector<NetEvent*> run;
    auto flushRun = [&] { if (!run.empty()) { OnNetDataRun(run); run.clear(); } };
    for (NetEvent& ev : evs) {
        if (ev.kind == NetEvent::Kind::Data) { run.push_back(&ev); continue; }
        flushRun();
        switch (ev.kind) {
        case NetEvent::Kind::Listening:    OnNetListening(ev.value);   break;
        case NetEvent::Kind::Stopped:      OnNetStopped();             break;
        case NetEvent::Kind::Connected:    OnNetClientConnected(ev);   break;
        case NetEvent::Kind::Disconnected: OnNetClientDisconnected();  break;
        case NetEvent::Kind::Waiting:      OnNetWaiting(ev);           break;
        case NetEvent::Kind::SendComplete: OnNetSendComplete(ev);      break;
        case NetEvent::Kind::Error:        OnNetError(ev.text);        break;
        default: break;
        }
    }
    flushRun();
    if (--m_logBatch == 0) FlushLogView();
}

void ServerMainWindow::OnNetListening(int port)
{
    m_starting = false;
    m_port     = port;
    m_waiting  = 0;
    SetServerUI(true, false);

    // Save port to history
    {
        std::wstring entry = std::to_wstring(port);
        Utils::SaveHistory(L"TcpServer", entry, m_portHistory);
        SetWindowTextW(m_hComboPort, entry.c_str());
        PopulatePortCombo();
    }

    wchar_t buf[80];
    swprintf_s(buf, L"◌ Listening on port %d", port);
    SetWindowTextW(m_hStaticStatus, buf);
    SetWindowTextW(m_hStaticClient, m_udpActive ? L"No datagram received yet" : L"No client connected");
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    InvalidateRect(m_hStaticClient, nullptr, TRUE);

    LogEntry le;
    le.type      = LogType::Listening;
    le.timestamp = Utils::NowTimestamp();
    wchar_t leBuf[128];
    swprintf_s(leBuf, L"[%s] Listening on port %d (IPv4 and IPv6)", m_udpActive ? L"UDP" : L"TCP", port);
    le.preview   = leBuf;
    AddLogEntry(std::move(le));

    // Exhaustive: listen socket info
    if (m_net) {
        std::wstring ep;
        m_net->WithListenSocket([&](SOCKET s) { ep = NetAddr::LocalName(s); });
        if (!ep.empty()) {
            LogEntry li;
            li.type = LogType::Info;
            li.timestamp = Utils::NowTimestamp();
            li.preview = L"Listen endpoint: " + ep;
            AddLogEntry(std::move(li));
        }
    }

    RefreshDiagnostics();
}

void ServerMainWindow::OnNetStopped()
{
    const bool wasListening = m_listening;
    m_starting = false;
    m_waiting  = 0;
    m_peerText.clear();
    SetServerUI(false, false);
    SetWindowTextW(m_hStaticStatus, L"● Not listening");
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    // After a failed start the client line keeps showing the error.
    if (wasListening) {
        SetWindowTextW(m_hStaticClient, L"");
        InvalidateRect(m_hStaticClient, nullptr, TRUE);

        LogEntry le;
        le.type      = LogType::Stopped;
        le.timestamp = Utils::NowTimestamp();
        le.preview   = L"Server stopped";
        AddLogEntry(std::move(le));
    }

    RefreshDiagnostics();
}

void ServerMainWindow::OnNetClientConnected(const NetEvent& ev)
{
    const std::wstring& addr = ev.text;
    m_peerText = addr;
    m_waiting  = ev.value;

    SetServerUI(true, true);
    SetWindowTextW(m_hStaticStatus, m_udpActive ? L"● Listening (receiving)" : L"● Listening (client connected)");
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    UpdateClientLabel();

    {
        wchar_t sb[64];
        swprintf_s(sb, L"Client time: %s", Utils::NowTimestamp().c_str());
        SetWindowTextW(m_hStatCon, sb);
        InvalidateRect(m_hStatCon, nullptr, TRUE);
    }

    LogEntry le;
    le.type      = LogType::ClientConn;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = std::wstring(L"[") + (m_udpActive ? L"UDP] Datagrams from: " : L"TCP] Client connected: ") + addr
                 + (ev.fromWaiting ? L"  (was waiting, now served)" : L"");
    AddLogEntry(std::move(le));

    // Exhaustive: client socket info (TCP only; a UDP "client" has no socket)
    if (m_net && !m_udpActive) {
        m_net->WithClientSocket([&](SOCKET csock) {
            int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
            getsockopt(csock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
            optLen = sizeof(int);
            getsockopt(csock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);
            BOOL nodelay = FALSE; optLen = sizeof(BOOL);
            getsockopt(csock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);

            wchar_t infoBuf[256];
            swprintf_s(infoBuf, L"Socket opts: SNDBUF=%dB RCVBUF=%dB TCP_NODELAY=%s",
                       sndbuf, rcvbuf, nodelay ? L"ON" : L"OFF");
            LogEntry li;
            li.type = LogType::Info;
            li.timestamp = Utils::NowTimestamp();
            li.preview = infoBuf;
            AddLogEntry(std::move(li));

            const std::wstring local = NetAddr::LocalName(csock);
            if (!local.empty()) {
                LogEntry li2;
                li2.type = LogType::Info;
                li2.timestamp = Utils::NowTimestamp();
                li2.preview = L"Server-side endpoint: " + local;
                AddLogEntry(std::move(li2));
            }
        });
    }

    RefreshDiagnostics();
}

// A further client connected while another one is served: it is parked.
void ServerMainWindow::OnNetWaiting(const NetEvent& ev)
{
    m_waiting = ev.value;
    UpdateClientLabel();

    LogEntry le;
    le.type      = LogType::Info;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = L"[TCP] Client waiting for its turn: " + ev.text + L"  (" + std::to_wstring(ev.value) + L" waiting)";
    AddLogEntry(std::move(le));
}

void ServerMainWindow::OnNetClientDisconnected()
{
    // A UTF-8 character still waiting for its rest will not get it now.
    if (!m_recvTail.empty() && !m_hexRecvMode) AppendReceiveText({}, true);
    m_recvTail.clear();
    m_recvLastCR = false;

    m_peerText.clear();
    SetServerUI(m_listening, false);
    if (m_listening) {
        SetWindowTextW(m_hStaticStatus, L"◌ Listening");
        if (m_udpActive)
            SetWindowTextW(m_hStaticClient, L"Sender cleared -- waiting for a datagram...");
        else if (m_waiting > 0)
            SetWindowTextW(m_hStaticClient, (L"Client disconnected -- next of " + std::to_wstring(m_waiting) + L" waiting is being served...").c_str());
        else
            SetWindowTextW(m_hStaticClient, L"Client disconnected -- waiting...");
    } else {
        SetWindowTextW(m_hStaticStatus, L"● Not listening");
        SetWindowTextW(m_hStaticClient, L"");
    }
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    InvalidateRect(m_hStaticClient, nullptr, TRUE);

    LogEntry le;
    le.type      = LogType::ClientDisconn;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = m_udpActive ? L"[UDP] Sender cleared"
                               : L"[TCP] Client disconnected (connection closed)";
    AddLogEntry(std::move(le));

    RefreshDiagnostics();
}

// Data chunks that arrived back to back: one update of the receive views, one of
// the totals and statistics, one log entry per chunk (the entry keeps its bytes
// for "Reload to Send/Receive").
void ServerMainWindow::OnNetDataRun(std::vector<NetEvent*>& run)
{
    size_t total = 0;
    for (NetEvent* ev : run) total += ev->data.size();

    m_totalBytesRecv += total;
    {
        wchar_t buf[80];
        swprintf_s(buf, L"Total recv: %s", Utils::FormatBytes(m_totalBytesRecv).c_str());
        SetWindowTextW(m_hStatTotalRecv, buf);
    }

    // Auto-clear receive window before appending new data
    if (Button_GetCheck(m_hChkAutoClearRecv) == BST_CHECKED) {
        m_recvData.clear();
        m_lastRecvData.clear();
        ResetRecvTextState();
        SetWindowTextW(m_hEditRecv, L"");
        HexView::ClearData(m_hHexRecv);
        m_textStale = m_hexStale = false;
    }

    std::vector<BYTE> joined;
    if (run.size() == 1) joined = run[0]->data;
    else {
        joined.reserve(total);
        for (NetEvent* ev : run) joined.insert(joined.end(), ev->data.begin(), ev->data.end());
    }

    m_recvData.insert(m_recvData.end(), joined.begin(), joined.end());
    m_lastRecvData.insert(m_lastRecvData.end(), joined.begin(), joined.end());
    TrimToNewestHalf(m_lastRecvData);
    if (TrimToNewestHalf(m_recvData)) {
        RebuildReceiveView();
    }
    else if (m_hexRecvMode) {
        HexView::AppendData(m_hHexRecv, joined.data(), joined.size());
        m_textStale = true;                 // the text view is hidden: rebuilt when it is shown
    }
    else {
        AppendReceiveText(joined);
        m_hexStale = true;                  // the hex view is hidden: rebuilt when it is shown
    }

    UpdateStats(run.back()->stats);

    for (NetEvent* ev : run) {
        const size_t len = ev->data.size();
        // Latency of the first reply after a manual Send.
        const CycleStats& st = ev->stats;
        double latency = -1;
        if (st.valid && st.bytesReceived == len && st.sendEnd.time_since_epoch().count() != 0
            && st.recvStart > st.sendEnd)
            latency = st.SendToFirstRecvMs();

        LogEntry le;
        le.type      = LogType::Received;
        le.timestamp = Utils::NowTimestamp();
        le.data      = std::move(ev->data);
        le.byteCount = len;
        le.latencyMs = latency;
        std::wstring prev;
        for (size_t i = 0; i < std::min(len, (size_t)80); ++i) {
            BYTE b = le.data[i];
            prev += (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
        }
        le.preview = std::wstring(L"[") + (m_udpActive ? L"UDP" : L"TCP") + L"] " + prev;
        AddLogEntry(std::move(le));
    }
}

void ServerMainWindow::OnNetSendComplete(const NetEvent& ev)
{
    const CycleStats& s = ev.stats;
    const size_t bytesSent = ev.bytes;

    m_totalBytesSent += bytesSent;
    {
        wchar_t buf[80];
        swprintf_s(buf, L"Total sent: %s", Utils::FormatBytes(m_totalBytesSent).c_str());
        SetWindowTextW(m_hStatTotalSent, buf);
    }

    if (s.isEcho) {
        LogEntry le;
        le.type      = LogType::Sent;
        le.timestamp = Utils::NowTimestamp();
        le.byteCount = bytesSent;
        le.stats     = s;
        wchar_t buf[96];
        swprintf_s(buf, s.ok ? L"[%s] Echoed %zu bytes" : L"[%s] Echo FAILED after %zu bytes",
                   m_udpActive ? L"UDP" : L"TCP", bytesSent);
        le.preview = buf;
        AddLogEntry(std::move(le));
    }
    else {
        int vis = -1;
        if (LogEntry* e = FindLogBySeq(s.id, &vis)) {
            e->stats = s;
            if (!s.ok) e->preview += L"  (NOT SENT)";
            // Inside a batch the list may not know this item yet; it is redrawn anyway.
            if (vis >= 0 && vis < ListView_GetItemCount(m_hListLog)) ListView_RedrawItems(m_hListLog, vis, vis);
        }
    }

    UpdateStats(s);
}

void ServerMainWindow::OnNetError(const std::wstring& msg)
{
    // Raised by the network threads, possibly while another tab is in front: log
    // it and show it on the status line instead of a modal box.
    ShowStatusNote(L"Error: " + msg);

    LogEntry le;
    le.type      = LogType::Error;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = msg;
    AddLogEntry(std::move(le));
}

// ============================================================
//  UpdateStats / ClearStats
// ============================================================

void ServerMainWindow::UpdateStats(const CycleStats& s)
{
    if (!s.valid) return;
    auto set = [](HWND hw, const wchar_t* lbl, double ms) {
        wchar_t buf[80];
        if (ms >= 0) swprintf_s(buf, L"%s %s", lbl, Utils::FormatMs(ms).c_str());
        else         swprintf_s(buf, L"%s ---", lbl);
        SetWindowTextW(hw, buf);
    };
    const bool sent = s.sendStart.time_since_epoch().count() != 0 && s.sendEnd.time_since_epoch().count() != 0;
    const bool recv = s.recvStart.time_since_epoch().count() != 0;
    set(m_hStatSendStart, L"Send start:", sent ? s.ConnectToSendMs() : -1);
    set(m_hStatSendEnd,   L"Send end:",   sent ? s.SendDurationMs() : -1);
    set(m_hStatRecvStart, L"Recv start:", (sent && recv) ? s.SendToFirstRecvMs() : -1);
    set(m_hStatRecvEnd,   L"Recv end:",   recv ? s.RecvDurationMs() : -1);
    set(m_hStatLatency,   L"Latency:",    (sent && recv) ? s.SendToFirstRecvMs() : -1);
    set(m_hStatTotal,     L"Total time:", (sent && recv) ? s.TotalMs() : -1);

    wchar_t buf[80];
    swprintf_s(buf, L"Bytes sent: %s", Utils::FormatBytes(s.bytesSent).c_str());
    SetWindowTextW(m_hStatBytesSent, buf);
    swprintf_s(buf, L"Bytes recv: %s", Utils::FormatBytes(s.bytesReceived).c_str());
    SetWindowTextW(m_hStatBytesRecv, buf);

    double th = s.ThroughputKBps();
    if (th > 0) { swprintf_s(buf, L"Throughput: %.1f KB/s", th); SetWindowTextW(m_hStatThrough, buf); }
    else          SetWindowTextW(m_hStatThrough, L"Throughput: ---");
}

void ServerMainWindow::ClearStats()
{
    struct { HWND* hw; const wchar_t* txt; } items[] = {
        {&m_hStatCon,L"Client time: ---"},{&m_hStatSendStart,L"Send start: ---"},
        {&m_hStatSendEnd,L"Send end: ---"},{&m_hStatRecvStart,L"Recv start: ---"},
        {&m_hStatRecvEnd,L"Recv end: ---"},{&m_hStatBytesSent,L"Bytes sent: ---"},
        {&m_hStatBytesRecv,L"Bytes recv: ---"},{&m_hStatLatency,L"Latency: ---"},
        {&m_hStatTotal,L"Total time: ---"},{&m_hStatThrough,L"Throughput: ---"},
        {&m_hStatTotalSent,L"Total sent: 0 B"},{&m_hStatTotalRecv,L"Total recv: 0 B"},
    };
    for (auto& i : items) SetWindowTextW(*i.hw, i.txt);
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
    case 2: return type == LogType::Listening || type == LogType::ClientConn
                || type == LogType::ClientDisconn || type == LogType::Stopped
                || type == LogType::Info;
    case 3: return type == LogType::Error;
    default: return true;
    }
}

// Re-applies the filter to the whole log (vector only; the list view is touched by FlushLogView).
void ServerMainWindow::RebuildLogVisible()
{
    m_logVisible.clear();
    for (int i = 0; i < (int)m_log.size(); ++i) {
        if (PassesLogFilter(m_log[i].type, m_logFilter))
            m_logVisible.push_back(i);
    }
}

void ServerMainWindow::RefreshLogView()
{
    RebuildLogVisible();
    ListView_SetItemCountEx(m_hListLog, (int)m_logVisible.size(), 0);
    InvalidateRect(m_hListLog, nullptr, TRUE);
}

// Drops the oldest 10 % while the log is over its entry or payload-byte cap.
// Entries keep their own sequence number, so the "#" column stays consistent.
void ServerMainWindow::TrimLog()
{
    while ((m_log.size() > LOG_MAX_ENTRIES || m_logBytes > LOG_MAX_BYTES) && m_log.size() > 1) {
        size_t n = std::max<size_t>(m_log.size() / 10, 1);
        for (size_t i = 0; i < n; ++i) m_logBytes -= m_log[i].data.size();
        m_log.erase(m_log.begin(), m_log.begin() + n);
    }
    RebuildLogVisible();
    m_logRebuild = true;
}

// Entries are numbered consecutively and only ever removed from the front.
LogEntry* ServerMainWindow::FindLogBySeq(unsigned long long seq, int* visibleIdx)
{
    if (visibleIdx) *visibleIdx = -1;
    if (m_log.empty() || seq < m_log.front().seq || seq > m_log.back().seq) return nullptr;
    int idx = (int)(seq - m_log.front().seq);
    if (visibleIdx) {
        auto it = std::lower_bound(m_logVisible.begin(), m_logVisible.end(), idx);
        if (it != m_logVisible.end() && *it == idx) *visibleIdx = (int)(it - m_logVisible.begin());
    }
    return &m_log[idx];
}

// Adds to the log data and, outside a batch, to the list view. During a batch
// (one drained slice of network events) the view is updated once at the end.
void ServerMainWindow::AddLogEntry(LogEntry&& e)
{
    e.seq = m_nextSeq++;
    m_logBytes += e.data.size();
    m_log.push_back(std::move(e));
    ++m_logNotify;                     // counts every appended entry, also filtered-out ones

    if (m_log.size() > LOG_MAX_ENTRIES || m_logBytes > LOG_MAX_BYTES)
        TrimLog();
    else if (PassesLogFilter(m_log.back().type, m_logFilter))
        m_logVisible.push_back((int)m_log.size() - 1);

    m_logDirty = true;
    if (m_logBatch == 0) FlushLogView();
}

void ServerMainWindow::FlushLogView()
{
    if (m_logNotify > 0) { NotifyToolEvent(m_hWnd, m_logNotify); m_logNotify = 0; }
    if (!m_logDirty) return;
    m_logDirty = false;

    const int count = (int)m_logVisible.size();
    if (m_logRebuild) {
        m_logRebuild = false;
        ListView_SetItemCountEx(m_hListLog, count, 0);
        InvalidateRect(m_hListLog, nullptr, TRUE);
    }
    else {
        const int before = ListView_GetItemCount(m_hListLog);
        ListView_SetItemCountEx(m_hListLog, count, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        if (count > before) ListView_RedrawItems(m_hListLog, before, count - 1);
    }
    if (count > 0) ListView_EnsureVisible(m_hListLog, count - 1, FALSE);
}

// ============================================================
//  OnNotify / OnContextMenuLog / DoReloadLogEntry
// ============================================================

void ServerMainWindow::OnNotify(NMHDR* nm)
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

            static wchar_t buf[1024];
            static const wchar_t* typeNames[] = {
                L"Listening", L"Client Conn", L"Client Disc", L"Sent", L"Received", L"Error", L"Stopped", L"Info"
            };

            if (di->item.mask & LVIF_TEXT) {
                switch (di->item.iSubItem) {
                case 0: swprintf_s(buf, L"%llu", le.seq); break;
                case 1: wcsncpy_s(buf, le.timestamp.c_str(), _TRUNCATE); break;
                case 2: { int t = (int)le.type; wcsncpy_s(buf, (t >= 0 && t < 8) ? typeNames[t] : L"?", _TRUNCATE); break; }
                case 3: if (le.byteCount > 0) swprintf_s(buf, L"%zu", le.byteCount); else buf[0] = 0; break;
                case 4: { double sms = le.stats.SendDurationMs(); if (sms >= 0 && le.type == LogType::Sent) swprintf_s(buf, L"%.1f", sms); else buf[0] = 0; break; }
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

void ServerMainWindow::OnContextMenuLog(POINT pt)
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
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);
    // The log can change while the menu is open (network messages are still handled).
    if (origIdx >= (int)m_log.size()) origIdx = -1;
    if (cmd == IDM_LOG_RELOAD && origIdx >= 0) DoReloadLogEntry(origIdx);
    if (cmd == IDM_LOG_COPY   && origIdx >= 0) Utils::CopyToClipboard(m_hWnd, m_log[origIdx].preview);
    if (cmd == IDM_LOG_CLEAR) {
        m_log.clear();
        m_logBytes = 0;
        RefreshLogView();
    }
}

void ServerMainWindow::DoReloadLogEntry(int idx)
{
    if (idx < 0 || idx >= (int)m_log.size()) return;
    const LogEntry le = m_log[idx];     // copy: ShowSendBytes / rebuilds must not see it change
    switch (le.type)
    {
    case LogType::Sent:
        if (!le.data.empty()) {
            ShowSendBytes(le.data, L"");
            SetFocus(m_hEditSend);
        }
        break;
    case LogType::Received:
        m_recvData = le.data;
        RebuildReceiveView();
        break;
    default: break;
    }
    if (le.stats.valid) UpdateStats(le.stats);
}

// ============================================================
//  CreateTooltips
// ============================================================

void ServerMainWindow::CreateTooltips()
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
    SendMessageW(m_hTooltip, TTM_ACTIVATE, FALSE, 0);

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
    //  SERVER GROUP
    // ================================================================
    tip(m_hComboPort,
        L"Listen port number (1\u201365535).\n\n"
        L"The server will bind to this port and wait for "
        L"incoming client connections. Pick from the dropdown "
        L"for recent ports or type a new one.\n\n"
        L"Common ports:\n"
        L"\u2022 80 / 443 \u2014 HTTP / HTTPS\n"
        L"\u2022 23 \u2014 Telnet\n"
        L"\u2022 502 \u2014 Modbus TCP\n"
        L"\u2022 9001\u201365535 \u2014 typical custom services\n\n"
        L"NOTE: Ports below 1024 may require administrator "
        L"privileges. A port already in use will be rejected.");

    tip(m_hEditTimeout,
        L"Send timeout in seconds.\n\n"
        L"How long a send to the client may stall (for example a peer that stops reading) before it is aborted. Receiving is not time-limited.\n\n"
        L"• Whole seconds, 0–3600.\n"
        L"• 0 = no limit (default is 5).\n\n"
        L"Fixed while the server is running.");

    tip(m_hComboProtocol,
        L"Network protocol the server uses (IPv4 and IPv6).\n\n"
        L"TCP — Transmission Control Protocol.\n"
        L"Reliable, ordered, byte-stream delivery. "
        L"Server serves one client at a time; further clients (up to 16) are accepted and shown as waiting, and are served in arrival order when the current client leaves.\n\n"
        L"UDP — User Datagram Protocol.\n"
        L"Connectionless datagrams. Data from any sender is received; the sender of the last datagram is shown as the client and is where Send and Echo replies go.");

    tip(m_hBtnStart,
        L"Start the server.\n\n"
        L"Binds to the configured port (all local IPv4 and IPv6 addresses, one dual-stack socket) and begins listening. The status line shows “Listening on port ...” once the server is ready.\n\n"
        L"The port is taken exclusively: if another process is already using it, the error is shown in the log and on the status line and the server stays idle.");

    tip(m_hBtnStop,
        L"Stop the server.\n\n"
        L"Closes the listening socket, disconnects any connected client (UDP: forgets the sender), and returns to the idle state. The port is released and becomes available for other applications.");

    tip(m_hChkEcho,
        L"Echo mode.\n\n"
        L"When checked, the server automatically sends back "
        L"an exact copy of every message it receives from "
        L"the client.\n\n"
        L"Useful for testing client send/receive logic, "
        L"verifying data integrity round-trips, and "
        L"latency/throughput measurement.\n\n"
        L"You can still send custom data manually while "
        L"echo mode is active.");

    tip(m_hStaticStatus,
        L"Server status indicator.\n\n"
        L"Shows the current state:\n"
        L"• Not listening — not running\n"
        L"• Listening on port ... — waiting for a client / datagram\n"
        L"• Listening (client connected) — active session");

    tip(m_hStaticClient,
        L"Client information and messages.\n\n"
        L"Displays the address and port of the connected client (UDP: the sender of the last datagram; IPv6 as [addr]:port), or the last error. Empty when idle.\n\n"
        L"\"(n waiting)\" means n more TCP clients are connected and parked; the next one is served when the current client leaves.");

    // ================================================================
    //  SEND GROUP
    // ================================================================
    tip(m_hEditSend,
        L"Send editor — type or paste the data to send to the client.\n\n"
        L"Text mode: the text is sent as UTF-8. A loaded file or a reloaded log entry is sent byte-exact until you edit the text.\n"
        L"Hex mode (Hex checkbox): enter hex byte pairs, e.g. 48 65 6C 6C 6F (= \"Hello\"). Spaces and line breaks are ignored.\n\n"
        L"Keyboard shortcuts:\n"
        L"• Ctrl+Enter — Send immediately\n"
        L"• Ctrl+A — Select all text");

    tip(m_hBtnSend,
        L"Send the current editor contents to the client.\n\n"
        L"The payload is framed according to the Separator "
        L"setting (prefix/suffix added automatically). "
        L"A client must be connected for sending to work.\n\n"
        L"Shortcut: Ctrl+Enter");

    tip(m_hBtnLoadFile,
        L"Load a file from disk into the send editor (up to 64 MB).\n\n"
        L"The file is read as raw bytes and sent exactly as loaded. Text files are shown as text (or hex, if Hex is on). Binary files up to 1 MB switch to hex; larger binary files show a one-line note instead.\n\n"
        L"Useful for sending binary protocols or template response files.");

    tip(m_hBtnClearSend,
        L"Clear the send editor and any loaded data.");

    tip(m_hChkHexSend,
        L"Hex mode for the send editor.\n\n"
        L"When checked, the editor shows the data as editable hex byte pairs (16 per line) and Send transmits the bytes you enter. Example: 48 65 6C 6C 6F = \"Hello\"\n\n"
        L"Switching converts the content: text → hex shows the bytes that would be sent; hex → text decodes them (invalid hex is reported and nothing changes). Data that is not plain text stays pinned so it is still sent exactly. Hex editing is limited to 1 MB.");

    tip(m_hChkAutoClearSend,
        L"Auto-clear the send editor after each send.\n\n"
        L"When checked, the editor is emptied automatically "
        L"after every successful transmission.");

    tip(m_hComboSep,
        L"Payload framing / delimiter.\n\n"
        L"Wraps outgoing data with a protocol-specific delimiter "
        L"before transmission:\n\n"
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
    //  RECEIVE GROUP
    // ================================================================
    tip(m_hEditRecv,
        L"Receive display (read-only).\n\n"
        L"Data received from the connected client appears here "
        L"in real time. New data is appended as it arrives.\n\n"
        L"Switch between text and hex with the Hex View "
        L"checkbox. Use Copy or Save to export the data.");

    tip(m_hBtnClearRecv,
        L"Clear the receive display and internal buffer.\n\n"
        L"Both the text view and the hex view are reset. "
        L"The log history is NOT affected.");

    tip(m_hBtnCopyRecv,
        L"Copy all received data to the clipboard.\n\n"
        L"Copies the entire contents of the receive display "
        L"(text or hex, whichever is active).");

    tip(m_hBtnSaveRecv,
        L"Save received data to a file.\n\n"
        L"Writes the raw bytes received since your last Send (or since the last Clear) to a file on disk (newest ~4–8 MB if more arrived).\n\n"
        L"Useful for capturing binary payloads or saving client data for offline analysis.");

    tip(m_hChkHexRecv,
        L"Toggle Hex View for the receive display.\n\n"
        L"When checked, received data is shown as a hex dump with an ASCII sidebar. When unchecked, it is shown as text. Both views hold the same data (up to 8 MB; the oldest half is dropped beyond that).");

    tip(m_hChkAutoClearRecv,
        L"Auto-clear the receive display on new data.\n\n"
        L"When checked, the display is cleared automatically "
        L"each time new data arrives, so you only see the "
        L"latest message.");

    // ================================================================
    //  BOTTOM PANEL
    // ================================================================
    tip(m_hTabBottom,
        L"Bottom panel tabs.\n\n"
        L"• Log — chronological history of events (listen, connections, sends, receives, errors; newest 20,000 entries). Right-click entries to reload or copy.\n\n"
        L"• Diagnostics — live socket information: buffer sizes, Nagle state, MSS/MTU estimate.\n\n"
        L"• Statistics — timing and byte counters.\n\n"
        L"Press F1 for a complete help guide.");

    tip(m_hBtnClearLog,
        L"Clear all log entries.\n\n"
        L"Removes every entry from the history log. "
        L"This cannot be undone.");

    tip(m_hComboLogFilter,
        L"Filter which log entries are displayed.\n\n"
        L"\u2022 All \u2014 show everything\n"
        L"\u2022 Data only \u2014 only sent/received payloads\n"
        L"\u2022 Connection \u2014 listen/connect/disconnect events\n"
        L"\u2022 Errors \u2014 only error messages\n\n"
        L"The underlying log data is preserved; switching "
        L"filters does not delete entries.");

    tip(m_hBtnRefreshDiag,
        L"Refresh socket diagnostics.\n\n"
        L"Queries the active sockets for live information:\n"
        L"• Send/receive buffer sizes (SO_SNDBUF/SO_RCVBUF)\n"
        L"• Nagle algorithm (TCP_NODELAY) state\n"
        L"• MSS and an estimated MTU (TCP, if available)\n\n"
        L"Only available while the server is running.");

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

} // namespace Server

// ---- Tools factory (see Tools.h) ---------------------------------
#include "Tools.h"
bool Tools::RegisterServerClass(HINSTANCE hInst) { return Server::ServerMainWindow::RegisterClass(hInst); }
HWND Tools::CreateServer(HINSTANCE hInst, HWND parent, int port, bool autoStart, bool echoMode, const std::wstring& logFile)
{
    return Server::ServerMainWindow::Create(hInst, parent, port, autoStart, echoMode, logFile);
}
