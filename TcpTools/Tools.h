#pragma once
#include "pch.h"

// ============================================================
//  Tools - how the frame makes tool pages.
//
//  Each tool implements its part at the end of its own main
//  window .cpp, so the frame never includes a tool's headers
//  (every tool has its own resource.h with its own IDs).
//  A page is a child window of `parent`; it deletes its own
//  state when it is destroyed.
// ============================================================
namespace Tools
{
    bool RegisterClientClass(HINSTANCE hInst);
    bool RegisterServerClass(HINSTANCE hInst);
    bool RegisterRelayClass(HINSTANCE hInst);

    HWND CreateClient(HINSTANCE hInst, HWND parent,
                      const std::wstring& host, int port, bool autoConnect);
    HWND CreateServer(HINSTANCE hInst, HWND parent,
                      int port, bool autoStart, bool echoMode, const std::wstring& logFile);
    HWND CreateRelay(HINSTANCE hInst, HWND parent);
}
