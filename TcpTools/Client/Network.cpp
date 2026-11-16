#include "pch.h"
#include <mstcpip.h>
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
//  Core - everything the session threads touch. Owned through shared_ptr by
//  the TcpNetwork handle and by the worker thread, so neither can outlive the
//  data the other one uses.
//
//  Locking (always in this order): sendMu -> mu.
//    mu      guards `notify` and `sock`
//    sendMu  held for the whole of a ::send(); the socket is closed under it,
//            so a sender never uses a closed (or recycled) handle
//  Only the worker closes the socket, and only after joining its send thread.
//  Everybody else just calls shutdown() (under mu), which wakes blocked
//  select/recv/send calls.
// ============================================================

static std::wstring ErrText(int err)
{
    wchar_t buf[256] = {};
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, (DWORD)err, 0, buf, 256, nullptr);
    std::wstring s(buf, n);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'.'))
        s.pop_back();
    if (s.empty()) s = L"error";
    return s + L" (" + std::to_wstring(err) + L")";
}

struct TcpNetwork::Core
{
    std::mutex          mu;
    HWND                notify {nullptr};
    SOCKET              sock   {INVALID_SOCKET};
    std::mutex          sendMu;

    std::atomic<bool>   connected {false};
    std::atomic<bool>   stop      {false};
    std::atomic<bool>   running   {false};
    bool                udp       {false};

    std::queue<SendRequest> queue;
    std::mutex              qMu;
    std::condition_variable qCv;
    bool                    sendStop {false};   // guarded by qMu

    CycleStats          stats;
    std::mutex          statsMu;

    // Delivers a message unless the window is gone. False = not delivered.
    bool Post(UINT msg, WPARAM w, LPARAM l)
    {
        std::lock_guard<std::mutex> lk(mu);
        if (!notify) return false;
        return PostMessageW(notify, msg, w, l) != FALSE;
    }
    // Like Post, but waits while the UI's message queue is full (back-pressure).
    bool PostWait(UINT msg, WPARAM w, LPARAM l)
    {
        while (!Post(msg, w, l)) {
            { std::lock_guard<std::mutex> lk(mu); if (!notify) return false; }
            if (stop) return false;
            Sleep(5);
        }
        return true;
    }
    void PostError(int err, const std::wstring& msg)
    {
        auto* p = new std::wstring(msg);
        if (!Post(WM_NET_ERROR, (WPARAM)err, (LPARAM)p)) delete p;
    }

    void ShutdownSocket()
    {
        std::lock_guard<std::mutex> lk(mu);
        if (sock != INVALID_SOCKET) shutdown(sock, SD_BOTH);
    }
    void RequestStop()
    {
        stop = true;
        ShutdownSocket();
        { std::lock_guard<std::mutex> lk(qMu); }
        qCv.notify_all();
    }
    void Abandon()   // the owner is going away: no more posts
    {
        { std::lock_guard<std::mutex> lk(mu); notify = nullptr; }
        RequestStop();
    }

    bool ConnectSocket(SOCKET s, const NetConfig& cfg);
    void Run(NetConfig cfg);
    void RecvLoop(SOCKET s);
    void SendLoop();
    bool Send(const std::vector<BYTE>& raw, const NetConfig& cfg);
};

bool TcpNetwork::Core::ConnectSocket(SOCKET s, const NetConfig& cfg)
{
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    // Name lookup cannot be interrupted; `stop` is checked right after it.
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
    std::string portStr = std::to_string(cfg.port);
    int gai = getaddrinfo(cfg.host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        int e = gai ? gai : WSAHOST_NOT_FOUND;
        if (!stop) PostError(e, L"Cannot resolve host '" + Utils::ToWide(cfg.host) + L"': " + ErrText(e));
        if (res) freeaddrinfo(res);
        return false;
    }
    sockaddr_in addr = {};
    memcpy(&addr, res->ai_addr, std::min<size_t>(res->ai_addrlen, sizeof(addr)));
    freeaddrinfo(res);
    if (stop) return false;

    const std::wstring target = Utils::ToWide(cfg.host) + L":" + std::to_wstring(cfg.port);
    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK) {
            PostError(e, L"Connection to " + target + L" failed: " + ErrText(e));
            return false;
        }
        // Wait in short slices so Disconnect() can cancel; timeout 0 = no limit.
        const bool limited = cfg.timeoutSec > 0;
        auto deadline = Utils::Now() + std::chrono::seconds(limited ? cfg.timeoutSec : 0);
        fd_set wfds, efds;
        for (;;) {
            if (stop) return false;
            FD_ZERO(&wfds); FD_SET(s, &wfds);
            FD_ZERO(&efds); FD_SET(s, &efds);
            timeval tv { 0, 100000 };
            int r = select(0, nullptr, &wfds, &efds, &tv);
            if (r < 0) {
                e = WSAGetLastError();
                if (!stop) PostError(e, L"Connection to " + target + L" failed: " + ErrText(e));
                return false;
            }
            if (r > 0) break;
            if (limited && Utils::Now() >= deadline) {
                PostError(WSAETIMEDOUT, L"Connection to " + target + L" timed out after "
                          + std::to_wstring(cfg.timeoutSec) + L" s");
                return false;
            }
        }
        int soErr = 0, len = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
        if (soErr == 0 && FD_ISSET(s, &efds)) soErr = WSAECONNREFUSED;
        if (soErr != 0) {
            PostError(soErr, L"Connection to " + target + L" failed: " + ErrText(soErr));
            return false;
        }
    }

    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    return true;
}

