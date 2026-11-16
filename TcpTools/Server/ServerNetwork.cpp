#include "pch.h"
#include "ServerNetwork.h"
#include "resource.h"

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

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

// How many clients may wait (accepted, not read) behind the one being served.
// Further ones stay in the listen backlog.
static constexpr size_t MAX_WAITING = 16;

// Blocking send of the whole buffer. Returns false on error (err = WSA error, sent = bytes out).
static bool SendAll(SOCKET s, const BYTE* p, int total, int& sent, int& err)
{
    sent = 0; err = 0;
    while (sent < total) {
        int n = ::send(s, (const char*)p + sent, total - sent, 0);
        if (n == SOCKET_ERROR) { err = WSAGetLastError(); return false; }
        sent += n;
    }
    return true;
}

// ============================================================
//  ServerNetwork
// ============================================================

ServerNetwork::ServerNetwork(HWND notifyWnd, UINT wakeMessage, bool echoMode)
    : m_echoMode(echoMode)
{
    m_events.SetTarget(notifyWnd, wakeMessage);
    m_sendThread = std::thread([this] { SendWorkerLoop(); });
}

ServerNetwork::~ServerNetwork()
{
    Shutdown();
}

bool ServerNetwork::Emit(NetEvent&& ev, size_t weight)
{
    return m_events.Push(std::move(ev), weight, m_stopRequested);
}

void ServerNetwork::PostError(int wsaErr, const std::wstring& msg)
{
    NetEvent ev;
    ev.kind  = NetEvent::Kind::Error;
    ev.value = wsaErr;
    ev.text  = msg;
    if (wsaErr != 0) ev.text += L": " + NetAddr::ErrorText(wsaErr);
    Emit(std::move(ev));
}

void ServerNetwork::AppendLogFile(const std::vector<BYTE>& data)
{
    if (m_logFile.empty()) return;
    // Opened per chunk on purpose: the file is complete after every write, so a
    // reader that has seen the echo can rely on it.
    std::ofstream f(m_logFile, std::ios::binary | std::ios::app);
    if (f) f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
    if ((!f || !f.good()) && !m_logErrorPosted) {
        m_logErrorPosted = true;
        PostError(0, L"Cannot write to log file " + m_logFile);
    }
}

CycleStats ServerNetwork::GetStats()
{
    std::lock_guard<std::mutex> lk(m_statsMutex);
    return m_stats;
}

bool ServerNetwork::WithListenSocket(const std::function<void(SOCKET)>& fn)
{
    std::lock_guard<std::mutex> lk(m_sockMutex);
    if (m_listenSock == INVALID_SOCKET) return false;
    fn(m_listenSock);
    return true;
}

bool ServerNetwork::WithClientSocket(const std::function<void(SOCKET)>& fn)
{
    std::lock_guard<std::mutex> lk(m_sockMutex);
    if (m_clientSock == INVALID_SOCKET) return false;
    fn(m_clientSock);
    return true;
}

// Closes a socket this thread owns. shutdown() first wakes a send that is blocked
// on a stalled peer; then the send mutex is taken so no send is mid-call when the
// handle goes away, and the member is invalidated before the handle is closed.
void ServerNetwork::CloseOwned(SOCKET& member)
{
    SOCKET s;
    { std::lock_guard<std::mutex> lk(m_sockMutex); s = member; }
    if (s == INVALID_SOCKET) return;
    shutdown(s, SD_BOTH);
    std::lock_guard<std::mutex> sl(m_sendMutex);
    { std::lock_guard<std::mutex> lk(m_sockMutex); member = INVALID_SOCKET; }
    closesocket(s);
}

// ---- Start / Stop ------------------------------------------

