#include "pch.h"
#include "RelayMainWindow.h"
#include "HexView.h"
#include "resource.h"
#include "../Version.h"

// ============================================================
//  RelayMainWindow implementation
// ============================================================

RelayMainWindow* RelayMainWindow::s_instance = nullptr;

// ---- Registration + Creation -----------------------------------

bool RelayMainWindow::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbWndExtra    = sizeof(LONG_PTR);
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WndClassName;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInst, L"IDI_APPICON");
    wc.hIconSm       = wc.hIcon;
    return RegisterClassExW(&wc) != 0;
}

HWND RelayMainWindow::Create(HINSTANCE hInst, int nCmdShow)
{
    auto* self = new RelayMainWindow();
    self->m_hInst = hInst;
    s_instance = self;

    HWND hWnd = CreateWindowExW(
        0, WndClassName, L"TCP/UDP Relay v" TCPTOOLS_VERSION,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        nullptr, nullptr, hInst, self);
    if (!hWnd) { delete self; s_instance = nullptr; return nullptr; }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return hWnd;
}

// ---- WndProc dispatcher ----------------------------------------

LRESULT CALLBACK RelayMainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RelayMainWindow* self = s_instance;
    if (self) return self->HandleMessage(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---- Helper: create a label + edit pair ------------------------

static HWND MakeLabel(HWND parent, HINSTANCE hInst, const wchar_t* text,
                      int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, nullptr, hInst, nullptr);
}

static HWND MakeEdit(HWND parent, HINSTANCE hInst, int id,
                     int x, int y, int w, int h, const wchar_t* text = L"")
{
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, hInst, nullptr);
    return e;
}

static HWND MakeButton(HWND parent, HINSTANCE hInst, int id,
                       int x, int y, int w, int h, const wchar_t* text)
{
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, hInst, nullptr);
}

static HWND MakeCheckbox(HWND parent, HINSTANCE hInst, int id,
                         int x, int y, int w, int h, const wchar_t* text, bool checked = true)
{
    HWND cb = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, hInst, nullptr);
    if (checked) SendMessageW(cb, BM_SETCHECK, BST_CHECKED, 0);
    return cb;
}

// ---- HandleMessage ---------------------------------------------

