#pragma once
#include "pch.h"

// ============================================================
//  HexView - custom hex editor-style read-only control
//
//  Layout per row (16 bytes):
//    XXXXXXXX  HH HH HH HH HH HH HH HH  HH HH HH HH HH HH HH HH  ................
//    offset    hex bytes (group of 8)    hex bytes (group of 8)     ASCII
// ============================================================

constexpr int HEX_BYTES_PER_ROW = 16;

class HexView
{
public:
    static bool       RegisterClass(HINSTANCE hInst);
    static HWND       Create(HWND parent, int x, int y, int w, int h, UINT id, HINSTANCE hInst);

    // Set / clear data
    static void       SetData(HWND hHex, const std::vector<BYTE>& data);
    static void       ClearData(HWND hHex);

    static const wchar_t* ClassName() { return L"TcpClientHexView"; }

private:
    struct State {
        std::vector<BYTE> data;
        int  lineHeight    {0};
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
};