void TcpNetwork::Core::Run(NetConfig cfg)
{
    bool wasConnected = false;
    std::thread sender;

    SOCKET s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        int e = WSAGetLastError();
        PostError(e, L"Cannot create socket: " + ErrText(e));
    } else {
        { std::lock_guard<std::mutex> lk(mu); sock = s; }

        if (!udp) {
            BOOL nodelay = TRUE;   // lowest latency
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
            if (cfg.keepAlive) {
                // Probe after 10 s idle, then every 1 s: a dead peer is noticed in ~20 s.
                tcp_keepalive ka = { 1, 10000, 1000 };
                DWORD ret = 0;
                if (WSAIoctl(s, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), nullptr, 0, &ret, nullptr, nullptr) != 0) {
                    BOOL on = TRUE;
                    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&on, sizeof(on));
                }
            }
        }

        if (!stop && ConnectSocket(s, cfg)) {
            // Blocking sends give up after the timeout (0 = never; Disconnect still wakes them).
            DWORD tv = (DWORD)std::max(0, cfg.timeoutSec) * 1000;
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

            {
                std::lock_guard<std::mutex> lk(statsMu);
                stats = {};
                stats.connectTime = Utils::Now();
                stats.valid = true;
            }
            {
                std::lock_guard<std::mutex> lk(qMu);
                while (!queue.empty()) queue.pop();
                sendStop = false;
            }
            wasConnected = true;
            connected = true;
            Post(WM_NET_CONNECTED, 0, 0);

            sender = std::thread([this] { SendLoop(); });
            RecvLoop(s);
        }
    }

    // Wake and join the send thread before the socket goes away.
    connected = false;
    ShutdownSocket();
    { std::lock_guard<std::mutex> lk(qMu); sendStop = true; }
    qCv.notify_all();
    if (sender.joinable()) sender.join();
    {
        std::lock_guard<std::mutex> lk(qMu);
        while (!queue.empty()) queue.pop();
    }
    {
        std::lock_guard<std::mutex> slk(sendMu);   // a cyclic Send() may still be inside
        std::lock_guard<std::mutex> lk(mu);
        if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; }
    }

    // `running` goes false first: the UI may call Connect() as soon as it sees the message.
    running = false;
    Post(WM_NET_DISCONNECTED, wasConnected ? 0 : 1, 0);
}

void TcpNetwork::Core::RecvLoop(SOCKET s)
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!stop) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(s, &rfds);
        timeval tv { 0, 100000 }; // 100ms poll
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) {
            int e = WSAGetLastError();
            if (!stop) PostError(e, L"Connection lost: " + ErrText(e));
            break;
        }
        if (r == 0) continue;

        int n = recv(s, (char*)buf.data(), BUF, 0);
        if (n == 0) {
            if (udp) continue;      // empty datagram
            break;                  // peer closed
        }
        if (n < 0) {
            int e = WSAGetLastError();
            if (!stop) PostError(e, L"Connection lost: " + ErrText(e));
            break;
        }

        bool firstRecv = false;
        double latMs = -1;
        {
            std::lock_guard<std::mutex> lk(statsMu);
            Utils::TimePoint now = Utils::Now();
            if (stats.bytesReceived == 0) {
                stats.recvStart = now;
                firstRecv = true;
                // Only meaningful when this data follows one of our sends.
                if (stats.sendEnd != Utils::TimePoint{} && stats.recvStart >= stats.sendEnd)
                    latMs = Utils::ElapsedMs(stats.sendEnd, stats.recvStart);
            }
            stats.bytesReceived += (size_t)n;
            stats.recvEnd = now;
        }

        // Post data to UI (UI must free the BYTE*)
        BYTE* copy = new BYTE[n];
        memcpy(copy, buf.data(), n);
        if (!PostWait(WM_NET_DATA_RECEIVED, (WPARAM)n, (LPARAM)copy)) { delete[] copy; break; }

        if (firstRecv && latMs >= 0) {
            int64_t latUs = (int64_t)(latMs * 1000.0);
            Post(WM_NET_RECV_LATENCY, (WPARAM)(DWORD)(latUs & 0xFFFFFFFF),
                 (LPARAM)(DWORD)((latUs >> 32) & 0xFFFFFFFF));
        }
    }
}