bool ServerNetwork::StartListening(int port, int timeoutSec, bool udpMode)
{
    std::lock_guard<std::mutex> life(m_lifeMutex);
    if (m_running) return false;
    if (m_acceptThread.joinable()) m_acceptThread.join();   // a finished earlier run

    m_stopRequested   = false;
    m_clientConnected = false;
    m_listening       = false;
    m_udpMode         = udpMode;
    m_sndTimeoutMs    = (DWORD)(timeoutSec > 0 ? timeoutSec * 1000 : 0);
    m_logErrorPosted  = false;
    {
        std::lock_guard<std::mutex> lk(m_statsMutex);
        m_stats = {};
    }
    m_running = true;
    m_acceptThread = std::thread([this, port, timeoutSec] { AcceptThreadMain(port, timeoutSec); });
    return true;
}

void ServerNetwork::StopServer()
{
    std::lock_guard<std::mutex> life(m_lifeMutex);
    m_stopRequested = true;
    ClearQueue();
    {   // wake a blocked recv / send of the client; the accept thread closes it
        std::lock_guard<std::mutex> lk(m_sockMutex);
        if (m_clientSock != INVALID_SOCKET) shutdown(m_clientSock, SD_BOTH);
    }
    if (m_acceptThread.joinable()) m_acceptThread.join();
}

void ServerNetwork::Shutdown()
{
    StopServer();
    m_events.Close();           // no more wake-ups; whatever is still queued is freed
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        m_workerQuit = true;
    }
    m_queueCv.notify_all();
    if (m_sendThread.joinable()) m_sendThread.join();
}

void ServerNetwork::ClearQueue()
{
    std::lock_guard<std::mutex> lk(m_queueMutex);
    m_queue.clear();
}

void ServerNetwork::DisconnectClient()
{
    if (m_udpMode) {
        bool was;
        {
            std::lock_guard<std::mutex> lk(m_sockMutex);
            was = m_clientConnected.exchange(false);
            m_udpPeer = {};
        }
        if (was) { NetEvent ev; ev.kind = NetEvent::Kind::Disconnected; Emit(std::move(ev)); }
        return;
    }
    // TCP: the accept thread sees the closed connection, closes the handle and reports it
    std::lock_guard<std::mutex> lk(m_sockMutex);
    if (m_clientSock != INVALID_SOCKET) shutdown(m_clientSock, SD_BOTH);
}

// ---- Accept thread -----------------------------------------

void ServerNetwork::AcceptThreadMain(int port, int timeoutSec)
{
    const bool udp = m_udpMode;
    bool ok = false;

    // One dual-stack socket (IPv4 and IPv6) with the port taken exclusively: a busy
    // port fails here, with a readable reason, instead of silently sharing it.
    int err = 0;
    SOCKET s = NetAddr::CreateListenSocket(udp ? SOCK_DGRAM : SOCK_STREAM, port, err);
    if (s == INVALID_SOCKET) {
        const bool busy = (err == WSAEADDRINUSE || err == WSAEACCES);
        PostError(err, busy ? L"Cannot listen on port " + std::to_wstring(port) + L" - the port is already in use"
                            : L"Cannot listen on port " + std::to_wstring(port));
    }
    else {
        if (udp) {
            // Without this an ICMP "port unreachable" (peer gone after an echo) makes
            // recvfrom fail with WSAECONNRESET.
            DWORD bytes = 0; BOOL off = FALSE;
            WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
            ok = true;
        }
        else if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
            PostError(WSAGetLastError(), L"listen() failed");
        }
        else ok = true;

        if (ok) {
            { std::lock_guard<std::mutex> lk(m_sockMutex); m_listenSock = s; }
            m_listening = true;
            { NetEvent ev; ev.kind = NetEvent::Kind::Listening; ev.value = port; Emit(std::move(ev)); }

            if (udp) ServeUdp(s);
            else     ServeTcp(s);

            m_listening = false;
            CloseOwned(m_listenSock);
        }
        else {
            closesocket(s);
        }
    }
    (void)timeoutSec;

    ClearQueue();
    m_clientConnected = false;
    // Last event of every run, also a failed start, so the UI always returns to idle.
    { NetEvent ev; ev.kind = NetEvent::Kind::Stopped; Emit(std::move(ev)); }
    m_running = false;
}

