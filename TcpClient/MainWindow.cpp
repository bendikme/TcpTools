#include "pch.h"
#include "MainWindow.h"

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

static HFONT CreateUIFont()
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    return CreateFontIndirectW(&ncm.lfMessageFont);
}
static HFONT CreateMonoUI()
{
    LOGFONTW lf = {};
    lf.lfHeight         = -13;
    lf.lfWeight         = FW_NORMAL;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lf.lfCharSet        = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    HFONT f = CreateFontIndirectW(&lf);
    if (!f) { wcscpy_s(lf.lfFaceName, L"Courier New"); f = CreateFontIndirectW(&lf); }
    return f;
}

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
    wc.hIcon         = LoadIconW(hInst, L"IDI_APPICON");
    wc.hIconSm       = wc.hIcon;
    return RegisterClassExW(&wc) != 0;
}

HWND MainWindow::Create(HINSTANCE hInst, int nCmdShow,
                        const std::wstring& defaultIP, int defaultPort, bool autoConnect)
{
    struct Params { std::wstring ip; int port; bool autoConnect; };
    auto* p = new Params{ defaultIP, defaultPort, autoConnect };

    HWND hWnd = CreateWindowExW(
        0, ClassName(),
        L"TCP Client v26.3.1",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 820,
        nullptr, nullptr, hInst, p);

    if (!hWnd) { delete p; return nullptr; }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
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
                L"TCP Client v26.3.1\n\nCopyright \u00A9 2026 Bendik Eide.\nAll rights reserved.",
                L"About TCP Client v26.3.1", MB_OK | MB_ICONINFORMATION);
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

    // ---- Network messages ------------------------------------
    case WM_NET_CONNECTED:
        if (self) self->OnNetConnected();
        return 0;
    case WM_NET_DISCONNECTED:
        if (self) self->OnNetDisconnected();
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
            double latMs = latUs / 1000.0;
            // Find last Sent entry and store latency
            for (int i = (int)self->m_log.size() - 1; i >= 0; --i) {
                if (self->m_log[i].type == LogType::Sent) {
                    self->m_log[i].latencyMs = latMs;
                    // Find this entry's virtual row and invalidate it
                    for (int v = (int)self->m_logVisible.size() - 1; v >= 0; --v) {
                        if (self->m_logVisible[v] == i) {
                            ListView_RedrawItems(self->m_hListLog, v, v);
                            break;
                        }
                    }
                    break;
                }
            }
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
        if (self) {
            wchar_t cbuf[64];
            swprintf_s(cbuf, L"Sent: %u", (unsigned)wParam);
            SetWindowTextW(self->m_hStaticCyclicCount, cbuf);
        }
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
    m_hFont     = CreateUIFont();
    m_hFontMono = CreateMonoUI();

    // --- Connection group (placeholder pos; real pos set in LayoutControls) ---
    m_hGrpConn     = MakeGroupBox(IDC_GRP_CONNECTION, L" Connection ", 0,0,10,10);
    m_hEditIP      = MakeEdit(IDC_EDIT_IP,   0,0,10,10);
    m_hEditPort    = MakeEdit(IDC_EDIT_PORT, 0,0,10,10);
    m_hEditTimeout = MakeEdit(IDC_EDIT_TIMEOUT,0,0,10,10);
    m_hBtnConnect    = MakeButton(IDC_BTN_CONNECT,    L"Connect",    0,0,10,10);
    m_hBtnDisconnect = MakeButton(IDC_BTN_DISCONNECT, L"Disconnect", 0,0,10,10);
    m_hChkKeepAlive  = MakeCheck(IDC_CHK_KEEPALIVE, L"Keep Alive",  0,0,10,10);
    m_hChkAutoDisc   = MakeCheck(IDC_CHK_AUTODISCONNECT, L"Auto-Disconnect After Send", 0,0,10,10);
    m_hChkAutoConnect = MakeCheck(IDC_CHK_AUTOCONNECT, L"Auto-Connect Before Send", 0,0,10,10);
    m_hStaticStatus  = MakeLabel(L"\u25CF Disconnected", 0,0,10,10);

    // Apply defaults from CLI args
    wchar_t portBuf[16]; swprintf_s(portBuf, L"%d", defaultPort);
    SetWindowTextW(m_hEditIP,      defaultIP.c_str());
    SetWindowTextW(m_hEditPort,    portBuf);
    SetWindowTextW(m_hEditTimeout, L"5");
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

    m_hChkHexSend   = MakeCheck(IDC_CHK_HEXVIEW,    L"Hex View", 0,0,10,10);
    m_hEditSend     = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_EDIT_SEND, hInst, nullptr);
    SendMessage(m_hEditSend, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SetWindowSubclass(m_hEditSend, SendEditSubclassProc, 1, 0);
    SendMessage(m_hEditSend, EM_SETLIMITTEXT, 0, 0); // no character limit

    m_hHexSend = HexView::Create(hWnd, 0,0,10,10, IDC_STATIC_HEXVIEW, hInst);
    ShowWindow(m_hHexSend, SW_HIDE);

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
        m_hImgListLog = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 8, 0);
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

    // Log columns
    LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    auto addCol = [&](const wchar_t* hdr, int w) {
        col.pszText = (LPWSTR)hdr; col.cx = w;
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

    // Tooltips
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
}

