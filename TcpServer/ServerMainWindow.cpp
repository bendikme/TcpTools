#include "pch.h"
#include "ServerMainWindow.h"
#include "../Version.h"

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
//  Constants
// ============================================================
static constexpr int MARGIN    = 8;
static constexpr int ROW_H     = 24;
static constexpr int BTN_W     = 90;
static constexpr int GRP_TITLE = 18;

static HFONT CreateUIFont()
{
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    return CreateFontIndirectW(&ncm.lfMessageFont);
}
static HFONT CreateMonoUI()
{
    LOGFONTW lf = {};
    lf.lfHeight = -13; lf.lfWeight = FW_NORMAL;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN; lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    HFONT f = CreateFontIndirectW(&lf);
    if (!f) { wcscpy_s(lf.lfFaceName, L"Courier New"); f = CreateFontIndirectW(&lf); }
    return f;
}

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
    wc.hIcon         = LoadIconW(hInst, L"IDI_APPICON");
    wc.hIconSm       = wc.hIcon;
    return RegisterClassExW(&wc) != 0;
}

HWND ServerMainWindow::Create(HINSTANCE hInst, int nCmdShow,
                               int defaultPort, bool autoStart, bool echoMode,
                               const std::wstring& logFile)
{
    // Pack startup params into a heap struct; WM_CREATE reads it
    struct Params { int port; bool autoStart; bool echoMode; std::wstring logFile; };
    auto* p = new Params{ defaultPort, autoStart, echoMode, logFile };

    HWND hWnd = CreateWindowExW(
        0, ClassName(), L"TCP Server v" TCPTOOLS_VERSION,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 820,
        nullptr, nullptr, hInst, p); // pass params via lpCreateParams

    if (!hWnd) { delete p; return nullptr; }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
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
        if (self) { self->OnDestroy(); delete self; SetWindowLongPtrW(hWnd, 0, 0); }
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (self && wParam != SIZE_MINIMIZED)
            self->OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO:
        if (self) self->OnGetMinMaxInfo((MINMAXINFO*)lParam);
        return 0;
    case WM_COMMAND:
        if (self) self->OnCommand(LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_F1) {
            MessageBoxW(hWnd,
                L"TCP/UDP Server v" TCPTOOLS_VERSION L"\n"
                TCPTOOLS_COPYRIGHT L"\n"
                L"\n"
                L"\u2500\u2500\u2500 GETTING STARTED \u2500\u2500\u2500\n"
                L"\n"
                L"1. Enter the port to listen on at the top.\n"
                L"Select from recent ports in the dropdown, "
                L"or type a new port number.\n"
                L"2. Choose TCP or UDP and set the timeout.\n"
                L"3. Click Start Server to begin listening.\n"
                L"4. Wait for a client to connect.\n"
                L"5. Received data appears in the Receive panel.\n"
                L"6. Type a response in Send and click Send.\n"
                L"\n"
                L"\u2500\u2500\u2500 KEYBOARD SHORTCUTS \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Ctrl+Enter \u2014 Send data to connected client\n"
                L"\u2022 F1 \u2014 This help screen\n"
                L"\n"
                L"\u2500\u2500\u2500 FEATURES \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Echo Mode \u2014 Automatically echoes received "
                L"data back to the client. Great for testing.\n"
                L"\u2022 Hex View \u2014 Send/receive data as hex bytes.\n"
                L"\u2022 Framing \u2014 Auto-wrap payloads with STX/ETX, "
                L"CR LF, LF, CR, NUL, or custom hex delimiters.\n"
                L"\u2022 Load/Save \u2014 Load files into send editor, "
                L"save received data to disk.\n"
                L"\u2022 Port History \u2014 Dropdown remembers recent "
                L"listen ports across sessions.\n"
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
                L"\u2022 Command-line: TcpServer.exe [port]\n"
                L"\u2022 Config stored in: %APPDATA%\\TcpTools\\\n",
                L"TCP/UDP Server \u2014 Help", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (self) self->OnNotify((NMHDR*)lParam);
        return 0;
    case WM_CONTEXTMENU:
        if (self && (HWND)wParam == self->m_hListLog) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            self->OnContextMenuLog(pt);
        }
        return 0;

    // Network messages
    case WM_NET_LISTENING:
        if (self) self->OnNetListening((int)wParam); return 0;
    case WM_NET_STOPPED:
        if (self) self->OnNetStopped(); return 0;
    case WM_NET_CONNECTED:
        if (self) self->OnNetClientConnected((std::wstring*)lParam); return 0;
    case WM_NET_DISCONNECTED:
        if (self) self->OnNetClientDisconnected(); return 0;
    case WM_NET_DATA_RECEIVED:
        if (self) self->OnNetDataReceived((size_t)wParam, (BYTE*)lParam); return 0;
    case WM_NET_SEND_COMPLETE:
        if (self) self->OnNetSendComplete((size_t)wParam, (CycleStats*)lParam); return 0;
    case WM_NET_ERROR:
        if (self) self->OnNetError((int)wParam, (std::wstring*)lParam); return 0;
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
    m_hFont     = CreateUIFont();
    m_hFontMono = CreateMonoUI();

    // --- Server group ---
    m_hGrpServer    = MakeGroupBox(IDC_GRP_SERVER, L" Server ", 0,0,10,10);
    m_hLblPort      = MakeLabel(L"Port:", 0,0,35,ROW_H);
    m_hComboPort    = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        0,0,10,200, hWnd, (HMENU)(UINT_PTR)IDC_COMBO_PORT, hInst, nullptr);
    SendMessage(m_hComboPort, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    m_hLblTimeout   = MakeLabel(L"Timeout (s):", 0,0,80,ROW_H);
    m_hEditTimeout  = MakeEdit(IDC_EDIT_TIMEOUT, 0,0,10,10);
    m_hBtnStart     = MakeButton(IDC_BTN_STARTSERVER, L"Start Server", 0,0,10,10);
    m_hBtnStop      = MakeButton(IDC_BTN_STOPSERVER,  L"Stop Server",  0,0,10,10);
    m_hChkEcho      = MakeCheck(IDC_CHK_ECHO, L"Echo Mode", 0,0,10,10);
    m_hStaticStatus = MakeLabel(L"\u25CF Not listening", 0,0,10,10);
    m_hStaticClient = MakeLabel(L"No client", 0,0,10,10);

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

    m_hChkHexSend = MakeCheck(IDC_CHK_HEXVIEW, L"Hex View", 0,0,10,10);
    m_hEditSend   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_EDIT_SEND, hInst, nullptr);
    SendMessage(m_hEditSend, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SetWindowSubclass(m_hEditSend, SendEditSubclassProc, 1, 0);
    SendMessage(m_hEditSend, EM_SETLIMITTEXT, 0, 0); // no character limit
    m_hHexSend = HexView::Create(hWnd, 0,0,10,10, IDC_STATIC_HEXVIEW, hInst);
    ShowWindow(m_hHexSend, SW_HIDE);

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
        m_hImgListLog = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 10, 0);
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
        HFONT hIconFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0,0,0,
            DEFAULT_CHARSET, 0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI Symbol");
        for (auto& def : iconDefs) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, 16, 16);
            SelectObject(mem, bmp);
            RECT r = {0,0,16,16};
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

    LVCOLUMNW col = {}; col.mask = LVCF_TEXT|LVCF_WIDTH|LVCF_FMT; col.fmt = LVCFMT_LEFT;
    auto addCol = [&](const wchar_t* h, int w) { col.pszText=(LPWSTR)h; col.cx=w; ListView_InsertColumn(m_hListLog,999,&col); };
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

    // Initialize network object (pass notify window)
    m_net = std::make_unique<ServerNetwork>(hWnd, echoMode);
    if (!logFile.empty()) m_net->SetLogFile(logFile);

    // Initial layout
    RECT rc; GetClientRect(hWnd, &rc);
    LayoutControls(rc.right, rc.bottom);

    // Help toggle button (top-right "?")
    m_hBtnHelp = CreateWindowExW(0, L"BUTTON", L"?",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_BTN_HELP, hInst, nullptr);
    SendMessage(m_hBtnHelp, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    // Tooltips (start disabled)
    CreateTooltips();

    // Auto-start if requested
    if (autoStart) {
        int port = _wtoi(Utils::GetWindowText(m_hComboPort).c_str());
        if (port <= 0) port = defaultPort;
        int timeout = ::GetDlgItemInt(hWnd, IDC_EDIT_TIMEOUT, nullptr, FALSE);
        if (timeout <= 0) timeout = 5;
        m_net->StartListening(port, timeout, false); // auto-start defaults to TCP
    }
}

