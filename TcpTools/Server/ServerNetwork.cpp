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

static std::wstring AddrToString(const sockaddr_in& a)
{
    wchar_t ip[64] = {};
    InetNtopW(AF_INET, (void*)&a.sin_addr, ip, 64);
    wchar_t combined[96];
    swprintf_s(combined, L"%s:%d", ip, (int)ntohs(a.sin_port));
    return combined;
}

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

ServerNetwork::ServerNetwork(HWND notifyWnd, bool echoMode)
    : m_notifyWnd(notifyWnd), m_echoMode(echoMode)
{
    m_sendThread = std::thread([this] { SendWorkerLoop(); });
}

ServerNetwork::~ServerNetwork()
{
    Shutdown();
}

bool ServerNetwork::Post(UINT msg, WPARAM w, LPARAM l)
{
    HWND h = m_notifyWnd.load();
    return h && PostMessageW(h, msg, w, l);
}

void ServerNetwork::PostError(int wsaErr, const std::wstring& msg)
{
    std::wstring text = msg;
    if (wsaErr != 0) {
        wchar_t buf[256] = {};
        DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, (DWORD)wsaErr, 0, buf, 256, nullptr);
        while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n' || buf[n - 1] == L' ')) buf[--n] = 0;
        if (n) text += L": " + std::wstring(buf);
        text += L" (" + std::to_wstring(wsaErr) + L")";
    }
    auto* p = new std::wstring(std::move(text));
    if (!Post(WM_NET_ERROR, (WPARAM)wsaErr, (LPARAM)p)) delete p;
}

void ServerNetwork::AppendLogFile(const std::vector<BYTE>& data)
{
    if (m_logFile.empty()) return;
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
    m_notifyWnd = nullptr;
    StopServer();
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
        if (was) Post(WM_NET_DISCONNECTED, 0, 0);
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
    SOCKET s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        PostError(WSAGetLastError(), L"socket() failed");
    }
    else {
        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((u_short)port);
        addr.sin_addr.s_addr = INADDR_ANY;

        bool ok = true;
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            PostError(WSAGetLastError(), L"bind() failed on port " + std::to_wstring(port));
            ok = false;
        }
        else if (udp) {
            // Without this an ICMP "port unreachable" (peer gone after an echo) makes
            // recvfrom fail with WSAECONNRESET.
            DWORD bytes = 0; BOOL off = FALSE;
            WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
        }
        else if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
            PostError(WSAGetLastError(), L"listen() failed");
            ok = false;
        }

        if (ok) {
            { std::lock_guard<std::mutex> lk(m_sockMutex); m_listenSock = s; }
            m_listening = true;
            Post(WM_NET_LISTENING, (WPARAM)port, 0);

            if (udp) ServeUdp(s);
            else     ServeTcp(s, timeoutSec);

            m_listening = false;
            CloseOwned(m_listenSock);
        }
        else {
            closesocket(s);
        }
    }

    ClearQueue();
    m_clientConnected = false;
    // Last message of every run, also a failed start, so the UI always returns to idle.
    Post(WM_NET_STOPPED, 0, 0);
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

        sockaddr_in from = {};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, (char*)buf.data(), BUF, 0, (sockaddr*)&from, &fromLen);
        Utils::TimePoint tRecv = Utils::Now();
        if (n <= 0) { if (n < 0) Sleep(5); continue; }

        bool newPeer;
        {
            std::lock_guard<std::mutex> lk(m_sockMutex);
            newPeer = !m_clientConnected
                   || memcmp(&from.sin_addr, &m_udpPeer.sin_addr, sizeof(from.sin_addr)) != 0
                   || from.sin_port != m_udpPeer.sin_port;
            if (newPeer) { m_udpPeer = from; m_clientConnected = true; }
        }
        if (newPeer) {
            auto* addrStr = new std::wstring(AddrToString(from));
            if (!Post(WM_NET_CONNECTED, 0, (LPARAM)addrStr)) delete addrStr;
            std::lock_guard<std::mutex> lk(m_statsMutex);
            m_stats = {};
            m_stats.connectTime = Utils::Now();
            m_stats.valid = true;
        }

        std::vector<BYTE> chunk(buf.begin(), buf.begin() + n);
        AppendLogFile(chunk);

        if (m_echoMode) {
            Utils::TimePoint echoStart = Utils::Now();
            int ns = 0;
            {
                std::lock_guard<std::mutex> sl(m_sendMutex);
                ns = sendto(sock, (char*)buf.data(), n, 0, (sockaddr*)&from, sizeof(from));
            }
            int err = (ns == SOCKET_ERROR) ? WSAGetLastError() : 0;
            Utils::TimePoint echoEnd = Utils::Now();
            if (ns == SOCKET_ERROR) { ns = 0; PostError(err, L"Echo sendto() failed"); }

            auto* pStats = new CycleStats;
            {
                std::lock_guard<std::mutex> slk(m_statsMutex);
                m_stats.recvStart = m_stats.recvEnd = tRecv;
                m_stats.bytesReceived = (size_t)n;
                m_stats.sendStart = echoStart;
                m_stats.sendEnd   = echoEnd;
                m_stats.bytesSent = (size_t)ns;
                *pStats = m_stats;
            }
            pStats->isEcho = true;
            pStats->ok     = (err == 0);
            if (!Post(WM_NET_SEND_COMPLETE, (WPARAM)ns, (LPARAM)pStats)) delete pStats;
        }
        else {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            if (m_stats.bytesReceived == 0) m_stats.recvStart = tRecv;
            m_stats.bytesReceived += (size_t)n;
            m_stats.recvEnd = tRecv;
        }

        BYTE* copy = new BYTE[n];
        memcpy(copy, buf.data(), n);
        if (!Post(WM_NET_DATA_RECEIVED, (WPARAM)n, (LPARAM)copy)) delete[] copy;
    }

    if (m_clientConnected.exchange(false))
        Post(WM_NET_DISCONNECTED, 0, 0);
}

