#include "pch.h"
#include "AppFrame.h"
#include "FrameRes.h"
#include "Tools.h"
#include "Utils.h"
#include "Version.h"

// GDI+ (anti-aliased tab state dots). Its headers need min/max and COM.
#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

// ============================================================
//  AppFrame implementation
// ============================================================

namespace
{
    constexpr wchar_t FRAME_CLASS[] = L"TcpToolsFrame";
    constexpr wchar_t APP_NAME[]    = L"TcpTools";
    constexpr UINT_PTR PAGE_SUBCLASS_ID = 1;
    constexpr UINT_PTR TAB_SUBCLASS_ID  = 2;

    const wchar_t* KindName(ToolKind k)
    {
        switch (k) {
        case ToolKind::Client: return L"Client";
        case ToolKind::Server: return L"Server";
        default:               return L"Relay";
        }
    }

    std::wstring IniPath()
    {
        wchar_t appData[MAX_PATH] = {};
        if (!GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) return {};
        std::wstring dir = std::wstring(appData) + L"\\TcpTools";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\TcpTools.ini";
    }

    std::wstring Trim(const std::wstring& s)
    {
        size_t a = s.find_first_not_of(L" \t");
        if (a == std::wstring::npos) return {};
        size_t b = s.find_last_not_of(L" \t");
        return s.substr(a, b - a + 1);
    }
}

// ---- Registration + creation ----------------------------------------

bool AppFrame::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.cbWndExtra    = sizeof(LONG_PTR);
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = FRAME_CLASS;
    wc.hIcon   = (HICON)LoadImageW(hInst, L"IDI_APPICON", IMAGE_ICON,
                     GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInst, L"IDI_APPICON", IMAGE_ICON,
                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    return RegisterClassExW(&wc) != 0;
}

HWND AppFrame::Create(HINSTANCE hInst, int nCmdShow, const StartupOptions& opt)
{
    auto* self = new AppFrame();
    self->m_hInst = hInst;

    struct Params { AppFrame* self; const StartupOptions* opt; } params{ self, &opt };

    HWND hWnd = CreateWindowExW(
        0, FRAME_CLASS, L"TcpTools v" TCPTOOLS_VERSION,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, Dpi::Scale(1200), Dpi::Scale(880),
        nullptr, nullptr, hInst, &params);
    if (!hWnd) return nullptr;              // WM_NCDESTROY freed `self`

    self->LoadPlacement(nCmdShow);
    UpdateWindow(hWnd);
    return hWnd;
}

AppFrame* AppFrame::FromHwnd(HWND hWnd)
{
    return reinterpret_cast<AppFrame*>(GetWindowLongPtrW(hWnd, 0));
}

LRESULT CALLBACK AppFrame::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct Params { AppFrame* self; const StartupOptions* opt; };

    if (msg == WM_NCCREATE) {
        auto* p = static_cast<Params*>(((CREATESTRUCTW*)lParam)->lpCreateParams);
        p->self->m_hWnd = hWnd;
        SetWindowLongPtrW(hWnd, 0, (LONG_PTR)p->self);
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    AppFrame* self = FromHwnd(hWnd);
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);

    if (msg == WM_CREATE) {
        auto* p = static_cast<Params*>(((CREATESTRUCTW*)lParam)->lpCreateParams);
        self->OnCreate(*p->opt);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        LRESULT r = DefWindowProcW(hWnd, msg, wParam, lParam);
        SetWindowLongPtrW(hWnd, 0, 0);
        delete self;
        // Only now: every page has been destroyed and has stopped its threads
        // (pages go between the frame's WM_DESTROY and this message).
        PostQuitMessage(0);
        return r;
    }
    return self->HandleMessage(msg, wParam, lParam);
}

// ---- Message handling -------------------------------------------------

