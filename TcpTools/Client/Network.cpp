#include "pch.h"
#include "Network.h"
#include "resource.h"

namespace Client {

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
        out = raw;
        out.push_back('\r'); out.push_back('\n');
        break;
    case SeparatorMode::LF:
        out = raw;
        out.push_back('\n');
        break;
    case SeparatorMode::CR:
        out = raw;
        out.push_back('\r');
        break;
    case SeparatorMode::NUL:
        out = raw;
        out.push_back(0x00);
        break;
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

// ============================================================
//  TcpNetwork
// ============================================================

TcpNetwork::TcpNetwork(HWND notifyWnd)
    : m_notifyWnd(notifyWnd)
{
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
}

TcpNetwork::~TcpNetwork()
{
    Disconnect();
    WSACleanup();
}

void TcpNetwork::PostError(int wsaErr, const std::wstring& msg)
{
    std::wstring* p = new std::wstring(msg.empty()
        ? [&]{ wchar_t buf[256]={}; FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,nullptr,(DWORD)wsaErr,0,buf,256,nullptr); return std::wstring(buf); }()
        : msg);
    PostMessage(m_notifyWnd, WM_NET_ERROR, (WPARAM)wsaErr, (LPARAM)p);
}

bool TcpNetwork::Connect(const NetConfig& cfg)
{
    if (m_connected) return false;

    m_udpMode = cfg.udpMode;
    SOCKET s = socket(AF_INET,
        m_udpMode ? SOCK_DGRAM  : SOCK_STREAM,
        m_udpMode ? IPPROTO_UDP : IPPROTO_TCP);
    if (s == INVALID_SOCKET) { PostError(WSAGetLastError(), L"socket() failed"); return false; }

    // Set send/recv timeout
    DWORD tv = (DWORD)(cfg.timeoutSec * 1000);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

    // Enable TCP_NODELAY for lowest latency (TCP only)
    if (!m_udpMode) {
        BOOL nodelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    }

    // Resolve + connect in a thread so UI stays responsive
    m_sock = s;
    NetConfig cfgCopy = cfg; // copy for thread

    std::thread([this, cfgCopy]() mutable {
        WorkerThread(cfgCopy);
    }).detach();

    return true;
}

void TcpNetwork::QueueSend(std::vector<BYTE> data, const NetConfig& cfg)
{
    {
        std::lock_guard<std::mutex> lk(m_sendQueueMutex);
        m_sendQueue.push({std::move(data), cfg});
    }
    m_sendQueueCV.notify_one();
}

void TcpNetwork::SendWorkerLoop()
{
    while (true) {
        SendRequest req;
        {
            std::unique_lock<std::mutex> lk(m_sendQueueMutex);
            m_sendQueueCV.wait(lk, [this] { return m_sendStop.load() || !m_sendQueue.empty(); });
            if (m_sendStop.load()) break;   // stop immediately, don't drain
            if (m_sendQueue.empty()) continue;
            req = std::move(m_sendQueue.front());
            m_sendQueue.pop();
        }
        Send(req.data, req.cfg);
    }
}

void TcpNetwork::WorkerThread(NetConfig cfg)
{
    SOCKET s = m_sock;

    // Set socket to non-blocking for connect with timeout
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)cfg.port);

    // Resolve hostname
    std::string host = cfg.host;
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = m_udpMode ? SOCK_DGRAM : SOCK_STREAM;
    std::string portStr = std::to_string(cfg.port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        PostError(WSAGetLastError(), L"Failed to resolve host: " + Utils::ToWide(host));
        closesocket(s);
        m_sock = INVALID_SOCKET;
        return;
    }
    memcpy(&addr, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    ::connect(s, (sockaddr*)&addr, sizeof(addr));

    // Wait for connect with timeout using select()
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_SET(s, &wfds);
    FD_ZERO(&efds); FD_SET(s, &efds);
    timeval timeout { cfg.timeoutSec, 0 };
    int r = select(0, nullptr, &wfds, &efds, &timeout);

    if (r <= 0 || FD_ISSET(s, &efds)) {
        int err = (r == 0) ? WSAETIMEDOUT : WSAGetLastError();
        PostError(err, r == 0 ? L"Connection timed out" : L"Connection failed");
        closesocket(s);
        m_sock = INVALID_SOCKET;
        return;
    }

    // Switch back to blocking
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);

    {
        std::lock_guard<std::mutex> lk(m_statsMutex);
        m_stats = {};
        m_stats.connectTime = Utils::Now();
        m_stats.valid = true;
    }

    m_connected = true;
    PostMessage(m_notifyWnd, WM_NET_CONNECTED, 0, 0);

    // Start persistent send worker thread
    m_sendStop = false;
    m_sendThread = std::thread(&TcpNetwork::SendWorkerLoop, this);

    // Start receive loop in this same thread
    m_stopRecv = false;
    RecvLoop();

    // Stop send worker thread
    {
        std::lock_guard<std::mutex> lk(m_sendQueueMutex);
        m_sendStop = true;
    }
    m_sendQueueCV.notify_all();
    if (m_sendThread.joinable()) m_sendThread.join();

    // Drain any remaining queued sends
    {
        std::lock_guard<std::mutex> lk(m_sendQueueMutex);
        while (!m_sendQueue.empty()) m_sendQueue.pop();
    }
}

