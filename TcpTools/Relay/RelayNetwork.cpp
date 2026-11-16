#include "pch.h"
#include "RelayNetwork.h"
#include "resource.h"

// ============================================================
//  Helpers
// ============================================================

static std::wstring WsaMessage(int code)
{
    switch (code) {
    case WSAECONNREFUSED: return L"Connection refused";
    case WSAETIMEDOUT:    return L"Connection timed out";
    case WSAEHOSTUNREACH: return L"Host unreachable";
    case WSAENETUNREACH:  return L"Network unreachable";
    case 0:               return L"Unknown error";
    }
    wchar_t buf[256] = {};
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, (DWORD)code, 0, buf, 256, nullptr);
    std::wstring s(buf, n);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' '))
        s.pop_back();
    return s.empty() ? L"Error " + std::to_wstring(code) : s;
}

static bool SetNonBlocking(SOCKET s)
{
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

// A UDP socket otherwise reports an ICMP "port unreachable" as WSAECONNRESET
// on the next receive, which is not an error of the relay itself.
static void DisableUdpConnReset(SOCKET s)
{
    BOOL off = FALSE;
    DWORD ret = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
}

// ============================================================
//  RelayNetwork
// ============================================================

RelayNetwork::RelayNetwork(HandoffQueue<RelayEvent>& events)
    : m_events(events)
{
}

RelayNetwork::~RelayNetwork()
{
    StopRelay();
}

// State changes weigh 0 and never wait. A relay that is stopping drops nothing
// that matters: the window clears the queue when it ends the session.
void RelayNetwork::Post(RelayEvent::Kind kind, int num, std::wstring text, bool fromWait)
{
    RelayEvent ev;
    ev.kind     = kind;
    ev.num      = num;
    ev.fromWait = fromWait;
    ev.text     = std::move(text);
    m_events.Push(std::move(ev), 0, m_stopRequested);
}

void RelayNetwork::PostError(int wsaErr, const std::wstring& msg)
{
    Post(RelayEvent::Kind::Error, wsaErr, msg.empty() ? WsaMessage(wsaErr) : msg);
}

void RelayNetwork::FatalStop(int wsaErr, const std::wstring& msg)
{
    PostError(wsaErr, msg);
    m_stopRequested = true;
    if (!m_stoppedPosted.exchange(true))
        Post(RelayEvent::Kind::Stopped);
}

// Data weighs its size: when the window falls behind, Push blocks and this
// thread simply reads its socket later. Forwarding was already done.
void RelayNetwork::PostData(bool c2t, const BYTE* data, size_t n, bool forwarded)
{
    if (n == 0) return;
    if (!forwarded) (c2t ? m_heldC2T : m_heldT2C).fetch_add(1);
    RelayEvent ev;
    ev.kind      = RelayEvent::Kind::Data;
    ev.c2t       = c2t;
    ev.forwarded = forwarded;
    ev.data.assign(data, data + n);
    m_events.Push(std::move(ev), n, m_stopRequested);
}

bool RelayNetwork::SleepInterruptible(int ms)
{
    for (int t = 0; t < ms && !m_stopRequested; t += 50)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return !m_stopRequested;
}

// ---- socket slots ----------------------------------------------
// Lock order: send mutex, then m_sockMutex. A sender holds its send mutex for
// the whole send, so a socket can never be closed under a running send().

void RelayNetwork::PublishSocket(std::atomic<SOCKET>& slot, std::mutex& sendMutex, SOCKET s)
{
    std::lock_guard<std::mutex> a(sendMutex);
    std::lock_guard<std::mutex> b(m_sockMutex);
    slot = s;
}

void RelayNetwork::CloseSocket(std::atomic<SOCKET>& slot, std::mutex* sendMutex,
                               std::atomic<bool>* closing)
{
    if (closing) *closing = true;   // makes a send in progress give up
    {
        std::unique_lock<std::mutex> a;
        if (sendMutex) a = std::unique_lock<std::mutex>(*sendMutex);
        std::lock_guard<std::mutex> b(m_sockMutex);
        SOCKET s = slot.exchange(INVALID_SOCKET);
        if (s != INVALID_SOCKET) {
            shutdown(s, SD_BOTH);
            closesocket(s);
        }
    }
    if (closing) *closing = false;
}

// ---- StartRelay / StopRelay ------------------------------------

bool RelayNetwork::StartRelay(int listenPort, const std::wstring& targetHost, int targetPort,
                               int timeoutSec, bool udpMode)
{
    if (m_acceptThread.joinable() || m_targetThread.joinable()) return false;

    m_stopRequested   = false;
    m_stoppedPosted   = false;
    m_listening       = false;
    m_clientConnected = false;
    m_targetConnected = false;
    m_heldC2T = m_heldT2C = 0;
    m_udpMode    = udpMode;
    m_timeoutSec = timeoutSec > 0 ? timeoutSec : 0;
    m_udpClient = {};
    m_waiting.clear();

    try {
        if (m_udpMode) {
            m_acceptThread = std::thread([this, listenPort, targetHost, targetPort]() {
                UdpLoop(listenPort, targetHost, targetPort);
            });
        } else {
            // Two independent threads: the target connection is made (and
            // re-made) whether or not a client is there.
            m_targetThread = std::thread([this, targetHost, targetPort]() {
                TargetConnectLoop(targetHost, targetPort);
            });
            m_acceptThread = std::thread([this, listenPort]() {
                AcceptClientLoop(listenPort);
            });
        }
    } catch (...) {
        StopRelay();
        return false;
    }
    return true;
}

void RelayNetwork::StopRelay()
{
    m_stopRequested = true;
    if (m_acceptThread.joinable()) m_acceptThread.join();
    if (m_targetThread.joinable()) m_targetThread.join();

    // The threads close their own sockets on exit; this is only a safety net.
    CloseSocket(m_clientSock, &m_clientSendMutex, &m_clientClosing);
    CloseSocket(m_targetSock, &m_targetSendMutex, &m_targetClosing);
    CloseSocket(m_listenSock, m_udpMode ? &m_clientSendMutex : nullptr, nullptr);
    m_clientConnected = false;
    m_targetConnected = false;
    m_listening       = false;
}

// ---- Sending ---------------------------------------------------

// Non-blocking send loop. Gives up when stopping, when the socket is being
// closed, on a socket error, or when no byte could be sent for maxStallMs
// (0 = wait without limit). Returns the number of bytes sent.
size_t RelayNetwork::SendAll(SOCKET s, const BYTE* p, size_t n, int maxStallMs,
                             const std::atomic<bool>& closing)
{
    size_t sent = 0;
    auto lastProgress = std::chrono::steady_clock::now();
    while (sent < n && !m_stopRequested && !closing) {
        int chunk = (int)std::min<size_t>(n - sent, 1 << 20);
        int r = ::send(s, (const char*)p + sent, chunk, 0);
        if (r > 0) {
            sent += (size_t)r;
            lastProgress = std::chrono::steady_clock::now();
            continue;
        }
        if (r == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set w; FD_ZERO(&w); FD_SET(s, &w);
            timeval tv { 0, 100000 };
            select(0, nullptr, &w, nullptr, &tv);
            if (maxStallMs > 0 &&
                std::chrono::steady_clock::now() - lastProgress > std::chrono::milliseconds(maxStallMs))
                break;
            continue;
        }
        break;
    }
    return sent;
}

bool RelayNetwork::SendDatagram(SOCKET s, const BYTE* p, size_t n, const NetAddr::Endpoint* to)
{
    if (n == 0 || n > 65507) return false;
    for (int tries = 0; tries < 20 && !m_stopRequested; ++tries) {
        int r = to ? sendto(s, (const char*)p, (int)n, 0, to->sa(), to->len)
                   : ::send(s, (const char*)p, (int)n, 0);
        if (r >= 0) return true;
        if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
        fd_set w; FD_ZERO(&w); FD_SET(s, &w);
        timeval tv { 0, 50000 };
        select(0, nullptr, &w, nullptr, &tv);
    }
    return false;
}

size_t RelayNetwork::SendToTarget(const BYTE* p, size_t n, int maxStallMs)
{
    std::lock_guard<std::mutex> lk(m_targetSendMutex);
    SOCKET s = m_targetSock;
    if (s == INVALID_SOCKET || !m_targetConnected) return 0;
    if (m_udpMode) return SendDatagram(s, p, n, nullptr) ? n : 0;
    return SendAll(s, p, n, maxStallMs, m_targetClosing);
}

size_t RelayNetwork::SendToClient(const BYTE* p, size_t n, int maxStallMs)
{
    std::lock_guard<std::mutex> lk(m_clientSendMutex);
    if (!m_clientConnected) return 0;
    if (m_udpMode) {
        SOCKET ls = m_listenSock;
        if (ls == INVALID_SOCKET) return 0;
        return SendDatagram(ls, p, n, &m_udpClient) ? n : 0;
    }
    SOCKET s = m_clientSock;
    if (s == INVALID_SOCKET) return 0;
    return SendAll(s, p, n, maxStallMs, m_clientClosing);
}

// The UI thread must not stall for long: wait at most 2 s without progress.
size_t RelayNetwork::ForwardToTarget(const BYTE* data, size_t len)
{
    if (!data || len == 0) return 0;
    return SendToTarget(data, len, 2000);
}

size_t RelayNetwork::ForwardToClient(const BYTE* data, size_t len)
{
    if (!data || len == 0) return 0;
    return SendToClient(data, len, 2000);
}

// One received chunk: forward it when allowed, and tell the UI what happened.
// A partly sent chunk is reported as two parts (sent / not sent) so the UI
// never forwards the same bytes twice.
void RelayNetwork::HandleData(bool c2t, const BYTE* d, int n)
{
    size_t sent = 0;
    bool autoOn = c2t ? m_autoForwardC2T.load() : m_autoForwardT2C.load();
    int  held   = c2t ? m_heldC2T.load()        : m_heldT2C.load();
    if (autoOn && held == 0) {
        int stall = m_timeoutSec > 0 ? m_timeoutSec * 1000 : 0;
        sent = c2t ? SendToTarget(d, (size_t)n, stall) : SendToClient(d, (size_t)n, stall);
    }
    if (sent > 0) PostData(c2t, d, sent, true);
    if (sent < (size_t)n) PostData(c2t, d + sent, (size_t)n - sent, false);
}

// ---- TargetConnectLoop -----------------------------------------
// Connects to the target, relays until it drops, reconnects every 2 s.

void RelayNetwork::TargetConnectLoop(std::wstring host, int port)
{
    std::wstring lastError;
    while (!m_stopRequested) {
        Post(RelayEvent::Kind::TargetConnecting);

        SOCKET s = ConnectToTarget(host, port, lastError);
        if (s != INVALID_SOCKET) {
            lastError.clear();
            PublishSocket(m_targetSock, m_targetSendMutex, s);
            m_targetConnected = true;
            Post(RelayEvent::Kind::TargetConn);

            TargetRecvLoop(s);

            m_targetConnected = false;
            CloseSocket(m_targetSock, &m_targetSendMutex, &m_targetClosing);
            if (!m_stopRequested) Post(RelayEvent::Kind::TargetDisconn);
        }
        if (!SleepInterruptible(2000)) break;
    }
    m_targetConnected = false;
    CloseSocket(m_targetSock, &m_targetSendMutex, &m_targetClosing);
}

// Returns a connected, non-blocking socket or INVALID_SOCKET. A failure is
// reported once per distinct reason (the loop retries every few seconds).
// Lookup and connect poll m_stopRequested, so Stop is never held up by them.
SOCKET RelayNetwork::ConnectToTarget(const std::wstring& host, int port, std::wstring& lastError)
{
    auto fail = [&](int code, const std::wstring& why) {
        std::wstring msg = L"Target " + NetAddr::JoinHostPort(host, port) +
                           L": " + why + L" (retrying)";
        if (msg != lastError) { lastError = msg; PostError(code, msg); }
    };

    std::vector<NetAddr::Endpoint> eps;
    int rc = NetAddr::Resolve(host, port, SOCK_STREAM, m_stopRequested, eps);
    if (rc == WSA_E_CANCELLED) return INVALID_SOCKET;
    if (rc != 0) { fail(rc, L"cannot resolve host name"); return INVALID_SOCKET; }

    // timeoutSec 0 ("No timeout") still needs a per-attempt limit: 3 s.
    const int limitMs = (m_timeoutSec > 0 ? m_timeoutSec : 3) * 1000;
    int err = 0;
    SOCKET s = NetAddr::Connect(eps, SOCK_STREAM, limitMs, m_stopRequested, err, true);
    if (s == INVALID_SOCKET) {
        if (err != WSA_E_CANCELLED) fail(err, WsaMessage(err));
        return INVALID_SOCKET;
    }
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    return s;
}

// ---- AcceptClientLoop ------------------------------------------
// Listens and serves one client at a time. While one is relayed, further
// connections are accepted and parked unread (m_waiting, in order, up to
// RELAY_MAX_WAITING); when the current client leaves the first one is next.

static std::wstring ListenFailText(int port, int err)
{
    if (err == WSAEADDRINUSE || err == WSAEACCES)
        return L"Listen port " + std::to_wstring(port) +
               L" is already in use (by another program or another tab)";
    return L"Cannot listen on port " + std::to_wstring(port) + L": " + WsaMessage(err);
}

bool RelayNetwork::AcceptOne(SOCKET ls, Waiting& out)
{
    NetAddr::Endpoint peer;
    peer.len = sizeof(peer.addr);
    SOCKET cs = accept(ls, peer.sa(), &peer.len);
    if (cs == INVALID_SOCKET) return false;
    BOOL nd = TRUE;
    setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));
    SetNonBlocking(cs);
    out.sock = cs;
    out.addr = NetAddr::ToString(peer);
    return true;
}