// ============================================================
//  Layout
// ============================================================

void ServerMainWindow::OnSize(int w, int h) { LayoutControls(w, h); PositionHelpButton(); }

void ServerMainWindow::PositionHelpButton()
{
    // Positioned inside LayoutControls
}

void ServerMainWindow::OnGetMinMaxInfo(MINMAXINFO* mm)
{
    mm->ptMinTrackSize = { 900, 650 };
}

void ServerMainWindow::LayoutControls(int W, int H)
{
    if (!m_hComboPort) return;

    HDWP hdwp = BeginDeferWindowPos(54);
    auto Move = [&](HWND hw, int x, int y, int w, int h) {
        if (hw && hdwp)
            hdwp = DeferWindowPos(hdwp, hw, nullptr, x, y, w, h,
                                  SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    };

    int x = MARGIN, y = MARGIN;

    // ====== SERVER GROUP ======
    int srvH = GRP_TITLE + ROW_H + MARGIN + ROW_H + MARGIN;
    Move(m_hGrpServer, x, y, W - 2*MARGIN, srvH);
    int cx = x + MARGIN + 2, cy = y + GRP_TITLE;

    // Row 1: Port | Timeout | Start | Stop | Status
    Move(m_hLblPort,    cx, cy, 35, ROW_H); cx += 37;
    Move(m_hComboPort, cx, cy, 80, 200); cx += 84;
    Move(m_hLblTimeout, cx, cy, 80, ROW_H); cx += 82;
    Move(m_hEditTimeout,cx, cy, 40, ROW_H); cx += 44;
    Move(m_hComboProtocol, cx, cy, 55, 200); cx += 59;
    Move(m_hBtnStart,   cx, cy, 100, ROW_H); cx += 104;
    Move(m_hBtnStop,    cx, cy, 100, ROW_H); cx += 108;
    Move(m_hStaticStatus, cx, cy, 200, ROW_H);
    Move(m_hBtnHelp, W - 2*MARGIN - 26, cy, 26, ROW_H);

    cx = x + MARGIN + 2; cy += ROW_H + 4;
    Move(m_hChkEcho,      cx, cy, 100, ROW_H); cx += 108;
    Move(m_hStaticClient, cx, cy, 400, ROW_H);

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
    Move(m_hChkHexSend, comboX, sy,  90, ROW_H); comboX += 94;
    Move(m_hLblPre, comboX, sy, 80, ROW_H);
    Move(m_hEditPreFix, comboX+82, sy, 80, ROW_H);
    Move(m_hLblSuf, comboX+170, sy, 80, ROW_H);
    Move(m_hEditSufFix, comboX+252, sy, 80, ROW_H);
    sy += ROW_H + 4;

    int sendAreaH = midH - GRP_TITLE - ROW_H - 4 - ROW_H - 6 - MARGIN;
    Move(m_hEditSend, sx, sy, sendCtrlW, sendAreaH);
    Move(m_hHexSend,  sx, sy, sendCtrlW, sendAreaH);
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
    RECT tabRc = { 0, 0, W - 2*MARGIN, tabH };
    TabCtrl_AdjustRect(m_hTabBottom, FALSE, &tabRc);
    int tx = x + tabRc.left;
    int ty = y + tabRc.top;
    int tw = tabRc.right - tabRc.left;
    int th = tabRc.bottom - tabRc.top;

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
    if (m_net) m_net->StopServer();
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
        SetWindowTextW(m_hEditSend, L"");
        HexView::ClearData(m_hHexSend);
        m_sendData.clear();
        break;

    case IDC_BTN_CLEARRECV:
        SetWindowTextW(m_hEditRecv, L"");
        HexView::ClearData(m_hHexRecv);
        m_recvData.clear();
        m_lastRecvData.clear();
        break;

    case IDC_BTN_COPYRECV:
        if (!m_hexRecvMode)
            Utils::CopyToClipboard(m_hWnd, Utils::GetWindowText(m_hEditRecv));
        else
            Utils::CopyToClipboard(m_hWnd, Utils::ToWide(Utils::BytesToHexStr(m_recvData)));
        break;

    case IDC_BTN_SAVERECV:
        DoSaveLastRecv();
        break;

    case IDC_BTN_CLEARSTATS:
        ClearStats();
        break;

    case IDC_BTN_CLEARLOG:
        m_log.clear();
        m_logVisible.clear();
        ListView_SetItemCountEx(m_hListLog, 0, 0);
        InvalidateRect(m_hListLog, nullptr, TRUE);
        break;

    case IDC_CHK_HEXVIEW:
        m_hexSendMode = Button_GetCheck(m_hChkHexSend) == BST_CHECKED;
        RefreshSendView();
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
            RECT rc; GetClientRect(m_hWnd, &rc);
            LayoutControls(rc.right, rc.bottom);
        }
        break;

    case IDC_CHK_ECHO:
        if (m_net) m_net->SetEchoMode(Button_GetCheck(m_hChkEcho) == BST_CHECKED);
        break;

    case IDC_EDIT_SEND:
        // Clear cached sendData when the user actually types so the hex view
        // re-syncs from the editor next time. Guard against programmatic SetWindowText.
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
    SendMessage(m_hComboPort, CB_RESETCONTENT, 0, 0);
    for (auto& e : m_portHistory)
        SendMessage(m_hComboPort, CB_ADDSTRING, 0, (LPARAM)e.c_str());
}

