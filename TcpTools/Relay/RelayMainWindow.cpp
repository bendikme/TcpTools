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
        TOOL_PAGE_EXSTYLE, WndClassName, L"Relay",
        TOOL_PAGE_STYLE,
        0, 0, Dpi::Scale(1200), Dpi::Scale(800),
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

// ---- Control helpers -------------------------------------------
// Positions and sizes are in 96-DPI units; Dpi::Scale turns them into pixels.

static HWND MakeLabel(HWND parent, HINSTANCE hInst, const wchar_t* text,
                      int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        Dpi::Scale(x), Dpi::Scale(y), Dpi::Scale(w), Dpi::Scale(h), parent, nullptr, hInst, nullptr);
}

static HWND MakeEdit(HWND parent, HINSTANCE hInst, int id,
                     int x, int y, int w, int h, const wchar_t* text = L"")
{
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        Dpi::Scale(x), Dpi::Scale(y), Dpi::Scale(w), Dpi::Scale(h), parent, (HMENU)(INT_PTR)id, hInst, nullptr);
    return e;
}

static HWND MakeButton(HWND parent, HINSTANCE hInst, int id,
                       int x, int y, int w, int h, const wchar_t* text)
{
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        Dpi::Scale(x), Dpi::Scale(y), Dpi::Scale(w), Dpi::Scale(h), parent, (HMENU)(INT_PTR)id, hInst, nullptr);
}

static HWND MakeCheckbox(HWND parent, HINSTANCE hInst, int id,
                         int x, int y, int w, int h, const wchar_t* text, bool checked = true)
{
    HWND cb = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        Dpi::Scale(x), Dpi::Scale(y), Dpi::Scale(w), Dpi::Scale(h), parent, (HMENU)(INT_PTR)id, hInst, nullptr);
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
        m_queue.SetTarget(hWnd, WM_RELAY_EVENTS);
        OnCreate(hWnd);
        return 0;

    case WM_SHOWWINDOW:
        // A view that was left alone while the page was hidden is rebuilt now.
        if (wParam) {
            if (m_viewStaleC2T) ShowViews(true);
            if (m_viewStaleT2C) ShowViews(false);
        }
        break;

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
                L"server to forward traffic to): a name, an IPv4 "
                L"address or an IPv6 address. You can type "
                L"host:port, [::1]:9002 or pick an entry from the "
                L"history and both fields auto-fill.\n"
                L"3. Choose TCP or UDP.\n"
                L"4. Click Start Relay.\n"
                L"5. The relay listens for a client and connects to "
                L"the target (and keeps retrying until you click "
                L"Stop), and forwards data both ways. One client "
                L"is served at a time; further clients that connect "
                L"meanwhile wait in line (the Client indicator shows "
                L"how many, up to 16) and get their turn in order "
                L"when the current client leaves. The client side "
                L"and the target side may use different address "
                L"families (IPv4 client, IPv6 target, or the other "
                L"way round).\n"
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
                L"man-in-the-middle for debugging. Ticking "
                L"Auto-forward again sends what is buffered first, "
                L"so the order is kept. Buffered data is discarded "
                L"when the peer it was meant for disconnects or the "
                L"relay stops.\n"
                L"\n"
                L"\u2500\u2500\u2500 KEYBOARD SHORTCUTS \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 F1 \u2014 This help screen\n"
                L"\u2022 Ctrl+Tab \u2014 Next tab; Ctrl+T \u2014 new tab; "
                L"Ctrl+W \u2014 close tab\n"
                L"\n"
                L"\u2500\u2500\u2500 FEATURES \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Hex View \u2014 Display relayed data as hex.\n"
                L"\u2022 Manual buffering \u2014 Pause, inspect, forward "
                L"or drop packets in each direction.\n"
                L"\u2022 Protocol support \u2014 TCP and UDP relay, IPv4 "
                L"and IPv6 (one dual-stack listening socket; the "
                L"port is taken exclusively). In UDP mode replies "
                L"go to the sender of the most recent datagram.\n"
                L"\u2022 Target History \u2014 Dropdown remembers recent "
                L"target host:port entries across sessions.\n"
                L"\n"
                L"\u2500\u2500\u2500 BOTTOM PANEL \u2500\u2500\u2500\n"
                L"\n"
                L"\u2022 Log \u2014 Full event history with direction, "
                L"size, and hex preview per entry.\n"
                L"\u2022 Socket Diagnostics \u2014 Live socket info for listen, "
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

    // ---- Relay network events: one wake-up, the events wait in m_queue ----
    case WM_RELAY_EVENTS:
        OnRelayEvents();
        return 0;

    case WM_PARSE_TARGET_COMBO:
        DoParseComboParts();
        return 0;

    case WM_DESTROY:
        // Stops and joins the network threads; then nothing is queued for this page any more.
        m_network.reset();
        m_queue.Close();
        if (m_hListLog) ListView_SetImageList(m_hListLog, nullptr, LVSIL_SMALL);  // the list must not free it too
        if (m_hImgListLog) { ImageList_Destroy(m_hImgListLog); m_hImgListLog = nullptr; }
        if (m_hTooltip) { DestroyWindow(m_hTooltip); m_hTooltip = nullptr; }
        if (m_hFontMono) { DeleteObject(m_hFontMono); m_hFontMono = nullptr; }
        if (m_hFontUI) { DeleteObject(m_hFontUI); m_hFontUI = nullptr; }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---- OnCreate --------------------------------------------------