LRESULT AppFrame::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) OnSize();
        return 0;

    case WM_GETMINMAXINFO:
    {
        auto* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize = { Dpi::Scale(940), Dpi::Scale(720) };
        return 0;
    }

    case WM_NOTIFY:
        OnNotify((NMHDR*)lParam);
        return 0;

    case WM_COMMAND:
        // IsDialogMessage turns Enter / Esc into IDOK / IDCANCEL commands
        // for the frame; there is nothing to confirm or cancel here.
        return 0;

    case WM_TOOL_STATE:
    {
        int i = FindPage((HWND)lParam);
        if (i >= 0) {
            m_pages[i].state = (ToolState)wParam;
            UpdateTabItem(i);
        }
        return 0;
    }

    case WM_TOOL_EVENT:
        OnToolEvent((HWND)lParam, (int)wParam);
        return 0;

    case WM_TIMER:
        if (wParam == IDT_UNREAD_LABELS) { FlushUnreadLabels(); return 0; }
        break;

    case WM_FRAME_PAGE_TITLE:
    {
        int i = FindPage((HWND)lParam);
        if (i >= 0) UpdatePageTitle(i);
        return 0;
    }

    case WM_ACTIVATE:
        // Remember which control had the focus so it comes back with the window.
        if (LOWORD(wParam) == WA_INACTIVE && m_active >= 0) {
            HWND f = GetFocus();
            if (f && IsChild(m_pages[m_active].hwnd, f)) m_pages[m_active].lastFocus = f;
        }
        break;

    case WM_SETFOCUS:
        if (m_active >= 0) {
            Page& p = m_pages[m_active];
            HWND f = p.lastFocus;
            if (!f || !IsWindow(f) || !IsChild(p.hwnd, f) || !IsWindowEnabled(f) || !IsWindowVisible(f))
                f = GetNextDlgTabItem(p.hwnd, nullptr, FALSE);
            if (f) SetFocus(f);
        }
        return 0;

    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        if (m_hTabs) SendMessageW(m_hTabs, msg, wParam, lParam);
        break;

    case WM_CLOSE:
        SavePlacement();
        DestroyWindow(m_hWnd);
        return 0;

    case WM_DESTROY:
        KillTimer(m_hWnd, IDT_UNREAD_LABELS);
        // The pages are child windows: Windows destroys them right after
        // this message, and each one stops its own network threads.
        if (m_hStateImages) { TabCtrl_SetImageList(m_hTabs, nullptr); ImageList_Destroy(m_hStateImages); m_hStateImages = nullptr; }
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = nullptr; }
        return 0;                       // the quit message is posted in WM_NCDESTROY
    }
    return DefWindowProcW(m_hWnd, msg, wParam, lParam);
}

void AppFrame::OnCreate(const StartupOptions& opt)
{
    m_hFont = Dpi::CreateUIFont();

    m_hTabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_TOOLTIPS,
        0, 0, 10, 10, m_hWnd, (HMENU)(UINT_PTR)IDC_FRAME_TABS, m_hInst, nullptr);
    SendMessageW(m_hTabs, WM_SETFONT, (WPARAM)m_hFont, FALSE);
    TabCtrl_SetPadding(m_hTabs, Dpi::Scale(10), Dpi::Scale(5));
    SetWindowSubclass(m_hTabs, TabSubclassProc, TAB_SUBCLASS_ID, (DWORD_PTR)this);

    CreateStateImages();

    // The "+" tab is always last.
    TCITEMW plus = {};
    plus.mask    = TCIF_TEXT | TCIF_IMAGE;
    plus.pszText = (LPWSTR)L"  +  ";
    plus.iImage  = -1;
    TabCtrl_InsertItem(m_hTabs, 0, &plus);

    // One page of each tool; the command line picks the one in front.
    int first[3];
    first[0] = AddPage(ToolKind::Client, &opt);
    first[1] = AddPage(ToolKind::Server, &opt);
    first[2] = AddPage(ToolKind::Relay,  &opt);

    int start = first[(int)opt.startTab];
    if (start < 0) start = m_pages.empty() ? -1 : 0;
    if (start >= 0) ActivatePage(start, false);
}

