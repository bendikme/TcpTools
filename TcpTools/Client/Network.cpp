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

static int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================
//  Core - everything the session threads touch.
//
//  Locking (always in this order): sendMu -> mu.
//    mu      guards `sock`
//    sendMu  held for the whole of a ::send(); the socket is closed under it,
//            so a sender never uses a closed (or recycled) handle
//  Only the worker closes the socket, and only after joining its send thread.
//  Everybody else just calls shutdown() (under mu), which wakes blocked
//  select/recv/send calls.
// ============================================================

struct TcpNetwork::Core
{
    std::mutex          mu;
    SOCKET              sock   {INVALID_SOCKET};
    std::mutex          sendMu;

    // The one channel to the window. Data waits when the window falls behind;
    // `stop` wakes a waiting Push.
    HandoffQueue<NetEvent> events;

    std::atomic<bool>   connected {false};
    std::atomic<bool>   stop      {false};
    std::atomic<bool>   running   {false};
    bool                udp       {false};

    // Auto-Disconnect state (see AUTODISC_*): `closing` is set when a send with
    // Auto-Disconnect is done; from then on no further send is accepted and the
    // receive loop ends the session by the quiet-time rules.
    std::atomic<bool>    closing       {false};
    std::atomic<bool>    endedByAuto   {false};
    std::atomic<int64_t> sendStartMs   {0};
    std::atomic<int64_t> sendEndMs     {0};
    std::atomic<int64_t> lastRecvMs    {0};

    std::queue<SendRequest> queue;
    std::mutex              qMu;
    std::condition_variable qCv;
    bool                    sendStop {false};   // guarded by qMu

    CycleStats          stats;
    std::mutex          statsMu;

    // State changes never wait; data waits for room (weight = payload size).
    void Emit(NetEvent&& ev, size_t weight = 0) { events.Push(std::move(ev), weight, stop); }
    void PostError(int err, const std::wstring& msg)
    {
        NetEvent ev;
        ev.kind = NetEvent::Kind::Error;
        ev.code = err;
        ev.text = msg;
        Emit(std::move(ev));
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
        events.Close();
        RequestStop();
    }

    SOCKET OpenSocket(const NetConfig& cfg);
    void Run(NetConfig cfg);
    void RecvLoop(SOCKET s);
    void SendLoop();
    bool Send(const std::vector<BYTE>& raw, const NetConfig& cfg);
};

// Resolves and connects over every address of the host; reports a failure as an
// Error event. INVALID_SOCKET also when cancelled (then without an error).
SOCKET TcpNetwork::Core::OpenSocket(const NetConfig& cfg)
{
    const int socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
    const std::wstring host   = Utils::ToWide(cfg.host);
    const std::wstring target = NetAddr::JoinHostPort(host, cfg.port);

    std::vector<NetAddr::Endpoint> eps;
    int rc = NetAddr::Resolve(host, cfg.port, socktype, stop, eps);
    if (rc == WSA_E_CANCELLED || stop) return INVALID_SOCKET;
    if (rc != 0) {
        PostError(rc, L"Cannot resolve host '" + host + L"': " + NetAddr::ErrorText(rc));
        return INVALID_SOCKET;
    }

    int err = 0;
    SOCKET s = NetAddr::Connect(eps, socktype, cfg.timeoutSec > 0 ? cfg.timeoutSec * 1000 : 0, stop, err);
    if (s != INVALID_SOCKET) return s;
    if (err == WSA_E_CANCELLED || stop) return INVALID_SOCKET;
    if (err == WSAETIMEDOUT)
        PostError(err, L"Connection to " + target + L" timed out after " + std::to_wstring(cfg.timeoutSec) + L" s");
    else
        PostError(err, L"Connection to " + target + L" failed: " + NetAddr::ErrorText(err));
    return INVALID_SOCKET;
}

