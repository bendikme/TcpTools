#pragma once
#include "pch.h"

// ============================================================
//  TextView - read-only text view for received data
//
//  Stands where a multi-line read-only EDIT control stood. An EDIT lays its
//  whole text out again and again as it grows (about 2 microseconds per
//  appended byte: seconds for a megabyte). This view keeps the text and an
//  index of its lines, and draws only the rows that are visible, so appending
//  costs what is appended and a megabyte is on screen at once.
//
//    - text is shown in a fixed-pitch grid and wrapped at the window's width
//    - the view follows new text while it shows the end, and stays where it
//      is when the user has scrolled back
//    - select with the mouse (drag, Shift+click, double-click a word), Ctrl+A,
//      Ctrl+C or the context menu; wheel, scroll bar, arrows, Page Up / Down,
//      Home / End scroll
//    - WM_SETTEXT / WM_GETTEXT / WM_GETTEXTLENGTH work as for any window
//      (GetWindowText, the tests), WM_SETFONT sets the font (not owned)
//
//  Lines end with CR LF or LF, as Utils::BytesToDisplayText produces them.
// ============================================================
class TextView
{
public:
    static bool RegisterClass(HINSTANCE hInst);
    static const wchar_t* ClassName() { return L"TcpToolsTextView"; }

    // Replace the text. showEnd: scroll to the end (live data), else to the top.
    static void   SetText(HWND hView, const std::wstring& text, bool showEnd = true);
    // Add text at the end; the view follows only when it was showing the end.
    static void   AppendText(HWND hView, const std::wstring& text);
    static void   Clear(HWND hView);
    static size_t GetLength(HWND hView);

private:
    struct Line {
        size_t   start  {0};        // offset of the line's first character in `text`
        uint32_t len    {0};        // characters, without the line end
        bool     simple {true};     // every character is one cell wide: rows by arithmetic
    };

    struct State {
        std::wstring          text;
        std::vector<Line>     lines;        // never empty
        std::vector<uint32_t> rows;         // display rows of each line at the current width
        std::vector<uint32_t> rowStart;     // first display row of each line
        uint32_t totalRows   {1};

        HFONT hFont      {nullptr};         // set by WM_SETFONT (owned by the sender)
        HFONT hOwnFont   {nullptr};         // used until a font is set
        int   lineHeight {16};
        int   charWidth  {8};
        int   cols       {80};
        int   visibleRows{1};
        int   topRow     {0};
        int   wheelAccum {0};

        size_t selAnchor {0};               // selection = [min(anchor, caret), max(anchor, caret))
        size_t selCaret  {0};
        bool   selecting {false};           // mouse captured for a drag selection
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static State* Get(HWND h) { return reinterpret_cast<State*>(GetWindowLongPtrW(h, 0)); }

    static void   Reset(State&);
    static void   AppendRaw(State&, const wchar_t* s, size_t n);
    static void   Reflow(HWND, State&, size_t firstLine);
    static void   Measure(HWND, State&);
    static void   Layout(HWND, State&);
    static void   UpdateScroll(HWND, State&);
    static void   ScrollTo(HWND, State&, int row);
    static int    MaxTop(const State&);
    static uint32_t RowsOf(const State&, const Line&);
    static void   RowRange(const State&, const Line&, uint32_t row, size_t& a, size_t& b);
    static size_t LineOfRow(const State&, uint32_t row);
    static uint32_t RowOfOffset(const State&, size_t pos);
    static size_t HitTest(HWND, const State&, int x, int y);
    static void   Paint(HWND, State&);
    static void   Copy(HWND, const State&);
    static void   SelectWord(State&, size_t pos);
};