void ServerNetwork::ServeUdp(SOCKET sock)
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!m_stopRequested) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(sock, &rfds);
        timeval tv { 0, 200000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r == SOCKET_ERROR) { PostError(WSAGetLastError(), L"select() failed"); break; }
        if (r == 0) continue;

        NetAddr::Endpoint from;
        from.len = sizeof(from.addr);
        int n = recvfrom(sock, (char*)buf.data(), BUF, 0, from.sa(), &from.len);
        Utils::TimePoint tRecv = Utils::Now();
        if (n <= 0) { if (n < 0) Sleep(5); continue; }

        bool newPeer;
        {
            std::lock_guard<std::mutex> lk(m_sockMutex);
            newPeer = !m_clientConnected || !NetAddr::Same(from, m_udpPeer);
            if (newPeer) { m_udpPeer = from; m_clientConnected = true; }
        }
        if (newPeer) {
            {
                std::lock_guard<std::mutex> lk(m_statsMutex);
                m_stats = {};
                m_stats.connectTime = Utils::Now();
                m_stats.valid = true;
            }
            NetEvent ev;
            ev.kind = NetEvent::Kind::Connected;
            ev.text = NetAddr::ToString(from);
            Emit(std::move(ev));
        }

        std::vector<BYTE> chunk(buf.begin(), buf.begin() + n);
        AppendLogFile(chunk);

        if (m_echoMode) {
            Utils::TimePoint echoStart = Utils::Now();
            int ns = 0;
            {
                std::lock_guard<std::mutex> sl(m_sendMutex);
                ns = sendto(sock, (char*)buf.data(), n, 0, from.sa(), from.len);
            }
            int err = (ns == SOCKET_ERROR) ? WSAGetLastError() : 0;
            Utils::TimePoint echoEnd = Utils::Now();
            if (ns == SOCKET_ERROR) { ns = 0; PostError(err, L"Echo sendto() failed"); }

            NetEvent ev;
            ev.kind = NetEvent::Kind::SendComplete;
            {
                std::lock_guard<std::mutex> slk(m_statsMutex);
                m_stats.recvStart = m_stats.recvEnd = tRecv;
                m_stats.bytesReceived = (size_t)n;
                m_stats.sendStart = echoStart;
                m_stats.sendEnd   = echoEnd;
                m_stats.bytesSent = (size_t)ns;
                ev.stats = m_stats;
            }
            ev.stats.isEcho = true;
            ev.stats.ok     = (err == 0);
            ev.bytes        = (size_t)ns;
            Emit(std::move(ev));
        }

        NetEvent ev;
        ev.kind = NetEvent::Kind::Data;
        {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            if (!m_echoMode) {
                if (m_stats.bytesReceived == 0) m_stats.recvStart = tRecv;
                m_stats.bytesReceived += (size_t)n;
                m_stats.recvEnd = tRecv;
            }
            ev.stats = m_stats;
        }
        ev.data = std::move(chunk);
        Emit(std::move(ev), (size_t)n);
    }

    if (m_clientConnected.exchange(false)) {
        NetEvent ev; ev.kind = NetEvent::Kind::Disconnected; Emit(std::move(ev));
    }
}

bool ServerNetwork::AcceptClient(SOCKET listenSock, Parked& out)
{
    NetAddr::Endpoint peer;
    peer.len = sizeof(peer.addr);
    SOCKET s = accept(listenSock, peer.sa(), &peer.len);
    if (s == INVALID_SOCKET) { Sleep(10); return false; }

    // Receive uses select() polling; the timeout bounds how long a send may stall.
    DWORD sndMs = m_sndTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&sndMs, sizeof(sndMs));
    BOOL nd = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));

    out.sock = s;
    out.addr = NetAddr::ToString(peer);
    return true;
}