RECT AppFrame::PageRect() const
{
    RECT rc; GetClientRect(m_hWnd, &rc);
    RECT strip = rc;
    TabCtrl_AdjustRect(m_hTabs, FALSE, &strip);      // strip.top = height of the tab row
    rc.top = std::min(rc.bottom, strip.top);
    return rc;
}

void AppFrame::OnSize()
{
    if (!m_hTabs) return;
    RECT rc; GetClientRect(m_hWnd, &rc);
    RECT page = PageRect();

    // Only the tab row of the tab control is used; the pages sit below it.
    SetWindowPos(m_hTabs, nullptr, 0, 0, rc.right, page.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    for (auto& p : m_pages)
        SetWindowPos(p.hwnd, nullptr, page.left, page.top,
                     page.right - page.left, page.bottom - page.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

void AppFrame::OnNotify(NMHDR* nm)
{
    if (nm->hwndFrom == m_hTabs) {
        if (nm->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(m_hTabs);
            if (sel == PlusIndex()) {
                // "+" is a button, not a page: keep the current page selected.
                TabCtrl_SetCurSel(m_hTabs, m_active);
                RECT r; TabCtrl_GetItemRect(m_hTabs, sel, &r);
                POINT pt = { r.left, r.bottom };
                ClientToScreen(m_hTabs, &pt);
                ShowNewTabMenu(pt);
            } else {
                ActivatePage(sel, true);
            }
        } else if (nm->code == NM_RCLICK) {
            TCHITTESTINFO ht = {};
            GetCursorPos(&ht.pt);
            POINT screen = ht.pt;
            ScreenToClient(m_hTabs, &ht.pt);
            int i = TabCtrl_HitTest(m_hTabs, &ht);
            if (i >= 0 && i < PlusIndex()) ShowTabContextMenu(i, screen);
            else                            ShowNewTabMenu(screen);
        }
        return;
    }

    // Tooltip of the tab strip.
    if (nm->code == TTN_GETDISPINFOW) {
        auto* di = (NMTTDISPINFOW*)nm;
        int i = (int)nm->idFrom;
        static std::wstring text;
        if (i >= 0 && i < PlusIndex()) {
            const Page& p = m_pages[i];
            const wchar_t* state = p.state == TOOL_ACTIVE  ? L"connected"
                                 : p.state == TOOL_WAITING ? L"waiting"
                                 :                           L"idle";
            text = PageLabel(p) + L"  (" + state + L")";
            if (p.unread > 0)
                text += L"\n" + std::to_wstring(p.unread) + L" new log event" + (p.unread == 1 ? L"" : L"s")
                      + L" since this tab was last in front.";
            text += L"\nRight-click for the tab menu.";
        } else {
            text = L"New tab (Ctrl+T): another client, server or relay.";
        }
        SendMessageW(nm->hwndFrom, TTM_SETMAXTIPWIDTH, 0, Dpi::Scale(420));
        di->lpszText = (LPWSTR)text.c_str();
    }
}

// ---- Pages --------------------------------------------------------------

int AppFrame::AddPage(ToolKind kind, const StartupOptions* opt)
{
    StartupOptions defaults;
    const StartupOptions& o = opt ? *opt : defaults;

    HWND hPage = nullptr;
    switch (kind) {
    case ToolKind::Client:
        hPage = Tools::CreateClient(m_hInst, m_hWnd, o.clientHost, o.clientPort, o.clientAutoConnect);
        break;
    case ToolKind::Server:
        hPage = Tools::CreateServer(m_hInst, m_hWnd, o.serverPort, o.serverAutoStart, o.serverEcho, o.serverLogFile);
        break;
    case ToolKind::Relay:
        hPage = Tools::CreateRelay(m_hInst, m_hWnd);
        break;
    }
    if (!hPage) {
        MessageBoxW(m_hWnd, L"The tab could not be created.", APP_NAME, MB_ICONERROR);
        return -1;
    }

    Page p;
    p.hwnd   = hPage;
    p.kind   = kind;
    p.number = m_nextNumber[(int)kind]++;
    m_pages.push_back(p);
    int index = (int)m_pages.size() - 1;

    // The frame follows the page's title (SetWindowText) for the tab label.
    SetWindowSubclass(hPage, PageSubclassProc, PAGE_SUBCLASS_ID, (DWORD_PTR)this);

    std::wstring label = PageLabel(m_pages[index]);
    TCITEMW ti = {};
    ti.mask    = TCIF_TEXT | TCIF_IMAGE;
    ti.pszText = (LPWSTR)label.c_str();
    ti.iImage  = TOOL_IDLE;
    TabCtrl_InsertItem(m_hTabs, index, &ti);      // before the "+" tab

    RECT page = PageRect();
    SetWindowPos(hPage, HWND_TOP, page.left, page.top,
                 page.right - page.left, page.bottom - page.top,
                 SWP_NOACTIVATE);                   // stays hidden until activated
    UpdatePageTitle(index);
    return index;
}

void AppFrame::ClosePage(int index, bool confirm)
{
    if (index < 0 || index >= (int)m_pages.size()) return;
    if (m_pages.size() <= 1) {
        MessageBeep(MB_ICONASTERISK);              // the last tab stays
        return;
    }
    if (confirm && m_pages[index].state != TOOL_IDLE) {
        std::wstring q = PageLabel(m_pages[index]) + L"\n\nThis tab is in use. Close it and end its connection?";
        if (MessageBoxW(m_hWnd, q.c_str(), L"Close tab", MB_ICONQUESTION | MB_OKCANCEL | MB_DEFBUTTON2) != IDOK)
            return;
    }

    HWND hPage = m_pages[index].hwnd;
    bool wasActive = (index == m_active);

    // Pick the neighbour first, so the closing page is never the active one
    // while it is being destroyed.
    if (wasActive) {
        int next = (index + 1 < (int)m_pages.size()) ? index + 1 : index - 1;
        ActivatePage(next, true);
    }

    m_pages.erase(m_pages.begin() + index);
    TabCtrl_DeleteItem(m_hTabs, index);
    if (m_active > index) --m_active;
    TabCtrl_SetCurSel(m_hTabs, m_active);

    DestroyWindow(hPage);                          // the page stops its threads and frees itself
    OnSize();                                      // the tab row may have changed height
    UpdateCaption();
}

void AppFrame::ActivatePage(int index, bool setFocus)
{
    if (index < 0 || index >= (int)m_pages.size()) return;

    if (m_active >= 0 && m_active < (int)m_pages.size() && m_active != index) {
        Page& old = m_pages[m_active];
        HWND f = GetFocus();
        if (f && IsChild(old.hwnd, f)) old.lastFocus = f;
        ShowWindow(old.hwnd, SW_HIDE);
    }

    m_active = index;
    Page& p = m_pages[index];
    TabCtrl_SetCurSel(m_hTabs, index);

    // The tab is in front now: what it counted has been seen.
    if (p.unread != 0 || p.labelDirty) {
        p.unread = 0;
        p.labelDirty = false;
        UpdateTabItem(index);
    }

    RECT page = PageRect();
    SetWindowPos(p.hwnd, HWND_TOP, page.left, page.top,
                 page.right - page.left, page.bottom - page.top, SWP_SHOWWINDOW);

    if (setFocus) {
        HWND f = p.lastFocus;
        if (!f || !IsWindow(f) || !IsChild(p.hwnd, f) || !IsWindowEnabled(f) || !IsWindowVisible(f))
            f = GetNextDlgTabItem(p.hwnd, nullptr, FALSE);
        if (f) SetFocus(f);
    }
    UpdateCaption();
}

void AppFrame::StepPage(int delta)
{
    int n = (int)m_pages.size();
    if (n < 2 || m_active < 0) return;
    ActivatePage(((m_active + delta) % n + n) % n, true);
}

int AppFrame::FindPage(HWND hwnd) const
{
    for (int i = 0; i < (int)m_pages.size(); ++i)
        if (m_pages[i].hwnd == hwnd) return i;
    return -1;
}

std::wstring AppFrame::PageLabel(const Page& p) const
{
    std::wstring s = KindName(p.kind);
    if (p.number > 1) s += L" " + std::to_wstring(p.number);
    if (!p.detail.empty()) s += L"  –  " + p.detail;
    return s;
}

// The tab text: the label plus, for a tab that is not in front, the number of
// log events it has had since it was last looked at - "Server  (12)".
static std::wstring TabText(const std::wstring& label, int unread)
{
    if (unread <= 0) return label;
    return label + (unread > 999 ? std::wstring(L"  (999+)")
                                 : L"  (" + std::to_wstring(unread) + L")");
}

void AppFrame::OnToolEvent(HWND page, int count)
{
    int i = FindPage(page);
    if (i < 0 || i == m_active || count <= 0) return;
    Page& p = m_pages[i];
    p.unread = (p.unread > INT_MAX - count) ? INT_MAX : p.unread + count;
    // A busy tool logs thousands of events a second: only count here and let
    // a timer repaint the tab a few times a second.
    if (!p.labelDirty) {
        p.labelDirty = true;
        SetTimer(m_hWnd, IDT_UNREAD_LABELS, 200, nullptr);
    }
}

void AppFrame::FlushUnreadLabels()
{
    KillTimer(m_hWnd, IDT_UNREAD_LABELS);
    bool any = false;
    for (int i = 0; i < (int)m_pages.size(); ++i) {
        if (!m_pages[i].labelDirty) continue;
        m_pages[i].labelDirty = false;
        UpdateTabItem(i);
        any = true;
    }
    if (any) OnSize();                             // wider labels can wrap the tab row
}

// A page titles itself "TCP Client v1.2.3  –  host:port": everything after
// the dash is the detail shown on its tab.
void AppFrame::UpdatePageTitle(int index)
{
    Page& p = m_pages[index];
    std::wstring title = Utils::GetWindowText(p.hwnd);
    std::wstring detail;
    size_t dash = title.find(L'–');
    if (dash != std::wstring::npos) detail = Trim(title.substr(dash + 1));
    if (detail.size() > 48) detail = detail.substr(0, 47) + L"…";
    if (detail != p.detail) {
        p.detail = detail;
        UpdateTabItem(index);
        OnSize();                                  // a longer label can wrap the tab row
    }
    if (index == m_active) UpdateCaption();
}

void AppFrame::UpdateTabItem(int index)
{
    std::wstring label = TabText(PageLabel(m_pages[index]), m_pages[index].unread);
    TCITEMW ti = {};
    ti.mask    = TCIF_TEXT | TCIF_IMAGE;
    ti.pszText = (LPWSTR)label.c_str();
    ti.iImage  = (int)m_pages[index].state;
    TabCtrl_SetItem(m_hTabs, index, &ti);
}

void AppFrame::UpdateCaption()
{
    std::wstring caption = L"TcpTools v" TCPTOOLS_VERSION;
    if (m_active >= 0 && m_active < (int)m_pages.size())
        caption += L"  –  " + PageLabel(m_pages[m_active]);
    SetWindowTextW(m_hWnd, caption.c_str());
}

// ---- Menus ----------------------------------------------------------------

static void AppendNewTabItems(HMENU hMenu)
{
    AppendMenuW(hMenu, MF_STRING, IDM_NEW_CLIENT, L"New &Client tab");
    AppendMenuW(hMenu, MF_STRING, IDM_NEW_SERVER, L"New &Server tab");
    AppendMenuW(hMenu, MF_STRING, IDM_NEW_RELAY,  L"New &Relay tab");
}

void AppFrame::ShowNewTabMenu(POINT screenPt)
{
    HMENU hMenu = CreatePopupMenu();
    AppendNewTabItems(hMenu);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_HELP_TOOL, L"&Help for this tab\tF1");
    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT,     L"&About TcpTools");
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);
    RunCommand(cmd, m_active);
}