LRESULT RelayMainWindow::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        m_hWnd = hWnd;
        OnCreate(hWnd);
        return 0;

    case WM_SIZE:
        OnSize(hWnd, LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_COMMAND:
        OnCommand(hWnd, LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_F1) {
            MessageBoxW(hWnd,
                L"TCP/UDP Relay v" TCPTOOLS_VERSION L"\n\n" TCPTOOLS_COPYRIGHT,
                L"About TCP/UDP Relay v" TCPTOOLS_VERSION, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;

    case WM_NOTIFY:
        return OnNotify(hWnd, (NMHDR*)lParam);

    case WM_CONTEXTMENU:
        if ((HWND)wParam == m_hListLog)
            OnContextMenuLog(hWnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    // ---- Relay network events ----
    case WM_RELAY_LISTENING:
        OnRelayListening((int)wParam);
        return 0;

    case WM_RELAY_CLIENT_CONN: {
        std::wstring* addr = (std::wstring*)lParam;
        OnRelayClientConn(addr ? *addr : L"unknown");
        delete addr;
        return 0;
    }
    case WM_RELAY_CLIENT_DISCONN:
        OnRelayClientDisconn();
        return 0;

    case WM_RELAY_TARGET_CONN:
        OnRelayTargetConn();
        return 0;

    case WM_RELAY_TARGET_DISCONN:
        OnRelayTargetDisconn();
        return 0;

    case WM_RELAY_CLIENT_DATA: {
        BYTE* data = (BYTE*)lParam;
        bool forwarded = (wParam & RELAY_FLAG_FORWARDED) != 0;
        int len = (int)(wParam & 0x7FFFFFFF);
        OnRelayClientData(data, len, forwarded);
        delete[] data;
        return 0;
    }
    case WM_RELAY_TARGET_DATA: {
        BYTE* data = (BYTE*)lParam;
        bool forwarded = (wParam & RELAY_FLAG_FORWARDED) != 0;
        int len = (int)(wParam & 0x7FFFFFFF);
        OnRelayTargetData(data, len, forwarded);
        delete[] data;
        return 0;
    }
    case WM_RELAY_ERROR: {
        std::wstring* msg2 = (std::wstring*)lParam;
        OnRelayError((int)wParam, msg2 ? *msg2 : L"Unknown error");
        delete msg2;
        return 0;
    }
    case WM_RELAY_STOPPED:
        OnRelayStopped();
        return 0;

    case WM_RELAY_TARGET_CONNECTING:
        OnRelayTargetConnecting();
        return 0;

    case WM_PARSE_TARGET_COMBO:
        DoParseComboParts();
        return 0;

    case WM_DESTROY:
        if (m_network) m_network->StopRelay();
        if (m_hImgListLog) ImageList_Destroy(m_hImgListLog);
        if (m_hTooltip) { DestroyWindow(m_hTooltip); m_hTooltip = nullptr; }
        if (m_hFontMono) { DeleteObject(m_hFontMono); m_hFontMono = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---- OnCreate --------------------------------------------------

void RelayMainWindow::OnCreate(HWND hWnd)
{
    HINSTANCE hI = m_hInst;

    // Create monospace font for diagnostics
    m_hFontMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    // ---- Row 0: config labels + edits ----
    int y0 = 10;
    MakeLabel(hWnd, hI, L"Listen Port:", 10, y0 + 2, 75, 18);
    m_hEditListenPort = MakeEdit(hWnd, hI, IDC_EDIT_LISTEN_PORT, 88, y0, 55, 22, L"9001");

    MakeLabel(hWnd, hI, L"Target:", 155, y0 + 2, 45, 18);
    m_hComboTarget = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        200, y0, 155, 200, hWnd, (HMENU)(INT_PTR)IDC_COMBO_TARGET, hI, nullptr);
    SetWindowTextW(m_hComboTarget, L"127.0.0.1");
    MakeLabel(hWnd, hI, L":", 358, y0 + 2, 8, 18);
    m_hEditTargetPort = MakeEdit(hWnd, hI, IDC_EDIT_TARGET_PORT, 368, y0, 55, 22, L"9002");

    // Load target history and populate combo
    m_targetHistory = Utils::LoadHistory(L"TcpRelay");
    PopulateTargetCombo();
    DoParseComboParts();

    MakeLabel(hWnd, hI, L"Protocol:", 435, y0 + 2, 55, 18);
    m_hComboProtocol = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        492, y0, 70, 200, hWnd, (HMENU)IDC_COMBO_PROTOCOL, hI, nullptr);
    SendMessageW(m_hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"TCP");
    SendMessageW(m_hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"UDP");
    SendMessageW(m_hComboProtocol, CB_SETCURSEL, 0, 0);

    MakeLabel(hWnd, hI, L"Timeout:", 575, y0 + 2, 52, 18);
    m_hEditTimeout  = MakeEdit(hWnd, hI, IDC_EDIT_TIMEOUT, 628, y0, 35, 22, L"5");
    MakeLabel(hWnd, hI, L"s", 666, y0 + 2, 10, 18);
    m_hChkNoTimeout = MakeCheckbox(hWnd, hI, IDC_CHK_NO_TIMEOUT, 680, y0 + 2, 90, 18, L"No timeout", false);

    // ---- Row 1: Start/Stop + Status + Connection indicators ----
    int y1 = y0 + 28;
    m_hBtnStart = MakeButton(hWnd, hI, IDC_BTN_START, 10, y1, 90, 24, L"Start Relay");
    m_hBtnStop  = MakeButton(hWnd, hI, IDC_BTN_STOP, 108, y1, 90, 24, L"Stop Relay");
    EnableWindow(m_hBtnStop, FALSE);

    MakeLabel(hWnd, hI, L"Status:", 210, y1 + 4, 42, 18);
    m_hStaticStatus = CreateWindowExW(0, L"STATIC", L"Idle",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        255, y1 + 4, 300, 18, hWnd, (HMENU)IDC_STATIC_STATUS, hI, nullptr);

    // Connection status indicators (colored text)
    m_hStaticClientInd = CreateWindowExW(0, L"STATIC", L"\u25CF Client: -",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        560, y1 + 4, 250, 18, hWnd, (HMENU)IDC_STATIC_CLIENT_IND, hI, nullptr);
    m_hStaticTargetInd = CreateWindowExW(0, L"STATIC", L"\u25CF Target: -",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        820, y1 + 4, 250, 18, hWnd, (HMENU)IDC_STATIC_TARGET_IND, hI, nullptr);

    // ---- Middle: C2T and T2C panels ----
    int yMid = y1 + 32;

    // C2T panel
    m_hGrpC2T = CreateWindowExW(0, L"BUTTON", L"Client \u2192 Target",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 10, 10, hWnd, (HMENU)IDC_GRP_C2T, hI, nullptr);

    m_hChkAutoC2T    = MakeCheckbox(hWnd, hI, IDC_CHK_AUTO_C2T, 0, 0, 110, 20, L"Auto-forward", true);
    m_hBtnFwdC2T     = MakeButton(hWnd, hI, IDC_BTN_FWD_C2T, 0, 0, 60, 22, L"Forward");
    m_hBtnDropC2T    = MakeButton(hWnd, hI, IDC_BTN_DROP_C2T, 0, 0, 50, 22, L"Drop");
    m_hStaticBufC2T  = CreateWindowExW(0, L"STATIC", L"Buffer: 0 B, 0 pkts",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 180, 16, hWnd, (HMENU)IDC_STATIC_BUF_C2T, hI, nullptr);
    EnableWindow(m_hBtnFwdC2T, FALSE);
    EnableWindow(m_hBtnDropC2T, FALSE);

    // C2T HexView (hidden by default) + text view (shown by default) + hex toggle
    m_hHexC2T = HexView::Create(hWnd, 0, 0, 10, 10, IDC_STATIC_HEX_C2T, hI);
    ShowWindow(m_hHexC2T, SW_HIDE);  // text view is default
    m_hEditTextC2T = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 10, 10, hWnd, (HMENU)IDC_EDIT_TEXT_C2T, hI, nullptr);
    m_hChkHexC2T = MakeCheckbox(hWnd, hI, IDC_CHK_HEX_C2T, 0, 0, 80, 20, L"Hex View", false);

    // T2C panel
    m_hGrpT2C = CreateWindowExW(0, L"BUTTON", L"Target \u2192 Client",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 10, 10, hWnd, (HMENU)IDC_GRP_T2C, hI, nullptr);

    m_hChkAutoT2C    = MakeCheckbox(hWnd, hI, IDC_CHK_AUTO_T2C, 0, 0, 110, 20, L"Auto-forward", true);
    m_hBtnFwdT2C     = MakeButton(hWnd, hI, IDC_BTN_FWD_T2C, 0, 0, 60, 22, L"Forward");
    m_hBtnDropT2C    = MakeButton(hWnd, hI, IDC_BTN_DROP_T2C, 0, 0, 50, 22, L"Drop");
    m_hStaticBufT2C  = CreateWindowExW(0, L"STATIC", L"Buffer: 0 B, 0 pkts",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 180, 16, hWnd, (HMENU)IDC_STATIC_BUF_T2C, hI, nullptr);
    EnableWindow(m_hBtnFwdT2C, FALSE);
    EnableWindow(m_hBtnDropT2C, FALSE);

    // T2C HexView (hidden by default) + text view (shown by default) + hex toggle
    m_hHexT2C = HexView::Create(hWnd, 0, 0, 10, 10, IDC_STATIC_HEX_T2C, hI);
    ShowWindow(m_hHexT2C, SW_HIDE);  // text view is default
    m_hEditTextT2C = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 10, 10, hWnd, (HMENU)IDC_EDIT_TEXT_T2C, hI, nullptr);
    m_hChkHexT2C = MakeCheckbox(hWnd, hI, IDC_CHK_HEX_T2C, 0, 0, 80, 20, L"Hex View", false);

    // ---- Bottom: Tab Control ----
    m_hTab = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 10, 10, hWnd, (HMENU)IDC_TAB_BOTTOM, hI, nullptr);

    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"Log";
    TabCtrl_InsertItem(m_hTab, 0, &ti);
    ti.pszText = (LPWSTR)L"Socket Diagnostics";
    TabCtrl_InsertItem(m_hTab, 1, &ti);
    ti.pszText = (LPWSTR)L"Statistics";
    TabCtrl_InsertItem(m_hTab, 2, &ti);

    // Log filter controls (inside tab content area)
    m_hComboFilter = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 100, 200, hWnd, (HMENU)IDC_COMBO_LOGFILTER, hI, nullptr);
    const wchar_t* filters[] = { L"All", L"Data only", L"Events", L"Errors" };
    for (auto f : filters) SendMessageW(m_hComboFilter, CB_ADDSTRING, 0, (LPARAM)f);
    SendMessageW(m_hComboFilter, CB_SETCURSEL, 0, 0);
    m_hBtnClearLog = MakeButton(hWnd, hI, IDC_BTN_CLEARLOG, 0, 0, 70, 22, L"Clear Log");

    // Log ListView (virtual mode)
    // NOTE: No WS_CLIPSIBLINGS — the list sits inside the tab control's rect.
    // With WS_CLIPSIBLINGS the tab (a sibling) would be clipped from the list's
    // paint region, making the entire update region empty and the list invisible.
    m_hListLog = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_OWNERDATA | LVS_NOSORTHEADER,
        0, 0, 10, 10, hWnd, (HMENU)IDC_LIST_LOG, hI, nullptr);

    ListView_SetExtendedListViewStyle(m_hListLog,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Log icons (ImageList)
    {
        m_hImgListLog = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, LOG_TYPE_COUNT, 0);
        struct { COLORREF color; const wchar_t* glyph; } iconDefs[] = {
            { RGB(128,128,128), L"\x2139" }, // 0 Info (gray info)
            { RGB(0,160,0),     L"\x25CF" }, // 1 ClientConn (green circle)
            { RGB(160,0,0),     L"\x25CF" }, // 2 ClientDisconn (red circle)
            { RGB(0,120,200),   L"\x25CF" }, // 3 TargetConn (blue circle)
            { RGB(200,100,0),   L"\x25CF" }, // 4 TargetDisconn (orange circle)
            { RGB(0,100,200),   L"\x25B6" }, // 5 DataC2T (blue right arrow)
            { RGB(0,160,80),    L"\x25C0" }, // 6 DataT2C (green left arrow)
            { RGB(200,0,0),     L"\x26A0" }, // 7 Error (red warning)
            { RGB(100,0,180),   L"\x25B6" }, // 8 Listening (purple play)
            { RGB(100,100,100), L"\x25A0" }, // 9 Stopped (gray square)
        };
        HDC hdc = GetDC(hWnd);
        HFONT hIconFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Symbol");
        for (auto& def : iconDefs) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, 16, 16);
            SelectObject(mem, bmp);
            RECT r = { 0, 0, 16, 16 };
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
    struct { const wchar_t* t; int w; } cols[] = {
        { L"#",         75 },
        { L"Time",      90 },
        { L"Type",      95 },
        { L"Dir",       50 },
        { L"Size",      70 },
        { L"Preview",   320 },
        { L"Status",    80 },
    };
    for (int i = 0; i < _countof(cols); ++i) {
        col.pszText = (LPWSTR)cols[i].t;
        col.cx = cols[i].w;
        ListView_InsertColumn(m_hListLog, i, &col);
    }

    // Statistics panel (all created at dummy positions; hidden initially)
    m_hStatC2TBytes  = MakeLabel(hWnd, hI, L"C\u2192T bytes: 0", 0, 0, 10, 10);
    m_hStatC2TPkts   = MakeLabel(hWnd, hI, L"C\u2192T packets: 0", 0, 0, 10, 10);
    m_hStatT2CBytes  = MakeLabel(hWnd, hI, L"T\u2192C bytes: 0", 0, 0, 10, 10);
    m_hStatT2CPkts   = MakeLabel(hWnd, hI, L"T\u2192C packets: 0", 0, 0, 10, 10);
    m_hStatClients   = MakeLabel(hWnd, hI, L"Client: -", 0, 0, 10, 10);
    m_hStatTarget    = MakeLabel(hWnd, hI, L"Target: -", 0, 0, 10, 10);
    m_hBtnClearStats = MakeButton(hWnd, hI, IDC_BTN_CLEARSTATS, 0, 0, 80, 22, L"Clear Stats");

    ShowWindow(m_hStatC2TBytes, SW_HIDE);
    ShowWindow(m_hStatC2TPkts, SW_HIDE);
    ShowWindow(m_hStatT2CBytes, SW_HIDE);
    ShowWindow(m_hStatT2CPkts, SW_HIDE);
    ShowWindow(m_hStatClients, SW_HIDE);
    ShowWindow(m_hStatTarget, SW_HIDE);
    ShowWindow(m_hBtnClearStats, SW_HIDE);

    // Diagnostics panel (hidden initially)
    m_hStaticDiagInfo = CreateWindowExW(0, L"STATIC", L"Start relay to view diagnostics",
        WS_CHILD | SS_LEFT | SS_NOPREFIX,
        0, 0, 10, 10, hWnd, (HMENU)IDC_STATIC_DIAG_INFO, hI, nullptr);
    SendMessageW(m_hStaticDiagInfo, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    m_hBtnRefreshDiag = MakeButton(hWnd, hI, IDC_BTN_REFRESHDIAG, 0, 0, 80, 22, L"Refresh");
    ShowWindow(m_hStaticDiagInfo, SW_HIDE);
    ShowWindow(m_hBtnRefreshDiag, SW_HIDE);

    // Default font
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    EnumChildWindows(hWnd, [](HWND h, LPARAM lp) -> BOOL {
        SendMessageW(h, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)hFont);
    // Re-apply monospace font to diagnostics and text views
    SendMessageW(m_hStaticDiagInfo, WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SendMessageW(m_hEditTextC2T,    WM_SETFONT, (WPARAM)m_hFontMono, TRUE);
    SendMessageW(m_hEditTextT2C,    WM_SETFONT, (WPARAM)m_hFontMono, TRUE);

    // Initial layout from WM_CREATE
    RECT rc; GetClientRect(hWnd, &rc);
    OnSize(hWnd, rc.right, rc.bottom);
    SwitchBottomTab(0); // ensure log tab is visible by default
    CreateTooltips();
}

// ---- OnSize ----------------------------------------------------

void RelayMainWindow::OnSize(HWND hWnd, int cx, int cy)
{
    if (cx < 200 || cy < 200) return;

    const int M = 8;
    const int ROW_H = 22;
    const int y0 = 10;
    const int y1 = y0 + 28;   // Start/Stop row
    const int yMid = y1 + 32; // panels start

    // Middle panels split the available height 60/40 with the tab panel
    int availH = cy - yMid - M;
    // Middle gets at least 160px, tab gets at least 200px
    int midH = std::max(160, availH * 6 / 10);
    if (availH - midH - M < 200) midH = std::max(160, availH - 200 - M);
    int hexH = midH - 46;
    if (hexH < 40) hexH = 40;

    int panelW = (cx - 3 * M) / 2;
    if (panelW < 80) panelW = 80;
    int c2tX = M;
    int t2cX = M + panelW + M;

    HDWP dwp = BeginDeferWindowPos(45);
    auto Move = [&](HWND h, int x, int y, int w, int hh) {
        if (h && dwp) dwp = DeferWindowPos(dwp, h, nullptr, x, y, w, hh,
            SWP_NOZORDER | SWP_NOACTIVATE);
    };

    // Connection indicators in row 1 (responsive, after status label)
    int indW = std::max(10, (cx - 560) / 2 - 10);
    Move(m_hStaticClientInd, 560,           y1 + 4, indW, 18);
    Move(m_hStaticTargetInd, 560 + indW + 10, y1 + 4, indW, 18);

    // --- C2T panel ---
    Move(m_hGrpC2T,       c2tX,          yMid,      panelW, midH);
    // Toolbar row: AutoFwd | Forward | Drop | [Hex View chk right-aligned]
    Move(m_hChkAutoC2T,   c2tX + 10,     yMid + 18, 110, 20);
    Move(m_hBtnFwdC2T,    c2tX + 125,    yMid + 17, 60,  ROW_H);
    Move(m_hBtnDropC2T,   c2tX + 190,    yMid + 17, 50,  ROW_H);
    int hexChkX_c2t = c2tX + panelW - 90;
    Move(m_hChkHexC2T,    hexChkX_c2t,   yMid + 17, 82,  ROW_H);
    Move(m_hStaticBufC2T, c2tX + 248,    yMid + 20, hexChkX_c2t - c2tX - 258, 16);
    // Data area: text or hex view (same rect, toggled)
    Move(m_hHexC2T,       c2tX + 5,      yMid + 44, panelW - 10, hexH);
    Move(m_hEditTextC2T,  c2tX + 5,      yMid + 44, panelW - 10, hexH);

    // --- T2C panel ---
    Move(m_hGrpT2C,       t2cX,          yMid,      panelW, midH);
    Move(m_hChkAutoT2C,   t2cX + 10,     yMid + 18, 110, 20);
    Move(m_hBtnFwdT2C,    t2cX + 125,    yMid + 17, 60,  ROW_H);
    Move(m_hBtnDropT2C,   t2cX + 190,    yMid + 17, 50,  ROW_H);
    int hexChkX_t2c = t2cX + panelW - 90;
    Move(m_hChkHexT2C,    hexChkX_t2c,   yMid + 17, 82,  ROW_H);
    Move(m_hStaticBufT2C, t2cX + 248,    yMid + 20, hexChkX_t2c - t2cX - 258, 16);
    Move(m_hHexT2C,       t2cX + 5,      yMid + 44, panelW - 10, hexH);
    Move(m_hEditTextT2C,  t2cX + 5,      yMid + 44, panelW - 10, hexH);

    // --- Bottom tab area ---
    int yBot = yMid + midH + M;
    int botH = cy - yBot - M;
    if (botH < 120) botH = 120;
    Move(m_hTab, M, yBot, cx - 2 * M, botH);

    // Compute content rectangle inside the tab control
    RECT tabRc = { 0, 0, cx - 2 * M, botH };
    TabCtrl_AdjustRect(m_hTab, FALSE, &tabRc);
    int tx = M + tabRc.left;
    int ty = yBot + tabRc.top;
    int tw = tabRc.right - tabRc.left;
    int th = tabRc.bottom - tabRc.top;

    // Filter row (inside tab content, top) - no separate label, matches other apps
    Move(m_hComboFilter,  tx + 2,   ty + 2, 120, 200);
    Move(m_hBtnClearLog,  tx + 126, ty + 2, 74,  ROW_H);

    // Log ListView (below filter row)
    int listY = ty + ROW_H + 6;
    int listH = th - ROW_H - 8;
    if (listH < 40) listH = 40;
    Move(m_hListLog, tx + 2, listY, tw - 4, listH);

    // Statistics (same content area, shown when tab == 2)
    Move(m_hStatC2TBytes,  tx + 10, ty + 10,  tw - 20, 18);
    Move(m_hStatC2TPkts,   tx + 10, ty + 30,  tw - 20, 18);
    Move(m_hStatT2CBytes,  tx + 10, ty + 50,  tw - 20, 18);
    Move(m_hStatT2CPkts,   tx + 10, ty + 70,  tw - 20, 18);
    Move(m_hStatClients,   tx + 10, ty + 100, tw - 20, 18);
    Move(m_hStatTarget,    tx + 10, ty + 120, tw - 20, 18);
    Move(m_hBtnClearStats, tx + 10, ty + 150, 90,      ROW_H);

    // Diagnostics (same content area, shown when tab == 1)
    Move(m_hStaticDiagInfo, tx + 10, ty + 10,       tw - 20, th - 50);
    Move(m_hBtnRefreshDiag, tx + 10, ty + th - 34,  90,      ROW_H);

    if (dwp) EndDeferWindowPos(dwp);
    RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    // Re-apply tab visibility so that controls get a fresh SW_SHOW/SW_HIDE after
    // every resize.  Without this, the tab's WS_CLIPSIBLINGS paint can race
    // against the repositioned list and leave a stale clip region.
    SwitchBottomTab(m_bottomTab);
}

// ---- OnCommand -------------------------------------------------

void RelayMainWindow::OnCommand(HWND hWnd, int id, int code, HWND hCtl)
{
    switch (id)
    {
    case IDC_BTN_START:      DoStartRelay(); break;
    case IDC_BTN_STOP:       DoStopRelay();  break;
    case IDC_BTN_FWD_C2T:    DoForwardC2T(); break;
    case IDC_BTN_DROP_C2T:   DoDropC2T();    break;
    case IDC_BTN_FWD_T2C:    DoForwardT2C(); break;
    case IDC_BTN_DROP_T2C:   DoDropT2C();    break;
    case IDC_BTN_CLEARLOG:   ClearLog();     break;

    case IDC_CHK_NO_TIMEOUT:
        EnableWindow(m_hEditTimeout,
            SendMessageW(m_hChkNoTimeout, BM_GETCHECK, 0, 0) == BST_UNCHECKED);
        break;

    case IDC_CHK_HEX_C2T:
        m_hexModeC2T = (SendMessageW(m_hChkHexC2T, BM_GETCHECK, 0, 0) == BST_CHECKED);
        RefreshC2TView();
        break;
    case IDC_CHK_HEX_T2C:
        m_hexModeT2C = (SendMessageW(m_hChkHexT2C, BM_GETCHECK, 0, 0) == BST_CHECKED);
        RefreshT2CView();
        break;
    case IDC_BTN_CLEARSTATS:
        m_bytesC2T = m_bytesT2C = m_packetsC2T = m_packetsT2C = 0;
        UpdateStats();
        break;

    case IDC_BTN_REFRESHDIAG:
        RefreshDiagnostics();
        break;

    case IDC_CHK_AUTO_C2T:
        if (m_network) {
            bool on = (SendMessageW(m_hChkAutoC2T, BM_GETCHECK, 0, 0) == BST_CHECKED);
            m_network->SetAutoForwardC2T(on);
        }
        break;
    case IDC_CHK_AUTO_T2C:
        if (m_network) {
            bool on = (SendMessageW(m_hChkAutoT2C, BM_GETCHECK, 0, 0) == BST_CHECKED);
            m_network->SetAutoForwardT2C(on);
        }
        break;

    case IDC_COMBO_LOGFILTER:
        if (code == CBN_SELCHANGE) {
            m_filterIndex = (int)SendMessageW(m_hComboFilter, CB_GETCURSEL, 0, 0);
            RefreshLogView();
        }
        break;

    case IDC_COMBO_TARGET:
        if (code == CBN_SELCHANGE) OnTargetComboSelChange();
        break;
    }
}

// ---- OnNotify --------------------------------------------------

LRESULT RelayMainWindow::OnNotify(HWND hWnd, NMHDR* nm)
{
    if (nm->hwndFrom == m_hTab && nm->code == TCN_SELCHANGE)
    {
        int tab = TabCtrl_GetCurSel(m_hTab);
        SwitchBottomTab(tab);
        return 0;
    }

    if (nm->hwndFrom == m_hListLog && nm->code == LVN_GETDISPINFOW)
    {
        NMLVDISPINFOW* di = (NMLVDISPINFOW*)nm;
        int row = di->item.iItem;
        if (row < 0 || row >= (int)m_logVisible.size()) return 0;

        const LogEntry& e = m_log[m_logVisible[row]];
        static wchar_t buf[512];

        if (di->item.mask & LVIF_IMAGE)
            di->item.iImage = (int)e.type;

        if (di->item.mask & LVIF_TEXT)
        {
            static const wchar_t* typeNames[] = {
                L"Info", L"Client Conn", L"Client Disc",
                L"Target Conn", L"Target Disc",
                L"Data C\u2192T", L"Data T\u2192C",
                L"Error", L"Listening", L"Stopped"
            };

            switch (di->item.iSubItem)
            {
            case 0: // #
                swprintf_s(buf, L"%d", e.seq);
                di->item.pszText = buf;
                break;
            case 1: // Time
                di->item.pszText = (LPWSTR)e.time.c_str();
                break;
            case 2: // Type
            {
                int ti = (int)e.type;
                di->item.pszText = (LPWSTR)(ti < _countof(typeNames) ? typeNames[ti] : L"?");
                break;
            }
            case 3: // Direction
                di->item.pszText = (LPWSTR)e.direction.c_str();
                break;
            case 4: // Size
                if (e.size > 0) {
                    swprintf_s(buf, L"%zu B", e.size);
                    di->item.pszText = buf;
                } else {
                    di->item.pszText = (LPWSTR)L"-";
                }
                break;
            case 5: // Preview
                di->item.pszText = (LPWSTR)e.preview.c_str();
                break;
            case 6: // Status
                if (e.type == LogType::DataC2T || e.type == LogType::DataT2C)
                    di->item.pszText = (LPWSTR)(e.forwarded ? L"Fwd" : L"Buffered");
                else
                    di->item.pszText = (LPWSTR)L"-";
                break;
            }
        }
        return 0;
    }
    return 0;
}

// ---- OnContextMenuLog ------------------------------------------

void RelayMainWindow::OnContextMenuLog(HWND hWnd, int x, int y)
{
    int sel = ListView_GetNextItem(m_hListLog, -1, LVNI_SELECTED);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | (sel >= 0 ? 0 : MF_GRAYED), IDM_LOG_COPY, L"Copy Entry");
    AppendMenuW(hMenu, MF_STRING, IDM_LOG_CLEAR, L"Clear Log");
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == IDM_LOG_COPY && sel >= 0 && sel < (int)m_logVisible.size()) {
        const auto& e = m_log[m_logVisible[sel]];
        std::wstring txt = std::to_wstring(e.seq) + L"\t" + e.time + L"\t" +
                           e.direction + L"\t" + std::to_wstring(e.size) + L"\t" + e.preview;
        Utils::CopyToClipboard(hWnd, txt);
    } else if (cmd == IDM_LOG_CLEAR) {
        ClearLog();
    }
}