void TcpNetwork::Core::Run(NetConfig cfg)
{
    bool wasConnected = false;
    std::thread sender;

    SOCKET s = stop ? INVALID_SOCKET : OpenSocket(cfg);
    if (s != INVALID_SOCKET) {
        { std::lock_guard<std::mutex> lk(mu); sock = s; }
        if (stop) {
            // Disconnect arrived between the connect and publishing the socket.
        } else {
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
            { NetEvent ev; ev.kind = NetEvent::Kind::Connected; Emit(std::move(ev)); }

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
        else if (s != INVALID_SOCKET) closesocket(s);
    }

    // `running` goes false first: the UI may call Connect() as soon as it sees the event.
    running = false;
    NetEvent ev;
    ev.kind = NetEvent::Kind::Disconnected;
    ev.code = wasConnected ? 0 : 1;
    ev.flag = endedByAuto.load();
    Emit(std::move(ev));
}

void TcpNetwork::Core::RecvLoop(SOCKET s)
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!stop) {
        // Auto-Disconnect: the reply is complete when it has gone quiet.
        const bool closingNow = closing.load();
        if (closingNow) {
            const int64_t now = NowMs(), last = lastRecvMs.load();
            const bool gotReply = last != 0 && last >= sendStartMs.load();
            const int64_t idle = now - (gotReply ? last : sendEndMs.load());
            if (idle >= (gotReply ? AUTODISC_QUIET_MS : AUTODISC_NO_REPLY_MS)) {
                endedByAuto = true;
                break;
            }
        }

        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(s, &rfds);
        timeval tv { 0, closingNow ? 20000 : 100000 };   // finer polling while the quiet time is measured
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) {
            int e = WSAGetLastError();
            if (!stop) PostError(e, L"Connection lost: " + NetAddr::ErrorText(e));
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
            if (!stop) PostError(e, L"Connection lost: " + NetAddr::ErrorText(e));
            break;
        }
        lastRecvMs = NowMs();

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

        NetEvent ev;
        ev.kind = NetEvent::Kind::Data;
        ev.data.assign(buf.begin(), buf.begin() + n);
        if (firstRecv) ev.latMs = latMs;
        if (!events.Push(std::move(ev), (size_t)n, stop)) break;   // stopped or the window is gone
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
    if (stop || !connected || closing) return false;

    std::vector<BYTE> data = ApplySeparator(raw, cfg);

    std::lock_guard<std::mutex> lk(sendMu);
    SOCKET s;
    { std::lock_guard<std::mutex> mlk(mu); s = sock; }
    if (s == INVALID_SOCKET || stop || closing) return false;

    {
        std::lock_guard<std::mutex> slk(statsMu);
        stats.sendStart     = Utils::Now();
        stats.sendEnd       = {};
        stats.bytesReceived = 0;
        stats.recvStart     = {};
        stats.recvEnd       = {};
    }
    sendStartMs = NowMs();

    size_t sent = 0;
    while (sent < data.size()) {
        int chunk = (int)std::min<size_t>(data.size() - sent, 1u << 20);
        int n = ::send(s, (const char*)data.data() + sent, chunk, 0);
        if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (!stop) {
                PostError(e, L"Send failed: " + NetAddr::ErrorText(e));
                if (!udp) RequestStop();   // a TCP stream is in an unknown state after a failed send
            }
            return false;
        }
        sent += (size_t)n;
    }

    NetEvent ev;
    ev.kind = NetEvent::Kind::SendComplete;
    {
        std::lock_guard<std::mutex> slk(statsMu);
        stats.sendEnd   = Utils::Now();
        stats.bytesSent = sent;
        ev.stats        = stats;
    }
    ev.bytesSent = sent;
    ev.flag      = cfg.autoDisconnect;
    Emit(std::move(ev));

    if (cfg.autoDisconnect) {
        // The session stays open for the reply; the receive loop ends it.
        sendEndMs = NowMs();
        closing   = true;
    }
    return true;
}

// ============================================================
//  TcpNetwork - the handle the UI thread uses
// ============================================================

TcpNetwork::TcpNetwork(HWND notifyWnd)
    : m_core(std::make_shared<Core>())
{
    m_core->events.SetTarget(notifyWnd, WM_NET_QUEUE);
}

TcpNetwork::~TcpNetwork()
{
    m_core->Abandon();
    if (m_worker.joinable()) m_worker.join();
}

bool TcpNetwork::Connect(const NetConfig& cfg)
{
    if (m_core->running) return false;
    if (m_worker.joinable()) m_worker.join();   // already past its last statement

    m_core->udp = cfg.udpMode;
    m_core->stop = false;
    m_core->connected = false;
    m_core->closing = false;
    m_core->endedByAuto = false;
    m_core->sendStartMs = 0;
    m_core->sendEndMs = 0;
    m_core->lastRecvMs = 0;
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

std::vector<NetEvent> TcpNetwork::Drain(size_t maxBytes)
{
    return m_core->events.Drain(maxBytes);
}

void TcpNetwork::QueueSend(std::vector<BYTE> data, const NetConfig& cfg)
{
    if (!m_core->connected || m_core->stop || m_core->closing) return;
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
