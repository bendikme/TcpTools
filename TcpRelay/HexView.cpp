#include "pch.h"
#include "HexView.h"

// ============================================================
//  HexView implementation
// ============================================================

static HFONT CreateMonoFont(int height = -14)
{
    LOGFONTW lf = {};
    lf.lfHeight         = height;
    lf.lfWeight         = FW_NORMAL;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lf.lfCharSet        = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    HFONT f = CreateFontIndirectW(&lf);
    if (!f) {
        wcscpy_s(lf.lfFaceName, L"Courier New");
        f = CreateFontIndirectW(&lf);
    }
    return f;
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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
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
    s->totalLines = (int)((data.size() + HEX_BYTES_PER_ROW - 1) / HEX_BYTES_PER_ROW);
    OnSize(hHex, *s);
    UpdateScroll(hHex, *s);
    InvalidateRect(hHex, nullptr, TRUE);
}

void HexView::ClearData(HWND hHex)
{
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(hHex, 0));
    if (!s) return;
    s->data.clear();
    s->scrollPos  = 0;
    s->wheelAccum = 0;
    s->totalLines = 0;
    UpdateScroll(hHex, *s);
    InvalidateRect(hHex, nullptr, TRUE);
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

void HexView::UpdateScroll(HWND hWnd, State& s)
{
    int contentPx = s.totalLines * s.lineHeight;
    RECT rc; GetClientRect(hWnd, &rc);
    int viewH = rc.bottom - rc.top;
    int maxScroll = std::max(0, contentPx - viewH + s.lineHeight + 4); // +header

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = maxScroll + viewH - 1;
    si.nPage  = (UINT)viewH;
    si.nPos   = s.scrollPos;
    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

void HexView::Paint(HWND hWnd, State& s)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);

    // Background
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

    if (!s.hFont) { EndPaint(hWnd, &ps); return; }

    HFONT oldFont = (HFONT)SelectObject(hdc, s.hFont);
    SetBkMode(hdc, TRANSPARENT);

    int lh = s.lineHeight;
    int cw = s.charWidth;

    // Column positions (all in chars from left, then convert to pixels)
    auto px = [&](int chars) { return chars * cw + 4; };

    int offsetCol = px(0);
    int hex1Col   = px(10);
    int hex2Col   = px(10 + 3*8 + 1);
    int asciiCol  = px(10 + 3*8 + 1 + 3*8 + 2);

    // Draw header
    SetTextColor(hdc, RGB(100, 100, 200));
    {
        wchar_t hdr[128];
        swprintf_s(hdr, L"  Offset  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   ASCII");
        TextOutW(hdc, 4, 2, hdr, (int)wcslen(hdr));
    }

    // Divider line
    MoveToEx(hdc, 4, lh + 2, nullptr);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 200));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    LineTo(hdc, rc.right - 4, lh + 2);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    int topLine = s.scrollPos / lh;
    int yOff    = -(s.scrollPos % lh);
    int yBase   = lh + 4 + yOff;

    for (int line = topLine; ; ++line) {
        int y = yBase + (line - topLine) * lh;
        if (y >= rc.bottom) break;
        if ((size_t)(line * HEX_BYTES_PER_ROW) >= s.data.size()) break;

        size_t rowStart = (size_t)line * HEX_BYTES_PER_ROW;
        int    rowLen   = (int)std::min((size_t)HEX_BYTES_PER_ROW, s.data.size() - rowStart);

        // Offset
        SetTextColor(hdc, RGB(100, 100, 200));
        wchar_t offBuf[16];
        swprintf_s(offBuf, L"%08X", (UINT)rowStart);
        TextOutW(hdc, 4, y, offBuf, 8);

        // Hex bytes + ASCII
        SetTextColor(hdc, RGB(20, 20, 20));
        wchar_t hexBuf[3];
        wchar_t asciiStr[HEX_BYTES_PER_ROW + 1] = {};
        int xHex = px(10);

        for (int i = 0; i < rowLen; ++i) {
            BYTE b = s.data[rowStart + i];
            swprintf_s(hexBuf, L"%02X", b);
            TextOutW(hdc, xHex, y, hexBuf, 2);
            xHex += 3 * cw;
            if (i == 7) xHex += cw;

            asciiStr[i] = (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
        }
        for (int i = rowLen; i < HEX_BYTES_PER_ROW; ++i) asciiStr[i] = L' ';
        asciiStr[HEX_BYTES_PER_ROW] = 0;

        // ASCII column
        SetTextColor(hdc, RGB(0, 100, 0));
        int xAscii = px(10 + 3*8 + 1 + 3*8 + 3);
        TextOutW(hdc, xAscii, y, asciiStr, HEX_BYTES_PER_ROW);
    }

    SelectObject(hdc, oldFont);
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
        s->hFont = CreateMonoFont(-14);
        HDC hdc = GetDC(hWnd);
        HFONT old = (HFONT)SelectObject(hdc, s->hFont);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        s->lineHeight = tm.tmHeight + tm.tmExternalLeading + 2;
        s->charWidth  = tm.tmAveCharWidth;
        SelectObject(hdc, old);
        ReleaseDC(hWnd, hdc);
        return 0;
    }
    case WM_DESTROY:
        if (s) {
            if (s->hFont) DeleteObject(s->hFont);
            delete s;
        }
        return 0;

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
        int lh = s->lineHeight > 0 ? s->lineHeight : 1;

        SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(hWnd, SB_VERT, &si);
        int pos = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:     pos -= lh;              break;
        case SB_LINEDOWN:   pos += lh;              break;
        case SB_PAGEUP:     pos -= (int)si.nPage;   break;
        case SB_PAGEDOWN:   pos += (int)si.nPage;   break;
        case SB_THUMBTRACK: pos = si.nTrackPos;      break;
        case SB_TOP:        pos = si.nMin;           break;
        case SB_BOTTOM:     pos = si.nMax;           break;
        }
        int maxScroll = std::max(0, (int)(si.nMax - (int)si.nPage + 1));
        pos = std::max(0, std::min(maxScroll, pos));
        if (pos != s->scrollPos) {
            s->scrollPos = pos;
            UpdateScroll(hWnd, *s);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        if (!s) break;
        int lh = s->lineHeight > 0 ? s->lineHeight : 1;

        s->wheelAccum += GET_WHEEL_DELTA_WPARAM(wParam);
        int pixelDelta = (s->wheelAccum * lh * 3) / WHEEL_DELTA;
        if (pixelDelta == 0) return 0;
        s->wheelAccum -= (pixelDelta * WHEEL_DELTA) / (lh * 3);

        int contentPx = s->totalLines * lh;
        RECT rc; GetClientRect(hWnd, &rc);
        int viewH = rc.bottom - rc.top;
        int maxScroll = std::max(0, contentPx - viewH + lh + 4);

        int newPos = std::max(0, std::min(maxScroll, s->scrollPos - pixelDelta));
        if (newPos != s->scrollPos) {
            s->scrollPos = newPos;
            UpdateScroll(hWnd, *s);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
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

    case WM_SETFOCUS:
        CreateCaret(hWnd, nullptr, 0, 0);
        return 0;
    case WM_KILLFOCUS:
        DestroyCaret();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