// ============================================================
//  DoStartServer / DoStopServer
// ============================================================

void ServerMainWindow::DoStartServer()
{
    if (m_listening) return;

    // Validate port
    std::wstring portW = Utils::GetWindowText(m_hComboPort);
    int port = 0;
    if (!Utils::IsValidPort(portW, port)) {
        MessageBoxW(m_hWnd, L"Please enter a valid port number (1\u201365535).", L"Invalid port", MB_ICONWARNING);
        SetFocus(m_hComboPort);
        return;
    }

    int timeout = ::GetDlgItemInt(m_hWnd, IDC_EDIT_TIMEOUT, nullptr, FALSE);
    if (timeout <= 0) timeout = 5;

    bool udp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);

    // Check if port is already in use
    if (!Utils::IsPortAvailable(port, udp)) {
        wchar_t msg[128];
        swprintf_s(msg, L"Port %d is already in use.\nPlease choose a different port.", port);
        MessageBoxW(m_hWnd, msg, L"Port busy", MB_ICONWARNING);
        SetFocus(m_hComboPort);
        return;
    }

    if (!m_net) m_net = std::make_unique<ServerNetwork>(m_hWnd);
    bool echo = Button_GetCheck(m_hChkEcho) == BST_CHECKED;
    m_net->SetEchoMode(echo);
    m_net->StartListening(port, timeout, udp);
}

void ServerMainWindow::DoStopServer()
{
    if (m_net) m_net->StopServer();
}

// ============================================================
//  DoSend
// ============================================================