void ServerNetwork::ServeTcp(SOCKET listenSock)
{
    // One client is served at a time. Others that connect meanwhile are accepted
    // and parked - never read, so whatever they send stays in their socket buffer -
    // and get their turn in arrival order.
    std::deque<Parked> parked;

    while (!m_stopRequested)
    {
        Parked cur {INVALID_SOCKET, {}};
        bool fromWaiting = false;

        if (!parked.empty()) {
            cur = std::move(parked.front());
            parked.pop_front();
            fromWaiting = true;
        }
        else {
            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(listenSock, &rfds);
            timeval tv { 0, 200000 };
            int r = select(0, &rfds, nullptr, nullptr, &tv);
            if (r == SOCKET_ERROR) { PostError(WSAGetLastError(), L"select() failed"); break; }
            if (r == 0) continue;
            if (!AcceptClient(listenSock, cur)) continue;
        }

        {
            std::lock_guard<std::mutex> lk(m_sockMutex);
            m_clientSock = cur.sock;
        }
        {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            m_stats = {};
            m_stats.connectTime = Utils::Now();
            m_stats.valid = true;
        }
        m_clientConnected = true;

        {
            NetEvent ev;
            ev.kind        = NetEvent::Kind::Connected;
            ev.text        = cur.addr;
            ev.value       = (int)parked.size();
            ev.fromWaiting = fromWaiting;
            Emit(std::move(ev));
        }

        RecvLoop(cur.sock, listenSock, parked);

        m_clientConnected = false;
        ClearQueue();
        CloseOwned(m_clientSock);
        { NetEvent ev; ev.kind = NetEvent::Kind::Disconnected; Emit(std::move(ev)); }
    }

    for (Parked& p : parked) closesocket(p.sock);
}

void ServerNetwork::RecvLoop(SOCKET clientSock, SOCKET listenSock, std::deque<Parked>& parked)
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!m_stopRequested)
    {
        // The listen socket is watched too, so a newcomer is noticed (and shown as
        // waiting) while this client is being served.
        const bool watchListen = parked.size() < MAX_WAITING;
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(clientSock, &rfds);
        if (watchListen) FD_SET(listenSock, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r == SOCKET_ERROR) break;
        if (r == 0) continue;

        if (watchListen && FD_ISSET(listenSock, &rfds)) {
            Parked p {INVALID_SOCKET, {}};
            if (AcceptClient(listenSock, p)) {
                NetEvent ev;
                ev.kind  = NetEvent::Kind::Waiting;
                ev.text  = p.addr;
                parked.push_back(std::move(p));
                ev.value = (int)parked.size();
                Emit(std::move(ev));
            }
        }
        if (!FD_ISSET(clientSock, &rfds)) continue;

        int n = recv(clientSock, (char*)buf.data(), BUF, 0);
        if (n <= 0) break;
        Utils::TimePoint tRecv = Utils::Now();

        std::vector<BYTE> chunk(buf.begin(), buf.begin() + n);

        // The log file is written before the echo so a reader that has seen the
        // echo can rely on the file being complete.
        AppendLogFile(chunk);

        int echoErr = 0;
        if (m_echoMode) {
            // The echo goes out here, on the network thread, before the event is
            // queued: it never waits for the window.
            Utils::TimePoint echoStart = Utils::Now();
            int sent = 0;
            {
                std::lock_guard<std::mutex> sl(m_sendMutex);
                SendAll(clientSock, buf.data(), n, sent, echoErr);
            }
            Utils::TimePoint echoEnd = Utils::Now();
            if (echoErr) PostError(echoErr, L"Echo send() failed");

            // An echo is its own transaction: statistics describe this chunk only.
            NetEvent ev;
            ev.kind = NetEvent::Kind::SendComplete;
            {
                std::lock_guard<std::mutex> slk(m_statsMutex);
                m_stats.recvStart = m_stats.recvEnd = tRecv;
                m_stats.bytesReceived = (size_t)n;
                m_stats.sendStart = echoStart;
                m_stats.sendEnd   = echoEnd;
                m_stats.bytesSent = (size_t)sent;
                ev.stats = m_stats;
            }
            ev.stats.isEcho = true;
            ev.stats.ok     = (echoErr == 0);
            ev.bytes        = (size_t)sent;
            Emit(std::move(ev));
        }

        NetEvent ev;
        ev.kind = NetEvent::Kind::Data;
        {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            if (!m_echoMode) {
                if (m_stats.bytesReceived == 0) m_stats.recvStart = tRecv;
                m_stats.bytesReceived += (size_t)n;
                m_stats.recvEnd = tRecv;
            }
            ev.stats = m_stats;
        }
        ev.data = std::move(chunk);
        Emit(std::move(ev), (size_t)n);

        // The peer is not taking data (timeout) or is gone: drop the connection.
        // The data just received was reported above.
        if (echoErr) break;
    }
}