void TcpNetwork::Core::SendLoop()
{
    for (;;) {
        SendRequest req;
        {
            std::unique_lock<std::mutex> lk(qMu);
            qCv.wait(lk, [this] { return sendStop || stop || !queue.empty(); });
            if (sendStop || stop) return;   // don't drain
            req = std::move(queue.front());
            queue.pop();
        }
        Send(req.data, req.cfg);
    }
}

bool TcpNetwork::Core::Send(const std::vector<BYTE>& raw, const NetConfig& cfg)
{
    if (stop || !connected) return false;

    std::vector<BYTE> data = ApplySeparator(raw, cfg);

    std::lock_guard<std::mutex> lk(sendMu);
    SOCKET s;
    { std::lock_guard<std::mutex> mlk(mu); s = sock; }
    if (s == INVALID_SOCKET || stop) return false;

    {
        std::lock_guard<std::mutex> slk(statsMu);
        stats.sendStart     = Utils::Now();
        stats.sendEnd       = {};
        stats.bytesReceived = 0;
        stats.recvStart     = {};
        stats.recvEnd       = {};
    }

    size_t sent = 0;
    while (sent < data.size()) {
        int chunk = (int)std::min<size_t>(data.size() - sent, 1u << 20);
        int n = ::send(s, (const char*)data.data() + sent, chunk, 0);
        if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (!stop) {
                PostError(e, L"Send failed: " + ErrText(e));
                if (!udp) RequestStop();   // a TCP stream is in an unknown state after a failed send
            }
            return false;
        }
        sent += (size_t)n;
    }

    auto* pStats = new CycleStats;
    {
        std::lock_guard<std::mutex> slk(statsMu);
        stats.sendEnd   = Utils::Now();
        stats.bytesSent = sent;
        *pStats = stats;
    }
    if (!Post(WM_NET_SEND_COMPLETE, (WPARAM)sent, (LPARAM)pStats)) delete pStats;

    if (cfg.autoDisconnect) RequestStop();
    return true;
}

// ============================================================
//  TcpNetwork - the handle the UI thread uses
// ============================================================

TcpNetwork::TcpNetwork(HWND notifyWnd)
    : m_core(std::make_shared<Core>())
{
    m_core->notify = notifyWnd;
}

TcpNetwork::~TcpNetwork()
{
    m_core->Abandon();
    if (m_worker.joinable()) {
        // Normally the worker is done within ~100 ms, but it may be inside a name
        // lookup that cannot be interrupted: never block the UI thread on that.
        // It owns a reference to the Core, so letting it finish alone is safe.
        if (!m_core->running) m_worker.join();
        else                  m_worker.detach();
    }
}

bool TcpNetwork::Connect(const NetConfig& cfg)
{
    if (m_core->running) return false;
    if (m_worker.joinable()) m_worker.join();   // already past its last statement

    m_core->udp = cfg.udpMode;
    m_core->stop = false;
    m_core->connected = false;
    m_core->running = true;

    std::shared_ptr<Core> core = m_core;
    NetConfig cfgCopy = cfg;
    try {
        m_worker = std::thread([core, cfgCopy]() mutable { core->Run(std::move(cfgCopy)); });
    } catch (...) {
        m_core->running = false;
        return false;
    }
    return true;
}

void TcpNetwork::Disconnect()
{
    if (m_core->running) m_core->RequestStop();
}

void TcpNetwork::QueueSend(std::vector<BYTE> data, const NetConfig& cfg)
{
    if (!m_core->connected || m_core->stop) return;
    {
        std::lock_guard<std::mutex> lk(m_core->qMu);
        m_core->queue.push({std::move(data), cfg});
    }
    m_core->qCv.notify_one();
}

bool TcpNetwork::Send(const std::vector<BYTE>& data, const NetConfig& cfg)
{
    return m_core->Send(data, cfg);
}

bool TcpNetwork::IsConnected() const { return m_core->connected.load(); }
bool TcpNetwork::IsActive()    const { return m_core->running.load(); }

bool TcpNetwork::WithSocket(const std::function<void(SOCKET)>& fn)
{
    std::lock_guard<std::mutex> lk(m_core->mu);
    if (m_core->sock == INVALID_SOCKET || !m_core->connected) return false;
    fn(m_core->sock);
    return true;
}

} // namespace Client
