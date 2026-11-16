#include "pch.h"
#include "HexView.h"
#include "Utils.h"

// ============================================================
//  HexView implementation
// ============================================================

namespace
{
    constexpr int HEADER_GAP = 4;       // pixels between the header line and the first row

    int HeaderHeight(int lineHeight) { return lineHeight + HEADER_GAP; }
}

// ---- Class registration ----------------------------------------

bool HexView::RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbWndExtra    = sizeof(LONG_PTR); // store State*
    wc.hInstance     = hInst;
    wc.hbrBackground = nullptr;          // painted completely in WM_PAINT
    wc.lpszClassName = ClassName();
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    return RegisterClassExW(&wc) != 0;
}

HWND HexView::Create(HWND parent, int x, int y, int w, int h, UINT id, HINSTANCE hInst)
{
    return CreateWindowExW(
        WS_EX_CLIENTEDGE,
        ClassName(),
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        x, y, w, h,
        parent, (HMENU)(UINT_PTR)id, hInst, nullptr);
}

// ---- Public helpers --------------------------------------------

void HexView::SetData(HWND hHex, const std::vector<BYTE>& data)
{
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(hHex, 0));
    if (!s) return;
    s->data       = data;
    s->scrollPos  = 0;
    s->wheelAccum = 0;
    s->totalLines = (int)((s->data.size() + HEX_BYTES_PER_ROW - 1) / HEX_BYTES_PER_ROW);
    OnSize(hHex, *s);
    UpdateScroll(hHex, *s);
    InvalidateRect(hHex, nullptr, FALSE);
}

void HexView::AppendData(HWND hHex, const BYTE* data, size_t len)
{
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(hHex, 0));
    if (!s || !data || len == 0) return;

    // Follow the data only when the view already shows the end; someone who
    // scrolled back to read is not pulled away by new bytes.
    const bool atEnd = s->scrollPos >= MaxScroll(hHex, *s) - s->lineHeight;

    s->data.insert(s->data.end(), data, data + len);
    s->totalLines = (int)((s->data.size() + HEX_BYTES_PER_ROW - 1) / HEX_BYTES_PER_ROW);
    if (atEnd) s->scrollPos = MaxScroll(hHex, *s);
    UpdateScroll(hHex, *s);
    InvalidateRect(hHex, nullptr, FALSE);
}

void HexView::ClearData(HWND hHex)
{
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(hHex, 0));
    if (!s) return;
    s->data.clear();
    s->data.shrink_to_fit();
    s->scrollPos  = 0;
    s->wheelAccum = 0;
    s->totalLines = 0;
    UpdateScroll(hHex, *s);
    InvalidateRect(hHex, nullptr, FALSE);
}

size_t HexView::GetDataSize(HWND hHex)
{
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(hHex, 0));
    return s ? s->data.size() : 0;
}

// ---- Internal --------------------------------------------------

void HexView::OnSize(HWND hWnd, State& s)
{
    if (s.lineHeight <= 0) return;
    RECT rc;
    GetClientRect(hWnd, &rc);
    s.visibleLines = (rc.bottom - rc.top) / s.lineHeight;
    if (s.visibleLines < 1) s.visibleLines = 1;
}

// The largest scroll offset (pixels): the last row ends at the bottom edge.
int HexView::MaxScroll(HWND hWnd, const State& s)
{
    RECT rc; GetClientRect(hWnd, &rc);
    int viewH     = rc.bottom - rc.top;
    int contentPx = s.totalLines * s.lineHeight + HeaderHeight(s.lineHeight);
    return std::max(0, contentPx - viewH);
}

