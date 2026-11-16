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

static std::wstring AddrStr(const sockaddr_in& a)
{
    wchar_t ip[64] = {};
    InetNtopW(AF_INET, (void*)&a.sin_addr, ip, 64);
    return std::wstring(ip) + L":" + std::to_wstring(ntohs(a.sin_port));
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

static int ResolveV4(const std::string& host, int port, sockaddr_in& out)
{
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0 || !res) return rc ? rc : WSAHOST_NOT_FOUND;
    out = {};
    memcpy(&out, res->ai_addr, std::min<size_t>(res->ai_addrlen, sizeof(out)));
    freeaddrinfo(res);
    return 0;
}

// ============================================================
//  RelayNetwork
// ============================================================

RelayNetwork::RelayNetwork(HWND notifyWnd)
    : m_notifyWnd(notifyWnd)
{
}

RelayNetwork::~RelayNetwork()
{
    StopRelay();
}

// A full message queue (10 000 messages) makes PostMessage fail; wait for
// the UI to catch up instead of losing data. Any other failure (window
// gone) gives up.
bool RelayNetwork::PostRetry(UINT msg, WPARAM wp, LPARAM lp)
{
    for (;;) {
        if (PostMessageW(m_notifyWnd, msg, wp, lp)) return true;
        if (GetLastError() != ERROR_NOT_ENOUGH_QUOTA || m_stopRequested) return false;
        Sleep(5);
    }
}

void RelayNetwork::PostError(int wsaErr, const std::wstring& msg)
{
    auto* p = new std::wstring(msg.empty() ? WsaMessage(wsaErr) : msg);
    if (!PostRetry(WM_RELAY_ERROR, (WPARAM)wsaErr, (LPARAM)p)) delete p;
}

void RelayNetwork::FatalStop(int wsaErr, const std::wstring& msg)
{
    PostError(wsaErr, msg);
    m_stopRequested = true;
    if (!m_stoppedPosted.exchange(true))
        PostMessageW(m_notifyWnd, WM_RELAY_STOPPED, 0, 0);
}