// A parked client that went away without sending anything (or reset) is
// dropped; one that sent data and then closed still gets its turn.
void RelayNetwork::SweepWaiting()
{
    for (size_t i = 0; i < m_waiting.size(); ) {
        SOCKET s = m_waiting[i].sock;
        fd_set rfds; FD_ZERO(&rfds); FD_SET(s, &rfds);
        timeval tv { 0, 0 };
        bool gone = false;
        if (select(0, &rfds, nullptr, nullptr, &tv) > 0) {
            char c;
            int n = recv(s, &c, 1, MSG_PEEK);
            if (n == 0) gone = true;
            else if (n < 0) {
                int e = WSAGetLastError();
                gone = (e != WSAEWOULDBLOCK && e != WSAEMSGSIZE);
            }
        }
        if (!gone) { ++i; continue; }
        std::wstring addr = m_waiting[i].addr;
        closesocket(s);
        m_waiting.erase(m_waiting.begin() + i);
        Post(RelayEvent::Kind::ClientWaitLeft, (int)m_waiting.size(), addr);
    }
}

void RelayNetwork::CloseWaiting()
{
    for (auto& w : m_waiting) closesocket(w.sock);
    m_waiting.clear();
}

void RelayNetwork::AcceptClientLoop(int listenPort)
{
    int err = 0;
    SOCKET ls = NetAddr::CreateListenSocket(SOCK_STREAM, listenPort, err);
    if (ls == INVALID_SOCKET) { FatalStop(err, ListenFailText(listenPort, err)); return; }
    if (listen(ls, SOMAXCONN) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        closesocket(ls);
        FatalStop(e, L"listen() failed: " + WsaMessage(e));
        return;
    }
    SetNonBlocking(ls);

    {
        std::lock_guard<std::mutex> lk(m_sockMutex);
        m_listenSock = ls;
    }
    m_listening = true;
    Post(RelayEvent::Kind::Listening, listenPort);

    while (!m_stopRequested) {
        Waiting cur;
        bool fromWait = false;
        if (!m_waiting.empty()) {
            cur = m_waiting.front();
            m_waiting.pop_front();
            fromWait = true;
        } else {
            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(ls, &rfds);
            timeval tv { 0, 100000 };
            int r = select(0, &rfds, nullptr, nullptr, &tv);
            if (r < 0) { FatalStop(WSAGetLastError(), L"Listening socket failed"); break; }
            if (r == 0 || !AcceptOne(ls, cur)) continue;
        }

        PublishSocket(m_clientSock, m_clientSendMutex, cur.sock);
        m_clientConnected = true;
        Post(RelayEvent::Kind::ClientConn, (int)m_waiting.size(), cur.addr, fromWait);

        ClientRecvLoop(cur.sock, ls);

        m_clientConnected = false;
        CloseSocket(m_clientSock, &m_clientSendMutex, &m_clientClosing);
        if (!m_stopRequested) Post(RelayEvent::Kind::ClientDisconn);
    }

    CloseWaiting();
    m_listening = false;
    CloseSocket(m_listenSock, nullptr, nullptr);
    m_stopRequested = true;   // lets TargetConnectLoop end too
}