void HexView::UpdateScroll(HWND hWnd, State& s)
{
    RECT rc; GetClientRect(hWnd, &rc);
    int viewH     = rc.bottom - rc.top;
    int maxScroll = MaxScroll(hWnd, s);
    s.scrollPos   = std::max(0, std::min(s.scrollPos, maxScroll));

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = maxScroll + std::max(viewH, 1) - 1;
    si.nPage  = (UINT)std::max(viewH, 1);
    si.nPos   = s.scrollPos;
    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

void HexView::ScrollTo(HWND hWnd, State& s, int pos)
{
    pos = std::max(0, std::min(MaxScroll(hWnd, s), pos));
    if (pos == s.scrollPos) return;
    s.scrollPos = pos;
    UpdateScroll(hWnd, s);
    InvalidateRect(hWnd, nullptr, FALSE);
}

void HexView::Paint(HWND hWnd, State& s)
{
    PAINTSTRUCT ps;
    HDC hdcWnd = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);
    const int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) { EndPaint(hWnd, &ps); return; }

    // Draw off-screen and copy once: the view is repainted for every received
    // chunk and would flicker otherwise.
    HDC     hdc    = CreateCompatibleDC(hdcWnd);
    HBITMAP hbm    = CreateCompatibleBitmap(hdcWnd, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, hbm);

    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));

    if (s.hFont) {
        HFONT oldFont = (HFONT)SelectObject(hdc, s.hFont);
        SetBkMode(hdc, TRANSPARENT);

        const int lh     = s.lineHeight;
        const int chW    = s.charWidth;
        const int left   = Dpi::Scale(4);
        const int header = HeaderHeight(lh);

        // Column positions in characters from the left edge:
        // [8 offset] [2 spaces] [8 x "HH "] [1 space] [8 x "HH "] [2 spaces] [16 ASCII]
        auto px = [&](int chars) { return chars * chW + left; };

        const COLORREF colOffset = RGB(100, 100, 200);
        const COLORREF colHex    = GetSysColor(COLOR_WINDOWTEXT);
        const COLORREF colAscii  = RGB(0, 110, 0);

        // ---- Rows (clipped so that a half-scrolled row never covers the header)
        const int topLine = s.scrollPos / lh;
        const int yBase   = header - (s.scrollPos % lh);

        HRGN rowClip = CreateRectRgn(0, header, cw, ch);
        SelectClipRgn(hdc, rowClip);
        for (int line = topLine; ; ++line) {
            int y = yBase + (line - topLine) * lh;
            if (y >= ch) break;
            size_t rowStart = (size_t)line * HEX_BYTES_PER_ROW;
            if (rowStart >= s.data.size()) break;
            int rowLen = (int)std::min((size_t)HEX_BYTES_PER_ROW, s.data.size() - rowStart);

            wchar_t offBuf[16];
            swprintf_s(offBuf, L"%08X", (UINT)rowStart);
            SetTextColor(hdc, colOffset);
            TextOutW(hdc, px(0), y, offBuf, 8);

            // One TextOut for the hex part and one for the ASCII part of the row.
            wchar_t hexStr[HEX_BYTES_PER_ROW * 3 + 2];
            wchar_t asciiStr[HEX_BYTES_PER_ROW + 1];
            static const wchar_t digits[] = L"0123456789ABCDEF";
            int h = 0;
            for (int i = 0; i < rowLen; ++i) {
                BYTE b = s.data[rowStart + i];
                hexStr[h++] = digits[b >> 4];
                hexStr[h++] = digits[b & 0x0F];
                hexStr[h++] = L' ';
                if (i == 7) hexStr[h++] = L' ';       // gap between the two groups of 8
                asciiStr[i] = (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
            }
            SetTextColor(hdc, colHex);
            TextOutW(hdc, px(10), y, hexStr, h);
            SetTextColor(hdc, colAscii);
            TextOutW(hdc, px(10 + 3 * 8 + 1 + 3 * 8 + 3), y, asciiStr, rowLen);
        }
        SelectClipRgn(hdc, nullptr);
        DeleteObject(rowClip);

        // ---- Header + divider (fixed, on top)
        static const wchar_t hdr[] =
            L"  Offset  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   ASCII";
        SetTextColor(hdc, colOffset);
        TextOutW(hdc, left, Dpi::Scale(1), hdr, (int)wcslen(hdr));

        HPEN pen    = CreatePen(PS_SOLID, 1, RGB(180, 180, 200));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, left, header - 2, nullptr);
        LineTo(hdc, cw - left, header - 2);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        SelectObject(hdc, oldFont);
    }

    BitBlt(hdcWnd, 0, 0, cw, ch, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, oldBmp);
    DeleteObject(hbm);
    DeleteDC(hdc);
    EndPaint(hWnd, &ps);
}

