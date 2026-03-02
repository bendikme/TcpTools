#include "pch.h"
#include "MainWindow.h"
#include "HexView.h"

// -------------------------------------------------------
//  Arg helpers
// -------------------------------------------------------
static std::wstring GetArg(const std::vector<std::wstring>& args, const wchar_t* prefix)
{
    size_t plen = wcslen(prefix);
    for (const auto& a : args)
        if (_wcsnicmp(a.c_str(), prefix, plen) == 0 && a.size() > plen)
            return a.substr(plen);
    return L"";
}
static bool HasFlag(const std::vector<std::wstring>& args, const wchar_t* name)
{
    for (const auto& a : args)
        if (_wcsicmp(a.c_str(), name) == 0) return true;
    return false;
}

// ============================================================
//  WinMain entry point
// ============================================================

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPWSTR    /*lpCmdLine*/,
    _In_     int       nCmdShow)
{
    // Parse command-line args: /ip:addr /port:N /autoconnect
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    LocalFree(argv);

    std::wstring defaultIP   = GetArg(args, L"/ip:");
    std::wstring portStr     = GetArg(args, L"/port:");
    bool         autoConnect = HasFlag(args, L"/autoconnect");

    if (defaultIP.empty())   defaultIP   = L"127.0.0.1";
    int defaultPort = 9001;
    if (!portStr.empty()) {
        int p = _wtoi(portStr.c_str());
        if (p > 0 && p <= 65535) defaultPort = p;
    }

    // Enable visual styles (requires linker manifest pragma in pch.h)
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    // Register custom controls
    if (!HexView::RegisterClass(hInstance)) {
        MessageBoxW(nullptr, L"Failed to register HexView class.", L"Fatal", MB_ICONERROR);
        return 1;
    }
    if (!MainWindow::RegisterClass(hInstance)) {
        MessageBoxW(nullptr, L"Failed to register MainWindow class.", L"Fatal", MB_ICONERROR);
        return 1;
    }

    // Create main window
    HWND hWnd = MainWindow::Create(hInstance, nCmdShow, defaultIP, defaultPort, autoConnect);
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
