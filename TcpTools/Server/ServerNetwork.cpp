#include "pch.h"
#include "ServerNetwork.h"
#include "resource.h"

namespace Server {

// ============================================================
//  Helpers
// ============================================================

static std::vector<BYTE> ApplySeparator(const std::vector<BYTE>& raw, const NetConfig& cfg)
{
    std::vector<BYTE> out;
    switch (cfg.separator)
    {
    case SeparatorMode::STX_ETX:
        out.push_back(0x02);
        out.insert(out.end(), raw.begin(), raw.end());
        out.push_back(0x03);
        break;
    case SeparatorMode::CR_LF:
        out = raw; out.push_back('\r'); out.push_back('\n'); break;
    case SeparatorMode::LF:
        out = raw; out.push_back('\n'); break;
    case SeparatorMode::CR:
        out = raw; out.push_back('\r'); break;
    case SeparatorMode::NUL:
        out = raw; out.push_back(0x00); break;
    case SeparatorMode::Custom:
        out.insert(out.end(), cfg.customPrefix.begin(), cfg.customPrefix.end());
        out.insert(out.end(), raw.begin(), raw.end());
        out.insert(out.end(), cfg.customSuffix.begin(), cfg.customSuffix.end());
        break;
    default:
        out = raw;
    }
    return out;
}

static std::wstring GetClientAddrStr(SOCKET s)
{
    sockaddr_storage ss = {};
    int len = sizeof(ss);
    if (getpeername(s, (sockaddr*)&ss, &len) != 0) return L"unknown";
    wchar_t buf[64] = {};
    if (ss.ss_family == AF_INET) {
        sockaddr_in* in4 = (sockaddr_in*)&ss;
        InetNtopW(AF_INET, &in4->sin_addr, buf, 64);
        wchar_t combined[80];
        swprintf_s(combined, L"%s:%d", buf, ntohs(in4->sin_port));
        return combined;
    }
    return L"unknown";
}

// ============================================================
//  ServerNetwork
// ============================================================

ServerNetwork::ServerNetwork(HWND notifyWnd, bool echoMode)
    : m_notifyWnd(notifyWnd), m_echoMode(echoMode)
{
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
}

ServerNetwork::~ServerNetwork()
{
    StopServer();
    WSACleanup();
}

void ServerNetwork::PostError(int wsaErr, const std::wstring& msg)
{
    std::wstring* p = new std::wstring(msg.empty()
        ? [&]() -> std::wstring {
              wchar_t buf[256] = {};
              FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, (DWORD)wsaErr, 0, buf, 256, nullptr);
              return buf;
          }()
        : msg);
    PostMessage(m_notifyWnd, WM_NET_ERROR, (WPARAM)wsaErr, (LPARAM)p);
}