void AppFrame::ShowTabContextMenu(int index, POINT screenPt)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | (m_pages.size() > 1 ? 0 : MF_GRAYED), IDM_CLOSE_TAB, L"Close &tab\tCtrl+W");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendNewTabItems(hMenu);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, m_hWnd, nullptr);
    DestroyMenu(hMenu);
    RunCommand(cmd, index);
}

void AppFrame::RunCommand(int cmd, int pageIndex)
{
    switch (cmd) {
    case IDM_NEW_CLIENT:
    case IDM_NEW_SERVER:
    case IDM_NEW_RELAY:
    {
        ToolKind k = cmd == IDM_NEW_CLIENT ? ToolKind::Client
                   : cmd == IDM_NEW_SERVER ? ToolKind::Server : ToolKind::Relay;
        int i = AddPage(k);
        if (i >= 0) { OnSize(); ActivatePage(i, true); }
        break;
    }
    case IDM_CLOSE_TAB:
        ClosePage(pageIndex, true);
        break;
    case IDM_HELP_TOOL:
        if (m_active >= 0) SendMessageW(m_pages[m_active].hwnd, WM_TOOL_HELP, 0, 0);
        break;
    case IDM_ABOUT:
        ShowAbout();
        break;
    }
}

void AppFrame::ShowAbout()
{
    MessageBoxW(m_hWnd,
        L"TcpTools v" TCPTOOLS_VERSION L"\n"
        TCPTOOLS_COPYRIGHT L"\n"
        L"\n"
        L"TCP/UDP client, server and relay in one window.\n"
        L"\n"
        L"• Each tab is one tool. The “+” tab (Ctrl+T) adds another "
        L"client, server or relay; Ctrl+W or a middle-click closes a tab.\n"
        L"• Ctrl+Tab / Ctrl+Shift+Tab switch tabs.\n"
        L"• The dot on a tab shows its state: grey idle, amber waiting "
        L"(listening or connecting), green connected. A number in brackets "
        L"counts a tab's log events since it was last in front.\n"
        L"• F1 opens the help of the tab in front.\n"
        L"\n"
        L"Command line:\n"
        L"TcpTools.exe [/tab:client|server|relay]\n"
        L"    client:  /ip:<host>  /port:<n>  /autoconnect\n"
        L"    server:  /port:<n>  /autostart  /echo  /logfile:<path>\n"
        L"\n"
        L"Settings and history: %APPDATA%\\TcpTools\\",
        L"About TcpTools", MB_OK | MB_ICONINFORMATION);
}

