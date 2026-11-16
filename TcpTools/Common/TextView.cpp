#include "pch.h"
#include "TextView.h"
#include "Utils.h"

// ============================================================
//  TextView implementation
// ============================================================

namespace
{
    // Lines longer than this are split in the line index (never in the text),
    // so that no single line has to be scanned at megabyte length.
    constexpr uint32_t MAX_LINE   = 8192;
    constexpr UINT_PTR DRAG_TIMER = 1;
    constexpr UINT     IDM_COPY = 1, IDM_SELECT_ALL = 2;

    // Cells a UTF-16 unit takes in the fixed-pitch grid.
    inline uint32_t CellWidth(wchar_t c)
    {
        if (c < 0x1100) return 1;
        if (c >= 0xDC00 && c <= 0xDFFF) return 0;            // second half of a pair: drawn with the first
        if (c >= 0xD800 && c <= 0xDBFF) return 2;            // emoji, rare CJK
        if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
            (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
            (c >= 0xFFE0 && c <= 0xFFE6))
            return 2;                                        // East Asian wide
        return 1;
    }

    inline bool IsWordChar(wchar_t c) { return iswalnum(c) || c == L'_'; }

    int LeftMargin() { return Dpi::Scale(4); }
    int TopMargin()  { return Dpi::Scale(2); }
}

// ---- Registration --------------------------------------------------------

bool TextView::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.cbWndExtra    = sizeof(LONG_PTR);
    wc.hInstance     = hInst;
    wc.hbrBackground = nullptr;                              // painted completely in WM_PAINT
    wc.hCursor       = LoadCursor(nullptr, IDC_IBEAM);
    wc.lpszClassName = ClassName();
    return RegisterClassExW(&wc) != 0;
}

// ---- Public API ------------------------------------------------------------

void TextView::SetText(HWND hView, const std::wstring& text, bool showEnd)
{
    State* s = Get(hView);
    if (!s) return;
    Reset(*s);
    AppendRaw(*s, text.data(), text.size());
    Reflow(hView, *s, 0);
    s->topRow = showEnd ? MaxTop(*s) : 0;
    UpdateScroll(hView, *s);
    InvalidateRect(hView, nullptr, FALSE);
}

void TextView::AppendText(HWND hView, const std::wstring& text)
{
    State* s = Get(hView);
    if (!s || text.empty()) return;

    // Follow the text only when the end is in view and nobody is selecting:
    // someone reading further up is not pulled away by new data.
    const bool follow = s->topRow >= MaxTop(*s) && !s->selecting;
    const size_t firstChanged = s->lines.size() - 1;         // the last line may grow

    AppendRaw(*s, text.data(), text.size());
    Reflow(hView, *s, firstChanged);
    if (follow) s->topRow = MaxTop(*s);
    UpdateScroll(hView, *s);
    InvalidateRect(hView, nullptr, FALSE);
}

void TextView::Clear(HWND hView)
{
    SetText(hView, std::wstring(), false);
}

size_t TextView::GetLength(HWND hView)
{
    State* s = Get(hView);
    return s ? s->text.size() : 0;
}

// ---- Text and line index -----------------------------------------------------

void TextView::Reset(State& s)
{
    s.text.clear();
    s.text.shrink_to_fit();
    s.lines.assign(1, Line{});
    s.rows.assign(1, 1);
    s.rowStart.assign(1, 0);
    s.totalRows = 1;
    s.topRow    = 0;
    s.selAnchor = s.selCaret = 0;
}

// Appends to the text and extends the line index. A line ends at LF; a CR
// before it belongs to the line end, not to the line.
void TextView::AppendRaw(State& s, const wchar_t* str, size_t n)
{
    if (n == 0) return;
    const size_t base = s.text.size();
    s.text.append(str, n);
    const wchar_t* t = s.text.data();

    for (size_t i = base; i < base + n; ++i) {
        const wchar_t c = t[i];
        Line& cur = s.lines.back();
        if (c == L'\n') {
            size_t end = i;
            if (end > cur.start && t[end - 1] == L'\r') --end;
            cur.len = (uint32_t)(end - cur.start);
            s.lines.push_back(Line{ i + 1, 0, true });
            continue;
        }
        if (c >= 0x1100) cur.simple = false;
        cur.len = (uint32_t)(i + 1 - cur.start);
        // Split an endless line in the index - but not between CR and LF, and
        // not inside a surrogate pair.
        if (cur.len >= MAX_LINE && c != L'\r' && !(c >= 0xD800 && c <= 0xDBFF))
            s.lines.push_back(Line{ i + 1, 0, true });
    }
}