void ServerNetwork::AppendLogFile(const std::vector<BYTE>& data)
{
    if (m_logFile.empty()) return;
    std::ofstream f(m_logFile, std::ios::binary | std::ios::app);
    if (f) f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// ---- StartListening ----------------------------------------

bool ServerNetwork::StartListening(int port, int timeoutSec, bool udpMode)
{
    if (m_listening) return false;
    m_stopRequested = false;
    m_udpMode = udpMode;

    // Launch accept loop thread
    std::thread([this, port, timeoutSec]() {
        AcceptLoop(port, timeoutSec);
    }).detach();

    return true;
}

void ServerNetwork::AcceptLoop(int port, int timeoutSec)
{
    // Create listen socket
    SOCKET listenSock = socket(AF_INET,
        m_udpMode ? SOCK_DGRAM  : SOCK_STREAM,
        m_udpMode ? IPPROTO_UDP : IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        PostError(WSAGetLastError(), L"socket() failed");
        return;
    }

    // Allow address reuse
    BOOL reuse = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    if (!m_udpMode) {
        BOOL nodelay = TRUE;
        setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    }

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        PostError(WSAGetLastError(), L"bind() failed on port " + std::to_wstring(port));
        closesocket(listenSock);
        return;
    }

    if (m_udpMode) {
        // ---- UDP mode: no listen/accept, just recvfrom ----
        m_listenSock = listenSock;
        m_listening  = true;
        PostMessage(m_notifyWnd, WM_NET_LISTENING, (WPARAM)port, 0);

        constexpr int BUF = 65536;
        std::vector<BYTE> buf(BUF);
        m_udpClientAddr = {};

        while (!m_stopRequested) {
            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(listenSock, &rfds);
            timeval tv { 0, 200000 }; // 200ms poll
            int r = select(0, &rfds, nullptr, nullptr, &tv);
            if (r < 0) break;
            if (r == 0) continue;

            sockaddr_in fromAddr = {};
            int fromLen = sizeof(fromAddr);
            int n = recvfrom(listenSock, (char*)buf.data(), BUF, 0, (sockaddr*)&fromAddr, &fromLen);
            if (n <= 0) continue;

            // Detect new client
            if (!m_clientConnected || memcmp(&fromAddr.sin_addr, &m_udpClientAddr.sin_addr, sizeof(fromAddr.sin_addr)) != 0
                || fromAddr.sin_port != m_udpClientAddr.sin_port) {
                m_udpClientAddr = fromAddr;
                m_clientConnected = true;
                wchar_t ipBuf[64] = {};
                InetNtopW(AF_INET, &fromAddr.sin_addr, ipBuf, 64);
                wchar_t combined[80];
                swprintf_s(combined, L"%s:%d", ipBuf, ntohs(fromAddr.sin_port));
                std::wstring* addrStr = new std::wstring(combined);
                PostMessage(m_notifyWnd, WM_NET_CONNECTED, 0, (LPARAM)addrStr);

                std::lock_guard<std::mutex> lk(m_statsMutex);
                m_stats = {};
                m_stats.connectTime = Utils::Now();
                m_stats.valid = true;
            }

            // Update stats
            {
                std::lock_guard<std::mutex> lk(m_statsMutex);
                if (m_stats.bytesReceived == 0)
                    m_stats.recvStart = Utils::Now();
                m_stats.bytesReceived += (size_t)n;
                m_stats.recvEnd = Utils::Now();
            }

            std::vector<BYTE> chunk(buf.begin(), buf.begin() + n);
            AppendLogFile(chunk);

            if (m_echoMode) {
                Utils::TimePoint echoStart = Utils::Now();
                int ns = sendto(listenSock, (char*)buf.data(), n, 0, (sockaddr*)&fromAddr, sizeof(fromAddr));
                Utils::TimePoint echoEnd = Utils::Now();

                CycleStats* pStats = new CycleStats;
                {
                    std::lock_guard<std::mutex> slk(m_statsMutex);
                    m_stats.sendStart = echoStart;
                    m_stats.sendEnd   = echoEnd;
                    m_stats.bytesSent += (size_t)(ns > 0 ? ns : 0);
                    *pStats = m_stats;
                    pStats->valid = true;
                }
                PostMessage(m_notifyWnd, WM_NET_SEND_COMPLETE,
                            (WPARAM)(ns > 0 ? ns : 0), (LPARAM)pStats);
            }

            BYTE* copy = new BYTE[n];
            memcpy(copy, buf.data(), n);
            PostMessage(m_notifyWnd, WM_NET_DATA_RECEIVED, (WPARAM)n, (LPARAM)copy);
        }
    } else {
        // ---- TCP mode: listen + accept loop ----
        if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
            PostError(WSAGetLastError(), L"listen() failed");
            closesocket(listenSock);
            return;
        }

        m_listenSock = listenSock;
        m_listening  = true;
        PostMessage(m_notifyWnd, WM_NET_LISTENING, (WPARAM)port, 0);

        // Accept loop - one client at a time
        while (!m_stopRequested)
        {
            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(listenSock, &rfds);
            timeval tv { 0, 200000 }; // 200ms poll
            int r = select(0, &rfds, nullptr, nullptr, &tv);
            if (r < 0) break;
            if (r == 0) continue;

            sockaddr_in clientAddr = {};
            int clen = sizeof(clientAddr);
            SOCKET clientSock = accept(listenSock, (sockaddr*)&clientAddr, &clen);
            if (clientSock == INVALID_SOCKET) {
                if (m_stopRequested) break;
                continue;
            }

            // Set recv/send timeouts
            DWORD tvMs = (DWORD)(timeoutSec * 1000);
            setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tvMs, sizeof(tvMs));
            setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (char*)&tvMs, sizeof(tvMs));
            BOOL nd = TRUE;
            setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));

            m_clientSock      = clientSock;
            m_clientConnected = true;

            // Post client info
            std::wstring* addrStr = new std::wstring(GetClientAddrStr(clientSock));
            PostMessage(m_notifyWnd, WM_NET_CONNECTED, 0, (LPARAM)addrStr);

            // Update stats
            {
                std::lock_guard<std::mutex> lk(m_statsMutex);
                m_stats = {};
                m_stats.connectTime = Utils::Now();
                m_stats.valid = true;
            }

            // Block here receiving until client disconnects or server stops
            RecvLoop(clientSock);

            m_clientConnected = false;
            // Only close if DisconnectClient() hasn't already done so
            if (m_clientSock != INVALID_SOCKET) {
                closesocket(m_clientSock);
                m_clientSock = INVALID_SOCKET;
            }

            if (!m_stopRequested)
                PostMessage(m_notifyWnd, WM_NET_DISCONNECTED, 0, 0);
        }
    }

    m_listening  = false;
    m_listenSock = INVALID_SOCKET;
    closesocket(listenSock);

    if (!m_stopRequested)
        PostMessage(m_notifyWnd, WM_NET_STOPPED, 0, 0);
}

