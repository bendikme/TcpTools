#pragma once
#include "pch.h"
#include "ToolHost.h"

// ============================================================
//  AppFrame - the TcpTools main window.
//
//  One top-level window with a tab strip; every tab hosts one
//  tool page (Client, Server or Relay) as a child window. The
//  last tab ("+") opens a menu that adds more pages, so several
//  clients / servers / relays can run side by side.
// ============================================================

enum class ToolKind : int { Client = 0, Server = 1, Relay = 2 };

// What the command line asked for (see main.cpp).
struct StartupOptions {
    ToolKind     startTab          {ToolKind::Client};
    std::wstring clientHost        {L"127.0.0.1"};
    int          clientPort        {9001};
    bool         clientAutoConnect {false};
    int          serverPort        {9001};
    bool         serverAutoStart   {false};
    bool         serverEcho        {false};
    std::wstring serverLogFile;
};

class AppFrame
{
public:
    static bool RegisterClass(HINSTANCE hInst);
    static HWND Create(HINSTANCE hInst, int nCmdShow, const StartupOptions& opt);

    // Called from the message loop before IsDialogMessage: frame-wide
    // shortcuts (F1, Ctrl+Tab, Ctrl+T, Ctrl+W). Returns true when handled.
    static bool PreTranslateMessage(HWND hFrame, MSG* msg);

private:
    struct Page {
        HWND         hwnd   {nullptr};
        ToolKind     kind   {ToolKind::Client};
        int          number {1};            // 1-based per kind: "Client", "Client 2", ...
        ToolState    state  {TOOL_IDLE};
        std::wstring detail;                // endpoint text taken from the page's title
        HWND         lastFocus {nullptr};   // control that had the focus when the tab was left
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PageSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK TabSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static AppFrame* FromHwnd(HWND hWnd);

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void    OnCreate(const StartupOptions& opt);
    void    OnSize();
    void    OnNotify(NMHDR* nm);

    int     AddPage(ToolKind kind, const StartupOptions* opt = nullptr);   // returns the page index, -1 on failure
    void    ClosePage(int index, bool confirm);
    void    ActivatePage(int index, bool setFocus);
    void    StepPage(int delta);
    int     FindPage(HWND hwnd) const;
    void    UpdatePageTitle(int index);
    void    UpdateTabItem(int index);
    void    UpdateCaption();
    std::wstring PageLabel(const Page& p) const;

    void    ShowNewTabMenu(POINT screenPt);
    void    ShowTabContextMenu(int index, POINT screenPt);
    void    RunCommand(int cmd, int pageIndex);
    void    ShowAbout();
    void    CreateStateImages();
    RECT    PageRect() const;
    int     PlusIndex() const { return (int)m_pages.size(); }

    void    LoadPlacement(int nCmdShow);
    void    SavePlacement();

    HWND        m_hWnd   {nullptr};
    HINSTANCE   m_hInst  {nullptr};
    HWND        m_hTabs  {nullptr};
    HFONT       m_hFont  {nullptr};
    HIMAGELIST  m_hStateImages {nullptr};

    std::vector<Page> m_pages;
    int         m_active {-1};
    int         m_nextNumber[3] {1, 1, 1};
};
