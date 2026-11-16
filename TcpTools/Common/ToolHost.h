#pragma once
#include "pch.h"

// ============================================================
//  ToolHost - the contract between a tool page (Client, Server,
//  Relay) and the frame window that hosts it as a tab.
//
//  A page is a WS_CHILD window of the frame. It tells the frame
//  what it is doing by posting WM_TOOL_STATE to its parent; the
//  frame reads the page's window text (SetWindowText on the page)
//  as the tab's title.
// ============================================================

// page -> frame:  wParam = ToolState, lParam = (LPARAM)page HWND
#define WM_TOOL_STATE   (WM_APP + 200)

// frame -> page:  show the tool's help (F1)
#define WM_TOOL_HELP    (WM_APP + 201)

// page -> frame:  wParam = number of new log events, lParam = (LPARAM)page HWND.
// The frame counts them on the tab while the page is not the one in front.
#define WM_TOOL_EVENT   (WM_APP + 202)

enum ToolState : int {
    TOOL_IDLE    = 0,   // not connected / not listening
    TOOL_WAITING = 1,   // connecting, or listening without a peer
    TOOL_ACTIVE  = 2    // connected / relaying
};

inline void NotifyToolState(HWND page, ToolState state)
{
    HWND host = GetParent(page);
    if (host) PostMessageW(host, WM_TOOL_STATE, (WPARAM)state, (LPARAM)page);
}

// Call once for every entry a page adds to its log (from the UI thread, where
// the log lives). Sent, not posted: it is a plain call on the same thread, so a
// burst of events cannot flood the message queue; the frame only counts here
// and repaints its tabs on a timer.
inline void NotifyToolEvent(HWND page, int count = 1)
{
    HWND host = GetParent(page);
    if (host && count > 0) SendMessageW(host, WM_TOOL_EVENT, (WPARAM)count, (LPARAM)page);
}

// Window styles every tool page is created with.
constexpr DWORD TOOL_PAGE_STYLE   = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD TOOL_PAGE_EXSTYLE = WS_EX_CONTROLPARENT;