// ---- Tab state dots (grey / amber / green) --------------------------------

void AppFrame::CreateStateImages()
{
    const int size = Dpi::Scale(16);
    m_hStateImages = ImageList_Create(size, size, ILC_COLOR32, 3, 0);
    if (!m_hStateImages) return;

    const Gdiplus::Color colors[3] = {
        Gdiplus::Color(255, 160, 164, 170),     // idle     grey
        Gdiplus::Color(255, 232, 160,  20),     // waiting  amber
        Gdiplus::Color(255,  40, 170,  70),     // active   green
    };
    for (const auto& c : colors) {
        Gdiplus::Bitmap bmp(size, size, PixelFormat32bppARGB);
        {
            Gdiplus::Graphics g(&bmp);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            const float d   = size * 0.56f;
            const float off = (size - d) / 2.0f;
            Gdiplus::SolidBrush fill(c);
            g.FillEllipse(&fill, off, off + size * 0.03f, d, d);
            Gdiplus::Pen rim(Gdiplus::Color(70, 0, 0, 0), 1.0f);
            g.DrawEllipse(&rim, off, off + size * 0.03f, d, d);
        }
        HBITMAP hbm = nullptr;
        if (bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbm) == Gdiplus::Ok && hbm) {
            ImageList_Add(m_hStateImages, hbm, nullptr);
            DeleteObject(hbm);
        }
    }
    TabCtrl_SetImageList(m_hTabs, m_hStateImages);
}

