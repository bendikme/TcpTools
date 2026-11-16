#include "pch.h"
#include "AppFrame.h"
#include "HexView.h"
#include "Tools.h"
#include "Utils.h"

#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

// ============================================================
//  TcpTools - TCP/UDP client, server and relay in one window
//
//  Command line:
//    /tab:client|server|relay   the tab in front at start (default client)
//    Client:  /ip:<host>  /port:<n>  /autoconnect
//    Server:  /port:<n>   /autostart  /echo  /logfile:<path>
//  /port: applies to the tab named by /tab: (client when /tab: is absent).
// ============================================================

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

static StartupOptions ParseCommandLine()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        LocalFree(argv);
    }

    StartupOptions opt;

    std::wstring tab = GetArg(args, L"/tab:");
    bool serverFlags = HasFlag(args, L"/autostart") || HasFlag(args, L"/echo") ||
                       !GetArg(args, L"/logfile:").empty();
    if      (_wcsicmp(tab.c_str(), L"server") == 0) opt.startTab = ToolKind::Server;
    else if (_wcsicmp(tab.c_str(), L"relay")  == 0) opt.startTab = ToolKind::Relay;
    else if (_wcsicmp(tab.c_str(), L"client") == 0) opt.startTab = ToolKind::Client;
    else if (serverFlags)                           opt.startTab = ToolKind::Server;  // TcpServer.exe's switches

    std::wstring ip = GetArg(args, L"/ip:");
    if (!ip.empty()) opt.clientHost = ip;

    int port = 0;
    if (Utils::IsValidPort(GetArg(args, L"/port:"), port)) {
        if (opt.startTab == ToolKind::Server) opt.serverPort = port;
        else                                  opt.clientPort = port;
    }

    opt.clientAutoConnect = HasFlag(args, L"/autoconnect");
    opt.serverAutoStart   = HasFlag(args, L"/autostart");
    opt.serverEcho        = HasFlag(args, L"/echo");
    opt.serverLogFile     = GetArg(args, L"/logfile:");
    return opt;
}

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPWSTR    /*lpCmdLine*/,
    _In_     int       nCmdShow)
{
    StartupOptions opt = ParseCommandLine();

    // Winsock for the whole process (the tools start and stop sockets freely).
    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        MessageBoxW(nullptr, L"Winsock could not be started.", L"TcpTools", MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc),
        ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    int exitCode = 1;
    if (!HexView::RegisterClass(hInstance) ||
        !Tools::RegisterClientClass(hInstance) ||
        !Tools::RegisterServerClass(hInstance) ||
        !Tools::RegisterRelayClass(hInstance) ||
        !AppFrame::RegisterClass(hInstance))
    {
        MessageBoxW(nullptr, L"The window classes could not be registered.", L"TcpTools", MB_ICONERROR);
    }
    else if (HWND hFrame = AppFrame::Create(hInstance, nCmdShow, opt))
    {
        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (AppFrame::PreTranslateMessage(hFrame, &msg)) continue;
            // Tab / Shift+Tab / arrow-key navigation between the controls of a page.
            if (IsWindow(hFrame) && IsDialogMessageW(hFrame, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        exitCode = (int)msg.wParam;
    }
    else
    {
        MessageBoxW(nullptr, L"The main window could not be created.", L"TcpTools", MB_ICONERROR);
    }

    if (gdipToken) Gdiplus::GdiplusShutdown(gdipToken);
    WSACleanup();
    return exitCode;
}
