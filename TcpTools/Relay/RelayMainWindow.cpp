#include "pch.h"
#include "RelayMainWindow.h"
#include "HexView.h"
#include "resource.h"
#include "ToolHost.h"
#include "Version.h"

// ============================================================
//  RelayMainWindow implementation
// ============================================================

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
    return RegisterClassExW(&wc) != 0;
}

HWND RelayMainWindow::Create(HINSTANCE hInst, HWND parent)
{
    // The object belongs to its window: WM_NCCREATE stores it in the
    // window extra bytes, WM_NCDESTROY deletes it. Several relay
    // pages can exist side by side.
    auto* self = new RelayMainWindow();
    self->m_hInst = hInst;

    return CreateWindowExW(
        TOOL_PAGE_EXSTYLE, WndClassName, L"TCP Relay",
        TOOL_PAGE_STYLE,
        0, 0, 1200, 800,
        parent, nullptr, hInst, self);
}

// ---- WndProc dispatcher ----------------------------------------

LRESULT CALLBACK RelayMainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RelayMainWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<RelayMainWindow*>(((CREATESTRUCTW*)lParam)->lpCreateParams);
        SetWindowLongPtrW(hWnd, 0, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<RelayMainWindow*>(GetWindowLongPtrW(hWnd, 0));
    }
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);

    if (msg == WM_NCDESTROY) {
        LRESULT r = DefWindowProcW(hWnd, msg, wParam, lParam);
        SetWindowLongPtrW(hWnd, 0, 0);
        delete self;
        return r;
    }
    return self->HandleMessage(hWnd, msg, wParam, lParam);
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

    case WM_TOOL_HELP:
        {
            MessageBoxW(hWnd,
                L"TCP/UDP Relay v" TCPTOOLS_VERSION L"\n"
                TCPTOOLS_COPYRIGHT L"\n"
                L"\n"
                L"\u2500\u2500\u2500 GETTING STARTED \u2500\u2500\u2500\n"
                L"\n"
                L"1. Set the Listen Port (local port for clients).\n"
                L"2. Enter the Target address and port (upstream "
                L"server to forward traffic to). You can type "
                L"host:port and both fields auto-fill.\n"
                L"3. Choose TCP or UDP.\n"
                L"4. Click Start Relay.\n"
                L"5. The relay listens for a client, connects to "
                L"the target, and forwards data both ways.\n"
                L"\n"
                L"\u2500\u2500\u2500 DATA FLOW \u2500\u2500\u2500\n"
                L"\n"
                L"Client \u2192 [Relay] \u2192 Target (C\u2192T)\n"
                L"Target \u2192 [Relay] \u2192 Client (T\u2192C)\n\n"
                L"Each direction can be:\n"
                L"\u2022 Auto-forwarded \u2014 data passes through "
                L"instantly (real-time proxy).\n"
                L"\u2022 Manual \u2014 data is buffered. You can inspect "
                L"it, then Forward or Drop it. This acts as a "
                L"man-in-the-middle for debugging.\n"
                L"\n"
                L"\u2500\u2500\u2500 KEYBOARD SHORTCUTS \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 F1 \u2014 This help screen\n"
                L"\n"
                L"\u2500\u2500\u2500 FEATURES \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Hex View \u2014 Display relayed data as hex.\n"
                L"\u2022 Manual buffering \u2014 Pause, inspect, forward "
                L"or drop packets in each direction.\n"
                L"\u2022 Protocol support \u2014 TCP and UDP relay.\n"
                L"\u2022 Target History \u2014 Dropdown remembers recent "
                L"target host:port entries across sessions.\n"
                L"\n"
                L"\u2500\u2500\u2500 BOTTOM PANEL \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Log \u2014 Full event history with direction, "
                L"size, and hex preview per entry.\n"
                L"\u2022 Diagnostics \u2014 Live socket info for listen, "
                L"client, and target sockets.\n"
                L"\u2022 Statistics \u2014 Byte and packet counters for "
                L"both C\u2192T and T\u2192C directions.\n"
                L"\n"
                L"\u2500\u2500\u2500 TIPS \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Click the [?] button to enable help tooltips.\n"
                L"\u2022 Config stored in: %APPDATA%\\TcpTools\\\n",
                L"TCP/UDP Relay \u2014 Help", MB_OK | MB_ICONINFORMATION);
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

    // Help toggle button (top-right "?")
    m_hBtnHelp = CreateWindowExW(0, L"BUTTON", L"?",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0,0,10,10, hWnd, (HMENU)(UINT_PTR)IDC_BTN_HELP, hI, nullptr);
    SendMessageW(m_hBtnHelp, WM_SETFONT, (WPARAM)hFont, TRUE);

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
    PositionHelpButton();
    RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    // Re-apply tab visibility so that controls get a fresh SW_SHOW/SW_HIDE after
    // every resize.  Without this, the tab's WS_CLIPSIBLINGS paint can race
    // against the repositioned list and leave a stale clip region.
    SwitchBottomTab(m_bottomTab);
}