// ---- Log management (virtual ListView) -------------------------

static bool PassesFilter(const RelayMainWindow::LogType& type, int filterIndex)
{
    // 0=All, 1=Data only, 2=Events, 3=Errors
    if (filterIndex == 0) return true;
    if (filterIndex == 1) return (type == RelayMainWindow::LogType::DataC2T ||
                                  type == RelayMainWindow::LogType::DataT2C);
    if (filterIndex == 2) return (type != RelayMainWindow::LogType::DataC2T &&
                                  type != RelayMainWindow::LogType::DataT2C &&
                                  type != RelayMainWindow::LogType::Error);
    if (filterIndex == 3) return (type == RelayMainWindow::LogType::Error);
    return true;
}

void RelayMainWindow::AddLogEntry(LogType type, const std::wstring& dir, size_t size,
                                   const std::wstring& preview, bool forwarded)
{
    LogEntry e;
    e.seq       = ++m_logSeq;
    e.time      = Utils::NowTimestamp();
    e.type      = type;
    e.direction = dir;
    e.size      = size;
    e.preview   = preview;
    e.forwarded = forwarded;

    int idx = (int)m_log.size();
    m_log.push_back(std::move(e));

    if (PassesFilter(type, m_filterIndex)) {
        m_logVisible.push_back(idx);
        int count = (int)m_logVisible.size();
        ListView_SetItemCountEx(m_hListLog, count, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        ListView_RedrawItems(m_hListLog, count - 1, count - 1);
        ListView_EnsureVisible(m_hListLog, count - 1, FALSE);
    }
}

void RelayMainWindow::RefreshLogView()
{
    m_logVisible.clear();
    for (int i = 0; i < (int)m_log.size(); ++i) {
        if (PassesFilter(m_log[i].type, m_filterIndex))
            m_logVisible.push_back(i);
    }
    ListView_SetItemCountEx(m_hListLog, (int)m_logVisible.size(), 0);
    InvalidateRect(m_hListLog, nullptr, TRUE);
}

void RelayMainWindow::ClearLog()
{
    m_log.clear();
    m_logVisible.clear();
    m_logSeq = 0;
    ListView_SetItemCountEx(m_hListLog, 0, 0);
    InvalidateRect(m_hListLog, nullptr, TRUE);
}

// ---- Connection history helpers --------------------------------

void RelayMainWindow::PopulateTargetCombo()
{
    SendMessageW(m_hComboTarget, CB_RESETCONTENT, 0, 0);
    for (auto& e : m_targetHistory)
        SendMessageW(m_hComboTarget, CB_ADDSTRING, 0, (LPARAM)e.c_str());
}

void RelayMainWindow::OnTargetComboSelChange()
{
    // Defer parsing: Windows overwrites the combo edit text AFTER
    // CBN_SELCHANGE returns, so we post a message to parse later.
    PostMessageW(m_hWnd, WM_PARSE_TARGET_COMBO, 0, 0);
}

void RelayMainWindow::DoParseComboParts()
{
    std::wstring text = Utils::GetWindowText(m_hComboTarget);
    auto pos = text.rfind(L':');
    if (pos != std::wstring::npos && pos > 0 && pos < text.size() - 1) {
        std::wstring host = text.substr(0, pos);
        std::wstring port = text.substr(pos + 1);
        int p = 0;
        if (Utils::IsValidPort(port, p)) {
            SetWindowTextW(m_hComboTarget, host.c_str());
            SetWindowTextW(m_hEditTargetPort, port.c_str());
        }
    }
}

// ---- Actions ---------------------------------------------------

void RelayMainWindow::DoStartRelay()
{
    // Validate listen port
    std::wstring listenPortW = Utils::GetWindowText(GetDlgItem(m_hWnd, IDC_EDIT_LISTEN_PORT));
    int listenPort = 0;
    if (!Utils::IsValidPort(listenPortW, listenPort)) {
        MessageBoxW(m_hWnd, L"Please enter a valid listen port (1\u201365535).", L"Invalid port", MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_LISTEN_PORT));
        return;
    }

    // Parse "host:port" from combo if the user typed/pasted a combined value
    std::wstring targetW = Utils::GetWindowText(m_hComboTarget);
    auto colon = targetW.rfind(L':');
    if (colon != std::wstring::npos && colon > 0 && colon < targetW.size() - 1) {
        std::wstring maybePort = targetW.substr(colon + 1);
        int p = 0;
        if (Utils::IsValidPort(maybePort, p)) {
            targetW = targetW.substr(0, colon);
            SetWindowTextW(m_hComboTarget, targetW.c_str());
            SetWindowTextW(GetDlgItem(m_hWnd, IDC_EDIT_TARGET_PORT), maybePort.c_str());
        }
    }

    // Validate target host/IP
    if (!Utils::IsValidHostOrIP(targetW)) {
        MessageBoxW(m_hWnd, L"Please enter a valid target hostname or IP address.", L"Invalid target", MB_ICONWARNING);
        SetFocus(m_hComboTarget);
        return;
    }
    std::string targetIP = Utils::ToUTF8(targetW);

    // Validate target port
    std::wstring targetPortW = Utils::GetWindowText(GetDlgItem(m_hWnd, IDC_EDIT_TARGET_PORT));
    int targetPort = 0;
    if (!Utils::IsValidPort(targetPortW, targetPort)) {
        MessageBoxW(m_hWnd, L"Please enter a valid target port (1\u201365535).", L"Invalid port", MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_TARGET_PORT));
        return;
    }

    bool noTimeout = (SendMessageW(m_hChkNoTimeout, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int timeout    = noTimeout ? 0 : Utils::GetDlgInt(m_hWnd, IDC_EDIT_TIMEOUT, 5);
    bool udpMode   = (SendMessageW(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);

    // Check if listen port is already in use
    if (!Utils::IsPortAvailable(listenPort, udpMode)) {
        wchar_t msg[128];
        swprintf_s(msg, L"Listen port %d is already in use.\nPlease choose a different port.", listenPort);
        MessageBoxW(m_hWnd, msg, L"Port busy", MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_LISTEN_PORT));
        return;
    }

    m_network = std::make_unique<RelayNetwork>(m_hWnd);
    if (!m_network->StartRelay(listenPort, targetIP, targetPort, timeout, udpMode)) {
        MessageBoxW(m_hWnd, L"Failed to start relay.", L"Relay", MB_ICONERROR);
        m_network.reset();
        return;
    }

    SetRelayUI(true);

    // Save target to history
    {
        std::wstring entry = Utils::ToWide(targetIP) + L":" + std::to_wstring(targetPort);
        Utils::SaveHistory(L"TcpRelay", entry, m_targetHistory);
        PopulateTargetCombo();
        SetWindowTextW(m_hComboTarget, Utils::ToWide(targetIP).c_str());
    }

    std::wstring proto = udpMode ? L"UDP" : L"TCP";
    AddLogEntry(LogType::Info, L"-", 0,
                L"[" + proto + L"] Starting relay: listen " + std::to_wstring(listenPort) +
                L" \u2192 " + Utils::ToWide(targetIP) + L":" + std::to_wstring(targetPort),
                false);
}

void RelayMainWindow::DoStopRelay()
{
    if (m_network) {
        m_network->StopRelay();
        m_network.reset();
    }
    SetRelayUI(false);
}

void RelayMainWindow::DoForwardC2T()
{
    if (m_bufferC2T.empty() || !m_network) return;

    bool ok = m_network->ForwardToTarget(m_bufferC2T);
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";

    if (ok) {
        // Build preview
        std::wstring preview = L"[" + proto + L"] Manual fwd: ";
        size_t sz = m_bufferC2T.size();
        for (size_t i = 0; i < std::min(sz, (size_t)32); ++i) {
            wchar_t h[4]; swprintf_s(h, L"%02X ", m_bufferC2T[i]);
            preview += h;
        }
        if (sz > 32) preview += L"...";
        AddLogEntry(LogType::DataC2T, L"C\u2192T", sz, preview, true);
        m_bytesC2T += sz;
        m_packetsC2T++;
    }
    m_bufferC2T.clear();
    m_bufC2TPackets = 0;
    UpdateBufferLabel(true);
}

void RelayMainWindow::DoForwardT2C()
{
    if (m_bufferT2C.empty() || !m_network) return;

    bool ok = m_network->ForwardToClient(m_bufferT2C);
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";

    if (ok) {
        std::wstring preview = L"[" + proto + L"] Manual fwd: ";
        size_t sz = m_bufferT2C.size();
        for (size_t i = 0; i < std::min(sz, (size_t)32); ++i) {
            wchar_t h[4]; swprintf_s(h, L"%02X ", m_bufferT2C[i]);
            preview += h;
        }
        if (sz > 32) preview += L"...";
        AddLogEntry(LogType::DataT2C, L"T\u2192C", sz, preview, true);
        m_bytesT2C += sz;
        m_packetsT2C++;
    }
    m_bufferT2C.clear();
    m_bufT2CPackets = 0;
    UpdateBufferLabel(false);
}

void RelayMainWindow::DoDropC2T()
{
    if (m_bufferC2T.empty()) return;
    size_t sz = m_bufferC2T.size();
    m_bufferC2T.clear();
    m_bufC2TPackets = 0;
    UpdateBufferLabel(true);
    HexView::ClearData(m_hHexC2T);
    SetWindowTextW(m_hEditTextC2T, L"");
    AddLogEntry(LogType::Info, L"C\u2192T", sz, L"Dropped buffered data", false);
}

void RelayMainWindow::DoDropT2C()
{
    if (m_bufferT2C.empty()) return;
    size_t sz = m_bufferT2C.size();
    m_bufferT2C.clear();
    m_bufT2CPackets = 0;
    UpdateBufferLabel(false);
    HexView::ClearData(m_hHexT2C);
    SetWindowTextW(m_hEditTextT2C, L"");
    AddLogEntry(LogType::Info, L"T\u2192C", sz, L"Dropped buffered data", false);
}

// ---- View-mode refresh helpers --------------------------------

void RelayMainWindow::RefreshC2TView()
{
    if (m_hexModeC2T) {
        ShowWindow(m_hEditTextC2T, SW_HIDE);
        ShowWindow(m_hHexC2T,      SW_SHOW);
        HexView::SetData(m_hHexC2T, m_bufferC2T);
    } else {
        ShowWindow(m_hHexC2T,      SW_HIDE);
        ShowWindow(m_hEditTextC2T, SW_SHOW);
    }
}

void RelayMainWindow::RefreshT2CView()
{
    if (m_hexModeT2C) {
        ShowWindow(m_hEditTextT2C, SW_HIDE);
        ShowWindow(m_hHexT2C,      SW_SHOW);
        HexView::SetData(m_hHexT2C, m_bufferT2C);
    } else {
        ShowWindow(m_hHexT2C,      SW_HIDE);
        ShowWindow(m_hEditTextT2C, SW_SHOW);
    }
}

// Helper: append a packet as text to a read-only multiline edit
static void AppendPacketText(HWND hEdit, const BYTE* data, int len)
{
    // Prevent unbounded growth
    if (GetWindowTextLengthW(hEdit) > 80000)
        SetWindowTextW(hEdit, L"[Output cleared - exceeded display limit]\r\n");

    std::wstring s;
    s.reserve(len + 32);
    wchar_t hdr[48];
    swprintf_s(hdr, L"[%d bytes] ", len);
    s += hdr;
    for (int i = 0; i < len; i++) {
        BYTE b = data[i];
        if      (b == '\r') continue;
        else if (b == '\n') s += L"\r\n";
        else if (b == '\t') s += L'\t';
        else if (b >= 0x20 && b < 0x7F) s += (wchar_t)b;
        else s += L'.';
    }
    s += L"\r\n";
    int textLen = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, textLen, textLen);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
}