// ---- Subclasses -------------------------------------------------------------

LRESULT CALLBACK AppFrame::PageSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR id, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<AppFrame*>(ref);
    if (msg == WM_SETTEXT) {
        LRESULT r = DefSubclassProc(hWnd, msg, wParam, lParam);
        PostMessageW(self->m_hWnd, WM_FRAME_PAGE_TITLE, 0, (LPARAM)hWnd);
        return r;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, PageSubclassProc, id);
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK AppFrame::TabSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR id, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<AppFrame*>(ref);
    if (msg == WM_MBUTTONUP) {                     // middle-click closes a tab
        TCHITTESTINFO ht = {};
        ht.pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int i = TabCtrl_HitTest(hWnd, &ht);
        if (i >= 0 && i < self->PlusIndex()) self->ClosePage(i, true);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, TabSubclassProc, id);
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ---- Keyboard shortcuts ------------------------------------------------------

bool AppFrame::PreTranslateMessage(HWND hFrame, MSG* msg)
{
    if (msg->message != WM_KEYDOWN) return false;
    if (GetAncestor(msg->hwnd, GA_ROOT) != hFrame) return false;   // a message box, a tooltip...
    AppFrame* self = FromHwnd(hFrame);
    if (!self) return false;

    const bool ctrl  = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT)   < 0;
    const bool alt   = GetKeyState(VK_MENU)    < 0;                // AltGr = Ctrl+Alt: not a shortcut
    if (alt) return false;

    if (!ctrl) {
        if (msg->wParam == VK_F1) {
            if (self->m_active >= 0)
                SendMessageW(self->m_pages[self->m_active].hwnd, WM_TOOL_HELP, 0, 0);
            return true;
        }
        return false;
    }

    switch (msg->wParam) {
    case VK_TAB:
        self->StepPage(shift ? -1 : +1);
        return true;
    case 'T':
    {
        RECT r; TabCtrl_GetItemRect(self->m_hTabs, self->PlusIndex(), &r);
        POINT pt = { r.left, r.bottom };
        ClientToScreen(self->m_hTabs, &pt);
        self->ShowNewTabMenu(pt);
        return true;
    }
    case 'W':
    case VK_F4:
        self->ClosePage(self->m_active, true);
        return true;
    }
    return false;
}