uint32_t TextView::RowsOf(const State& s, const Line& l)
{
    const uint32_t cols = (uint32_t)std::max(1, s.cols);
    if (l.len == 0) return 1;
    if (l.simple) return (l.len + cols - 1) / cols;

    uint32_t rows = 1, used = 0;
    const wchar_t* p = s.text.data() + l.start;
    for (uint32_t i = 0; i < l.len; ++i) {
        const uint32_t w = CellWidth(p[i]);
        if (w && used && used + w > cols) { ++rows; used = 0; }
        used += w;
    }
    return rows;
}

// The characters [a, b) of the text that display row `row` of line `l` shows.
void TextView::RowRange(const State& s, const Line& l, uint32_t row, size_t& a, size_t& b)
{
    const uint32_t cols = (uint32_t)std::max(1, s.cols);
    if (l.simple) {
        a = l.start + std::min<size_t>(l.len, (size_t)row * cols);
        b = l.start + std::min<size_t>(l.len, (size_t)(row + 1) * cols);
        return;
    }
    uint32_t r = 0, used = 0;
    a = l.start;
    b = l.start + l.len;
    const wchar_t* p = s.text.data() + l.start;
    for (uint32_t i = 0; i < l.len; ++i) {
        const uint32_t w = CellWidth(p[i]);
        if (w && used && used + w > cols) {
            if (r == row) { b = l.start + i; return; }
            ++r; used = 0;
            if (r == row) a = l.start + i;
        }
        used += w;
    }
    if (r < row) a = b;
}

size_t TextView::LineOfRow(const State& s, uint32_t row)
{
    auto it = std::upper_bound(s.rowStart.begin(), s.rowStart.end(), row);
    return it == s.rowStart.begin() ? 0 : (size_t)(it - s.rowStart.begin()) - 1;
}

// The display row that shows text offset `pos`.
uint32_t TextView::RowOfOffset(const State& s, size_t pos)
{
    auto it = std::upper_bound(s.lines.begin(), s.lines.end(), pos,
                               [](size_t p, const Line& l) { return p < l.start; });
    const size_t li = it == s.lines.begin() ? 0 : (size_t)(it - s.lines.begin()) - 1;
    const Line& l = s.lines[li];
    const uint32_t rows = s.rows[li];
    for (uint32_t r = 0; r + 1 < rows; ++r) {
        size_t a, b;
        RowRange(s, l, r, a, b);
        if (pos < b) return s.rowStart[li] + r;
    }
    return s.rowStart[li] + rows - 1;
}

// Recomputes the rows of the lines from `firstLine` on (all of them after a
// change of width, only the tail after an append).
void TextView::Reflow(HWND, State& s, size_t firstLine)
{
    const size_t n = s.lines.size();
    s.rows.resize(n);
    s.rowStart.resize(n);
    for (size_t i = firstLine; i < n; ++i) {
        s.rows[i]     = RowsOf(s, s.lines[i]);
        s.rowStart[i] = i ? s.rowStart[i - 1] + s.rows[i - 1] : 0;
    }
    s.totalRows = s.rowStart[n - 1] + s.rows[n - 1];
}

// ---- Geometry -----------------------------------------------------------------