void RelayMainWindow::SetRelayUI(bool listening)
{
    m_isListening = listening;
    EnableWindow(m_hBtnStart, !listening);
    EnableWindow(m_hBtnStop,   listening);

    // Disable config while running
    EnableWindow(m_hEditListenPort, !listening);
    EnableWindow(m_hComboTarget,    !listening);
    EnableWindow(m_hEditTargetPort, !listening);
    EnableWindow(m_hComboProtocol,  !listening);
    EnableWindow(m_hChkNoTimeout,   !listening);
    // Only enable timeout edit if not listening AND not in no-timeout mode
    if (!listening) {
        bool noTimeout = (SendMessageW(m_hChkNoTimeout, BM_GETCHECK, 0, 0) == BST_CHECKED);
        EnableWindow(m_hEditTimeout, !noTimeout);
    } else {
        EnableWindow(m_hEditTimeout, FALSE);
    }

    if (!listening) {
        Utils::SetWindowText(m_hStaticStatus, L"Idle");
    }
}

void RelayMainWindow::UpdateBufferLabel(bool c2t)
{
    if (c2t) {
        std::wstring s = L"Buffer: " + Utils::FormatBytes(m_bufferC2T.size()) +
                         L", " + std::to_wstring(m_bufC2TPackets) + L" pkts";
        Utils::SetWindowText(m_hStaticBufC2T, s);
        EnableWindow(m_hBtnFwdC2T,  !m_bufferC2T.empty());
        EnableWindow(m_hBtnDropC2T, !m_bufferC2T.empty());
    } else {
        std::wstring s = L"Buffer: " + Utils::FormatBytes(m_bufferT2C.size()) +
                         L", " + std::to_wstring(m_bufT2CPackets) + L" pkts";
        Utils::SetWindowText(m_hStaticBufT2C, s);
        EnableWindow(m_hBtnFwdT2C,  !m_bufferT2C.empty());
        EnableWindow(m_hBtnDropT2C, !m_bufferT2C.empty());
    }
}