// ---- WndProc ---------------------------------------------------

LRESULT CALLBACK HexView::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    State* s = reinterpret_cast<State*>(GetWindowLongPtrW(hWnd, 0));

    switch (msg)
    {
    case WM_CREATE:
    {
        s = new State();
        SetWindowLongPtrW(hWnd, 0, (LONG_PTR)s);
        s->hFont = Dpi::CreateMonoFont(14);
        // Measure char size
        HDC hdc = GetDC(hWnd);
        HFONT old = (HFONT)SelectObject(hdc, s->hFont);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        s->lineHeight = tm.tmHeight + tm.tmExternalLeading + 2;
        s->charWidth  = tm.tmAveCharWidth;
        SelectObject(hdc, old);
        ReleaseDC(hWnd, hdc);
        if (s->lineHeight < 1) s->lineHeight = 1;
        return 0;
    }
    case WM_NCDESTROY:
        if (s) {
            if (s->hFont) DeleteObject(s->hFont);
            SetWindowLongPtrW(hWnd, 0, 0);
            delete s;
        }
        break;

    case WM_SIZE:
        if (s) { OnSize(hWnd, *s); UpdateScroll(hWnd, *s); }
        return 0;

    case WM_PAINT:
        if (s) Paint(hWnd, *s);
        else { PAINTSTRUCT ps; BeginPaint(hWnd, &ps); EndPaint(hWnd, &ps); }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_VSCROLL:
    {
        if (!s) break;
        int lh = s->lineHeight;

        SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(hWnd, SB_VERT, &si);
        int pos = s->scrollPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:        pos -= lh;                       break;
        case SB_LINEDOWN:      pos += lh;                       break;
        case SB_PAGEUP:        pos -= std::max(lh, (int)si.nPage - lh); break;
        case SB_PAGEDOWN:      pos += std::max(lh, (int)si.nPage - lh); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos;              break;
        case SB_TOP:           pos = 0;                         break;
        case SB_BOTTOM:        pos = MaxScroll(hWnd, *s);       break;
        }
        ScrollTo(hWnd, *s, pos);
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        if (!s) break;
        int lh = s->lineHeight;

        // Accumulate sub-notch wheel input for smooth high-precision mice
        s->wheelAccum += GET_WHEEL_DELTA_WPARAM(wParam);
        int pixelDelta = (s->wheelAccum * lh * 3) / WHEEL_DELTA;
        if (pixelDelta == 0) return 0;
        s->wheelAccum -= (pixelDelta * WHEEL_DELTA) / (lh * 3);

        ScrollTo(hWnd, *s, s->scrollPos - pixelDelta);
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetFocus(hWnd);                 // so the keyboard can scroll it
        return 0;

    case WM_GETDLGCODE:
        // The frame runs IsDialogMessage: without this the arrow keys would
        // move the focus to the next control instead of scrolling.
        return DLGC_WANTARROWS;

    case WM_KEYDOWN:
        if (!s) break;
        switch (wParam) {
        case VK_UP:     SendMessage(hWnd, WM_VSCROLL, SB_LINEUP, 0);   break;
        case VK_DOWN:   SendMessage(hWnd, WM_VSCROLL, SB_LINEDOWN, 0); break;
        case VK_PRIOR:  SendMessage(hWnd, WM_VSCROLL, SB_PAGEUP, 0);   break;
        case VK_NEXT:   SendMessage(hWnd, WM_VSCROLL, SB_PAGEDOWN, 0); break;
        case VK_HOME:   SendMessage(hWnd, WM_VSCROLL, SB_TOP, 0);      break;
        case VK_END:    SendMessage(hWnd, WM_VSCROLL, SB_BOTTOM, 0);   break;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