void TcpNetwork::RecvLoop()
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!m_stopRecv && m_connected) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(m_sock, &rfds);
        timeval tv { 0, 100000 }; // 100ms poll
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
        if (r == 0) continue;

        int n = recv(m_sock, (char*)buf.data(), BUF, 0);
        if (n <= 0) break;

        bool firstRecv = false;
        {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            if (m_stats.bytesReceived == 0) {
                m_stats.recvStart = Utils::Now();
                firstRecv = true;
            }
            m_stats.bytesReceived += (size_t)n;
            m_stats.recvEnd = Utils::Now();
        }

        // Post data to UI (UI must free the BYTE*)
        BYTE* copy = new BYTE[n];
        memcpy(copy, buf.data(), n);
        PostMessage(m_notifyWnd, WM_NET_DATA_RECEIVED, (WPARAM)n, (LPARAM)copy);

        // Post latency to UI on first recv (avoids UI calling GetStats with mutex)
        if (firstRecv) {
            double latMs = -1;
            {
                std::lock_guard<std::mutex> lk(m_statsMutex);
                latMs = m_stats.SendToFirstRecvMs();
            }
            if (latMs >= 0) {
                int64_t latUs = (int64_t)(latMs * 1000.0);
                PostMessage(m_notifyWnd, WM_NET_RECV_LATENCY,
                            (WPARAM)(DWORD)(latUs & 0xFFFFFFFF),
                            (LPARAM)(DWORD)((latUs >> 32) & 0xFFFFFFFF));
            }
        }
    }

    if (m_connected.exchange(false)) {
        PostMessage(m_notifyWnd, WM_NET_DISCONNECTED, 0, 0);
    }
}

void TcpNetwork::Disconnect()
{
    bool wasConnected = m_connected.exchange(false);

    // Signal send worker to stop
    {
        std::lock_guard<std::mutex> lk(m_sendQueueMutex);
        m_sendStop = true;
    }
    m_sendQueueCV.notify_all();

    if (m_sock != INVALID_SOCKET) {
        shutdown(m_sock, SD_BOTH);
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    if (wasConnected) {
        PostMessage(m_notifyWnd, WM_NET_DISCONNECTED, 0, 0);
    }
}

bool TcpNetwork::Send(const std::vector<BYTE>& raw, const NetConfig& cfg)
{
    if (!m_connected || m_sock == INVALID_SOCKET) return false;

    std::vector<BYTE> data = ApplySeparator(raw, cfg);

    std::lock_guard<std::mutex> lk(m_sendMutex);

    Utils::TimePoint sendStart = Utils::Now();
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        m_stats.sendStart    = sendStart;
        m_stats.bytesReceived = 0;
        m_stats.recvStart    = {};
        m_stats.recvEnd      = {};
    }

    int total = (int)data.size();
    int sent  = 0;
    while (sent < total) {
        int n = ::send(m_sock, (char*)data.data() + sent, total - sent, 0);
        if (n == SOCKET_ERROR) {
            PostError(WSAGetLastError(), L"send() failed");
            return false;
        }
        sent += n;
    }

    Utils::TimePoint sendEnd = Utils::Now();
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        m_stats.sendEnd    = sendEnd;
        m_stats.bytesSent  = (size_t)sent;
    }

    // Post SEND_COMPLETE with a copy of stats
    CycleStats* pStats = new CycleStats;
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        *pStats = m_stats;
    }
    PostMessage(m_notifyWnd, WM_NET_SEND_COMPLETE, (WPARAM)sent, (LPARAM)pStats);

    if (cfg.autoDisconnect) {
        Disconnect();
    }
    return true;
}

} // namespace Client