void RelayNetwork::PostData(bool c2t, const BYTE* data, size_t n, bool forwarded)
{
    if (n == 0) return;
    if (!forwarded) (c2t ? m_heldC2T : m_heldT2C).fetch_add(1);
    BYTE* copy = new BYTE[n];
    memcpy(copy, data, n);
    WPARAM wp = (WPARAM)n;
    if (forwarded) wp |= RELAY_FLAG_FORWARDED;
    if (!PostRetry(c2t ? WM_RELAY_CLIENT_DATA : WM_RELAY_TARGET_DATA, wp, (LPARAM)copy))
        delete[] copy;
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

bool RelayNetwork::StartRelay(int listenPort, const std::string& targetHost, int targetPort,
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
    m_udpClientAddr = {};

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

bool RelayNetwork::SendDatagram(SOCKET s, const BYTE* p, size_t n, const sockaddr_in* to)
{
    if (n == 0 || n > 65507) return false;
    for (int tries = 0; tries < 20 && !m_stopRequested; ++tries) {
        int r = to ? sendto(s, (const char*)p, (int)n, 0, (const sockaddr*)to, sizeof(*to))
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
        return SendDatagram(ls, p, n, &m_udpClientAddr) ? n : 0;
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

void RelayNetwork::TargetConnectLoop(std::string host, int port)
{
    std::wstring lastError;
    while (!m_stopRequested) {
        PostRetry(WM_RELAY_TARGET_CONNECTING, 0, 0);

        SOCKET s = ConnectToTarget(host, port, lastError);
        if (s != INVALID_SOCKET) {
            lastError.clear();
            PublishSocket(m_targetSock, m_targetSendMutex, s);
            m_targetConnected = true;
            PostRetry(WM_RELAY_TARGET_CONN, 0, 0);

            TargetRecvLoop(s);

            m_targetConnected = false;
            CloseSocket(m_targetSock, &m_targetSendMutex, &m_targetClosing);
            if (!m_stopRequested) PostRetry(WM_RELAY_TARGET_DISCONN, 0, 0);
        }
        if (!SleepInterruptible(2000)) break;
    }
    m_targetConnected = false;
    CloseSocket(m_targetSock, &m_targetSendMutex, &m_targetClosing);
}

// Returns a connected, non-blocking socket or INVALID_SOCKET. A failure is
// reported once per distinct reason (the loop retries every few seconds).
SOCKET RelayNetwork::ConnectToTarget(const std::string& host, int port, std::wstring& lastError)
{
    auto fail = [&](int code, const std::wstring& why) {
        std::wstring msg = L"Target " + Utils::ToWide(host) + L":" + std::to_wstring(port) +
                           L": " + why + L" (retrying)";
        if (msg != lastError) { lastError = msg; PostError(code, msg); }
    };

    sockaddr_in addr = {};
    int rc = ResolveV4(host, port, addr);
    if (rc != 0) { fail(rc, L"cannot resolve host name"); return INVALID_SOCKET; }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { fail(WSAGetLastError(), L"socket() failed"); return INVALID_SOCKET; }
    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    SetNonBlocking(s);

    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK) {
        fail(WSAGetLastError(), WsaMessage(WSAGetLastError()));
        closesocket(s);
        return INVALID_SOCKET;
    }

    // timeoutSec 0 ("No timeout") still needs a per-attempt limit: 3 s.
    int limitMs = (m_timeoutSec > 0 ? m_timeoutSec : 3) * 1000;
    auto start = std::chrono::steady_clock::now();
    fd_set wfds, efds;
    for (;;) {
        if (m_stopRequested) { closesocket(s); return INVALID_SOCKET; }
        FD_ZERO(&wfds); FD_SET(s, &wfds);
        FD_ZERO(&efds); FD_SET(s, &efds);
        timeval tv { 0, 100000 };
        int r = select(0, nullptr, &wfds, &efds, &tv);
        if (r > 0) break;
        if (r < 0) { fail(WSAGetLastError(), L"select() failed"); closesocket(s); return INVALID_SOCKET; }
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(limitMs)) {
            fail(WSAETIMEDOUT, L"connection timed out");
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
    if (FD_ISSET(s, &efds)) {
        int err = 0, len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        fail(err, WsaMessage(err));
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

// ---- AcceptClientLoop ------------------------------------------
// Listens and serves one client at a time (further clients wait in the backlog).

void RelayNetwork::AcceptClientLoop(int listenPort)
{
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { FatalStop(WSAGetLastError(), L"socket() failed"); return; }

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)listenPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        closesocket(ls);
        FatalStop(e, L"bind() failed on port " + std::to_wstring(listenPort) + L": " + WsaMessage(e));
        return;
    }
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
    PostRetry(WM_RELAY_LISTENING, (WPARAM)listenPort, 0);

    while (!m_stopRequested) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(ls, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) { FatalStop(WSAGetLastError(), L"Listening socket failed"); break; }
        if (r == 0) continue;

        sockaddr_in ca = {}; int clen = sizeof(ca);
        SOCKET cs = accept(ls, (sockaddr*)&ca, &clen);
        if (cs == INVALID_SOCKET) continue;

        BOOL nd = TRUE;
        setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));
        SetNonBlocking(cs);

        PublishSocket(m_clientSock, m_clientSendMutex, cs);
        m_clientConnected = true;
        auto* a = new std::wstring(AddrStr(ca));
        if (!PostRetry(WM_RELAY_CLIENT_CONN, 0, (LPARAM)a)) delete a;

        ClientRecvLoop(cs);

        m_clientConnected = false;
        CloseSocket(m_clientSock, &m_clientSendMutex, &m_clientClosing);
        if (!m_stopRequested) PostRetry(WM_RELAY_CLIENT_DISCONN, 0, 0);
    }

    m_listening = false;
    CloseSocket(m_listenSock, nullptr, nullptr);
    m_stopRequested = true;   // lets TargetConnectLoop end too
}

// ---- ClientRecvLoop / TargetRecvLoop ---------------------------

void RelayNetwork::ClientRecvLoop(SOCKET s)
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
// datagram; replies from the target go to that address.

void RelayNetwork::UdpLoop(int listenPort, std::string host, int targetPort)
{
    SOCKET ls = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ls == INVALID_SOCKET) { FatalStop(WSAGetLastError(), L"socket() failed"); return; }
    DisableUdpConnReset(ls);

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)listenPort);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        closesocket(ls);
        FatalStop(e, L"bind() failed on port " + std::to_wstring(listenPort) + L": " + WsaMessage(e));
        return;
    }

    sockaddr_in taddr = {};
    int rc = ResolveV4(host, targetPort, taddr);
    if (rc != 0) {
        closesocket(ls);
        FatalStop(rc, L"Failed to resolve target: " + Utils::ToWide(host));
        return;
    }
    SOCKET ts = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ts == INVALID_SOCKET || ::connect(ts, (sockaddr*)&taddr, sizeof(taddr)) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (ts != INVALID_SOCKET) closesocket(ts);
        closesocket(ls);
        FatalStop(e, L"Cannot create the target socket: " + WsaMessage(e));
        return;
    }
    DisableUdpConnReset(ts);
    SetNonBlocking(ls);
    SetNonBlocking(ts);

    PublishSocket(m_listenSock, m_clientSendMutex, ls);
    PublishSocket(m_targetSock, m_targetSendMutex, ts);
    m_listening       = true;
    m_targetConnected = true;
    PostRetry(WM_RELAY_LISTENING, (WPARAM)listenPort, 0);
    PostRetry(WM_RELAY_TARGET_CONN, 0, 0);

    std::vector<BYTE> buf(65536);
    while (!m_stopRequested) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(ls, &rfds); FD_SET(ts, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) { FatalStop(WSAGetLastError(), L"select() failed"); break; }
        if (r == 0) continue;

        if (FD_ISSET(ls, &rfds)) {
            sockaddr_in from = {};
            int fromLen = sizeof(from);
            int n = recvfrom(ls, (char*)buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);
            if (n > 0) {
                bool isNew;
                {
                    std::lock_guard<std::mutex> lk(m_clientSendMutex);
                    isNew = !m_clientConnected ||
                            from.sin_addr.s_addr != m_udpClientAddr.sin_addr.s_addr ||
                            from.sin_port != m_udpClientAddr.sin_port;
                    if (isNew) { m_udpClientAddr = from; m_clientConnected = true; }
                }
                if (isNew) {
                    auto* a = new std::wstring(AddrStr(from));
                    if (!PostRetry(WM_RELAY_CLIENT_CONN, 0, (LPARAM)a)) delete a;
                }
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