void TextView::Measure(HWND hWnd, State& s)
{
    HFONT font = s.hFont ? s.hFont : s.hOwnFont;
    HDC hdc = GetDC(hWnd);
    HFONT old = (HFONT)SelectObject(hdc, font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(hWnd, hdc);
    s.lineHeight = std::max<int>(1, tm.tmHeight + tm.tmExternalLeading);
    s.charWidth  = std::max<int>(1, tm.tmAveCharWidth);
}

int TextView::MaxTop(const State& s)
{
    return std::max(0, (int)s.totalRows - s.visibleRows);
}

// After a change of size or font: new column count, rows of every line, and the
// same text at the top as before (or the end, when the end was showing).
void TextView::Layout(HWND hWnd, State& s)
{
    RECT rc; GetClientRect(hWnd, &rc);
    const bool   atEnd   = s.topRow >= MaxTop(s) && s.topRow > 0;
    size_t topA = 0, topB = 0;
    {
        const size_t li = LineOfRow(s, (uint32_t)s.topRow);
        RowRange(s, s.lines[li], (uint32_t)s.topRow - s.rowStart[li], topA, topB);
    }

    s.cols        = std::max(1, (int)(rc.right - 2 * LeftMargin()) / s.charWidth);
    s.visibleRows = std::max(1, (int)(rc.bottom - TopMargin()) / s.lineHeight);
    Reflow(hWnd, s, 0);

    s.topRow = atEnd ? MaxTop(s) : std::min((int)RowOfOffset(s, topA), MaxTop(s));
    UpdateScroll(hWnd, s);
}

void TextView::UpdateScroll(HWND hWnd, State& s)
{
    s.topRow = std::max(0, std::min(s.topRow, MaxTop(s)));
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    // The bar stays (disabled) when there is nothing to scroll: a bar that comes
    // and goes changes the width, and with it the wrapping of every line.
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin   = 0;
    si.nMax   = (int)s.totalRows - 1;
    si.nPage  = (UINT)s.visibleRows;
    si.nPos   = s.topRow;
    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

void TextView::ScrollTo(HWND hWnd, State& s, int row)
{
    row = std::max(0, std::min(row, MaxTop(s)));
    if (row == s.topRow) return;
    s.topRow = row;
    UpdateScroll(hWnd, s);
    InvalidateRect(hWnd, nullptr, FALSE);
}

// The text offset under a point of the client area (the nearest character gap).
size_t TextView::HitTest(HWND, const State& s, int x, int y)
{
    int rowOnScreen = (y - TopMargin()) / s.lineHeight;
    if (y < TopMargin()) rowOnScreen = y < 0 ? -1 : 0;
    long long row = (long long)s.topRow + rowOnScreen;
    if (row < 0) row = 0;
    if (row >= (long long)s.totalRows) return s.text.size();

    const size_t li = LineOfRow(s, (uint32_t)row);
    size_t a, b;
    RowRange(s, s.lines[li], (uint32_t)row - s.rowStart[li], a, b);

    int px = LeftMargin();
    for (size_t i = a; i < b; ++i) {
        const int w = (int)CellWidth(s.text[i]) * s.charWidth;
        if (x < px + w / 2) return i;
        px += w;
    }
    return b;
}

// ---- Painting -----------------------------------------------------------------

void TextView::Paint(HWND hWnd, State& s)
{
    PAINTSTRUCT ps;
    HDC hdcWnd = BeginPaint(hWnd, &ps);
    RECT rc; GetClientRect(hWnd, &rc);
    const int cw = rc.right, ch = rc.bottom;
    if (cw <= 0 || ch <= 0) { EndPaint(hWnd, &ps); return; }

    HDC     hdc    = CreateCompatibleDC(hdcWnd);
    HBITMAP hbm    = CreateCompatibleBitmap(hdcWnd, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, hbm);
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));

    HFONT oldFont = (HFONT)SelectObject(hdc, s.hFont ? s.hFont : s.hOwnFont);
    SetBkMode(hdc, TRANSPARENT);

    const size_t selMin = std::min(s.selAnchor, s.selCaret);
    const size_t selMax = std::max(s.selAnchor, s.selCaret);
    const bool   focus  = GetFocus() == hWnd;
    const COLORREF textColor = GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF selText   = focus ? GetSysColor(COLOR_HIGHLIGHTTEXT) : textColor;
    HBRUSH         selBrush  = GetSysColorBrush(focus ? COLOR_HIGHLIGHT : COLOR_BTNFACE);

    std::wstring buf;
    std::vector<int> dx;

    size_t   li  = LineOfRow(s, (uint32_t)s.topRow);
    uint32_t sub = (uint32_t)s.topRow - s.rowStart[li];
    for (int y = TopMargin(); y < ch && li < s.lines.size(); y += s.lineHeight) {
        const Line& l = s.lines[li];
        size_t a, b;
        RowRange(s, l, sub, a, b);
        const bool lastRowOfLine = sub + 1 >= s.rows[li];

        // The row's characters as drawn (TAB and a stray CR show as a blank) and
        // the advance of each, so every glyph sits in its cell whatever font
        // Windows substitutes for it.
        buf.assign(s.text, a, b - a);
        dx.resize(buf.size());
        for (size_t i = 0; i < buf.size(); ++i) {
            dx[i] = (int)CellWidth(buf[i]) * s.charWidth;
            if (buf[i] == L'\t' || buf[i] == L'\r') buf[i] = L' ';
        }

        // Selected part of this row: [sa, sb) relative to the row.
        const size_t sa = std::min(std::max(selMin, a), b) - a;
        const size_t sb = std::min(std::max(selMax, a), b) - a;
        const bool breakSelected = lastRowOfLine && selMin <= b && selMax > b;   // the line end itself

        auto xOf = [&](size_t n) { int x = LeftMargin(); for (size_t i = 0; i < n; ++i) x += dx[i]; return x; };

        if (sb > sa || breakSelected) {
            RECT sel = { xOf(sa), y, xOf(sb) + (breakSelected ? s.charWidth / 2 : 0), y + s.lineHeight };
            FillRect(hdc, &sel, selBrush);
        }
        auto draw = [&](size_t from, size_t to, COLORREF color) {
            if (to <= from) return;
            SetTextColor(hdc, color);
            ExtTextOutW(hdc, xOf(from), y, 0, nullptr, buf.data() + from, (UINT)(to - from), dx.data() + from);
        };
        draw(0, sa, textColor);
        draw(sa, sb, selText);
        draw(sb, buf.size(), textColor);

        if (++sub >= s.rows[li]) { ++li; sub = 0; }
    }

    SelectObject(hdc, oldFont);
    BitBlt(hdcWnd, 0, 0, cw, ch, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(hbm);
    DeleteDC(hdc);
    EndPaint(hWnd, &ps);
}

// ---- Selection ------------------------------------------------------------------

void TextView::Copy(HWND hWnd, const State& s)
{
    const size_t a = std::min(s.selAnchor, s.selCaret), b = std::max(s.selAnchor, s.selCaret);
    if (b > a) Utils::CopyToClipboard(hWnd, s.text.substr(a, b - a));
}

void TextView::SelectWord(State& s, size_t pos)
{
    const size_t n = s.text.size();
    if (pos >= n) { s.selAnchor = s.selCaret = n; return; }
    size_t a = pos, b = pos;
    if (IsWordChar(s.text[pos])) {
        while (a > 0 && IsWordChar(s.text[a - 1])) --a;
        while (b < n && IsWordChar(s.text[b])) ++b;
    } else {
        b = pos + 1;
    }
    s.selAnchor = a;
    s.selCaret  = b;
}

// ---- Window procedure --------------------------------------------------------------

LRESULT CALLBACK TextView::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    State* s = Get(hWnd);

    if (msg == WM_NCCREATE) {
        s = new State();
        Reset(*s);
        s->hOwnFont = Dpi::CreateMonoFont(13);
        SetWindowLongPtrW(hWnd, 0, (LONG_PTR)s);
        Measure(hWnd, *s);
        // DefWindowProc would store the creation text as the window text; this
        // window's text is its content.
        ((CREATESTRUCTW*)lParam)->lpszName = nullptr;
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    if (!s) return DefWindowProcW(hWnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_NCDESTROY:
        if (s->hOwnFont) DeleteObject(s->hOwnFont);
        SetWindowLongPtrW(hWnd, 0, 0);
        delete s;
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    // ---- text, as for any window
    case WM_SETTEXT:
        SetText(hWnd, lParam ? std::wstring((const wchar_t*)lParam) : std::wstring(), false);
        return TRUE;
    case WM_GETTEXTLENGTH:
        return (LRESULT)s->text.size();
    case WM_GETTEXT:
    {
        if (wParam == 0 || !lParam) return 0;
        const size_t n = std::min<size_t>(s->text.size(), (size_t)wParam - 1);
        wchar_t* out = (wchar_t*)lParam;
        memcpy(out, s->text.data(), n * sizeof(wchar_t));
        out[n] = 0;
        return (LRESULT)n;
    }

    case WM_SETFONT:
        s->hFont = (HFONT)wParam;
        Measure(hWnd, *s);
        Layout(hWnd, *s);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)(s->hFont ? s->hFont : s->hOwnFont);

    case WM_SIZE:
        Layout(hWnd, *s);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_PAINT:
        Paint(hWnd, *s);
        return 0;
    case WM_ERASEBKGND:
        return 1;

    // ---- scrolling
    case WM_VSCROLL:
    {
        SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_TRACKPOS;
        GetScrollInfo(hWnd, SB_VERT, &si);
        int row = s->topRow;
        const int page = std::max(1, s->visibleRows - 1);
        switch (LOWORD(wParam)) {
        case SB_LINEUP:        row -= 1;               break;
        case SB_LINEDOWN:      row += 1;               break;
        case SB_PAGEUP:        row -= page;            break;
        case SB_PAGEDOWN:      row += page;            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: row = si.nTrackPos;     break;
        case SB_TOP:           row = 0;                break;
        case SB_BOTTOM:        row = MaxTop(*s);       break;
        }
        ScrollTo(hWnd, *s, row);
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) lines = (UINT)std::max(1, s->visibleRows - 1);
        s->wheelAccum += GET_WHEEL_DELTA_WPARAM(wParam) * (int)lines;
        const int rows = s->wheelAccum / WHEEL_DELTA;
        s->wheelAccum -= rows * WHEEL_DELTA;
        if (rows) ScrollTo(hWnd, *s, s->topRow - rows);
        return 0;
    }

    // ---- selection with the mouse
    case WM_LBUTTONDOWN:
    {
        SetFocus(hWnd);
        const size_t pos = HitTest(hWnd, *s, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        s->selCaret = pos;
        if (!(wParam & MK_SHIFT)) s->selAnchor = pos;
        s->selecting = true;
        SetCapture(hWnd);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        SelectWord(*s, HitTest(hWnd, *s, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (s->selecting) {
            const int y = GET_Y_LPARAM(lParam);
            RECT rc; GetClientRect(hWnd, &rc);
            s->selCaret = HitTest(hWnd, *s, GET_X_LPARAM(lParam), y);
            // Outside the view the timer keeps scrolling while the button is held.
            if (y < 0 || y >= rc.bottom) SetTimer(hWnd, DRAG_TIMER, 40, nullptr);
            else                         KillTimer(hWnd, DRAG_TIMER);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (wParam == DRAG_TIMER && s->selecting) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
            RECT rc; GetClientRect(hWnd, &rc);
            if (pt.y < 0)               ScrollTo(hWnd, *s, s->topRow - 2);
            else if (pt.y >= rc.bottom) ScrollTo(hWnd, *s, s->topRow + 2);
            s->selCaret = HitTest(hWnd, *s, pt.x, pt.y);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (s->selecting) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        s->selecting = false;
        KillTimer(hWnd, DRAG_TIMER);
        return 0;

    case WM_CONTEXTMENU:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pt.x == -1 && pt.y == -1) {                 // from the keyboard
            pt = { LeftMargin(), TopMargin() };
            ClientToScreen(hWnd, &pt);
        }
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (s->selAnchor != s->selCaret ? 0 : MF_GRAYED), IDM_COPY, L"&Copy\tCtrl+C");
        AppendMenuW(menu, MF_STRING | (s->text.empty() ? MF_GRAYED : 0), IDM_SELECT_ALL, L"Select &All\tCtrl+A");
        const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
        DestroyMenu(menu);
        if (cmd == IDM_COPY) Copy(hWnd, *s);
        if (cmd == IDM_SELECT_ALL) { s->selAnchor = 0; s->selCaret = s->text.size(); InvalidateRect(hWnd, nullptr, FALSE); }
        return 0;
    }

    // ---- keyboard
    case WM_GETDLGCODE:
        // The frame runs IsDialogMessage: keep the arrow keys for scrolling.
        return DLGC_WANTARROWS;
    case WM_KEYDOWN:
    {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        const int page = std::max(1, s->visibleRows - 1);
        switch (wParam) {
        case 'A':       if (ctrl) { s->selAnchor = 0; s->selCaret = s->text.size(); InvalidateRect(hWnd, nullptr, FALSE); } break;
        case 'C':
        case VK_INSERT: if (ctrl) Copy(hWnd, *s);                 break;
        case VK_UP:     ScrollTo(hWnd, *s, s->topRow - 1);        break;
        case VK_DOWN:   ScrollTo(hWnd, *s, s->topRow + 1);        break;
        case VK_PRIOR:  ScrollTo(hWnd, *s, s->topRow - page);     break;
        case VK_NEXT:   ScrollTo(hWnd, *s, s->topRow + page);     break;
        case VK_HOME:   ScrollTo(hWnd, *s, 0);                    break;
        case VK_END:    ScrollTo(hWnd, *s, MaxTop(*s));           break;
        }
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hWnd, nullptr, FALSE);       // the selection changes colour
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