void ServerNetwork::RecvLoop(SOCKET clientSock)
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!m_stopRequested)
    {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(clientSock, &rfds);
        timeval tv { 0, 100000 }; // 100ms poll
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
        if (r == 0) continue;

        int n = recv(clientSock, (char*)buf.data(), BUF, 0);
        if (n <= 0) break;

        std::vector<BYTE> chunk(buf.begin(), buf.begin() + n);

        // Update stats
        {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            if (m_stats.bytesReceived == 0)
                m_stats.recvStart = Utils::Now();
            m_stats.bytesReceived += (size_t)n;
            m_stats.recvEnd = Utils::Now();
        }

        // Append to log file (CLI/test mode)
        AppendLogFile(chunk);

        // Echo mode: immediately send back what we received
        if (m_echoMode) {
            Utils::TimePoint echoStart = Utils::Now();
            int total = n, sent = 0;
            while (sent < total) {
                int s = ::send(clientSock, (char*)buf.data() + sent, total - sent, 0);
                if (s == SOCKET_ERROR) break;
                sent += s;
            }
            Utils::TimePoint echoEnd = Utils::Now();

            CycleStats* pStats = new CycleStats;
            {
                std::lock_guard<std::mutex> slk(m_statsMutex);
                m_stats.sendStart = echoStart;
                m_stats.sendEnd   = echoEnd;
                m_stats.bytesSent += (size_t)sent;
                *pStats = m_stats;
                pStats->valid = true;
            }
            PostMessage(m_notifyWnd, WM_NET_SEND_COMPLETE, (WPARAM)sent, (LPARAM)pStats);
        }

        // Post data to UI
        BYTE* copy = new BYTE[n];
        memcpy(copy, buf.data(), n);
        PostMessage(m_notifyWnd, WM_NET_DATA_RECEIVED, (WPARAM)n, (LPARAM)copy);
    }
}

// ---- StopServer --------------------------------------------

void ServerNetwork::StopServer()
{
    m_stopRequested = true;
    m_clientConnected = false;
    if (m_clientSock != INVALID_SOCKET) {
        shutdown(m_clientSock, SD_BOTH);
        closesocket(m_clientSock);
        m_clientSock = INVALID_SOCKET;
    }
    if (m_listenSock != INVALID_SOCKET) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }
    m_listening = false;
    PostMessage(m_notifyWnd, WM_NET_STOPPED, 0, 0);
}

// ---- DisconnectClient --------------------------------------

void ServerNetwork::DisconnectClient()
{
    if (m_clientSock != INVALID_SOCKET) {
        shutdown(m_clientSock, SD_BOTH);
        closesocket(m_clientSock);
        m_clientSock = INVALID_SOCKET;
    }
    m_clientConnected = false;
}

// ---- Send --------------------------------------------------

bool ServerNetwork::Send(const std::vector<BYTE>& raw, const NetConfig& cfg)
{
    if (!m_clientConnected) return false;

    std::vector<BYTE> data = ApplySeparator(raw, cfg);
    std::lock_guard<std::mutex> lk(m_sendMutex);

    Utils::TimePoint sendStart = Utils::Now();
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        m_stats.sendStart     = sendStart;
        m_stats.bytesReceived = 0;
        m_stats.recvStart     = {};
        m_stats.recvEnd       = {};
    }

    int total = (int)data.size(), sent = 0;
    if (m_udpMode) {
        int n = sendto(m_listenSock, (char*)data.data(), total, 0,
                       (sockaddr*)&m_udpClientAddr, sizeof(m_udpClientAddr));
        if (n == SOCKET_ERROR) { PostError(WSAGetLastError(), L"sendto() failed"); return false; }
        sent = n;
    } else {
        while (sent < total) {
            int n = ::send(m_clientSock, (char*)data.data() + sent, total - sent, 0);
            if (n == SOCKET_ERROR) {
                PostError(WSAGetLastError(), L"send() failed");
                return false;
            }
            sent += n;
        }
    }

    Utils::TimePoint sendEnd = Utils::Now();
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        m_stats.sendEnd   = sendEnd;
        m_stats.bytesSent = (size_t)sent;
    }

    CycleStats* pStats = new CycleStats;
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        *pStats = m_stats;
    }
    PostMessage(m_notifyWnd, WM_NET_SEND_COMPLETE, (WPARAM)sent, (LPARAM)pStats);

    if (cfg.autoDisconnect)
        DisconnectClient();

    return true;
}

} // namespace Server
