#include "pch.h"
#include "ServerMainWindow.h"

// -------------------------------------------------------
//  Parse /key:value  or  /key  style command-line args
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

// -------------------------------------------------------
//  Entry point
// -------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    // Parse raw argv[]
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    LocalFree(argv);

    // Extract options
    std::wstring portStr    = GetArg(args, L"/port:");
    std::wstring logFile    = GetArg(args, L"/logfile:");
    bool autoStart          = HasFlag(args, L"/autostart");
    bool echoMode           = HasFlag(args, L"/echo");

    int defaultPort = 9001;
    if (!portStr.empty()) {
        int p = _wtoi(portStr.c_str());
        if (p > 0 && p <= 65535) defaultPort = p;
    }

    // Init common controls (for ListView)
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    // Make sure the custom hex-view control class is registered
    HexView::RegisterClass(hInst);

    // Register and create main window
    if (!ServerMainWindow::RegisterClass(hInst)) {
        MessageBoxW(nullptr, L"RegisterClass failed.", L"Fatal error", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = ServerMainWindow::Create(hInst, nCmdShow,
                                          defaultPort, autoStart, echoMode, logFile);
    if (!hWnd) {
        MessageBoxW(nullptr, L"CreateWindow failed.", L"Fatal error", MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