// ---- Window placement ---------------------------------------------------------

void AppFrame::LoadPlacement(int nCmdShow)
{
    std::wstring ini = IniPath();
    wchar_t buf[128] = {};
    if (!ini.empty())
        GetPrivateProfileStringW(L"Window", L"Placement", L"", buf, 128, ini.c_str());

    int l, t, r, b, maximized;
    if (swscanf_s(buf, L"%d,%d,%d,%d,%d", &l, &t, &r, &b, &maximized) == 5 &&
        r - l >= 400 && b - t >= 300)
    {
        WINDOWPLACEMENT wp = {};
        wp.length  = sizeof(wp);
        wp.rcNormalPosition = { l, t, r, b };
        // Honour "start minimized" etc.; otherwise come back as it was closed.
        if (nCmdShow == SW_SHOWNORMAL || nCmdShow == SW_SHOWDEFAULT || nCmdShow == SW_SHOW)
            wp.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        else
            wp.showCmd = nCmdShow;
        SetWindowPlacement(m_hWnd, &wp);       // moves an off-screen rectangle back onto a monitor
        return;
    }
    ShowWindow(m_hWnd, nCmdShow);
}

void AppFrame::SavePlacement()
{
    std::wstring ini = IniPath();
    if (ini.empty()) return;
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(m_hWnd, &wp)) return;
    wchar_t buf[128];
    swprintf_s(buf, L"%d,%d,%d,%d,%d",
               wp.rcNormalPosition.left, wp.rcNormalPosition.top,
               wp.rcNormalPosition.right, wp.rcNormalPosition.bottom,
               (wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED)) ? 1 : 0);
    WritePrivateProfileStringW(L"Window", L"Placement", buf, ini.c_str());
}