void ServerMainWindow::DoSend()
{
    if (!m_clientConn) {
        MessageBoxW(m_hWnd, L"No client connected.", L"Not connected", MB_ICONWARNING);
        return;
    }
    std::vector<BYTE> data = GetSendData();
    if (data.empty()) {
        MessageBoxW(m_hWnd, L"Nothing to send.", L"Empty data", MB_ICONWARNING);
        return;
    }
    NetConfig cfg = BuildNetConfig();

    bool isUdp = cfg.udpMode;
    const wchar_t* proto = isUdp ? L"UDP" : L"TCP";

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

    std::thread([this, data, cfg]() mutable {
        if (m_net) m_net->Send(data, cfg);
    }).detach();

    AddLogEntry(std::move(le));

    // Auto-clear send window after sending
    if (Button_GetCheck(m_hChkAutoClearSend) == BST_CHECKED) {
        SetWindowTextW(m_hEditSend, L"");
        HexView::ClearData(m_hHexSend);
        m_sendData.clear();
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
    auto sz = f.tellg(); f.seekg(0);
    m_sendData.resize((size_t)sz);
    f.read((char*)m_sendData.data(), sz);
    f.close();

    std::string raw(m_sendData.begin(), m_sendData.end());
    HexView::SetData(m_hHexSend, m_sendData);
    m_ignoreEditChange = true;
    SetWindowTextW(m_hEditSend, Utils::ToWide(raw).c_str());
    m_ignoreEditChange = false;
    RefreshSendView();
}

// ============================================================
//  DoSaveLastRecv
// ============================================================

void ServerMainWindow::DoSaveLastRecv()
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

    std::ofstream out(buf, std::ios::binary);
    if (!out) { MessageBoxW(m_hWnd, L"Cannot write file.", L"Save error", MB_ICONERROR); return; }
    out.write(reinterpret_cast<const char*>(m_lastRecvData.data()), m_lastRecvData.size());
    out.close();

    wchar_t msg[MAX_PATH + 64];
    swprintf_s(msg, L"Saved %zu bytes to:\n%s", m_lastRecvData.size(), buf);
    MessageBoxW(m_hWnd, msg, L"Saved", MB_ICONINFORMATION);
}

// ============================================================
//  GetSendData / BuildNetConfig / GetSeparatorMode
// ============================================================

std::vector<BYTE> ServerMainWindow::GetSendData()
{
    if (m_hexSendMode) {
        std::wstring ws = Utils::GetWindowText(m_hEditSend);
        std::string s   = Utils::ToUTF8(ws);
        std::string clean;
        for (char c : s) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') clean += c;
        std::vector<BYTE> out;
        if (!Utils::HexStrToBytes(clean, out)) {
            MessageBoxW(m_hWnd, L"Invalid hex input.", L"Hex error", MB_ICONWARNING);
            return {};
        }
        return out;
    } else {
        // Always encode current editor content as UTF-8 so edits are reflected
        std::wstring ws = Utils::GetWindowText(m_hEditSend);
        std::string s   = Utils::ToUTF8(ws);
        return std::vector<BYTE>(s.begin(), s.end());
    }
}

NetConfig ServerMainWindow::BuildNetConfig()
{
    NetConfig cfg;
    cfg.port       = _wtoi(Utils::GetWindowText(m_hComboPort).c_str());
    cfg.timeoutSec = ::GetDlgItemInt(m_hWnd, IDC_EDIT_TIMEOUT, nullptr, FALSE);
    if (cfg.timeoutSec <= 0) cfg.timeoutSec = 5;
    cfg.separator  = GetSeparatorMode();
    cfg.udpMode    = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    if (cfg.separator == SeparatorMode::Custom) {
        std::string pre = Utils::ToUTF8(Utils::GetWindowText(m_hEditPreFix));
        std::string suf = Utils::ToUTF8(Utils::GetWindowText(m_hEditSufFix));
        Utils::HexStrToBytes(pre, cfg.customPrefix);
        Utils::HexStrToBytes(suf, cfg.customSuffix);
    }
    return cfg;
}

SeparatorMode ServerMainWindow::GetSeparatorMode()
{
    int sel = (int)SendMessage(m_hComboSep, CB_GETCURSEL, 0, 0);
    return (sel < 0) ? SeparatorMode::None : (SeparatorMode)sel;
}

// ============================================================
//  RefreshSendView / RefreshRecvView
// ============================================================

void ServerMainWindow::RefreshSendView()
{
    if (m_hexSendMode) {
        ShowWindow(m_hEditSend, SW_HIDE);
        ShowWindow(m_hHexSend,  SW_SHOW);
        if (m_sendData.empty()) {
            std::wstring ws = Utils::GetWindowText(m_hEditSend);
            std::string s   = Utils::ToUTF8(ws);
            m_sendData.assign(s.begin(), s.end());
        }
        HexView::SetData(m_hHexSend, m_sendData);
    } else {
        ShowWindow(m_hHexSend,  SW_HIDE);
        ShowWindow(m_hEditSend, SW_SHOW);
    }
}

