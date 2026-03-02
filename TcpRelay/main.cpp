#include "pch.h"
#include "RelayMainWindow.h"
#include "HexView.h"

// ============================================================
//  WinMain entry point for TcpRelay
// ============================================================

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPWSTR    /*lpCmdLine*/,
    _In_     int       nCmdShow)
{
    // Enable visual styles
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    // Register custom controls
    if (!HexView::RegisterClass(hInstance)) {
        MessageBoxW(nullptr, L"Failed to register HexView class.", L"Fatal", MB_ICONERROR);
        return 1;
    }
    if (!RelayMainWindow::RegisterClass(hInstance)) {
        MessageBoxW(nullptr, L"Failed to register RelayMainWindow class.", L"Fatal", MB_ICONERROR);
        return 1;
    }

    // Create main window
    HWND hWnd = RelayMainWindow::Create(hInstance, nCmdShow);
    if (!hWnd) {
        MessageBoxW(nullptr, L"Failed to create main window.", L"Fatal", MB_ICONERROR);
        return 1;
    }

    // Message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