// ---- ClientRecvLoop / TargetRecvLoop ---------------------------

void RelayNetwork::ClientRecvLoop(SOCKET s, SOCKET ls)
{
    std::vector<BYTE> buf(65536);
    ULONGLONG lastSweep = GetTickCount64();
    while (!m_stopRequested) {
        // At the limit the listen socket is left alone: further clients wait
        // in the system's backlog instead.
        const bool watchListen = m_waiting.size() < RELAY_MAX_WAITING;
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(s, &rfds);
        if (watchListen) FD_SET(ls, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;

        if (watchListen && r > 0 && FD_ISSET(ls, &rfds)) {
            Waiting w;
            if (AcceptOne(ls, w)) {
                m_waiting.push_back(w);
                Post(RelayEvent::Kind::ClientWaiting, (int)m_waiting.size(), w.addr);
            }
        }
        if (GetTickCount64() - lastSweep >= 250) {
            lastSweep = GetTickCount64();
            SweepWaiting();
        }
        if (r == 0 || !FD_ISSET(s, &rfds)) continue;

        int n = recv(s, (char*)buf.data(), (int)buf.size(), 0);
        if (n == 0) break;
        if (n < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
            break;
        }
        HandleData(true, buf.data(), n);
    }
}

void RelayNetwork::TargetRecvLoop(SOCKET s)
{
    std::vector<BYTE> buf(65536);
    while (!m_stopRequested) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(s, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
        if (r == 0) continue;

        int n = recv(s, (char*)buf.data(), (int)buf.size(), 0);
        if (n == 0) break;
        if (n < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
            break;
        }
        HandleData(false, buf.data(), n);
    }
}

// ---- UdpLoop ---------------------------------------------------
// One thread serves both directions. The "client" is whoever sent the last
// datagram; replies from the target go to that exact endpoint. The listen
// socket is dual-stack; the target socket has the family of the target.

void RelayNetwork::UdpLoop(int listenPort, std::wstring host, int targetPort)
{
    int err = 0;
    SOCKET ls = NetAddr::CreateListenSocket(SOCK_DGRAM, listenPort, err);
    if (ls == INVALID_SOCKET) { FatalStop(err, ListenFailText(listenPort, err)); return; }
    DisableUdpConnReset(ls);

    std::vector<NetAddr::Endpoint> eps;
    int rc = NetAddr::Resolve(host, targetPort, SOCK_DGRAM, m_stopRequested, eps);
    if (rc != 0) {
        closesocket(ls);
        if (rc != WSA_E_CANCELLED)
            FatalStop(rc, L"Failed to resolve target: " + NetAddr::JoinHostPort(host, targetPort));
        return;
    }
    SOCKET ts = NetAddr::Connect(eps, SOCK_DGRAM, 0, m_stopRequested, err, true);
    if (ts == INVALID_SOCKET) {
        closesocket(ls);
        if (err != WSA_E_CANCELLED)
            FatalStop(err, L"Cannot create the target socket: " + WsaMessage(err));
        return;
    }
    DisableUdpConnReset(ts);
    SetNonBlocking(ls);

    PublishSocket(m_listenSock, m_clientSendMutex, ls);
    PublishSocket(m_targetSock, m_targetSendMutex, ts);
    m_listening       = true;
    m_targetConnected = true;
    Post(RelayEvent::Kind::Listening, listenPort);
    Post(RelayEvent::Kind::TargetConn);

    std::vector<BYTE> buf(65536);
    while (!m_stopRequested) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(ls, &rfds); FD_SET(ts, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) { FatalStop(WSAGetLastError(), L"select() failed"); break; }
        if (r == 0) continue;

        if (FD_ISSET(ls, &rfds)) {
            NetAddr::Endpoint from;
            from.len = sizeof(from.addr);
            int n = recvfrom(ls, (char*)buf.data(), (int)buf.size(), 0, from.sa(), &from.len);
            if (n > 0) {
                bool isNew;
                {
                    std::lock_guard<std::mutex> lk(m_clientSendMutex);
                    isNew = !m_clientConnected || !NetAddr::Same(from, m_udpClient);
                    if (isNew) { m_udpClient = from; m_clientConnected = true; }
                }
                if (isNew) Post(RelayEvent::Kind::ClientConn, 0, NetAddr::ToString(from));
                HandleData(true, buf.data(), n);
            }
        }
        if (FD_ISSET(ts, &rfds)) {
            int n = recv(ts, (char*)buf.data(), (int)buf.size(), 0);
            if (n > 0) HandleData(false, buf.data(), n);
        }
    }

    m_listening       = false;
    m_targetConnected = false;
    m_clientConnected = false;
    CloseSocket(m_targetSock, &m_targetSendMutex, &m_targetClosing);
    CloseSocket(m_listenSock, &m_clientSendMutex, nullptr);
}
