// pch.h - Precompiled Header
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cassert>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