void ServerNetwork::ServeTcp(SOCKET listenSock, int /*timeoutSec*/)
{
    // One client at a time; further connections wait in the backlog until the
    // current client is gone.
    while (!m_stopRequested)
    {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(listenSock, &rfds);
        timeval tv { 0, 200000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r == SOCKET_ERROR) { PostError(WSAGetLastError(), L"select() failed"); break; }
        if (r == 0) continue;

        sockaddr_in clientAddr = {};
        int clen = sizeof(clientAddr);
        SOCKET clientSock = accept(listenSock, (sockaddr*)&clientAddr, &clen);
        if (clientSock == INVALID_SOCKET) { Sleep(10); continue; }

        // Receive uses select() polling; the timeout bounds how long a send may stall.
        DWORD sndMs = m_sndTimeoutMs;
        setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (char*)&sndMs, sizeof(sndMs));
        BOOL nd = TRUE;
        setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));

        {
            std::lock_guard<std::mutex> lk(m_sockMutex);
            m_clientSock = clientSock;
        }
        {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            m_stats = {};
            m_stats.connectTime = Utils::Now();
            m_stats.valid = true;
        }
        m_clientConnected = true;

        auto* addrStr = new std::wstring(AddrToString(clientAddr));
        if (!Post(WM_NET_CONNECTED, 0, (LPARAM)addrStr)) delete addrStr;

        RecvLoop(clientSock);

        m_clientConnected = false;
        ClearQueue();
        CloseOwned(m_clientSock);
        Post(WM_NET_DISCONNECTED, 0, 0);
    }
}

void ServerNetwork::RecvLoop(SOCKET clientSock)
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!m_stopRequested)
    {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(clientSock, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r == SOCKET_ERROR) break;
        if (r == 0) continue;

        int n = recv(clientSock, (char*)buf.data(), BUF, 0);
        if (n <= 0) break;
        Utils::TimePoint tRecv = Utils::Now();

        std::vector<BYTE> chunk(buf.begin(), buf.begin() + n);

        // The log file is written before the echo so a reader that has seen the
        // echo can rely on the file being complete.
        AppendLogFile(chunk);

        if (m_echoMode) {
            Utils::TimePoint echoStart = Utils::Now();
            int sent = 0, err = 0;
            {
                std::lock_guard<std::mutex> sl(m_sendMutex);
                SendAll(clientSock, buf.data(), n, sent, err);
            }
            Utils::TimePoint echoEnd = Utils::Now();
            if (err) PostError(err, L"Echo send() failed");

            // An echo is its own transaction: statistics describe this chunk only.
            auto* pStats = new CycleStats;
            {
                std::lock_guard<std::mutex> slk(m_statsMutex);
                m_stats.recvStart = m_stats.recvEnd = tRecv;
                m_stats.bytesReceived = (size_t)n;
                m_stats.sendStart = echoStart;
                m_stats.sendEnd   = echoEnd;
                m_stats.bytesSent = (size_t)sent;
                *pStats = m_stats;
            }
            pStats->isEcho = true;
            pStats->ok     = (err == 0);
            if (!Post(WM_NET_SEND_COMPLETE, (WPARAM)sent, (LPARAM)pStats)) delete pStats;
            if (err) {
                // The peer is not taking data (timeout) or is gone: drop the connection.
                // The data just received is still reported below.
                BYTE* copy = new BYTE[n];
                memcpy(copy, buf.data(), n);
                if (!Post(WM_NET_DATA_RECEIVED, (WPARAM)n, (LPARAM)copy)) delete[] copy;
                break;
            }
        }
        else {
            std::lock_guard<std::mutex> lk(m_statsMutex);
            if (m_stats.bytesReceived == 0) m_stats.recvStart = tRecv;
            m_stats.bytesReceived += (size_t)n;
            m_stats.recvEnd = tRecv;
        }

        BYTE* copy = new BYTE[n];
        memcpy(copy, buf.data(), n);
        if (!Post(WM_NET_DATA_RECEIVED, (WPARAM)n, (LPARAM)copy)) delete[] copy;
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
        sockaddr_in peer = {};
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
            int n = sendto(s, (const char*)data.data(), total, 0, (sockaddr*)&peer, sizeof(peer));
            if (n == SOCKET_ERROR) { ok = false; err = WSAGetLastError(); what = L"sendto() failed"; }
            else sent = n;
        }
        else {
            ok = SendAll(s, data.data(), total, sent, err);
        }
    }
    Utils::TimePoint t1 = Utils::Now();
    if (!ok) PostError(err, what);

    auto* pStats = new CycleStats;
    {
        std::lock_guard<std::mutex> slk(m_statsMutex);
        m_stats.sendEnd   = t1;
        m_stats.bytesSent = (size_t)sent;
        *pStats = m_stats;
    }
    pStats->isEcho = false;
    pStats->ok     = ok;
    pStats->id     = job.id;
    if (!Post(WM_NET_SEND_COMPLETE, (WPARAM)sent, (LPARAM)pStats)) delete pStats;

    if (ok && job.cfg.autoDisconnect)
        DisconnectClient();
}

} // namespace Server
