#pragma once
#include "pch.h"

// ============================================================
//  HexView - custom hex editor-style read-only control
//
//  Layout per row (16 bytes):
//    XXXXXXXX  HH HH HH HH HH HH HH HH  HH HH HH HH HH HH HH HH  ................
//    offset    hex bytes (group of 8)    hex bytes (group of 8)     ASCII
//
//  The control keeps its own copy of the bytes. Scroll with the wheel, the
//  scroll bar, or (after a click) the arrow / Page / Home / End keys.
// ============================================================

constexpr int HEX_BYTES_PER_ROW = 16;

class HexView
{
public:
    static bool       RegisterClass(HINSTANCE hInst);
    static HWND       Create(HWND parent, int x, int y, int w, int h, UINT id, HINSTANCE hInst);

    // Replace the content; the view goes back to the top.
    static void       SetData(HWND hHex, const std::vector<BYTE>& data);
    // Add bytes at the end. The view keeps its position, or follows the new
    // data when it was already showing the end. Use this for live data.
    static void       AppendData(HWND hHex, const BYTE* data, size_t len);
    static void       ClearData(HWND hHex);
    static size_t     GetDataSize(HWND hHex);

    static const wchar_t* ClassName() { return L"TcpToolsHexView"; }

private:
    struct State {
        std::vector<BYTE> data;
        int  lineHeight    {1};
        int  charWidth     {0};
        int  scrollPos     {0};   // pixel offset from top
        int  totalLines    {0};
        HFONT hFont        {nullptr};
        int  visibleLines  {0};
        int  wheelAccum    {0};   // sub-notch wheel accumulator
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void Paint(HWND, State&);
    static void UpdateScroll(HWND, State&);
    static void OnSize(HWND, State&);
    static int  MaxScroll(HWND, const State&);
    static void ScrollTo(HWND, State&, int pos);
};
