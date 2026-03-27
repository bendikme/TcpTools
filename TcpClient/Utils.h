#pragma once
#include "pch.h"

// ============================================================
//  Utilities
// ============================================================

namespace Utils
{
    // ---- String conversions --------------------------------
    inline std::wstring ToWide(const std::string& s) {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }
    inline std::string ToUTF8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }
    inline std::string ToANSI(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0);
        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    // ---- Window helpers ------------------------------------
    inline std::wstring GetWindowText(HWND hWnd) {
        int len = ::GetWindowTextLengthW(hWnd);
        if (len <= 0) return {};
        std::wstring s(len + 1, 0);
        ::GetWindowTextW(hWnd, s.data(), len + 1);
        s.resize(len);
        return s;
    }
    inline void SetWindowText(HWND hWnd, const std::wstring& s) {
        ::SetWindowTextW(hWnd, s.c_str());
    }
    inline int GetDlgInt(HWND hDlg, int id, int def = 0) {
        BOOL ok = FALSE;
        int v = (int)GetDlgItemInt(hDlg, id, &ok, TRUE);
        return ok ? v : def;
    }

    // ---- Timing --------------------------------------------
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = std::chrono::duration<double, std::milli>; // ms

    inline TimePoint Now() { return Clock::now(); }

    inline double ElapsedMs(TimePoint start, TimePoint end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    inline std::wstring FormatMs(double ms) {
        if (ms < 0) return L"---";
        if (ms < 1000.0)
            return std::to_wstring((long long)std::round(ms)) + L" ms";
        return std::wstring(L"") + [&](){
            std::wostringstream ss;
            ss << std::fixed << std::setprecision(2) << ms / 1000.0 << L" s";
            return ss.str();
        }();
    }

    inline std::wstring NowTimestamp() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return buf;
    }

    // ---- Hex -----------------------------------------------
    inline std::string BytesToHexStr(const std::vector<BYTE>& data) {
        std::ostringstream ss;
        for (size_t i = 0; i < data.size(); ++i) {
            if (i) ss << ' ';
            ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)data[i];
        }
        return ss.str();
    }

    // Parse "DE AD BE EF" or "DEADBEEF" -> bytes
    inline bool HexStrToBytes(const std::string& hex, std::vector<BYTE>& out) {
        out.clear();
        std::string h;
        for (char c : hex) if (c != ' ' && c != '\t' && c != '\n' && c != '\r') h += c;
        if (h.size() % 2 != 0) return false;
        for (size_t i = 0; i < h.size(); i += 2) {
            char buf[3] = { h[i], h[i+1], 0 };
            char* ep = nullptr;
            long v = strtol(buf, &ep, 16);
            if (*ep != 0) return false;
            out.push_back((BYTE)v);
        }
        return true;
    }

    // ---- File dialog ---------------------------------------
    inline bool OpenFileDialog(HWND owner, std::wstring& pathOut) {
        wchar_t buf[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = owner;
        ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.txt\0Binary Files\0*.bin\0\0";
        ofn.lpstrFile   = buf;
        ofn.nMaxFile    = MAX_PATH;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            pathOut = buf;
            return true;
        }
        return false;
    }

    // ---- Clipboard -----------------------------------------
    inline bool CopyToClipboard(HWND hWnd, const std::wstring& text) {
        if (!OpenClipboard(hWnd)) return false;
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!hg) { CloseClipboard(); return false; }
        memcpy(GlobalLock(hg), text.c_str(), bytes);
        GlobalUnlock(hg);
        SetClipboardData(CF_UNICODETEXT, hg);
        CloseClipboard();
        return true;
    }

    // ---- DPI helpers ---------------------------------------
    inline int ScaleDPI(HWND hWnd, int pixels) {
        HDC hdc  = GetDC(hWnd);
        int dpi  = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(hWnd, hdc);
        return MulDiv(pixels, dpi, 96);
    }

    // ---- Prettify bytes ------------------------------------
    inline std::wstring FormatBytes(size_t bytes) {
        if (bytes < 1024) return std::to_wstring(bytes) + L" B";
        if (bytes < 1024*1024) {
            std::wostringstream ss; ss << std::fixed << std::setprecision(1) << bytes/1024.0 << L" KB"; return ss.str();
        }
        std::wostringstream ss; ss << std::fixed << std::setprecision(2) << bytes/(1024.0*1024.0) << L" MB"; return ss.str();
    }

    // ---- Input validation ----------------------------------
    inline bool IsValidPort(const std::wstring& s, int& portOut) {
        if (s.empty()) return false;
        for (wchar_t c : s) { if (c < L'0' || c > L'9') return false; }
        int v = _wtoi(s.c_str());
        if (v < 1 || v > 65535) return false;
        portOut = v;
        return true;
    }

    inline bool IsValidHostOrIP(const std::wstring& s) {
        if (s.empty()) return false;
        // Reject strings with spaces or control chars
        for (wchar_t c : s) {
            if (c <= 0x20 || c == 0x7F) return false;
        }
        // Must start and end with alphanumeric or digit (not '.' or '-')
        wchar_t first = s.front(), last = s.back();
        if (!iswalnum(first) && first != L'[') return false; // '[' for IPv6 bracket notation
        if (!iswalnum(last) && last != L']') return false;
        return true;
    }

    inline bool IsPortAvailable(int port, bool udp) {
        WSADATA wsa;
        bool needInit = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

        SOCKET s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM,
                          udp ? IPPROTO_UDP : IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            if (needInit) WSACleanup();
            return false;
        }
        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((u_short)port);
        addr.sin_addr.s_addr = INADDR_ANY;
        bool ok = (bind(s, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR);
        closesocket(s);
        if (needInit) WSACleanup();
        return ok;
    }

    // ---- Connection history ---------------------------------
    inline std::wstring GetHistoryFilePath(const wchar_t* appName) {
        wchar_t appData[MAX_PATH] = {};
        if (!GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) return {};
        std::wstring dir = std::wstring(appData) + L"\\TcpTools";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\" + appName + L"_history.txt";
    }

    inline std::vector<std::wstring> LoadHistory(const wchar_t* appName) {
        std::vector<std::wstring> entries;
        std::wstring path = GetHistoryFilePath(appName);
        if (path.empty()) return entries;
        std::wifstream f(path);
        if (!f) return entries;
        std::wstring line;
        while (std::getline(f, line)) {
            if (!line.empty()) entries.push_back(line);
        }
        return entries;
    }

    inline void SaveHistory(const wchar_t* appName, const std::wstring& entry,
                            std::vector<std::wstring>& entries, int maxEntries = 10) {
        // Remove duplicate if present
        entries.erase(std::remove(entries.begin(), entries.end(), entry), entries.end());
        // Insert at front (most recent first)
        entries.insert(entries.begin(), entry);
        // Limit size
        if ((int)entries.size() > maxEntries) entries.resize(maxEntries);
        // Write to file
        std::wstring path = GetHistoryFilePath(appName);
        if (path.empty()) return;
        std::wofstream f(path, std::ios::trunc);
        if (!f) return;
        for (auto& e : entries) f << e << L"\n";
    }

} // namespace Utils