// ---- Send --------------------------------------------------

bool ServerNetwork::QueueSend(const std::vector<BYTE>& data, const NetConfig& cfg, unsigned long long id)
{
    if (!m_clientConnected) return false;
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        if (m_queue.size() >= 256) return false;
        m_queue.push_back(SendJob{ data, cfg, id });
    }
    m_queueCv.notify_one();
    return true;
}

void ServerNetwork::SendWorkerLoop()
{
    for (;;) {
        SendJob job;
        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_queueCv.wait(lk, [this] { return m_workerQuit || !m_queue.empty(); });
            if (m_workerQuit) return;
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }
        ProcessJob(job);
    }
}

void ServerNetwork::ProcessJob(SendJob& job)
{
    std::vector<BYTE> data = ApplySeparator(job.data, job.cfg);
    const int total = (int)data.size();
    int sent = 0, err = 0;
    const wchar_t* what = L"send() failed";
    bool ok = true;

    Utils::TimePoint t0 = Utils::Now();
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        m_stats.sendStart     = t0;
        m_stats.bytesReceived = 0;
        m_stats.recvStart     = {};
        m_stats.recvEnd       = {};
    }

    {
        // The send mutex keeps CloseOwned() from closing the handle during the call.
        std::lock_guard<std::mutex> sl(m_sendMutex);
        const bool udp = m_udpMode;
        SOCKET s = INVALID_SOCKET;
        NetAddr::Endpoint peer;
        {
            std::lock_guard<std::mutex> lk(m_sockMutex);
            if (m_clientConnected) {
                s    = udp ? m_listenSock : m_clientSock;
                peer = m_udpPeer;
            }
        }
        if (s == INVALID_SOCKET) {
            ok = false; err = 0; what = L"Not sent: the client is gone";
        }
        else if (udp) {
            int n = sendto(s, (const char*)data.data(), total, 0, peer.sa(), peer.len);
            if (n == SOCKET_ERROR) { ok = false; err = WSAGetLastError(); what = L"sendto() failed"; }
            else sent = n;
        }
        else {
            ok = SendAll(s, data.data(), total, sent, err);
        }
    }
    Utils::TimePoint t1 = Utils::Now();
    if (!ok) PostError(err, what);

    NetEvent ev;
    ev.kind  = NetEvent::Kind::SendComplete;
    ev.bytes = (size_t)sent;
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        m_stats.sendEnd   = t1;
        m_stats.bytesSent = (size_t)sent;
        ev.stats = m_stats;
    }
    ev.stats.isEcho = false;
    ev.stats.ok     = ok;
    ev.stats.id     = job.id;
    Emit(std::move(ev));

    if (ok && job.cfg.autoDisconnect)
        DisconnectClient();
}

} // namespace Server