void RelayMainWindow::OnCreate(HWND hWnd)
{
    HINSTANCE hI = m_hInst;

    // Create monospace font for diagnostics
    m_hFontMono = Dpi::CreateMonoFont(13);
    m_hFontUI   = Dpi::CreateUIFont();

    // ---- Row 0: config labels + edits ----
    int y0 = 10;
    MakeLabel(hWnd, hI, L"Listen Port:", 10, y0 + 2, 75, 18);
    m_hEditListenPort = MakeEdit(hWnd, hI, IDC_EDIT_LISTEN_PORT, 88, y0, 55, 22, L"9001");

    MakeLabel(hWnd, hI, L"Target:", 155, y0 + 2, 45, 18);
    m_hComboTarget = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        Dpi::Scale(200), Dpi::Scale(y0), Dpi::Scale(155), Dpi::Scale(200),
        hWnd, (HMENU)(INT_PTR)IDC_COMBO_TARGET, hI, nullptr);
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
        Dpi::Scale(492), Dpi::Scale(y0), Dpi::Scale(70), Dpi::Scale(200),
        hWnd, (HMENU)IDC_COMBO_PROTOCOL, hI, nullptr);
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
        Dpi::Scale(255), Dpi::Scale(y1 + 4), Dpi::Scale(300), Dpi::Scale(18),
        hWnd, (HMENU)IDC_STATIC_STATUS, hI, nullptr);

    // Connection status indicators (colored text)
    m_hStaticClientInd = CreateWindowExW(0, L"STATIC", L"\u25CF Client: -",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        Dpi::Scale(560), Dpi::Scale(y1 + 4), Dpi::Scale(250), Dpi::Scale(18),
        hWnd, (HMENU)IDC_STATIC_CLIENT_IND, hI, nullptr);
    m_hStaticTargetInd = CreateWindowExW(0, L"STATIC", L"\u25CF Target: -",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        Dpi::Scale(820), Dpi::Scale(y1 + 4), Dpi::Scale(250), Dpi::Scale(18),
        hWnd, (HMENU)IDC_STATIC_TARGET_IND, hI, nullptr);

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
    m_hEditTextC2T = CreateWindowExW(WS_EX_CLIENTEDGE, TextView::ClassName(), L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
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
    m_hEditTextT2C = CreateWindowExW(WS_EX_CLIENTEDGE, TextView::ClassName(), L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
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
        0, 0, Dpi::Scale(100), Dpi::Scale(200), hWnd, (HMENU)IDC_COMBO_LOGFILTER, hI, nullptr);
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
        const int ICON = Dpi::Scale(16);
        m_hImgListLog = ImageList_Create(ICON, ICON, ILC_COLOR32 | ILC_MASK, LOG_TYPE_COUNT, 0);
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
        HFONT hIconFont = CreateFontW(Dpi::Scale(14), 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Symbol");
        for (auto& def : iconDefs) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, ICON, ICON);
            SelectObject(mem, bmp);
            RECT r = { 0, 0, ICON, ICON };
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
        col.cx = Dpi::Scale(cols[i].w);
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
    HFONT hFont = m_hFontUI;
    // A drop-down combo that gets a new font re-selects the first list entry
    // starting with its text ("127.0.0.1" -> a "127.0.0.1:9901" from the
    // history), so the target text is put back afterwards.
    const std::wstring targetText = Utils::GetWindowText(m_hComboTarget);
    EnumChildWindows(hWnd, [](HWND h, LPARAM lp) -> BOOL {
        SendMessageW(h, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)hFont);
    if (Utils::GetWindowText(m_hComboTarget) != targetText)
        SetWindowTextW(m_hComboTarget, targetText.c_str());
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
    UpdateTitle();
}

// ---- OnSize ----------------------------------------------------

void RelayMainWindow::OnSize(HWND hWnd, int cx, int cy)
{
    if (!m_hComboTarget || !m_hTab) return;   // before WM_CREATE has built the controls

    // Layout arithmetic is in 96-DPI units; Move() scales to pixels.
    cx = Dpi::Unscale(cx);
    cy = Dpi::Unscale(cy);
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

    // A resized drop-down combo re-selects the first list entry that starts
    // with its text; remember what the user sees and put it back afterwards.
    std::wstring targetText = Utils::GetWindowText(m_hComboTarget);

    HDWP dwp = BeginDeferWindowPos(45);
    auto Move = [&](HWND h, int x, int y, int w, int hh) {
        if (h && dwp) dwp = DeferWindowPos(dwp, h, nullptr,
            Dpi::Scale(x), Dpi::Scale(y), Dpi::Scale(w), Dpi::Scale(hh),
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
    Move(m_hStaticBufC2T, c2tX + 248,    yMid + 20, std::max(10, hexChkX_c2t - c2tX - 258), 16);
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
    Move(m_hStaticBufT2C, t2cX + 248,    yMid + 20, std::max(10, hexChkX_t2c - t2cX - 258), 16);
    Move(m_hHexT2C,       t2cX + 5,      yMid + 44, panelW - 10, hexH);
    Move(m_hEditTextT2C,  t2cX + 5,      yMid + 44, panelW - 10, hexH);

    // --- Bottom tab area ---
    int yBot = yMid + midH + M;
    int botH = cy - yBot - M;
    if (botH < 120) botH = 120;
    Move(m_hTab, M, yBot, cx - 2 * M, botH);

    // Content rectangle inside the tab control (AdjustRect works in pixels)
    RECT tabRc = { 0, 0, Dpi::Scale(cx - 2 * M), Dpi::Scale(botH) };
    TabCtrl_AdjustRect(m_hTab, FALSE, &tabRc);
    int tx = M + Dpi::Unscale(tabRc.left);
    int ty = yBot + Dpi::Unscale(tabRc.top);
    int tw = Dpi::Unscale(tabRc.right - tabRc.left);
    int th = Dpi::Unscale(tabRc.bottom - tabRc.top);

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
    Move(m_hStaticDiagInfo, tx + 10, ty + 10,       tw - 20, std::max(20, th - 50));
    Move(m_hBtnRefreshDiag, tx + 10, ty + th - 34,  90,      ROW_H);

    if (dwp) EndDeferWindowPos(dwp);
    if (Utils::GetWindowText(m_hComboTarget) != targetText)
        SetWindowTextW(m_hComboTarget, targetText.c_str());
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
    // Place at end of config row 0, right-aligned (rc is in pixels)
    SetWindowPos(m_hBtnHelp, HWND_TOP,
        rc.right - Dpi::Scale(8 + 26), Dpi::Scale(10), Dpi::Scale(26), Dpi::Scale(22),
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

    // Turning auto-forward on sends what is held first; the network keeps
    // holding new data until the UI reports the buffer empty, so the order
    // of the stream is kept.
    case IDC_CHK_AUTO_C2T:
        if (m_network) {
            bool on = (SendMessageW(m_hChkAutoC2T, BM_GETCHECK, 0, 0) == BST_CHECKED);
            m_network->SetAutoForwardC2T(on);
            if (on) FlushHeld(true);
        }
        break;
    case IDC_CHK_AUTO_T2C:
        if (m_network) {
            bool on = (SendMessageW(m_hChkAutoT2C, BM_GETCHECK, 0, 0) == BST_CHECKED);
            m_network->SetAutoForwardT2C(on);
            if (on) FlushHeld(false);
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

    // Cap the log: drop the oldest 10 % (the "#" column keeps its numbers).
    constexpr size_t LOG_MAX = 20000;
    if (m_log.size() >= LOG_MAX) {
        m_log.erase(m_log.begin(), m_log.begin() + LOG_MAX / 10);
        RefreshLogView();
    }

    int idx = (int)m_log.size();
    m_log.push_back(std::move(e));
    NotifyToolEvent(m_hWnd);   // every entry counts, also ones the log filter hides

    if (PassesFilter(type, m_filterIndex)) m_logVisible.push_back(idx);
    if (!m_inSlice) SyncLogList();   // inside a slice the list is updated once, at its end
}

// Tells the list control about the entries added since it was last told.
void RelayMainWindow::SyncLogList()
{
    const int count  = (int)m_logVisible.size();
    const int before = ListView_GetItemCount(m_hListLog);
    if (count == before) return;
    ListView_SetItemCountEx(m_hListLog, count, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    if (count > before) {
        ListView_RedrawItems(m_hListLog, before, count - 1);
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
    // CB_RESETCONTENT clears the edit text; keep what the user sees.
    std::wstring text = Utils::GetWindowText(m_hComboTarget);
    SendMessageW(m_hComboTarget, CB_RESETCONTENT, 0, 0);
    for (auto& e : m_targetHistory)
        SendMessageW(m_hComboTarget, CB_ADDSTRING, 0, (LPARAM)e.c_str());
    SetWindowTextW(m_hComboTarget, text.c_str());
}

void RelayMainWindow::OnTargetComboSelChange()
{
    // Defer parsing: Windows overwrites the combo edit text AFTER
    // CBN_SELCHANGE returns, so we post a message to parse later.
    PostMessageW(m_hWnd, WM_PARSE_TARGET_COMBO, 0, 0);
}

// "host:port", "[::1]:9002" or "[::1]" typed or picked from the history: the
// host goes to the combo (without brackets), a port to the port field.
void RelayMainWindow::DoParseComboParts()
{
    std::wstring text = Utils::GetWindowText(m_hComboTarget);
    NetAddr::HostPort hp;
    if (!NetAddr::SplitHostPort(text, hp)) return;
    const bool bracketed = text.find(L'[') != std::wstring::npos;
    if (!hp.hasPort && !bracketed) return;
    SetWindowTextW(m_hComboTarget, hp.host.c_str());
    if (hp.hasPort) SetWindowTextW(m_hEditTargetPort, std::to_wstring(hp.port).c_str());
}

// ---- Actions ---------------------------------------------------

void RelayMainWindow::DoStartRelay()
{
    if (m_network) return;

    // Validate listen port
    std::wstring listenPortW = Utils::GetWindowText(GetDlgItem(m_hWnd, IDC_EDIT_LISTEN_PORT));
    int listenPort = 0;
    if (!Utils::IsValidPort(listenPortW, listenPort)) {
        MessageBoxW(m_hWnd, L"Please enter a valid listen port (1\u201365535).", L"Invalid port", MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_LISTEN_PORT));
        return;
    }

    // The combo may hold "host:port", "[::1]:9002" or a bare host.
    std::wstring targetText = Utils::GetWindowText(m_hComboTarget);
    NetAddr::HostPort hp;
    if (!NetAddr::SplitHostPort(targetText, hp) || !NetAddr::IsValidHost(hp.host)) {
        MessageBoxW(m_hWnd, L"Please enter a valid target host name, IPv4 address or IPv6 address "
                            L"(for example 192.168.1.10, myserver.local, ::1 or [::1]:9002).",
                    L"Invalid target", MB_ICONWARNING);
        SetFocus(m_hComboTarget);
        return;
    }
    std::wstring targetW = hp.host;
    SetWindowTextW(m_hComboTarget, targetW.c_str());
    if (hp.hasPort) SetWindowTextW(GetDlgItem(m_hWnd, IDC_EDIT_TARGET_PORT), std::to_wstring(hp.port).c_str());

    // Validate target port
    std::wstring targetPortW = Utils::GetWindowText(GetDlgItem(m_hWnd, IDC_EDIT_TARGET_PORT));
    int targetPort = 0;
    if (!Utils::IsValidPort(targetPortW, targetPort)) {
        MessageBoxW(m_hWnd, L"Please enter a valid target port (1\u201365535).", L"Invalid port", MB_ICONWARNING);
        SetFocus(GetDlgItem(m_hWnd, IDC_EDIT_TARGET_PORT));
        return;
    }

    bool noTimeout = (SendMessageW(m_hChkNoTimeout, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int timeout = 0;
    if (!noTimeout) {
        std::wstring tw = Utils::GetWindowText(m_hEditTimeout);
        bool ok = !tw.empty() && tw.size() <= 4;
        for (wchar_t c : tw) if (c < L'0' || c > L'9') ok = false;
        if (ok) timeout = _wtoi(tw.c_str());
        if (!ok || timeout < 1 || timeout > 3600) {
            MessageBoxW(m_hWnd, L"Please enter a timeout of 1\u20133600 seconds, or tick \u2018No timeout\u2019.",
                        L"Invalid timeout", MB_ICONWARNING);
            SetFocus(m_hEditTimeout);
            return;
        }
    }
    bool udpMode = (SendMessageW(m_hComboProtocol, CB_GETCURSEL, 0, 0) == 1);

    // A busy listen port is reported by the network thread (the port is taken
    // exclusively there): it ends up in the log and the status line.
    m_queue.Clear();
    ResetSessionUI();
    m_lastError.clear();
    m_proto      = udpMode ? L"UDP" : L"TCP";
    m_listenText = std::to_wstring(listenPort);
    m_targetText = NetAddr::JoinHostPort(targetW, targetPort);

    m_network = std::make_unique<RelayNetwork>(m_queue);
    m_network->SetAutoForwardC2T(SendMessageW(m_hChkAutoC2T, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_network->SetAutoForwardT2C(SendMessageW(m_hChkAutoT2C, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (!m_network->StartRelay(listenPort, targetW, targetPort, timeout, udpMode)) {
        MessageBoxW(m_hWnd, L"Failed to start relay.", L"Relay", MB_ICONERROR);
        m_network.reset();
        return;
    }

    SetRelayUI(true);
    Utils::SetWindowText(m_hStaticStatus, L"[" + m_proto + L"] Starting ...");

    // Save target to history
    Utils::SaveHistory(L"TcpRelay", m_targetText, m_targetHistory);
    PopulateTargetCombo();

    AddLogEntry(LogType::Info, L"-", 0,
                L"[" + m_proto + L"] Starting relay: listen " + std::to_wstring(listenPort) +
                L" \u2192 " + m_targetText,
                false);
    UpdateTitle();
}

void RelayMainWindow::DoStopRelay()
{
    if (!m_network) return;
    m_network->StopRelay();
    m_queue.Clear();          // events of the relay that just ended must not reach the next one
    m_lastError.clear();      // a stop the user asked for is not an error
    OnRelayStopped();
}

// Clears everything that belongs to one relay session.
void RelayMainWindow::ResetSessionUI()
{
    m_pendC2T.clear(); m_pendT2C.clear();
    m_pendBytesC2T = m_pendBytesT2C = 0;
    m_viewC2T.clear(); m_viewT2C.clear();
    m_clientUp = m_targetUp = false;
    m_clientAddr.clear(); m_targetAddr.clear();
    m_sliceC2T.clear(); m_sliceT2C.clear();
    m_waitingCount = 0;
    UpdateBufferLabel(true);
    UpdateBufferLabel(false);
    RefreshC2TView();
    RefreshT2CView();
    m_clientIndBase = L"\u25CB Client: -";
    UpdateClientIndicator();
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CB Target: -");
}

// One slice is about 64 KB of payload: the text view needs ~2 us per byte, so a
// slice stays near 100 ms and clicks get in between.
constexpr size_t RELAY_SLICE_BYTES = 64 * 1024;

// The wake-up: handles one slice of what the network threads queued, then
// returns. Everything the slice produced is shown once: one append per view,
// one list update, one refresh of the labels.
void RelayMainWindow::OnRelayEvents()
{
    std::vector<RelayEvent> events = m_queue.Drain(RELAY_SLICE_BYTES);
    if (events.empty()) return;

    m_inSlice        = true;
    m_stoppedInSlice = false;
    for (RelayEvent& ev : events) {
        switch (ev.kind) {
        case RelayEvent::Kind::Listening:        OnRelayListening(ev.num); break;
        case RelayEvent::Kind::ClientConn:       OnRelayClientConn(ev.text, ev.num, ev.fromWait); break;
        case RelayEvent::Kind::ClientWaiting:    OnRelayClientWaiting(ev.text, ev.num, false); break;
        case RelayEvent::Kind::ClientWaitLeft:   OnRelayClientWaiting(ev.text, ev.num, true); break;
        case RelayEvent::Kind::ClientDisconn:    OnRelayClientDisconn(); break;
        case RelayEvent::Kind::TargetConnecting: OnRelayTargetConnecting(); break;
        case RelayEvent::Kind::TargetConn:       OnRelayTargetConn(); break;
        case RelayEvent::Kind::TargetDisconn:    OnRelayTargetDisconn(); break;
        case RelayEvent::Kind::Data:
            OnRelayData(ev.c2t, ev.data.data(), (int)ev.data.size(), ev.forwarded);
            break;
        case RelayEvent::Kind::Error:            OnRelayError(ev.num, ev.text); break;
        case RelayEvent::Kind::Stopped:          OnRelayStopped(); break;
        }
        if (m_stoppedInSlice) break;   // the rest belongs to a relay that no longer exists
    }
    FlushSlice();
    m_inSlice = false;
}

// Shows what the slice collected.
void RelayMainWindow::FlushSlice()
{
    if (!m_sliceC2T.empty()) { AppendView(true,  m_sliceC2T.data(), m_sliceC2T.size()); m_sliceC2T.clear(); }
    if (!m_sliceT2C.empty()) { AppendView(false, m_sliceT2C.data(), m_sliceT2C.size()); m_sliceT2C.clear(); }
    if (m_bufDirtyC2T) { m_bufDirtyC2T = false; UpdateBufferLabel(true); }
    if (m_bufDirtyT2C) { m_bufDirtyT2C = false; UpdateBufferLabel(false); }
    SyncLogList();
    if (m_bottomTab == 2) UpdateStats();
}

static std::wstring HexPreview(const std::vector<BYTE>& b)
{
    std::wstring s;
    for (size_t i = 0; i < std::min(b.size(), (size_t)32); ++i) {
        wchar_t h[4]; swprintf_s(h, L"%02X ", b[i]);
        s += h;
    }
    if (b.size() > 32) s += L"...";
    return s;
}

// Sends the held packets of one direction, in order. A packet that was only
// partly accepted keeps its unsent rest, so nothing is sent twice. Returns
// true when nothing is left held.
bool RelayMainWindow::FlushHeld(bool c2t)
{
    auto& q     = c2t ? m_pendC2T : m_pendT2C;
    auto& bytes = c2t ? m_pendBytesC2T : m_pendBytesT2C;
    if (q.empty() || !m_network) return q.empty();

    int released = 0;
    size_t sentBytes = 0;
    std::vector<BYTE> head;
    while (!q.empty()) {
        auto& pkt = q.front();
        size_t n = c2t ? m_network->ForwardToTarget(pkt.data(), pkt.size())
                       : m_network->ForwardToClient(pkt.data(), pkt.size());
        if (n > 0 && head.size() < 32)
            head.insert(head.end(), pkt.begin(), pkt.begin() + std::min(n, (size_t)32));
        sentBytes += n;
        if (n == pkt.size()) {
            q.pop_front();
            ++released;
        } else {
            if (n > 0) pkt.erase(pkt.begin(), pkt.begin() + n);
            break;
        }
    }
    bytes -= sentBytes;
    m_network->ReleaseHeld(c2t, released);
    if (sentBytes > 0) {
        (c2t ? m_bytesC2T : m_bytesT2C) += sentBytes;
        (c2t ? m_packetsC2T : m_packetsT2C) += released;
        AddLogEntry(c2t ? LogType::DataC2T : LogType::DataT2C, c2t ? L"C\u2192T" : L"T\u2192C", sentBytes,
                    L"[" + m_proto + L"] Forwarded held data: " + HexPreview(head), true);
    }
    UpdateBufferLabel(c2t);
    return q.empty();
}

void RelayMainWindow::DoForwardC2T()
{
    if (!FlushHeld(true))
        AddLogEntry(LogType::Error, L"C\u2192T", 0,
                    L"Forward incomplete: the target is not connected or not accepting data; the rest stays buffered", false);
}

void RelayMainWindow::DoForwardT2C()
{
    if (!FlushHeld(false))
        AddLogEntry(LogType::Error, L"T\u2192C", 0,
                    L"Forward incomplete: no client connected or it is not accepting data; the rest stays buffered", false);
}

// Throws away the held packets of one direction (and says so in the log).
void RelayMainWindow::DiscardHeld(bool c2t, const wchar_t* why)
{
    auto& q     = c2t ? m_pendC2T : m_pendT2C;
    auto& bytes = c2t ? m_pendBytesC2T : m_pendBytesT2C;
    if (q.empty()) return;
    int pkts = (int)q.size();
    size_t sz = bytes;
    q.clear();
    bytes = 0;
    if (m_network) m_network->ReleaseHeld(c2t, pkts);
    UpdateBufferLabel(c2t);
    AddLogEntry(LogType::Info, c2t ? L"C\u2192T" : L"T\u2192C", sz, why, false);
}

void RelayMainWindow::DoDropC2T() { DiscardHeld(true,  L"Dropped buffered data"); }
void RelayMainWindow::DoDropT2C() { DiscardHeld(false, L"Dropped buffered data"); }

// ---- View helpers ---------------------------------------------
// The text view and the hex view of a direction show the same bytes
// (m_viewC2T / m_viewT2C); only the visible one is kept up to date.

constexpr size_t RELAY_VIEW_CAP = 1024 * 1024;        // shown traffic per direction
constexpr size_t RELAY_PEND_CAP = 64u * 1024 * 1024;  // held (unforwarded) data per direction

void RelayMainWindow::ShowViews(bool c2t)
{
    bool hexMode = c2t ? m_hexModeC2T : m_hexModeT2C;
    HWND hex  = c2t ? m_hHexC2T : m_hHexT2C;
    HWND edit = c2t ? m_hEditTextC2T : m_hEditTextT2C;
    const auto& v = c2t ? m_viewC2T : m_viewT2C;
    if (hexMode) {
        ShowWindow(edit, SW_HIDE);
        ShowWindow(hex,  SW_SHOW);
        HexView::SetData(hex, v);
    } else {
        ShowWindow(hex,  SW_HIDE);
        ShowWindow(edit, SW_SHOW);
        TextView::SetText(edit, Utils::BytesToDisplayText(v));
    }
    (c2t ? m_viewStaleC2T : m_viewStaleT2C) = false;
}

void RelayMainWindow::RefreshC2TView() { ShowViews(true); }
void RelayMainWindow::RefreshT2CView() { ShowViews(false); }

void RelayMainWindow::AppendView(bool c2t, const BYTE* data, size_t len)
{
    auto& v = c2t ? m_viewC2T : m_viewT2C;
    bool& stale = c2t ? m_viewStaleC2T : m_viewStaleT2C;
    v.insert(v.end(), data, data + len);
    const bool capped = v.size() > RELAY_VIEW_CAP;
    if (capped) v.erase(v.begin(), v.begin() + (v.size() - RELAY_VIEW_CAP / 2));

    // A hidden page is not drawn on: the view is rebuilt when the page is shown.
    if (!IsWindowVisible(m_hWnd)) { stale = true; return; }
    if (capped || stale) { ShowViews(c2t); return; }

    if (c2t ? m_hexModeC2T : m_hexModeT2C) {
        HexView::AppendData(c2t ? m_hHexC2T : m_hHexT2C, data, len);
    } else {
        std::wstring s = Utils::BytesToDisplayText(data, len);
        if (s.empty()) return;
        TextView::AppendText(c2t ? m_hEditTextC2T : m_hEditTextT2C, s);
    }
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
    if (m_inSlice) { (c2t ? m_bufDirtyC2T : m_bufDirtyT2C) = true; return; }   // once, at the slice's end
    const auto& q     = c2t ? m_pendC2T : m_pendT2C;
    size_t      bytes = c2t ? m_pendBytesC2T : m_pendBytesT2C;
    Utils::SetWindowText(c2t ? m_hStaticBufC2T : m_hStaticBufT2C,
        L"Buffer: " + Utils::FormatBytes(bytes) + L", " + std::to_wstring(q.size()) + L" pkts");
    EnableWindow(c2t ? m_hBtnFwdC2T  : m_hBtnFwdT2C,  !q.empty() && m_network != nullptr);
    EnableWindow(c2t ? m_hBtnDropC2T : m_hBtnDropT2C, !q.empty());
}

void RelayMainWindow::UpdateStats()
{
    Utils::SetWindowText(m_hStatC2TBytes, L"C\u2192T bytes forwarded: " + Utils::FormatBytes(m_bytesC2T));
    Utils::SetWindowText(m_hStatC2TPkts,  L"C\u2192T packets: " + std::to_wstring(m_packetsC2T));
    Utils::SetWindowText(m_hStatT2CBytes, L"T\u2192C bytes forwarded: " + Utils::FormatBytes(m_bytesT2C));
    Utils::SetWindowText(m_hStatT2CPkts,  L"T\u2192C packets: " + std::to_wstring(m_packetsT2C));
    Utils::SetWindowText(m_hStatClients,  L"Client: " + (m_clientAddr.empty() ? L"-" : m_clientAddr));
    Utils::SetWindowText(m_hStatTarget,   L"Target: " + (m_targetText.empty() ? std::wstring(L"-") : m_targetText));
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

    // Sockets are closed under this mutex, so the handles below stay valid.
    std::lock_guard<std::mutex> sockLock(m_network->SockMutex());

    std::wstring info;
    std::wstring proto = m_network->GetProtocolStr();
    info += L"Protocol: " + proto + L"\r\n\r\n";

    // Listen socket info
    SOCKET lsock = m_network->GetListenSocket();
    if (lsock != INVALID_SOCKET) {
        info += L"Listen: " + NetAddr::LocalName(lsock) + L"\r\n";

        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(lsock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(lsock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);
        wchar_t buf[256];
        swprintf_s(buf, L"  SO_SNDBUF: %d B  SO_RCVBUF: %d B", sndbuf, rcvbuf);
        info += buf;
        info += L"\r\n\r\n";
    }

    // Client / target socket info (the two sides may be of different families)
    auto describe = [&](SOCKET sock, const wchar_t* label) {
        std::wstring peer = NetAddr::PeerName(sock);
        if (!peer.empty()) info += std::wstring(label) + L": " + peer + L"\r\n";

        int sndbuf = 0, rcvbuf = 0, optLen = sizeof(int);
        getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &optLen);
        optLen = sizeof(int);
        getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optLen);

        wchar_t buf[256];
        swprintf_s(buf, L"  SO_SNDBUF: %d B  SO_RCVBUF: %d B", sndbuf, rcvbuf);
        info += buf;

        if (!m_network->IsUdpMode()) {
            BOOL nodelay = FALSE; optLen = sizeof(BOOL);
            getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, &optLen);
            info += nodelay ? L"  Nagle: OFF" : L"  Nagle: ON";

            int mss = 0; optLen = sizeof(int);
            if (getsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, &optLen) == 0 && mss > 0) {
                swprintf_s(buf, L"\r\n  MSS: %d B  MTU: ~%d B  Frame: ~%d B", mss, mss + 40, mss + 40 + 14);
                info += buf;
            }
        }
    };

    SOCKET csock = m_network->GetClientSocket();
    if (csock != INVALID_SOCKET && m_network->IsClientConnected()) {
        describe(csock, L"Client");
        info += L"\r\n";
        if (m_waitingCount > 0) info += L"  " + std::to_wstring(m_waitingCount) + L" more waiting\r\n";
        info += L"\r\n";
    } else if (m_network->IsUdpMode() && m_clientUp) {
        info += L"Client (last sender): " + m_clientAddr + L"\r\n\r\n";
    } else {
        info += L"Client: not connected\r\n\r\n";
    }

    SOCKET tsock = m_network->GetTargetSocket();
    if (tsock != INVALID_SOCKET && m_network->IsTargetConnected()) {
        describe(tsock, L"Target");
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

// The status line is derived from what the network events reported.
void RelayMainWindow::UpdateStatusText()
{
    if (!m_isListening) { Utils::SetWindowText(m_hStaticStatus, L"Idle"); return; }
    std::wstring p = L"[" + m_proto + L"] ";
    const std::wstring more = m_waitingCount > 0 ? L" (+" + std::to_wstring(m_waitingCount) + L" waiting)" : L"";
    if (m_clientUp && m_targetUp)
        Utils::SetWindowText(m_hStaticStatus, p + L"Relaying: " + m_clientAddr + L" \u2194 " + m_targetText + more);
    else if (m_targetUp)
        Utils::SetWindowText(m_hStaticStatus, p + L"Target connected, awaiting client...");
    else if (m_clientUp)
        Utils::SetWindowText(m_hStaticStatus, p + L"Client: " + m_clientAddr + L" | Target: not connected, retrying..." + more);
    else
        Utils::SetWindowText(m_hStaticStatus, p + L"Listening on port " + m_listenText + L" | Target: not connected, retrying...");
}

void RelayMainWindow::OnRelayListening(int port)
{
    UpdateStatusText();
    AddLogEntry(LogType::Listening, L"-", 0,
                L"[" + m_proto + L"] Listening on port " + std::to_wstring(port), false);
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

// The client indicator: the base text, plus how many clients wait behind it.
void RelayMainWindow::UpdateClientIndicator()
{
    std::wstring t = m_clientIndBase;
    if (m_waitingCount > 0) {
        if (!m_clientUp) t = L"\u25CB Client: -";
        t += L" (+" + std::to_wstring(m_waitingCount) + L" waiting)";
    }
    Utils::SetWindowText(m_hStaticClientInd, t);
}

void RelayMainWindow::OnRelayClientConn(const std::wstring& addr, int waiting, bool fromWait)
{
    m_clientUp      = true;
    m_clientAddr    = addr;
    m_waitingCount  = waiting;
    m_clientIndBase = L"\u25CF Client: " + addr;
    UpdateStatusText();
    UpdateClientIndicator();
    AddLogEntry(LogType::ClientConn, L"-", 0,
                L"[" + m_proto + L"] Client connected: " + addr +
                (fromWait ? L" (its turn after waiting)" : L""), false);
    // Data the target sent before a client was there.
    if (m_network && m_network->GetAutoForwardT2C()) FlushHeld(false);
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

// A further client waits (TCP relays one client at a time) or gave up waiting.
void RelayMainWindow::OnRelayClientWaiting(const std::wstring& addr, int waiting, bool left)
{
    m_waitingCount = waiting;
    UpdateClientIndicator();
    UpdateStatusText();
    if (left)
        AddLogEntry(LogType::Info, L"-", 0,
                    L"[" + m_proto + L"] Waiting client left: " + addr +
                    L" (" + std::to_wstring(waiting) + L" waiting)", false);
    else
        AddLogEntry(LogType::Info, L"-", 0,
                    L"[" + m_proto + L"] Client waiting: " + addr +
                    L" (" + std::to_wstring(waiting) + L" waiting; served when the current client leaves)", false);
    if (m_bottomTab == 1) RefreshDiagnostics();
}

void RelayMainWindow::OnRelayClientDisconn()
{
    std::wstring prevAddr = m_clientAddr;
    m_clientUp = false;
    m_clientAddr.clear();
    m_clientIndBase = L"\u25CB Client: disconnected";
    UpdateStatusText();
    UpdateClientIndicator();
    AddLogEntry(LogType::ClientDisconn, L"-", 0,
                L"[" + m_proto + L"] Client disconnected" + (prevAddr.empty() ? L"" : L": " + prevAddr),
                false);
    // What the target sent for that client cannot be delivered to the next one.
    DiscardHeld(false, L"Discarded buffered data: the client disconnected");
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayTargetConn()
{
    m_targetUp   = true;
    m_targetAddr = m_targetText;
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CF Target: " + m_targetText);
    UpdateStatusText();
    AddLogEntry(LogType::TargetConn, L"-", 0,
                L"[" + m_proto + L"] Connected to target: " + m_targetText, false);
    // Data the client sent while the target was not there.
    if (m_network && m_network->GetAutoForwardC2T()) FlushHeld(true);
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayTargetDisconn()
{
    m_targetUp = false;
    m_targetAddr.clear();
    AddLogEntry(LogType::TargetDisconn, L"-", 0,
                L"[" + m_proto + L"] Target disconnected", false);
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CC Target: reconnecting...");
    UpdateStatusText();
    DiscardHeld(true, L"Discarded buffered data: the target disconnected");
    if (m_bottomTab == 1) RefreshDiagnostics();
    UpdateTitle();
}

void RelayMainWindow::OnRelayTargetConnecting()
{
    if (m_targetUp) return;
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CC Target: connecting...");
    UpdateStatusText();
}

// The tab label and the tab's state dot.
void RelayMainWindow::UpdateTitle()
{
    if (!m_isListening) {
        SetWindowTextW(m_hWnd, L"Relay");
        NotifyToolState(m_hWnd, TOOL_IDLE);
        return;
    }
    std::wstring title = m_proto + L" Relay  \u2013  :" + m_listenText + L" \u2192 " + m_targetText;
    if (m_clientUp && m_targetUp)  title += L" (Relaying)";
    else if (m_targetUp)           title += L" (Target connected)";
    else if (m_clientUp)           title += L" (Client connected)";
    else                           title += L" (Listening)";
    SetWindowTextW(m_hWnd, title.c_str());
    NotifyToolState(m_hWnd, (m_clientUp && m_targetUp) ? TOOL_ACTIVE : TOOL_WAITING);
}

void RelayMainWindow::OnRelayData(bool c2t, const BYTE* data, int len, bool forwarded)
{
    std::wstring preview = L"[" + m_proto + L"] ";
    for (int i = 0; i < std::min(len, 32); ++i) {
        wchar_t h[4]; swprintf_s(h, L"%02X ", data[i]);
        preview += h;
    }
    if (len > 32) preview += L"...";

    const wchar_t* dir = c2t ? L"C\u2192T" : L"T\u2192C";
    LogType type = c2t ? LogType::DataC2T : LogType::DataT2C;

    (c2t ? m_sliceC2T : m_sliceT2C).insert((c2t ? m_sliceC2T : m_sliceT2C).end(), data, data + len);   // shown at the slice's end

    if (forwarded) {
        (c2t ? m_bytesC2T : m_bytesT2C) += len;
        (c2t ? m_packetsC2T : m_packetsT2C)++;
        AddLogEntry(type, dir, (size_t)len, preview, true);
        return;
    }

    // Held for Forward / Drop.
    auto& q     = c2t ? m_pendC2T : m_pendT2C;
    auto& bytes = c2t ? m_pendBytesC2T : m_pendBytesT2C;
    if (bytes + (size_t)len > RELAY_PEND_CAP) {
        if (m_network) m_network->ReleaseHeld(c2t, 1);
        AddLogEntry(LogType::Error, dir, (size_t)len,
                    L"Buffer limit (64 MB) reached: this data was dropped", false);
        return;
    }
    q.emplace_back(data, data + len);
    bytes += (size_t)len;
    UpdateBufferLabel(c2t);
    AddLogEntry(type, dir, (size_t)len, preview, false);

    // Auto-forward is on but this packet was held (peer was not ready, or
    // held data was still ahead of it): send everything held, in order.
    if (m_network && (c2t ? m_network->GetAutoForwardC2T() : m_network->GetAutoForwardT2C()))
        FlushHeld(c2t);
}

// Errors come from the network threads: log and status line, no message box
// (this tab may not be the one in front).
void RelayMainWindow::OnRelayError(int err, const std::wstring& msg)
{
    m_lastError = msg;
    AddLogEntry(LogType::Error, L"-", 0, L"Error " + std::to_wstring(err) + L": " + msg, false);
}

// The relay has ended - by the user (DoStopRelay) or by itself (fatal error).
void RelayMainWindow::OnRelayStopped()
{
    DiscardHeld(true,  L"Discarded buffered data: the relay stopped");
    DiscardHeld(false, L"Discarded buffered data: the relay stopped");
    m_network.reset();           // joins the network threads
    m_queue.Clear();             // whatever they queued last
    m_stoppedInSlice = true;

    SetRelayUI(false);           // sets m_isListening = false
    AddLogEntry(LogType::Stopped, L"-", 0, L"[" + m_proto + L"] Relay stopped", false);
    m_waitingCount  = 0;
    m_clientIndBase = L"\u25CB Client: -";
    UpdateClientIndicator();
    Utils::SetWindowText(m_hStaticTargetInd, L"\u25CB Target: -");
    m_clientUp = m_targetUp = false;
    m_clientAddr.clear();
    m_targetAddr.clear();
    UpdateBufferLabel(true);
    UpdateBufferLabel(false);
    if (!m_lastError.empty())
        Utils::SetWindowText(m_hStaticStatus, L"Stopped: " + m_lastError);
    UpdateTitle();               // idle label, idle dot
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
    //  CONFIGURATION ROW
    // ================================================================
    tip(m_hEditListenPort,
        L"Listen port (1\u201365535).\n\n"
        L"The relay binds to this local port and waits "
        L"for an incoming client connection. All data from "
        L"the client is forwarded to the target server "
        L"and vice-versa.\n\n"
        L"The relay listens on IPv4 and IPv6 (one dual-stack "
        L"socket) and takes the port exclusively.\n\n"
        L"NOTE: Ports below 1024 may need admin rights. "
        L"A port already in use is reported in the log and "
        L"the status line.");

    tip(m_hComboTarget,
        L"Target server address \u2014 IP address or hostname.\n\n"
        L"The upstream server the relay forwards data to. "
        L"Pick from recent targets in the dropdown or "
        L"type a new one.\n\n"
        L"Supported formats:\n"
        L"\u2022 IPv4 address: 192.168.1.10\n"
        L"\u2022 IPv6 address: ::1 or [::1] (brackets optional)\n"
        L"\u2022 Hostname: myserver.local\n"
        L"\u2022 host:port shortcut: 192.168.1.10:9002 or [::1]:9002\n"
        L"   (auto-fills both Target and Port fields; also when "
        L"picked from the history)\n\n"
        L"The client side and the target side may use different "
        L"address families.");

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
        L"Timeout in seconds (1\u20133600).\n\n"
        L"\u2022 How long each attempt to connect to the target may "
        L"take. The relay keeps retrying every 2 s until you click "
        L"Stop; each distinct failure is logged once.\n"
        L"\u2022 How long a forward may make no progress (peer not "
        L"accepting data) before the rest is held back.\n\n"
        L"Check \u2018No timeout\u2019 for no limit on stalled forwards "
        L"(connect attempts then last 3 s each).");

    tip(m_hChkNoTimeout,
        L"Disable timeout.\n\n"
        L"When checked, the timeout field is ignored: a stalled "
        L"forward waits as long as the connection lives, and "
        L"each connect attempt to the target lasts 3 s. The "
        L"relay retries the target connection either way.");

    tip(m_hBtnStart,
        L"Start the relay.\n\n"
        L"Binds to the listen port, relays one client at a "
        L"time (others wait in line) and connects to the target server right away "
        L"(retrying until it succeeds), then forwards data in "
        L"both directions. In UDP mode the client is whoever "
        L"sent the last datagram.\n\n"
        L"The listen port must be free \u2014 if another process "
        L"is using it, an error is shown in the log and the "
        L"status line. The target address and port are "
        L"validated first. Stop returns at once, also while a "
        L"slow name lookup or connect is running.");

    tip(m_hBtnStop,
        L"Stop the relay.\n\n"
        L"Closes the listening socket, disconnects both "
        L"client and target connections, and returns to "
        L"the idle state. Data still buffered is discarded. "
        L"The listen port is released for other applications.");

    tip(m_hStaticStatus,
        L"Relay status indicator.\n\n"
        L"Shows the current state:\n"
        L"\u2022 Idle \u2014 not running\n"
        L"\u2022 Listening on port \u2014 waiting for a client\n"
        L"\u2022 Relaying \u2014 both sides connected, data is "
        L"being forwarded");

    tip(m_hStaticClientInd,
        L"Client connection indicator.\n\n"
        L"Shows whether an incoming client is connected "
        L"and its IP address. A filled circle (\u25CF) means "
        L"connected; an empty circle (\u25CB) means idle.\n\n"
        L"The relay serves one TCP client at a time. Further "
        L"clients that connect meanwhile wait in line (up to 16, "
        L"unread, in order) and the indicator shows how many "
        L"are waiting, for example \u201c(+2 waiting)\u201d. Each "
        L"gets its turn when the client before it leaves; the "
        L"log notes when a client starts waiting and when it "
        L"gets its turn. Stop closes all waiting clients.");

    tip(m_hStaticTargetInd,
        L"Target connection indicator.\n\n"
        L"Shows whether the relay has an active connection "
        L"to the upstream target server. A filled circle "
        L"(\u25CF) means connected; a dotted or empty circle "
        L"means connecting / not connected.");

    // ================================================================
    //  CLIENT \u2192 TARGET PANEL
    // ================================================================
    tip(m_hChkAutoC2T,
        L"Auto-forward Client \u2192 Target.\n\n"
        L"When checked, data from the client is forwarded "
        L"to the target server immediately, in real time.\n\n"
        L"When unchecked, incoming client data is buffered "
        L"and held until you manually click Forward or Drop. "
        L"This lets you inspect or discard packets "
        L"before they reach the server \u2014 useful for debugging "
        L"and protocol analysis (man-in-the-middle mode).\n\n"
        L"Ticking it again first sends what is held, in order. "
        L"Held data is discarded if the target disconnects.");

    tip(m_hBtnFwdC2T,
        L"Forward buffered Client \u2192 Target data.\n\n"
        L"Sends all data currently held in the C\u2192T buffer "
        L"to the target server, packet by packet. Whatever "
        L"could not be sent stays buffered.\n\n"
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
        L"Accumulates when auto-forward is off, or when the "
        L"target is not connected.");

    tip(m_hChkHexC2T,
        L"Toggle Hex View for Client \u2192 Target data.\n\n"
        L"When checked, data is displayed as hexadecimal "
        L"bytes with an ASCII sidebar. When unchecked, "
        L"data is shown as plain text.\n\n"
        L"Both views show the same data.");

    tip(m_hEditTextC2T,
        L"Client \u2192 Target data view (read-only).\n\n"
        L"Shows the data received from the client, forwarded "
        L"or held, newest at the end (the newest 512 KB to 1 MB "
        L"are kept). Text is decoded as UTF-8; control "
        L"characters show as dots.");

    // ================================================================
    //  TARGET \u2192 CLIENT PANEL
    // ================================================================
    tip(m_hChkAutoT2C,
        L"Auto-forward Target \u2192 Client.\n\n"
        L"When checked, data from the target server is "
        L"forwarded to the client immediately, in real time.\n\n"
        L"When unchecked, incoming target data is buffered "
        L"and held until you click Forward or Drop. "
        L"This lets you inspect or discard server "
        L"responses before they reach the client.\n\n"
        L"Ticking it again first sends what is held, in order. "
        L"Held data is discarded if the client disconnects.");

    tip(m_hBtnFwdT2C,
        L"Forward buffered Target \u2192 Client data.\n\n"
        L"Sends all data currently held in the T\u2192C buffer "
        L"to the client, packet by packet. Whatever could not "
        L"be sent stays buffered.\n\n"
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
        L"Accumulates when auto-forward is off, or when no "
        L"client is connected.");

    tip(m_hChkHexT2C,
        L"Toggle Hex View for Target \u2192 Client data.\n\n"
        L"When checked, data is displayed as hexadecimal "
        L"bytes with an ASCII sidebar. When unchecked, "
        L"data is shown as plain text.\n\n"
        L"Both views show the same data.");

    tip(m_hEditTextT2C,
        L"Target \u2192 Client data view (read-only).\n\n"
        L"Shows the data received from the target server, "
        L"forwarded or held, newest at the end (the newest "
        L"512 KB to 1 MB are kept). Text is decoded as UTF-8; "
        L"control characters show as dots.");

    // ================================================================
    //  BOTTOM PANEL
    // ================================================================
    tip(m_hTab,
        L"Bottom panel tabs.\n\n"
        L"\u2022 Log \u2014 chronological history of all relay events "
        L"(connections, data, errors). Right-click entries to "
        L"copy.\n\n"
        L"\u2022 Socket Diagnostics \u2014 live socket information for the "
        L"listen, client, and target sockets.\n\n"
        L"\u2022 Statistics \u2014 traffic byte and packet counters "
        L"for both directions.\n\n"
        L"Press F1 for a complete help guide.");

    tip(m_hBtnClearLog,
        L"Clear all log entries.\n\n"
        L"Removes every entry from the relay log. "
        L"This cannot be undone. The log keeps the newest "
        L"20 000 entries.");

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
        L"Clears the traffic counters:\n"
        L"\u2022 Bytes C\u2192T / T\u2192C\n"
        L"\u2022 Packets C\u2192T / T\u2192C");

    tip(m_hListLog,
        L"Relay event log \u2014 chronological activity history.\n\n"
        L"Each row shows: sequence, time, direction (C\u2192T or "
        L"T\u2192C), size, and a hex preview of the data.\n\n"
        L"Right-click an entry for options:\n"
        L"\u2022 Copy Entry\n"
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