void MainWindow::OnGetMinMaxInfo(MINMAXINFO* mm)
{
    mm->ptMinTrackSize = { 900, 650 };
}

void MainWindow::LayoutControls(int W, int H)
{
    if (!m_hEditIP) return; // not yet created

    HDWP hdwp = BeginDeferWindowPos(54);
    auto Move = [&](HWND hw, int x, int y, int w, int h) {
        if (hw && hdwp)
            hdwp = DeferWindowPos(hdwp, hw, nullptr, x, y, w, h,
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
    Move(m_hEditIP, cx, cy, 140, ROW_H); cx += 140 + MARGIN;
    Move(m_hLblPort,    cx, cy, 32, ROW_H); cx += 32 + 2;
    Move(m_hEditPort,    cx, cy, 55, ROW_H); cx += 55 + MARGIN;
    Move(m_hLblTimeout, cx, cy, 80, ROW_H); cx += 80 + 2;
    Move(m_hEditTimeout, cx, cy, 40, ROW_H); cx += 40 + MARGIN;
    Move(m_hComboProtocol, cx, cy, 55, 200); cx += 55 + MARGIN;
    Move(m_hBtnConnect,    cx, cy, BTN_W,   ROW_H); cx += BTN_W + 4;
    Move(m_hBtnDisconnect, cx, cy, BTN_W,   ROW_H); cx += BTN_W + 2*MARGIN;
    Move(m_hStaticStatus,  cx, cy, 200, ROW_H);

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
    Move(m_hChkHexSend, comboX, sy, 90, ROW_H); comboX += 94;
    Move(m_hLblPre, comboX, sy, 80, ROW_H);
    Move(m_hEditPreFix, comboX+82, sy, 80, ROW_H);
    Move(m_hLblSuf, comboX+170, sy, 80, ROW_H);
    Move(m_hEditSufFix, comboX+252, sy, 80, ROW_H);
    sy += ROW_H + 4;

    // Send text/hex area
    int sendAreaH = midH - GRP_TITLE - ROW_H - 4 - ROW_H - 6 - ROW_H - 4 - MARGIN;
    Move(m_hEditSend, sx, sy, sendCtrlW, sendAreaH);
    Move(m_hHexSend,  sx, sy, sendCtrlW, sendAreaH);
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
    // Get tab client area
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
    if (m_cyclicRunning) {
        m_cyclicStop = true;
        if (m_cyclicThread.joinable()) m_cyclicThread.join();
        m_cyclicRunning = false;
    }
    if (m_net) m_net->Disconnect();
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
        SetWindowTextW(m_hEditSend, L"");
        HexView::ClearData(m_hHexSend);
        m_sendData.clear();
        break;

    case IDC_BTN_CLEARRECV:
        SetWindowTextW(m_hEditRecv, L"");
        HexView::ClearData(m_hHexRecv);
        m_recvData.clear();
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
        SetConnectionUI(m_connected);
        break;

    case IDC_EDIT_SEND:
        // Clear cached sendData when the user actually types so the hex view
        // re-syncs from the editor next time. Guard against programmatic SetWindowText.
        if (notify == EN_CHANGE && !m_ignoreEditChange) m_sendData.clear();
        break;

    case IDC_BTN_REFRESHDIAG:
        RefreshDiagnostics();
        break;

    case IDC_COMBO_LOGFILTER:
        if (notify == CBN_SELCHANGE) {
            m_logFilter = (int)SendMessage(m_hComboLogFilter, CB_GETCURSEL, 0, 0);
            RefreshLogView();
        }
        break;

    case IDC_BTN_CYCLIC_START:
    {
        if (m_cyclicRunning) {
            // Stop cyclic sending
            m_cyclicStop = true;
            if (m_cyclicThread.joinable()) m_cyclicThread.join();
            m_cyclicRunning = false;
            SetWindowTextW(m_hBtnCyclicStart, L"Start Cyclic");

            // Log summary
            LogEntry sle;
            sle.type = LogType::Info;
            sle.timestamp = Utils::NowTimestamp();
            sle.preview = L"Cyclic send stopped (" + std::to_wstring(m_cyclicCount.load()) + L" sends)";
            AddLogEntry(std::move(sle));
        } else {
            if (!m_connected) {
                if (Button_GetCheck(m_hChkAutoConnect) == BST_CHECKED) {
                    // Auto-connect then fire cyclic — same pattern as DoSend.
                    if (!m_pendingCyclicStart) {
                        m_pendingCyclicStart = true;
                        DoConnect();
                    }
                } else {
                    MessageBoxW(m_hWnd, L"Not connected.", L"Not connected", MB_ICONWARNING);
                }
                break;
            }
            // Parse interval
            wchar_t buf[32] = {};
            GetWindowTextW(m_hEditCyclicInterval, buf, 32);
            double sec = _wtof(buf);
            if (sec < 0.001) sec = 0.001;
            auto intervalUs = std::chrono::microseconds((long long)(sec * 1000000.0 + 0.5));

            // Pre-build data and config
            std::vector<BYTE> data = GetSendData();
            if (data.empty()) {
                MessageBoxW(m_hWnd, L"Nothing to send.", L"Empty data", MB_ICONWARNING);
                break;
            }
            NetConfig cfg = BuildNetConfig();

            m_cyclicStop  = false;
            m_cyclicCount = 0;
            m_cyclicRunning = true;
            SetWindowTextW(m_hBtnCyclicStart, L"Stop Cyclic");
            SetWindowTextW(m_hStaticCyclicCount, L"Sent: 0");

            // Log start
            LogEntry sle;
            sle.type = LogType::Info;
            sle.timestamp = Utils::NowTimestamp();
            {
                wchar_t ibuf[64];
                swprintf_s(ibuf, L"Cyclic send started (interval: %.3f s)", sec);
                sle.preview = ibuf;
            }
            AddLogEntry(std::move(sle));

            HWND hWnd = m_hWnd;
            m_cyclicThread = std::thread([this, data, cfg, intervalUs, hWnd]() {
                using clock = std::chrono::steady_clock;
                auto next = clock::now();
                auto lastUI = next;
                while (!m_cyclicStop.load(std::memory_order_relaxed)) {
                    if (!m_net || !m_net->IsConnected()) break;
                    if (!m_net->Send(data, cfg)) break;   // send failed (disconnected)
                    m_cyclicCount.fetch_add(1, std::memory_order_relaxed);
                    next += intervalUs;

                    // Throttled UI update every 100ms
                    auto now = clock::now();
                    if (now - lastUI >= std::chrono::milliseconds(100)) {
                        PostMessage(hWnd, WM_CYCLIC_UPDATE,
                                    (WPARAM)m_cyclicCount.load(std::memory_order_relaxed), 0);
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
                // Final UI update
                PostMessage(hWnd, WM_CYCLIC_UPDATE,
                            (WPARAM)m_cyclicCount.load(std::memory_order_relaxed), 0);
            });
        }
        break;
    }
    }
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
    if (!f) {
        MessageBoxW(m_hWnd, L"Cannot write to the selected file.", L"Save error", MB_ICONERROR);
        return;
    }
    f.write(reinterpret_cast<const char*>(m_lastRecvData.data()), m_lastRecvData.size());
    f.close();

    wchar_t msg[MAX_PATH + 64];
    swprintf_s(msg, L"Saved %zu bytes to:\n%s", m_lastRecvData.size(), buf);
    MessageBoxW(m_hWnd, msg, L"Saved", MB_ICONINFORMATION);
}



void MainWindow::DoConnect()
{
    if (m_connected) return;

    NetConfig cfg = BuildNetConfig();
    if (cfg.host.empty()) {
        MessageBoxW(m_hWnd, L"Please enter a host/IP address.", L"Missing input", MB_ICONWARNING);
        return;
    }
    if (cfg.port <= 0 || cfg.port > 65535) {
        MessageBoxW(m_hWnd, L"Please enter a valid port (1-65535).", L"Invalid port", MB_ICONWARNING);
        return;
    }

    if (!m_net) m_net = std::make_unique<TcpNetwork>(m_hWnd);
    m_connectTime = Utils::Now();

    SetWindowTextW(m_hStaticStatus, L"\u25CC Connecting...");
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
    EnableWindow(m_hBtnConnect, FALSE);
    m_net->Connect(cfg);
}

void MainWindow::DoDisconnect()
{
    // Stop cyclic send first so it doesn't keep firing on a closed socket
    if (m_cyclicRunning) {
        m_cyclicStop = true;
        if (m_cyclicThread.joinable()) m_cyclicThread.join();
        m_cyclicRunning = false;
        SetWindowTextW(m_hBtnCyclicStart, L"Start Cyclic");
    }
    if (m_net) m_net->Disconnect();
    // UI update via WM_NET_DISCONNECTED
}

// ============================================================
//  DoSend
// ============================================================

void MainWindow::DoSend()
{
    // Auto-connect if enabled and not connected.
    // Don't post a retry message — that causes an infinite re-entry loop while the
    // async connect is in flight.  Instead, set a flag; OnNetConnected() will call
    // DoSend() exactly once after the connection is established.
    if (!m_connected && Button_GetCheck(m_hChkAutoConnect) == BST_CHECKED) {
        if (!m_pendingAutoSend) {
            m_pendingAutoSend = true;
            DoConnect();
        }
        return;
    }
    if (!m_connected) {
        MessageBoxW(m_hWnd, L"Not connected.", L"Not connected", MB_ICONWARNING);
        return;
    }
    std::vector<BYTE> data = GetSendData();
    if (data.empty()) {
        MessageBoxW(m_hWnd, L"Nothing to send.", L"Empty data", MB_ICONWARNING);
        return;
    }

    NetConfig cfg = BuildNetConfig();

    // Determine protocol label
    bool isUdp = cfg.udpMode;
    const wchar_t* proto = isUdp ? L"UDP" : L"TCP";

    // Save to log immediately as "Sent" entry
    LogEntry le;
    le.type      = LogType::Sent;
    le.timestamp = Utils::NowTimestamp();
    le.data      = data;
    le.byteCount = data.size();
    // Preview: first 80 printable chars
    std::wstring prev;
    for (size_t i = 0; i < std::min((size_t)80, data.size()); ++i) {
        BYTE b = data[i];
        prev += (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
    }
    le.preview = std::wstring(L"[") + proto + L"] " + prev + L" [" + std::to_wstring(data.size()) + L" B]";
    m_lastRecvData.clear();   // reset last-transmission tracker
    if (Button_GetCheck(m_hChkAutoClearRecv) == BST_CHECKED) {
        m_recvData.clear();
        SetWindowTextW(m_hEditRecv, L"");
        HexView::ClearData(m_hHexRecv);
    }

    // Queue the send (non-blocking; send worker posts WM_NET_SEND_COMPLETE)
    if (m_net) m_net->QueueSend(std::move(data), cfg);

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

void MainWindow::DoLoadFile()
{
    std::wstring path;
    if (!Utils::OpenFileDialog(m_hWnd, path)) return;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { MessageBoxW(m_hWnd, L"Cannot open file.", L"Error", MB_ICONERROR); return; }

    auto sz = f.tellg();
    f.seekg(0);
    m_sendData.resize((size_t)sz);
    f.read((char*)m_sendData.data(), sz);
    f.close();

    // Update both text and hex views
    // Text view: attempt UTF-8 decode
    std::string raw(m_sendData.begin(), m_sendData.end());
    std::wstring wide = Utils::ToWide(raw);
    // Set hex view BEFORE the editor text so EN_CHANGE clearing m_sendData
    // doesn't wipe the data we just stored for the hex view.
    HexView::SetData(m_hHexSend, m_sendData);
    m_ignoreEditChange = true;
    SetWindowTextW(m_hEditSend, wide.c_str());
    m_ignoreEditChange = false;

    // Show appropriate view
    RefreshSendView();

    // Log
    LogEntry le;
    le.type      = LogType::Sent;       // will be overridden when actually sent
    le.timestamp = Utils::NowTimestamp();
    le.data      = m_sendData;
    le.byteCount = m_sendData.size();
    le.preview   = L"[File loaded: " + path.substr(path.find_last_of(L"\\/") + 1) + L"]";
    // We don't add to log here - only when user clicks Send
}

// ============================================================
//  GetSendData
// ============================================================

std::vector<BYTE> MainWindow::GetSendData()
{
    if (m_hexSendMode) {
        // Parse hex from edit
        std::wstring ws = Utils::GetWindowText(m_hEditSend);
        std::string s = Utils::ToUTF8(ws);
        // Remove whitespace
        std::string clean;
        for (char c : s) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') clean += c;
        std::vector<BYTE> out;
        if (!Utils::HexStrToBytes(clean, out)) {
            MessageBoxW(m_hWnd, L"Invalid hex input. Use pairs like: 48 65 6C 6C 6F", L"Hex error", MB_ICONWARNING);
            return {};
        }
        return out;
    } else {
        // Always encode current editor content as UTF-8 so edits are reflected
        std::wstring ws = Utils::GetWindowText(m_hEditSend);
        std::string s = Utils::ToUTF8(ws);
        return std::vector<BYTE>(s.begin(), s.end());
    }
}

// ============================================================
//  BuildNetConfig
// ============================================================

NetConfig MainWindow::BuildNetConfig()
{
    NetConfig cfg;
    cfg.host          = Utils::ToUTF8(Utils::GetWindowText(m_hEditIP));
    cfg.port          = Utils::GetDlgInt(m_hWnd, IDC_EDIT_PORT, 9000);
    cfg.timeoutSec    = Utils::GetDlgInt(m_hWnd, IDC_EDIT_TIMEOUT, 5);
    cfg.keepAlive     = Button_GetCheck(m_hChkKeepAlive) == BST_CHECKED;
    cfg.autoDisconnect= Button_GetCheck(m_hChkAutoDisc)  == BST_CHECKED;
    cfg.udpMode       = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    cfg.separator     = GetSeparatorMode();

    if (cfg.separator == SeparatorMode::Custom) {
        std::string pre = Utils::ToUTF8(Utils::GetWindowText(m_hEditPreFix));
        std::string suf = Utils::ToUTF8(Utils::GetWindowText(m_hEditSufFix));
        Utils::HexStrToBytes(pre, cfg.customPrefix);
        Utils::HexStrToBytes(suf, cfg.customSuffix);
    }
    return cfg;
}

SeparatorMode MainWindow::GetSeparatorMode()
{
    int sel = (int)SendMessage(m_hComboSep, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    return (SeparatorMode)sel;
}

// ============================================================
//  Refresh send/recv views
// ============================================================

void MainWindow::RefreshSendView()
{
    if (m_hexSendMode) {
        ShowWindow(m_hEditSend, SW_HIDE);
        ShowWindow(m_hHexSend,  SW_SHOW);
        // Sync hex: use raw sendData if available (from file load / log reload),
        // otherwise re-encode current editor text so user edits are reflected.
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

void MainWindow::RefreshRecvView()
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

void MainWindow::RefreshDiagnostics()
{
    if (!m_net || !m_connected) {
        SetWindowTextW(m_hStaticDiagInfo, L"Not connected \x2014 connect to view diagnostics");
        return;
    }

    SOCKET sock = m_net->GetSocket();
    if (sock == INVALID_SOCKET) {
        SetWindowTextW(m_hStaticDiagInfo, L"Socket invalid");
        return;
    }

    std::wstring info;

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
    info += (soType == SOCK_STREAM) ? L"Type: SOCK_STREAM  " : L"Type: ?  ";

    // Estimated MTU (Ethernet): MSS + 40 (TCP+IP headers)
    if (mss > 0) {
        swprintf_s(buf, L"Est. MTU: %d B  Frame: ~%d B", mss + 40, mss + 40 + 14);
        info += buf;
    } else {
        info += L"MTU: (query N/A)  Est. Frame: ~1514 B (Ethernet)";
    }

    SetWindowTextW(m_hStaticDiagInfo, info.c_str());
}

// ============================================================
//  SetConnectionUI
// ============================================================

void MainWindow::SetConnectionUI(bool connected)
{
    m_connected = connected;
    EnableWindow(m_hBtnConnect,    !connected);
    EnableWindow(m_hBtnDisconnect,  connected);
    EnableWindow(m_hBtnSend,        connected || Button_GetCheck(m_hChkAutoConnect) == BST_CHECKED);
    EnableWindow(m_hBtnCyclicStart, connected || Button_GetCheck(m_hChkAutoConnect) == BST_CHECKED);
    EnableWindow(m_hEditIP,        !connected);
    EnableWindow(m_hEditPort,      !connected);
    EnableWindow(m_hEditTimeout,   !connected);
    EnableWindow(m_hComboProtocol, !connected);
    EnableWindow(m_hChkKeepAlive,  !connected);
    EnableWindow(m_hChkAutoDisc,   !connected);
    EnableWindow(m_hChkAutoConnect,!connected);
    if (connected) {
        SetWindowTextW(m_hStaticStatus, L"\u25CF Connected");
        // Build title: "TCP Client  –  ip:port"
        std::wstring ip   = Utils::GetWindowText(m_hEditIP);
        std::wstring port = Utils::GetWindowText(m_hEditPort);
        bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
        std::wstring title = std::wstring(isUdp ? L"UDP Client v26.3.1" : L"TCP Client v26.3.1")
                             + L"  \u2013  " + ip + L":" + port;
        SetWindowTextW(m_hWnd, title.c_str());
    } else {
        SetWindowTextW(m_hStaticStatus, L"\u25CF Disconnected");
        SetWindowTextW(m_hWnd, L"TCP Client v26.3.1");
    }
    InvalidateRect(m_hStaticStatus, nullptr, TRUE);
}

// ============================================================
//  Network message handlers
// ============================================================

void MainWindow::OnNetConnected()
{
    m_connectTime = Utils::Now();
    SetConnectionUI(true);

    // If the connection was triggered by auto-connect-before-send, fire the send now.
    if (m_pendingAutoSend) {
        m_pendingAutoSend = false;
        DoSend(); // will proceed since m_connected is now true
    }
    // If the connection was triggered by auto-connect-before-cyclic, start cyclic now.
    if (m_pendingCyclicStart) {
        m_pendingCyclicStart = false;
        PostMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_CYCLIC_START, BN_CLICKED), 0);
    }

    bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    const wchar_t* proto = isUdp ? L"UDP" : L"TCP";

    LogEntry le;
    le.type      = LogType::Connected;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = std::wstring(L"[") + proto + L"] Connected to "
                   + Utils::ToWide(Utils::ToUTF8(Utils::GetWindowText(m_hEditIP)))
                   + L":" + Utils::GetWindowText(m_hEditPort);
    le.byteCount = 0;
    AddLogEntry(std::move(le));

    // Exhaustive socket info log
    if (m_net && m_net->GetSocket() != INVALID_SOCKET) {
        SOCKET sock = m_net->GetSocket();

        // Log socket options
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

        // Log local endpoint
        sockaddr_in localAddr = {};
        int localLen = sizeof(localAddr);
        if (getsockname(sock, (sockaddr*)&localAddr, &localLen) == 0) {
            wchar_t ip[64]; DWORD ipLen = 64;
            WSAAddressToStringW((sockaddr*)&localAddr, localLen, nullptr, ip, &ipLen);
            LogEntry li2;
            li2.type = LogType::Info;
            li2.timestamp = Utils::NowTimestamp();
            li2.preview = L"Local endpoint: " + std::wstring(ip);
            AddLogEntry(std::move(li2));
        }
    }

    UpdateStats(CycleStats{});
    {
        wchar_t buf[64];
        swprintf_s(buf, L"Connect time: %s", Utils::NowTimestamp().c_str());
        SetWindowTextW(m_hStatCon, buf);
    }

    RefreshDiagnostics();
}

void MainWindow::OnNetDisconnected()
{
    m_pendingAutoSend    = false; // cancel pending auto-send if connection dropped
    m_pendingCyclicStart = false; // cancel pending cyclic start if connection dropped
    if (m_cyclicRunning) {
        m_cyclicStop = true;
        if (m_cyclicThread.joinable()) m_cyclicThread.join();
        m_cyclicRunning = false;
        SetWindowTextW(m_hBtnCyclicStart, L"Start Cyclic");
    }
    SetConnectionUI(false);

    bool isUdp = (SendMessage(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);
    const wchar_t* proto = isUdp ? L"UDP" : L"TCP";

    LogEntry le;
    le.type      = LogType::Disconnected;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = std::wstring(L"[") + proto + L"] Disconnected (connection closed)";
    le.byteCount = 0;
    AddLogEntry(std::move(le));

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
        m_recvData.clear();
        m_lastRecvData.clear();
        SetWindowTextW(m_hEditRecv, L"");
        HexView::ClearData(m_hHexRecv);
    }

    m_recvData.insert(m_recvData.end(), chunk.begin(), chunk.end());
    m_lastRecvData.insert(m_lastRecvData.end(), chunk.begin(), chunk.end()); // accumulate for Save Last
    AppendReceiveText(chunk);

    // Determine protocol label
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

    // Compute latency: find last Sent entry with valid sendEnd but no latency stored yet
    for (int i = (int)m_log.size() - 1; i >= 0; --i) {
        if (m_log[i].type == LogType::Sent && m_log[i].stats.valid && m_log[i].latencyMs < 0) {
            // Approximate latency as time from sendEnd to now (UI thread time)
            double lat = Utils::ElapsedMs(m_log[i].stats.sendEnd, Utils::Now());
            if (lat >= 0) {
                m_log[i].latencyMs = lat;
                // Invalidate this row in the virtual ListView
                for (int v = (int)m_logVisible.size() - 1; v >= 0; --v) {
                    if (m_logVisible[v] == i) {
                        ListView_RedrawItems(m_hListLog, v, v);
                        break;
                    }
                }
            }
            break;
        }
    }

    // Update total stats labels
    {
        wchar_t buf[80];
        swprintf_s(buf, L"Total sent: %s", Utils::FormatBytes(m_totalBytesSent).c_str());
        SetWindowTextW(m_hStatTotalSent, buf);
        swprintf_s(buf, L"Total recv: %s", Utils::FormatBytes(m_totalBytesRecv).c_str());
        SetWindowTextW(m_hStatTotalRecv, buf);
    }
}

void MainWindow::OnNetSendComplete(size_t bytesSent, CycleStats* pStats)
{
    CycleStats s;
    if (pStats) { s = *pStats; delete pStats; }

    // Accumulate total bytes
    m_totalBytesSent += bytesSent;

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

void MainWindow::OnNetError(int wsaErr, std::wstring* pMsg)
{
    m_pendingAutoSend    = false; // cancel pending auto-send if connection failed
    m_pendingCyclicStart = false; // cancel pending cyclic start if connection failed
    std::wstring msg;
    if (pMsg) { msg = *pMsg; delete pMsg; }

    SetConnectionUI(false);
    if (m_net) m_net->Disconnect();

    wchar_t title[64];
    swprintf_s(title, L"Network Error (%d)", wsaErr);
    MessageBoxW(m_hWnd, msg.c_str(), title, MB_ICONERROR);

    LogEntry le;
    le.type      = LogType::Error;
    le.timestamp = Utils::NowTimestamp();
    le.preview   = msg;
    le.byteCount = 0;
    AddLogEntry(std::move(le));
}

// ============================================================
//  AppendReceiveText
// ============================================================

void MainWindow::AppendReceiveText(const std::vector<BYTE>& data)
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

void MainWindow::AddLogEntry(LogEntry&& e)
{
    m_log.push_back(std::move(e));
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
                case 0: swprintf_s(buf, L"%d", logIdx + 1); break;
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
    if (cmd == IDM_LOG_CLEAR) {
        m_log.clear();
        m_logVisible.clear();
        ListView_SetItemCountEx(m_hListLog, 0, 0);
        InvalidateRect(m_hListLog, nullptr, TRUE);
    }
}

// ============================================================
//  DoReloadLogEntry
// ============================================================

void MainWindow::DoReloadLogEntry(int idx)
{
    if (idx < 0 || idx >= (int)m_log.size()) return;
    const LogEntry& le = m_log[idx];

    switch (le.type)
    {
    case LogType::Sent:
    {
        m_sendData = le.data;
        std::string s(m_sendData.begin(), m_sendData.end());
        std::wstring ws = Utils::ToWide(s);
        HexView::SetData(m_hHexSend, m_sendData);
        m_ignoreEditChange = true;
        SetWindowTextW(m_hEditSend, ws.c_str());
        m_ignoreEditChange = false;
        RefreshSendView();
        // Switch to send tab / bring focus
        SetFocus(m_hEditSend);
        break;
    }
    case LogType::Received:
    {
        m_recvData = le.data;
        std::string s(m_recvData.begin(), m_recvData.end());
        std::wstring ws = Utils::ToWide(s);
        SetWindowTextW(m_hEditRecv, ws.c_str());
        HexView::SetData(m_hHexRecv, m_recvData);
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
    SendMessageW(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, 360);

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

    // ---- Connection group ----
    tip(m_hEditIP,           L"Server IP address or hostname to connect to.");
    tip(m_hEditPort,         L"Server port number (1\u201365535).");
    tip(m_hEditTimeout,      L"Connection timeout in seconds. Enter 0 to wait indefinitely.");
    tip(m_hComboProtocol,    L"Protocol:\n  TCP \u2014 reliable, ordered stream connection.\n  UDP \u2014 fast, connectionless datagrams (no delivery guarantee).");
    tip(m_hBtnConnect,       L"Open a connection to the configured server.");
    tip(m_hBtnDisconnect,    L"Close the current connection.");
    tip(m_hChkKeepAlive,     L"Enable TCP keep-alive probes so broken connections are detected and reported early.");
    tip(m_hChkAutoDisc,      L"Disconnect automatically after every send. Useful for testing request/response protocols where a fresh connection is needed each time.");
    tip(m_hChkAutoConnect,   L"Connect automatically before sending if not already connected. The send is deferred until the connection is established.");

    // ---- Send group ----
    tip(m_hEditSend,         L"Type the data to send here. Press Ctrl+Enter to send without clicking the button.");
    tip(m_hBtnSend,          L"Send the editor contents to the server.  [Shortcut: Ctrl+Enter]");
    tip(m_hBtnLoadFile,      L"Load a binary or text file from disk into the send editor.");
    tip(m_hBtnClearSend,     L"Clear the send editor.");
    tip(m_hChkHexSend,       L"Hex View \u2014 enter and display send data as space-separated hex byte pairs (e.g.  48 65 6C 6C 6F).");
    tip(m_hChkAutoClearSend, L"Clear the send editor automatically after each successful send.");
    tip(m_hComboSep,         L"Framing \u2014 wraps the payload with a delimiter before sending:\n"
                              L"  None     \u2014 no extra bytes added\n"
                              L"  STX/ETX  \u2014 0x02 \u2026 0x03\n"
                              L"  CR LF    \u2014 0x0D 0x0A\n"
                              L"  LF / CR / NUL\n"
                              L"  Custom   \u2014 enter your own prefix and suffix as hex bytes");
    tip(m_hEditPreFix,       L"Custom framing prefix as hex bytes (e.g.  AA BB CC). Prepended before every send.");
    tip(m_hEditSufFix,       L"Custom framing suffix as hex bytes (e.g.  0D 0A). Appended after every send.");

    // ---- Cyclic send ----
    tip(m_hEditCyclicInterval, L"Cyclic send interval in seconds (minimum 0.001 s = 1 ms). Examples: 1.0, 0.5, 0.1.");
    tip(m_hBtnCyclicStart,   L"Start / stop sending the current payload repeatedly at the configured interval.");

    // ---- Receive group ----
    tip(m_hEditRecv,         L"Data received from the server (read-only). New data is appended as it arrives.");
    tip(m_hBtnClearRecv,     L"Clear the receive display.");
    tip(m_hBtnCopyRecv,      L"Copy all received data to the clipboard.");
    tip(m_hBtnSaveRecv,      L"Save the data from the last received transmission to a file.\nOnly the most recent send/receive cycle is kept.");
    tip(m_hChkHexRecv,       L"Hex View \u2014 display received data as hexadecimal bytes.");
    tip(m_hChkAutoClearRecv, L"Clear the receive display automatically before each incoming message.");

    // ---- Bottom panel ----
    tip(m_hBtnClearLog,      L"Remove all entries from the history log.");
    tip(m_hComboLogFilter,   L"Filter the log:\n  All | Data only | Connection events | Errors");
    tip(m_hBtnRefreshDiag,   L"Refresh live socket diagnostics: buffer sizes, Nagle algorithm state, keep-alive, estimated MTU.");
    tip(m_hBtnClearStats,    L"Reset all timing and byte-count statistics to zero.");
}