void RelayMainWindow::UpdateStats()
{
    Utils::SetWindowText(m_hStatC2TBytes, L"C\u2192T bytes forwarded: " + Utils::FormatBytes(m_bytesC2T));
    Utils::SetWindowText(m_hStatC2TPkts,  L"C\u2192T packets: " + std::to_wstring(m_packetsC2T));
    Utils::SetWindowText(m_hStatT2CBytes, L"T\u2192C bytes forwarded: " + Utils::FormatBytes(m_bytesT2C));
    Utils::SetWindowText(m_hStatT2CPkts,  L"T\u2192C packets: " + std::to_wstring(m_packetsT2C));
    Utils::SetWindowText(m_hStatClients,  L"Client: " + (m_clientAddr.empty() ? L"-" : m_clientAddr));
    std::wstring tgt = Utils::GetWindowText(m_hComboTarget) + L":" + Utils::GetWindowText(m_hEditTargetPort);
    Utils::SetWindowText(m_hStatTarget,   L"Target: " + tgt);
}

// ============================================================
//  SwitchBottomTab - 0=Log, 1=Diagnostics, 2=Statistics
// ============================================================

void RelayMainWindow::SwitchBottomTab(int tab)
{
    m_bottomTab = tab;
    bool showLog   = (tab == 0);
    bool showDiag  = (tab == 1);
    bool showStats = (tab == 2);

    // Log controls
    ShowWindow(m_hListLog,     showLog ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hComboFilter, showLog ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnClearLog, showLog ? SW_SHOW : SW_HIDE);
    if (showLog) {
        // Ensure the list stays on top of its sibling tab control, and force a
        // full repaint so the content area isn't left blank after a resize.
        SetWindowPos(m_hListLog, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        InvalidateRect(m_hListLog, nullptr, TRUE);
    }

    // Diagnostics controls
    ShowWindow(m_hStaticDiagInfo,  showDiag ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnRefreshDiag,  showDiag ? SW_SHOW : SW_HIDE);
    if (showDiag) RefreshDiagnostics();

    // Statistics controls
    ShowWindow(m_hStatC2TBytes,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatC2TPkts,   showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatT2CBytes,  showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatT2CPkts,   showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatClients,   showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hStatTarget,    showStats ? SW_SHOW : SW_HIDE);
    ShowWindow(m_hBtnClearStats, showStats ? SW_SHOW : SW_HIDE);
    if (showStats) UpdateStats();
}

// ============================================================
//  RefreshDiagnostics - socket info for relay
// ============================================================

void RelayMainWindow::RefreshDiagnostics()
{
    if (!m_network) {
        Utils::SetWindowText(m_hStaticDiagInfo, L"Relay not started");
        return;
    }

    std::wstring info;
    std::wstring proto = m_network->GetProtocolStr();
    info += L"Protocol: " + proto + L"\r\n\r\n";

    // Listen socket info
    SOCKET lsock = m_network->GetListenSocket();
    if (lsock != INVALID_SOCKET) {
        sockaddr_in localAddr = {};
        int localLen = sizeof(localAddr);
        if (getsockname(lsock, (sockaddr*)&localAddr, &localLen) == 0) {
            wchar_t ip[64]; DWORD ipLen = 64;
            WSAAddressToStringW((sockaddr*)&localAddr, localLen, nullptr, ip, &ipLen);
            info += L"Listen: " + std::wstring(ip) + L"\r\n";
        }

        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(lsock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(lsock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);
        wchar_t buf[256];
        swprintf_s(buf, L"  SO_SNDBUF: %d B  SO_RCVBUF: %d B", sndbuf, rcvbuf);
        info += buf;
        info += L"\r\n\r\n";
    }

    // Client socket info
    SOCKET csock = m_network->GetClientSocket();
    if (csock != INVALID_SOCKET && m_network->IsClientConnected()) {
        sockaddr_in remoteAddr = {};
        int remoteLen = sizeof(remoteAddr);
        if (getpeername(csock, (sockaddr*)&remoteAddr, &remoteLen) == 0) {
            wchar_t ip[64]; DWORD ipLen = 64;
            WSAAddressToStringW((sockaddr*)&remoteAddr, remoteLen, nullptr, ip, &ipLen);
            info += L"Client: " + std::wstring(ip) + L"\r\n";
        }

        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(csock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(csock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);

        wchar_t buf[256];
        swprintf_s(buf, L"  SO_SNDBUF: %d B  SO_RCVBUF: %d B", sndbuf, rcvbuf);
        info += buf;

        if (!m_network->IsUdpMode()) {
            BOOL nodelay = FALSE; optLen = sizeof(BOOL);
            getsockopt(csock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);
            info += nodelay ? L"  Nagle: OFF" : L"  Nagle: ON";

            int mss = 0; optLen = sizeof(int);
            if (getsockopt(csock, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, &optLen) == 0 && mss > 0) {
                swprintf_s(buf, L"\r\n  MSS: %d B  MTU: ~%d B  Frame: ~%d B", mss, mss + 40, mss + 40 + 14);
                info += buf;
            }
        }
        info += L"\r\n\r\n";
    } else {
        info += L"Client: not connected\r\n\r\n";
    }

    // Target socket info
    SOCKET tsock = m_network->GetTargetSocket();
    if (tsock != INVALID_SOCKET && m_network->IsTargetConnected()) {
        sockaddr_in remoteAddr = {};
        int remoteLen = sizeof(remoteAddr);
        if (getpeername(tsock, (sockaddr*)&remoteAddr, &remoteLen) == 0) {
            wchar_t ip[64]; DWORD ipLen = 64;
            WSAAddressToStringW((sockaddr*)&remoteAddr, remoteLen, nullptr, ip, &ipLen);
            info += L"Target: " + std::wstring(ip) + L"\r\n";
        }

        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(tsock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(tsock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);

        wchar_t buf[256];
        swprintf_s(buf, L"  SO_SNDBUF: %d B  SO_RCVBUF: %d B", sndbuf, rcvbuf);
        info += buf;

        if (!m_network->IsUdpMode()) {
            BOOL nodelay = FALSE; optLen = sizeof(BOOL);
            getsockopt(tsock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);
            info += nodelay ? L"  Nagle: OFF" : L"  Nagle: ON";

            int mss = 0; optLen = sizeof(int);
            if (getsockopt(tsock, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, &optLen) == 0 && mss > 0) {
                swprintf_s(buf, L"\r\n  MSS: %d B  MTU: ~%d B  Frame: ~%d B", mss, mss + 40, mss + 40 + 14);
                info += buf;
            }
        }
        info += L"\r\n";
    } else {
        info += L"Target: not connected\r\n";
    }

    // Auto-forward status
    info += L"\r\nAuto-forward C\u2192T: ";
    info += m_network->GetAutoForwardC2T() ? L"ON" : L"OFF (paused)";
    info += L"\r\nAuto-forward T\u2192C: ";
    info += m_network->GetAutoForwardT2C() ? L"ON" : L"OFF (paused)";

    Utils::SetWindowText(m_hStaticDiagInfo, info);
}

// ---- Network event handlers ------------------------------------

void RelayMainWindow::OnRelayListening(int port)
{
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";
    Utils::SetWindowText(m_hStaticStatus,
        L"[" + proto + L"] Listening on port " + std::to_wstring(port) + L" ...");
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CC Target: connecting...");
    AddLogEntry(LogType::Listening, L"-", 0,
                L"[" + proto + L"] Listening on port " + std::to_wstring(port), false);
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayClientConn(const std::wstring& addr)
{
    m_clientAddr = addr;
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";
    bool targetOk = m_network && m_network->IsTargetConnected();
    std::wstring tgt = Utils::GetWindowText(m_hComboTarget) + L":" + Utils::GetWindowText(m_hEditTargetPort);
    Utils::SetWindowText(m_hStaticStatus,
        targetOk ? L"[" + proto + L"] Relaying: " + addr + L" \u2194 " + tgt
                 : L"[" + proto + L"] Client: " + addr + L" | Target: connecting...");
    Utils::SetWindowText(m_hStaticClientInd, L"\u25CF Client: " + addr);
    AddLogEntry(LogType::ClientConn, L"-", 0,
                L"[" + proto + L"] Client connected: " + addr, false);
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayClientDisconn()
{
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";
    std::wstring prevAddr = m_clientAddr;
    m_clientAddr.clear();
    bool targetOk = m_network && m_network->IsTargetConnected();
    Utils::SetWindowText(m_hStaticStatus,
        m_isListening ? (targetOk ? L"[" + proto + L"] Target connected, awaiting client..."
                                  : L"[" + proto + L"] Listening (client disconnected)...")
                      : L"Idle");
    Utils::SetWindowText(m_hStaticClientInd, L"\u25CB Client: disconnected");
    AddLogEntry(LogType::ClientDisconn, L"-", 0,
                L"[" + proto + L"] Client disconnected" + (prevAddr.empty() ? L"" : L": " + prevAddr),
                false);
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayTargetConn()
{
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";
    std::wstring tgt = Utils::GetWindowText(m_hComboTarget) + L":" + Utils::GetWindowText(m_hEditTargetPort);
    m_targetAddr = tgt;
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CF Target: " + tgt);
    if (m_clientAddr.empty())
        Utils::SetWindowText(m_hStaticStatus,
            L"[" + proto + L"] Target connected, awaiting client...");
    else
        Utils::SetWindowText(m_hStaticStatus,
            L"[" + proto + L"] Relaying: " + m_clientAddr + L" \u2194 " + tgt);
    AddLogEntry(LogType::TargetConn, L"-", 0,
                L"[" + proto + L"] Connected to target: " + tgt, false);
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayTargetDisconn()
{
    m_targetAddr.clear();
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";
    AddLogEntry(LogType::TargetDisconn, L"-", 0,
                L"[" + proto + L"] Target disconnected", false);
    // Relay auto-reconnects to target in new architecture
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CC Target: reconnecting...");
    if (m_clientAddr.empty())
        Utils::SetWindowText(m_hStaticStatus,
            L"[" + proto + L"] Listening | Target: reconnecting...");
    else
        Utils::SetWindowText(m_hStaticStatus,
            L"[" + proto + L"] Client: " + m_clientAddr + L" | Target: reconnecting...");
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayTargetConnecting()
{
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CC Target: connecting...");
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";
    if (m_clientAddr.empty())
        Utils::SetWindowText(m_hStaticStatus,
            L"[" + proto + L"] Listening | Target: connecting...");
    else
        Utils::SetWindowText(m_hStaticStatus,
            L"[" + proto + L"] Client: " + m_clientAddr + L" | Target: connecting...");
    UpdateTitle();
}

void RelayMainWindow::UpdateTitle()
{
    if (!m_isListening) {
        SetWindowTextW(m_hWnd, L"TCP Relay v" TCPTOOLS_VERSION);
        return;
    }
    std::wstring lp  = Utils::GetWindowText(m_hEditListenPort);
    std::wstring tip = Utils::GetWindowText(m_hComboTarget);
    std::wstring tp  = Utils::GetWindowText(m_hEditTargetPort);
    bool targetOk = m_network && m_network->IsTargetConnected();
    bool clientOk = m_network && m_network->IsClientConnected();
    std::wstring title = L"TCP Relay v" TCPTOOLS_VERSION L"  \u2013  :";
    title += lp + L" \u2192 " + tip + L":" + tp;
    if (clientOk && targetOk)
        title += L"  (Relaying)";
    else if (targetOk)
        title += L"  (Target connected)";
    else if (clientOk)
        title += L"  (Client connected)";
    else
        title += L"  (Listening)";
    SetWindowTextW(m_hWnd, title.c_str());
}

void RelayMainWindow::OnRelayClientData(const BYTE* data, int len, bool forwarded)
{
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";

    // Build preview
    std::wstring preview = L"[" + proto + L"] ";
    for (int i = 0; i < std::min(len, 32); ++i) {
        wchar_t h[4]; swprintf_s(h, L"%02X ", data[i]);
        preview += h;
    }
    if (len > 32) preview += L"...";

    // Update hex view and text view with latest data
    std::vector<BYTE> chunk(data, data + len);
    HexView::SetData(m_hHexC2T, chunk);
    AppendPacketText(m_hEditTextC2T, data, len);

    if (forwarded) {
        // Auto-forwarded - just log it
        m_bytesC2T += len;
        m_packetsC2T++;
        AddLogEntry(LogType::DataC2T, L"C\u2192T", (size_t)len, preview, true);
    } else {
        // Buffered (paused) - accumulate
        m_bufferC2T.insert(m_bufferC2T.end(), data, data + len);
        m_bufC2TPackets++;
        UpdateBufferLabel(true);
        // Show accumulated buffer in hex view
        HexView::SetData(m_hHexC2T, m_bufferC2T);
        AddLogEntry(LogType::DataC2T, L"C\u2192T", (size_t)len, preview, false);
    }
}

void RelayMainWindow::OnRelayTargetData(const BYTE* data, int len, bool forwarded)
{
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";

    std::wstring preview = L"[" + proto + L"] ";
    for (int i = 0; i < std::min(len, 32); ++i) {
        wchar_t h[4]; swprintf_s(h, L"%02X ", data[i]);
        preview += h;
    }
    if (len > 32) preview += L"...";

    // Update hex view and text view with latest data
    std::vector<BYTE> chunk(data, data + len);
    HexView::SetData(m_hHexT2C, chunk);
    AppendPacketText(m_hEditTextT2C, data, len);

    if (forwarded) {
        m_bytesT2C += len;
        m_packetsT2C++;
        AddLogEntry(LogType::DataT2C, L"T\u2192C", (size_t)len, preview, true);
    } else {
        m_bufferT2C.insert(m_bufferT2C.end(), data, data + len);
        m_bufT2CPackets++;
        UpdateBufferLabel(false);
        HexView::SetData(m_hHexT2C, m_bufferT2C);
        AddLogEntry(LogType::DataT2C, L"T\u2192C", (size_t)len, preview, false);
    }
}

void RelayMainWindow::OnRelayError(int err, const std::wstring& msg)
{
    AddLogEntry(LogType::Error, L"-", 0, L"Error " + std::to_wstring(err) + L": " + msg, false);
}

void RelayMainWindow::OnRelayStopped()
{
    SetRelayUI(false);   // sets m_isListening = false
    std::wstring proto = m_network ? m_network->GetProtocolStr() : L"TCP";
    AddLogEntry(LogType::Stopped, L"-", 0, L"[" + proto + L"] Relay stopped", false);
    Utils::SetWindowText(m_hStaticClientInd, L"\u25CB Client: -");
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CB Target: -");
    m_clientAddr.clear();
    m_targetAddr.clear();
    m_network.reset();
    UpdateTitle();       // shows "TCP Relay" (idle)
}

// ============================================================
//  CreateTooltips
// ============================================================

void RelayMainWindow::CreateTooltips()
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

    // ---- Config row ----
    tip(m_hEditListenPort, L"Port this relay listens on for incoming client connections.");
    tip(m_hComboTarget,   L"IP address or hostname of the upstream server. Select from recent targets or type a new one.");
    tip(m_hEditTargetPort, L"Port on the upstream server.");
    tip(m_hComboProtocol,  L"Protocol:\n  TCP \u2014 reliable, ordered stream relay.\n  UDP \u2014 connectionless datagram relay.");
    tip(m_hEditTimeout,    L"Connection timeout in seconds when connecting to the target server.");
    tip(m_hChkNoTimeout,   L"Disable timeout \u2014 the relay waits indefinitely when connecting to the target.");
    tip(m_hBtnStart,       L"Start the relay: begin listening for clients and forward to the target server.");
    tip(m_hBtnStop,        L"Stop the relay and close all active connections.");

    // ---- Client\u2192Target panel ----
    tip(m_hChkAutoC2T,     L"Auto-forward C\u2192T \u2014 Client\u2192Target data is forwarded immediately without buffering.\n"
                            L"Uncheck to manually inspect, forward, or drop each packet.");
    tip(m_hBtnFwdC2T,      L"Manually forward the buffered Client\u2192Target data to the target server.");
    tip(m_hBtnDropC2T,     L"Discard the buffered Client\u2192Target data. Nothing will be sent to the target.");
    tip(m_hChkHexC2T,      L"Show Client\u2192Target data as hexadecimal bytes instead of plain text.");

    // ---- Target\u2192Client panel ----
    tip(m_hChkAutoT2C,     L"Auto-forward T\u2192C \u2014 Target\u2192Client data is forwarded immediately without buffering.\n"
                            L"Uncheck to manually inspect, forward, or drop each packet.");
    tip(m_hBtnFwdT2C,      L"Manually forward the buffered Target\u2192Client data to the client.");
    tip(m_hBtnDropT2C,     L"Discard the buffered Target\u2192Client data. Nothing will be sent to the client.");
    tip(m_hChkHexT2C,      L"Show Target\u2192Client data as hexadecimal bytes instead of plain text.");

    // ---- Bottom panel ----
    tip(m_hBtnClearLog,    L"Remove all entries from the relay log.");
    tip(m_hComboFilter,    L"Filter the log:\n  All | Data only | Connection events | Errors");
    tip(m_hBtnRefreshDiag, L"Refresh live socket diagnostics for the listen, client, and target sockets.");
    tip(m_hBtnClearStats,  L"Reset all traffic byte and packet counters to zero.");
}