void RelayMainWindow::PositionHelpButton()
{
    if (!m_hBtnHelp || !m_hWnd) return;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    // Place at end of config row 0, right-aligned
    SetWindowPos(m_hBtnHelp, HWND_TOP,
        rc.right - 8 - 26, 10, 26, 22,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

    case IDC_BTN_HELP:
        m_tooltipsEnabled = (SendMessageW(m_hBtnHelp, BM_GETCHECK, 0, 0) == BST_CHECKED);
        SendMessageW(m_hTooltip, TTM_ACTIVATE, m_tooltipsEnabled ? TRUE : FALSE, 0);
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
    //  CONFIGURATION ROW
    // ================================================================
    tip(m_hEditListenPort,
        L"Listen port (1\u201365535).\n\n"
        L"The relay binds to this local port and waits "
        L"for an incoming client connection. All data from "
        L"the client is forwarded to the target server "
        L"and vice-versa.\n\n"
        L"NOTE: Ports below 1024 may need admin rights. "
        L"A port already in use will be rejected.");

    tip(m_hComboTarget,
        L"Target server address \u2014 IP or hostname.\n\n"
        L"The upstream server the relay forwards data to. "
        L"Pick from recent targets in the dropdown or "
        L"type a new one.\n\n"
        L"Supported formats:\n"
        L"\u2022 IPv4 address: 192.168.1.10\n"
        L"\u2022 IPv6 address: ::1 or [::1]\n"
        L"\u2022 Hostname: myserver.local\n"
        L"\u2022 host:port shortcut: 192.168.1.10:9002\n"
        L"   (auto-fills both Target and Port fields)");

    tip(m_hEditTargetPort,
        L"Target server port (1\u201365535).\n\n"
        L"The port on the upstream server that the relay "
        L"connects to when forwarding traffic.\n\n"
        L"Common ports:\n"
        L"\u2022 80 / 443 \u2014 HTTP / HTTPS\n"
        L"\u2022 502 \u2014 Modbus TCP\n"
        L"\u2022 9001\u201365535 \u2014 typical custom services");

    tip(m_hComboProtocol,
        L"Network protocol for the relay.\n\n"
        L"TCP \u2014 Transmission Control Protocol.\n"
        L"Reliable, ordered byte-stream relay. Both client "
        L"and target connections use TCP.\n\n"
        L"UDP \u2014 User Datagram Protocol.\n"
        L"Connectionless datagram relay. Each UDP datagram "
        L"is forwarded as-is.");

    tip(m_hEditTimeout,
        L"Connection timeout in seconds.\n\n"
        L"How long to wait when connecting to the target "
        L"server before giving up.\n\n"
        L"\u2022 Typical values: 3\u201310 seconds.\n"
        L"\u2022 Use a longer timeout for remote/VPN hosts.\n\n"
        L"Check \u2018No timeout\u2019 to wait indefinitely.");

    tip(m_hChkNoTimeout,
        L"Disable timeout.\n\n"
        L"When checked, the relay waits indefinitely for "
        L"the target server to respond. The timeout field "
        L"is ignored.\n\n"
        L"Useful when the target server has variable or "
        L"very long startup times.");

    tip(m_hBtnStart,
        L"Start the relay.\n\n"
        L"Binds to the listen port and begins accepting "
        L"client connections. When a client connects, the "
        L"relay also connects to the target server and "
        L"begins forwarding data in both directions.\n\n"
        L"The listen port must be free \u2014 if another process "
        L"is using it, an error is displayed. "
        L"The target address and port are validated first.");

    tip(m_hBtnStop,
        L"Stop the relay.\n\n"
        L"Closes the listening socket, disconnects both "
        L"client and target connections, and returns to "
        L"the idle state. The listen port is released for "
        L"other applications.");

    tip(m_hStaticStatus,
        L"Relay status indicator.\n\n"
        L"Shows the current state:\n"
        L"\u2022 Idle \u2014 not running\n"
        L"\u2022 Listening on :port \u2014 waiting for a client\n"
        L"\u2022 Relaying \u2014 both sides connected, data is "
        L"being forwarded");

    tip(m_hStaticClientInd,
        L"Client connection indicator.\n\n"
        L"Shows whether an incoming client is connected "
        L"and its IP address. A filled circle (\u25CF) means "
        L"connected; an empty circle (\u25CB) means idle.");

    tip(m_hStaticTargetInd,
        L"Target connection indicator.\n\n"
        L"Shows whether the relay has an active connection "
        L"to the upstream target server. A filled circle "
        L"(\u25CF) means connected; empty (\u25CB) means idle.");

    // ================================================================
    //  CLIENT \u2192 TARGET PANEL
    // ================================================================
    tip(m_hChkAutoC2T,
        L"Auto-forward Client \u2192 Target.\n\n"
        L"When checked, data from the client is forwarded "
        L"to the target server immediately, in real time.\n\n"
        L"When unchecked, incoming client data is buffered "
        L"and held until you manually click Forward or Drop. "
        L"This lets you inspect, modify, or discard packets "
        L"before they reach the server \u2014 useful for debugging "
        L"and protocol analysis (man-in-the-middle mode).");

    tip(m_hBtnFwdC2T,
        L"Forward buffered Client \u2192 Target data.\n\n"
        L"Sends all data currently held in the C\u2192T buffer "
        L"to the target server. The buffer is cleared "
        L"after forwarding.\n\n"
        L"Only active when Auto-forward is unchecked and "
        L"data has been buffered.");

    tip(m_hBtnDropC2T,
        L"Drop buffered Client \u2192 Target data.\n\n"
        L"Discards all data in the C\u2192T buffer without "
        L"sending anything to the target server.\n\n"
        L"Use this to silently block client requests "
        L"or test how the server handles missing data.");

    tip(m_hStaticBufC2T,
        L"Client \u2192 Target buffer status.\n\n"
        L"Shows the number of bytes and packets currently "
        L"buffered (waiting to be forwarded or dropped). "
        L"Only accumulates when auto-forward is off.");

    tip(m_hChkHexC2T,
        L"Toggle Hex View for Client \u2192 Target data.\n\n"
        L"When checked, data is displayed as hexadecimal "
        L"bytes with an ASCII sidebar. When unchecked, "
        L"data is shown as plain text.\n\n"
        L"Switching modes re-renders the current data.");

    tip(m_hEditTextC2T,
        L"Client \u2192 Target data view (read-only).\n\n"
        L"Shows the latest data received from the client. "
        L"In auto-forward mode, each packet is displayed "
        L"briefly as it passes through. In manual mode, "
        L"the full buffered content is shown.");

    // ================================================================
    //  TARGET \u2192 CLIENT PANEL
    // ================================================================
    tip(m_hChkAutoT2C,
        L"Auto-forward Target \u2192 Client.\n\n"
        L"When checked, data from the target server is "
        L"forwarded to the client immediately, in real time.\n\n"
        L"When unchecked, incoming target data is buffered "
        L"and held until you click Forward or Drop. "
        L"This lets you inspect, modify, or discard server "
        L"responses before they reach the client.");

    tip(m_hBtnFwdT2C,
        L"Forward buffered Target \u2192 Client data.\n\n"
        L"Sends all data currently held in the T\u2192C buffer "
        L"to the client. The buffer is cleared after "
        L"forwarding.\n\n"
        L"Only active when Auto-forward is unchecked and "
        L"data has been buffered.");

    tip(m_hBtnDropT2C,
        L"Drop buffered Target \u2192 Client data.\n\n"
        L"Discards all data in the T\u2192C buffer without "
        L"sending anything to the client.\n\n"
        L"Use this to silently suppress server responses "
        L"or test how the client handles missing replies.");

    tip(m_hStaticBufT2C,
        L"Target \u2192 Client buffer status.\n\n"
        L"Shows the number of bytes and packets currently "
        L"buffered (waiting to be forwarded or dropped). "
        L"Only accumulates when auto-forward is off.");

    tip(m_hChkHexT2C,
        L"Toggle Hex View for Target \u2192 Client data.\n\n"
        L"When checked, data is displayed as hexadecimal "
        L"bytes with an ASCII sidebar. When unchecked, "
        L"data is shown as plain text.\n\n"
        L"Switching modes re-renders the current data.");

    tip(m_hEditTextT2C,
        L"Target \u2192 Client data view (read-only).\n\n"
        L"Shows the latest data received from the target "
        L"server. In auto-forward mode, each packet is "
        L"displayed briefly as it passes through. In manual "
        L"mode, the full buffered content is shown.");

    // ================================================================
    //  BOTTOM PANEL
    // ================================================================
    tip(m_hTab,
        L"Bottom panel tabs.\n\n"
        L"\u2022 Log \u2014 chronological history of all relay events "
        L"(connections, data, errors). Right-click entries to "
        L"copy.\n\n"
        L"\u2022 Diagnostics \u2014 live socket information for the "
        L"listen, client, and target sockets.\n\n"
        L"\u2022 Statistics \u2014 traffic byte and packet counters "
        L"for both directions.\n\n"
        L"Press F1 for a complete help guide.");

    tip(m_hBtnClearLog,
        L"Clear all log entries.\n\n"
        L"Removes every entry from the relay log. "
        L"This cannot be undone.");

    tip(m_hComboFilter,
        L"Filter which log entries are displayed.\n\n"
        L"\u2022 All \u2014 show everything\n"
        L"\u2022 Data only \u2014 only relayed data packets\n"
        L"\u2022 Events \u2014 connect/disconnect events\n"
        L"\u2022 Errors \u2014 only error messages\n\n"
        L"The underlying log data is preserved; switching "
        L"filters does not delete entries.");

    tip(m_hBtnRefreshDiag,
        L"Refresh socket diagnostics.\n\n"
        L"Queries all active sockets (listen, client, target) "
        L"for live information:\n"
        L"\u2022 Send/receive buffer sizes\n"
        L"\u2022 Nagle algorithm state\n"
        L"\u2022 Connection state per socket\n\n"
        L"Only available while the relay is running.");

    tip(m_hBtnClearStats,
        L"Reset all statistics to zero.\n\n"
        L"Clears all traffic counters:\n"
        L"\u2022 Bytes C\u2192T / T\u2192C\n"
        L"\u2022 Packets C\u2192T / T\u2192C\n"
        L"\u2022 Client connection count\n"
        L"\u2022 Target connection status");

    tip(m_hListLog,
        L"Relay event log \u2014 chronological activity history.\n\n"
        L"Each row shows: sequence, time, direction (C\u2192T or "
        L"T\u2192C), size, and a hex preview of the data.\n\n"
        L"Right-click an entry for options:\n"
        L"\u2022 Copy Preview\n"
        L"\u2022 Clear Log");

    tip(m_hBtnHelp,
        L"Toggle help tooltips on/off.\n\n"
        L"When active, hover over any control to see a detailed "
        L"description of its function.\n\n"
        L"Press F1 for the full help guide.");
}

// ---- Tools factory (see Tools.h) ---------------------------------
#include "Tools.h"
bool Tools::RegisterRelayClass(HINSTANCE hInst) { return RelayMainWindow::RegisterClass(hInst); }
HWND Tools::CreateRelay(HINSTANCE hInst, HWND parent) { return RelayMainWindow::Create(hInst, parent); }