void ServerMainWindow::RefreshRecvView()
{
    if (m_hexRecvMode) {
        ShowWindow(m_hEditRecv, SW_HIDE);
        ShowWindow(m_hHexRecv,  SW_SHOW);
        HexView::SetData(m_hHexRecv, m_recvData);
    } else {
        ShowWindow(m_hHexRecv,  SW_HIDE);
        ShowWindow(m_hEditRecv, SW_SHOW);
    }
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

    // Listen socket info
    SOCKET lsock = m_net->GetListenSocket();
    if (lsock != INVALID_SOCKET) {
        sockaddr_in localAddr = {};
        int localLen = sizeof(localAddr);
        if (getsockname(lsock, (sockaddr*)&localAddr, &localLen) == 0) {
            wchar_t ip[64]; DWORD ipLen = 64;
            WSAAddressToStringW((sockaddr*)&localAddr, localLen, nullptr, ip, &ipLen);
            info += L"Listen: " + std::wstring(ip) + L"  ";
        }
    }

    info += L"Echo: ";
    info += m_net->GetEchoMode() ? L"ON  " : L"OFF  ";

    SOCKET csock = m_net->GetClientSocket();
    if (csock != INVALID_SOCKET && m_clientConn) {
        // Remote endpoint
        sockaddr_in remoteAddr = {};
        int remoteLen = sizeof(remoteAddr);
        if (getpeername(csock, (sockaddr*)&remoteAddr, &remoteLen) == 0) {
            wchar_t ip[64]; DWORD ipLen = 64;
            WSAAddressToStringW((sockaddr*)&remoteAddr, remoteLen, nullptr, ip, &ipLen);
            info += L"Client: " + std::wstring(ip);
        }
        info += L"\r\n";

        // Socket options
        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(csock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(csock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);
        BOOL nodelay = FALSE; optLen = sizeof(BOOL);
        getsockopt(csock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);

        wchar_t buf[256];
        swprintf_s(buf, L"SO_SNDBUF: %dB  SO_RCVBUF: %dB  Nagle: %s  ",
                   sndbuf, rcvbuf, nodelay ? L"OFF" : L"ON");
        info += buf;

        int mss = 0; optLen = sizeof(int);
        if (getsockopt(csock, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, &optLen) == 0 && mss > 0) {
            swprintf_s(buf, L"MSS: %dB  MTU: ~%dB  Frame: ~%dB", mss, mss + 40, mss + 40 + 14);
        } else {
            swprintf_s(buf, L"Est. Frame: ~1514B (Ethernet)");
        }
        info += buf;
    } else {
        info += L"\r\nNo client \x2014 connect a client for full diagnostics";
    }

    SetWindowTextW(m_hStaticDiagInfo, info.c_str());
}

// ============================================================
//  SetServerUI
// ============================================================

void ServerMainWindow::SetServerUI(bool listening, bool clientConnected)
{
    m_listening  = listening;
    m_clientConn = clientConnected;
    EnableWindow(m_hBtnStart,      !listening);
    EnableWindow(m_hBtnStop,        listening);
    EnableWindow(m_hComboPort,     !listening);
    EnableWindow(m_hEditTimeout,   !listening);
    EnableWindow(m_hComboProtocol, !listening);
    // Echo mode can always be toggled (SetEchoMode propagates to running network)
    EnableWindow(m_hChkEcho,       TRUE);
    EnableWindow(m_hBtnSend,        clientConnected);
}

// ============================================================
//  Network message handlers
// ============================================================

void ServerMainWindow::OnNetListening(int port)
{
    SetServerUI(true, false);

    // Save port to history
    {
        std::wstring entry = std::to_wstring(port);
        Utils::SaveHistory(L"TcpServer", entry, m_portHistory);
        PopulatePortCombo();
        SetWindowTextW(m_hComboPort, entry.c_str());
    }

    wchar_t buf[80];
    swprintf_s(buf, L"\u25CC Listening on port %d", port);
    SetWindowTextW(m_hStaticStatus, buf);
    SetWindowTextW(m_hStaticClient, L"No client connected");
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    InvalidateRect(m_hStaticClient, nullptr, TRUE);

    // Title bar
    wchar_t titleBuf[128];
    swprintf_s(titleBuf, L"TCP Server v" TCPTOOLS_VERSION L"  \u2013  Listening :%d", port);
    SetWindowTextW(m_hWnd, titleBuf);

    LogEntry le;
    le.type      = LogType::Listening;
    le.timestamp = Utils::NowTimestamp();
    bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    wchar_t leBuf[128];
    swprintf_s(leBuf, L"[%s] Listening on port %d", isUdp ? L"UDP" : L"TCP", port);
    le.preview   = leBuf;
    AddLogEntry(std::move(le));

    // Exhaustive: listen socket info
    if (m_net) {
        SOCKET lsock = m_net->GetListenSocket();
        if (lsock != INVALID_SOCKET) {
            sockaddr_in localAddr = {};
            int localLen = sizeof(localAddr);
            if (getsockname(lsock, (sockaddr*)&localAddr, &localLen) == 0) {
                wchar_t ip[64]; DWORD ipLen = 64;
                WSAAddressToStringW((sockaddr*)&localAddr, localLen, nullptr, ip, &ipLen);
                LogEntry li;
                li.type = LogType::Info;
                li.timestamp = Utils::NowTimestamp();
                li.preview = L"Listen endpoint: " + std::wstring(ip);
                AddLogEntry(std::move(li));
            }
        }
    }

    RefreshDiagnostics();
}

void ServerMainWindow::OnNetStopped()
{
    SetServerUI(false, false);
    SetWindowTextW(m_hStaticStatus, L"\u25CF Not listening");
    SetWindowTextW(m_hStaticClient, L"");
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    SetWindowTextW(m_hWnd, L"TCP Server v" TCPTOOLS_VERSION);

    LogEntry le;
    le.type      = LogType::Stopped;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = L"Server stopped";
    AddLogEntry(std::move(le));

    RefreshDiagnostics();
}

void ServerMainWindow::OnNetClientConnected(std::wstring* pAddrStr)
{
    std::wstring addr;
    if (pAddrStr) { addr = *pAddrStr; delete pAddrStr; }

    SetServerUI(true, true);
    SetWindowTextW(m_hStaticStatus, L"\u25CF Listening (client connected)");
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    wchar_t buf[128];
    swprintf_s(buf, L"Client: %s", addr.c_str());
    SetWindowTextW(m_hStaticClient, buf);
    InvalidateRect(m_hStaticClient, nullptr, TRUE);

    // Title bar
    {
        std::wstring portStr = Utils::GetWindowText(m_hComboPort);
        wchar_t titleBuf[256];
        swprintf_s(titleBuf, L"TCP Server v" TCPTOOLS_VERSION L"  \u2013  :%s  \u25CF  %s",
                   portStr.c_str(), addr.c_str());
        SetWindowTextW(m_hWnd, titleBuf);
    }

    {
        wchar_t sb[64];
        swprintf_s(sb, L"Client time: %s", Utils::NowTimestamp().c_str());
        SetWindowTextW(m_hStatCon, sb);
        InvalidateRect(m_hStatCon, nullptr, TRUE);
    }

    LogEntry le;
    le.type      = LogType::ClientConn;
    le.timestamp = Utils::NowTimestamp();
    {
        bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
        le.preview = std::wstring(L"[") + (isUdp ? L"UDP" : L"TCP") + L"] Client connected: " + addr;
    }
    AddLogEntry(std::move(le));

    // Exhaustive: client socket info
    if (m_net) {
        SOCKET csock = m_net->GetClientSocket();
        if (csock != INVALID_SOCKET) {
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

            // Local endpoint of client socket
            sockaddr_in localAddr = {};
            int localLen = sizeof(localAddr);
            if (getsockname(csock, (sockaddr*)&localAddr, &localLen) == 0) {
                wchar_t ip[64]; DWORD ipLen = 64;
                WSAAddressToStringW((sockaddr*)&localAddr, localLen, nullptr, ip, &ipLen);
                LogEntry li2;
                li2.type = LogType::Info;
                li2.timestamp = Utils::NowTimestamp();
                li2.preview = L"Server-side endpoint: " + std::wstring(ip);
                AddLogEntry(std::move(li2));
            }
        }
    }

    RefreshDiagnostics();
}

void ServerMainWindow::OnNetClientDisconnected()
{
    SetServerUI(m_listening, false);
    if (m_listening) {
        SetWindowTextW(m_hStaticStatus, L"\u25CC Listening");
        SetWindowTextW(m_hStaticClient, L"Client disconnected -- waiting...");
        // Restore listening title
        std::wstring portStr = Utils::GetWindowText(m_hComboPort);
        std::wstring titleStr = L"TCP Server v" TCPTOOLS_VERSION L"  \u2013  Listening :" + portStr;
        SetWindowTextW(m_hWnd, titleStr.c_str());
    } else {
        SetWindowTextW(m_hStaticStatus, L"\u25CF Not listening");
        SetWindowTextW(m_hStaticClient, L"");
        SetWindowTextW(m_hWnd, L"TCP Server v" TCPTOOLS_VERSION);
    }
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    InvalidateRect(m_hStaticClient, nullptr, TRUE);

    LogEntry le;
    le.type      = LogType::ClientDisconn;
    le.timestamp = Utils::NowTimestamp();
    {
        bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
        le.preview = std::wstring(L"[") + (isUdp ? L"UDP" : L"TCP") + L"] Client disconnected (connection closed)";
    }
    AddLogEntry(std::move(le));

    RefreshDiagnostics();
}

void ServerMainWindow::OnNetDataReceived(size_t len, BYTE* pData)
{
    std::vector<BYTE> chunk(pData, pData + len);
    delete[] pData;

    m_totalBytesRecv += len;
    // Always keep the Total recv label current, regardless of stats tab state
    {
        wchar_t buf[80];
        swprintf_s(buf, L"Total recv: %s", Utils::FormatBytes(m_totalBytesRecv).c_str());
        SetWindowTextW(m_hStatTotalRecv, buf);
    }

    // Auto-clear receive window before appending new data
    if (Button_GetCheck(m_hChkAutoClearRecv) == BST_CHECKED) {
        m_recvData.clear();
        m_lastRecvData.clear();
        SetWindowTextW(m_hEditRecv, L"");
        HexView::ClearData(m_hHexRecv);
    }

    m_recvData.insert(m_recvData.end(), chunk.begin(), chunk.end());
    m_lastRecvData.insert(m_lastRecvData.end(), chunk.begin(), chunk.end());
    AppendReceiveText(chunk);

    bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    const wchar_t* proto = isUdp ? L"UDP" : L"TCP";

    LogEntry le;
    le.type      = LogType::Received;
    le.timestamp = Utils::NowTimestamp();
    le.data      = chunk;
    le.byteCount = len;
    std::wstring prev;
    for (size_t i = 0; i < std::min(len, (size_t)80); ++i) {
        BYTE b = chunk[i];
        prev += (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
    }
    le.preview = std::wstring(L"[") + proto + L"] " + prev;
    AddLogEntry(std::move(le));
}

void ServerMainWindow::OnNetSendComplete(size_t bytesSent, CycleStats* pStats)
{
    CycleStats s;
    if (pStats) { s = *pStats; delete pStats; }

    m_totalBytesSent += bytesSent;
    // Always keep the Total sent label current, regardless of s.valid
    {
        wchar_t buf[80];
        swprintf_s(buf, L"Total sent: %s", Utils::FormatBytes(m_totalBytesSent).c_str());
        SetWindowTextW(m_hStatTotalSent, buf);
        swprintf_s(buf, L"Total recv: %s", Utils::FormatBytes(m_totalBytesRecv).c_str());
        SetWindowTextW(m_hStatTotalRecv, buf);
    }

    // Try to find the last pending Sent log entry (from manual send) and attach stats.
    bool foundSent = false;
    for (int i = (int)m_log.size() - 1; i >= 0; --i) {
        if (m_log[i].type == LogType::Sent) {
            m_log[i].stats = s;
            for (int v = (int)m_logVisible.size() - 1; v >= 0; --v) {
                if (m_logVisible[v] == i) {
                    ListView_RedrawItems(m_hListLog, v, v);
                    break;
                }
            }
            foundSent = true;
            break;
        }
    }

    // If no manual Sent entry exists this is an echo reply – add a log entry for it.
    if (!foundSent && bytesSent > 0
        && Button_GetCheck(m_hChkEcho) == BST_CHECKED) {
        bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
        const wchar_t* proto = isUdp ? L"UDP" : L"TCP";
        LogEntry le;
        le.type      = LogType::Sent;
        le.timestamp = Utils::NowTimestamp();
        le.byteCount = bytesSent;
        le.stats     = s;
        wchar_t buf[80];
        swprintf_s(buf, L"[%s] Echoed %zu bytes", proto, bytesSent);
        le.preview = buf;
        AddLogEntry(std::move(le));
    }

    UpdateStats(s);
}

void ServerMainWindow::OnNetError(int wsaErr, std::wstring* pMsg)
{
    std::wstring msg;
    if (pMsg) { msg = *pMsg; delete pMsg; }
    // Don't stop server on error, just log it
    wchar_t title[64]; swprintf_s(title, L"Network Error (%d)", wsaErr);
    MessageBoxW(m_hWnd, msg.c_str(), title, MB_ICONERROR);

    LogEntry le;
    le.type      = LogType::Error;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = msg;
    AddLogEntry(std::move(le));
}

// ============================================================
//  AppendReceiveText
// ============================================================

void ServerMainWindow::AppendReceiveText(const std::vector<BYTE>& data)
{
    if (m_hexRecvMode) {
        HexView::SetData(m_hHexRecv, m_recvData);
    } else {
        const int MAX_CHARS = 1 * 1024 * 1024; // 1 MB rolling cap
        const int TRIM_TO   = MAX_CHARS / 2;    // keep newest ~512 KB
        int len = GetWindowTextLengthW(m_hEditRecv);
        if (len > MAX_CHARS) {
            // Remove oldest half, preserving recent data
            SendMessage(m_hEditRecv, EM_SETSEL, 0, TRIM_TO);
            SendMessage(m_hEditRecv, EM_REPLACESEL, FALSE, (LPARAM)L"[...older output trimmed...]\r\n");
            len = GetWindowTextLengthW(m_hEditRecv);
        }
        std::string s(data.begin(), data.end());
        std::wstring ws = Utils::ToWide(s);
        SendMessage(m_hEditRecv, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(m_hEditRecv, EM_REPLACESEL, FALSE, (LPARAM)ws.c_str());
    }
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

    // Update total byte labels (totals accumulated in OnNetSendComplete/OnNetDataReceived)
    swprintf_s(buf, L"Total sent: %s", Utils::FormatBytes(m_totalBytesSent).c_str());
    SetWindowTextW(m_hStatTotalSent, buf);
    swprintf_s(buf, L"Total recv: %s", Utils::FormatBytes(m_totalBytesRecv).c_str());
    SetWindowTextW(m_hStatTotalRecv, buf);
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

void ServerMainWindow::RefreshLogView()
{
    m_logVisible.clear();
    for (int i = 0; i < (int)m_log.size(); ++i) {
        if (PassesLogFilter(m_log[i].type, m_logFilter))
            m_logVisible.push_back(i);
    }
    ListView_SetItemCountEx(m_hListLog, (int)m_logVisible.size(), 0);
    InvalidateRect(m_hListLog, nullptr, TRUE);
}

void ServerMainWindow::AddLogEntry(LogEntry&& e)
{
    m_log.push_back(std::move(e));
    const LogEntry& le = m_log.back();
    int origIdx = (int)(m_log.size() - 1);

    if (!PassesLogFilter(le.type, m_logFilter)) return;

    m_logVisible.push_back(origIdx);
    int count = (int)m_logVisible.size();
    ListView_SetItemCountEx(m_hListLog, count, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    ListView_RedrawItems(m_hListLog, count - 1, count - 1);
    ListView_EnsureVisible(m_hListLog, count - 1, FALSE);
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
                case 0: swprintf_s(buf, L"%d", logIdx + 1); break;
                case 1: wcsncpy_s(buf, le.timestamp.c_str(), _TRUNCATE); break;
                case 2: { int t = (int)le.type; wcsncpy_s(buf, (t >= 0 && t < 8) ? typeNames[t] : L"?", _TRUNCATE); break; }
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
    if (cmd == IDM_LOG_RELOAD && origIdx >= 0) DoReloadLogEntry(origIdx);
    if (cmd == IDM_LOG_COPY   && origIdx >= 0) Utils::CopyToClipboard(m_hWnd, m_log[origIdx].preview);
    if (cmd == IDM_LOG_CLEAR) {
        m_log.clear();
        m_logVisible.clear();
        ListView_SetItemCountEx(m_hListLog, 0, 0);
        InvalidateRect(m_hListLog, nullptr, TRUE);
    }
}

void ServerMainWindow::DoReloadLogEntry(int idx)
{
    if (idx < 0 || idx >= (int)m_log.size()) return;
    const LogEntry& le = m_log[idx];
    switch (le.type)
    {
    case LogType::Sent:
        m_sendData = le.data;
        HexView::SetData(m_hHexSend, m_sendData);
        m_ignoreEditChange = true;
        SetWindowTextW(m_hEditSend, Utils::ToWide(std::string(m_sendData.begin(), m_sendData.end())).c_str());
        m_ignoreEditChange = false;
        RefreshSendView();
        SetFocus(m_hEditSend);
        break;
    case LogType::Received:
        m_recvData = le.data;
        SetWindowTextW(m_hEditRecv, Utils::ToWide(std::string(m_recvData.begin(), m_recvData.end())).c_str());
        HexView::SetData(m_hHexRecv, m_recvData);
        RefreshRecvView();
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
    SendMessageW(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, 420);
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
        L"Receive / accept timeout in seconds.\n\n"
        L"How long the server waits for data or a new "
        L"client before timing out.\n\n"
        L"\u2022 Enter 0 to wait indefinitely (default).\n"
        L"\u2022 Typical values: 5\u201330 seconds.\n\n"
        L"A timeout of 0 keeps the server listening "
        L"without any time limit.");

    tip(m_hComboProtocol,
        L"Network protocol the server uses.\n\n"
        L"TCP \u2014 Transmission Control Protocol.\n"
        L"Reliable, ordered, byte-stream delivery. "
        L"Server accepts one client at a time.\n\n"
        L"UDP \u2014 User Datagram Protocol.\n"
        L"Connectionless datagrams. Receives data from "
        L"any sender without a connection handshake.");

    tip(m_hBtnStart,
        L"Start the server.\n\n"
        L"Binds to the configured port and begins listening "
        L"for incoming client connections. The status bar "
        L"shows \u201cListening on port ...\u201d once the server is ready.\n\n"
        L"The port must be free \u2014 if another process is "
        L"already using it, an error is displayed.");

    tip(m_hBtnStop,
        L"Stop the server.\n\n"
        L"Closes the listening socket, disconnects any "
        L"connected client, and returns to the idle state. "
        L"The port is released and becomes available for "
        L"other applications.");

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
        L"\u2022 Stopped \u2014 not running\n"
        L"\u2022 Listening on port ... \u2014 waiting for clients\n"
        L"\u2022 Client connected \u2014 active session");

    tip(m_hStaticClient,
        L"Connected client information.\n\n"
        L"Displays the IP address and port of the currently "
        L"connected client, or empty when no client is connected.");

    // ================================================================
    //  SEND GROUP
    // ================================================================
    tip(m_hEditSend,
        L"Send editor \u2014 type or paste data to send to the client.\n\n"
        L"In text mode, type plain ASCII/UTF-8 text. "
        L"In hex mode, enter space-separated hex pairs: "
        L"48 65 6C 6C 6F (= \"Hello\").\n\n"
        L"Keyboard shortcuts:\n"
        L"\u2022 Ctrl+Enter \u2014 Send immediately\n"
        L"\u2022 Ctrl+A \u2014 Select all text\n\n"
        L"You can also load binary data from a file.");

    tip(m_hBtnSend,
        L"Send the current editor contents to the client.\n\n"
        L"The payload is framed according to the Separator "
        L"setting (prefix/suffix added automatically). "
        L"A client must be connected for sending to work.\n\n"
        L"Shortcut: Ctrl+Enter");

    tip(m_hBtnLoadFile,
        L"Load a file from disk into the send editor.\n\n"
        L"The entire file is read as raw bytes. If Hex View "
        L"is active, data is shown as hex bytes; otherwise as text.\n\n"
        L"Useful for sending binary protocols or template "
        L"response files.");

    tip(m_hBtnClearSend,
        L"Clear the send editor and internal buffer.\n\n"
        L"Both the text view and the hex view are reset.");

    tip(m_hChkHexSend,
        L"Toggle Hex View for the send editor.\n\n"
        L"When checked, the editor displays and accepts data "
        L"as space-separated hexadecimal byte pairs.\n"
        L"Example: 48 65 6C 6C 6F = \"Hello\"\n\n"
        L"Switching modes converts the current data between "
        L"representations automatically.");

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
        L"Writes the raw bytes from the most recent "
        L"transmission to a file on disk. Only the last "
        L"send/receive cycle is kept.\n\n"
        L"Useful for capturing binary payloads or saving "
        L"client data for offline analysis.");

    tip(m_hChkHexRecv,
        L"Toggle Hex View for the receive display.\n\n"
        L"When checked, incoming data is shown as hexadecimal "
        L"bytes with an ASCII sidebar. When unchecked, data is "
        L"displayed as text.\n\n"
        L"Switching modes re-renders all accumulated data.");

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
        L"\u2022 Log \u2014 chronological history of all events "
        L"(listen, client connections, sends, receives, errors). "
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
        L"\u2022 Connection \u2014 listen/connect/disconnect events\n"
        L"\u2022 Errors \u2014 only error messages\n\n"
        L"The underlying log data is preserved; switching "
        L"filters does not delete entries.");

    tip(m_hBtnRefreshDiag,
        L"Refresh socket diagnostics.\n\n"
        L"Queries the active sockets for live information:\n"
        L"\u2022 Send/receive buffer sizes (SO_SNDBUF/SO_RCVBUF)\n"
        L"\u2022 Nagle algorithm (TCP_NODELAY) state\n"
        L"\u2022 Keep-alive probe status\n"
        L"\u2022 Estimated path MTU (if available)\n\n"
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
