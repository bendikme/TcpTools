#include "pch.h"
#include "RelayNetwork.h"
#include "resource.h"

// ============================================================
//  Helpers
// ============================================================

static std::wstring GetPeerAddrStr(SOCKET s)
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
//  RelayNetwork
// ============================================================

RelayNetwork::RelayNetwork(HWND notifyWnd)
    : m_notifyWnd(notifyWnd)
{
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
}

RelayNetwork::~RelayNetwork()
{
    StopRelay();
    WSACleanup();
}

void RelayNetwork::PostError(int wsaErr, const std::wstring& msg)
{
    std::wstring* p = new std::wstring(msg.empty()
        ? [&]() -> std::wstring {
              wchar_t buf[256] = {};
              FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, (DWORD)wsaErr, 0, buf, 256, nullptr);
              return buf;
          }()
        : msg);
    PostMessage(m_notifyWnd, WM_RELAY_ERROR, (WPARAM)wsaErr, (LPARAM)p);
}

// ---- StartRelay ------------------------------------------------

bool RelayNetwork::StartRelay(int listenPort, const std::string& targetHost, int targetPort,
                               int timeoutSec, bool udpMode)
{
    if (m_listening) return false;
    m_stopRequested = false;
    m_udpMode = udpMode;

    if (m_udpMode) {
        // UDP: single combined loop (target + client together)
        std::thread([this, listenPort, targetHost, targetPort, timeoutSec]() {
            AcceptLoop(listenPort, targetHost, targetPort, timeoutSec);
        }).detach();
    } else {
        // TCP: two independent threads - connect to target immediately AND accept clients
        std::thread([this, targetHost, targetPort, timeoutSec]() {
            TargetConnectLoop(targetHost, targetPort, timeoutSec);
        }).detach();
        std::thread([this, listenPort, timeoutSec]() {
            AcceptClientLoop(listenPort, timeoutSec);
        }).detach();
    }

    return true;
}

// ---- TargetConnectLoop -----------------------------------------
// Independent thread: continuously connects to target, retries on disconnect.

void RelayNetwork::TargetConnectLoop(std::string host, int port, int timeoutSec)
{
    while (!m_stopRequested) {
        // Notify UI that we're attempting to connect
        PostMessage(m_notifyWnd, WM_RELAY_TARGET_CONNECTING, 0, 0);

        if (ConnectToTarget(host, port, timeoutSec)) {
            // ConnectToTarget set m_targetConnected=true and posted WM_RELAY_TARGET_CONN
            TargetRecvLoop(); // blocks; TargetRecvLoop posts WM_RELAY_TARGET_DISCONN on exit

            // Cleanup dead target socket
            if (m_targetSock != INVALID_SOCKET) {
                shutdown(m_targetSock, SD_BOTH);
                closesocket(m_targetSock);
                m_targetSock = INVALID_SOCKET;
            }
        }

        if (m_stopRequested) break;

        // Wait 2 seconds before retrying (interruptible)
        for (int i = 0; i < 10 && !m_stopRequested; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (m_targetSock != INVALID_SOCKET) {
        shutdown(m_targetSock, SD_BOTH);
        closesocket(m_targetSock);
        m_targetSock = INVALID_SOCKET;
    }
    m_targetConnected = false;
}

// ---- AcceptClientLoop ------------------------------------------
// Independent thread: listens for clients, one at a time.

void RelayNetwork::AcceptClientLoop(int listenPort, int timeoutSec)
{
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        PostError(WSAGetLastError(), L"socket() failed");
        m_stopRequested = true;
        PostMessage(m_notifyWnd, WM_RELAY_STOPPED, 0, 0);
        return;
    }
    BOOL reuse = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    BOOL nodelay = TRUE;
    setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)listenPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        PostError(WSAGetLastError(), L"bind() failed on port " + std::to_wstring(listenPort));
        closesocket(listenSock);
        m_stopRequested = true;
        PostMessage(m_notifyWnd, WM_RELAY_STOPPED, 0, 0);
        return;
    }
    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        PostError(WSAGetLastError(), L"listen() failed");
        closesocket(listenSock);
        m_stopRequested = true;
        PostMessage(m_notifyWnd, WM_RELAY_STOPPED, 0, 0);
        return;
    }

    m_listenSock = listenSock;
    m_listening  = true;
    PostMessage(m_notifyWnd, WM_RELAY_LISTENING, (WPARAM)listenPort, 0);

    while (!m_stopRequested) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(listenSock, &rfds);
        timeval tv { 0, 200000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
        if (r == 0) continue;

        sockaddr_in clientAddr = {}; int clen = sizeof(clientAddr);
        SOCKET clientSock = accept(listenSock, (sockaddr*)&clientAddr, &clen);
        if (clientSock == INVALID_SOCKET) {
            if (m_stopRequested) break;
            continue;
        }

        BOOL nd = TRUE;
        setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));
        if (timeoutSec > 0) {
            DWORD tvMs = (DWORD)(timeoutSec * 1000);
            setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tvMs, sizeof(tvMs));
            setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (char*)&tvMs, sizeof(tvMs));
        }

        m_clientSock      = clientSock;
        m_clientConnected = true;
        PostMessage(m_notifyWnd, WM_RELAY_CLIENT_CONN, 0,
                    (LPARAM)new std::wstring(GetPeerAddrStr(clientSock)));

        ClientRecvLoop(); // blocks until client disconnects

        m_clientConnected = false;
        if (m_clientSock != INVALID_SOCKET) {
            closesocket(m_clientSock);
            m_clientSock = INVALID_SOCKET;
        }
        if (!m_stopRequested)
            PostMessage(m_notifyWnd, WM_RELAY_CLIENT_DISCONN, 0, 0);
    }

    m_listening  = false;
    m_listenSock = INVALID_SOCKET;
    closesocket(listenSock);

    // Signal TargetConnectLoop to exit too
    if (!m_stopRequested) {
        m_stopRequested = true;
        PostMessage(m_notifyWnd, WM_RELAY_STOPPED, 0, 0);
    }
}

// ---- StopRelay -------------------------------------------------

void RelayNetwork::StopRelay()
{
    m_stopRequested = true;
    m_clientConnected = false;
    m_targetConnected = false;

    if (m_clientSock != INVALID_SOCKET) {
        shutdown(m_clientSock, SD_BOTH);
        closesocket(m_clientSock);
        m_clientSock = INVALID_SOCKET;
    }
    if (m_targetSock != INVALID_SOCKET) {
        shutdown(m_targetSock, SD_BOTH);
        closesocket(m_targetSock);
        m_targetSock = INVALID_SOCKET;
    }
    if (m_listenSock != INVALID_SOCKET) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }
    m_listening = false;
    PostMessage(m_notifyWnd, WM_RELAY_STOPPED, 0, 0);
}

// ---- ConnectToTarget -------------------------------------------

bool RelayNetwork::ConnectToTarget(const std::string& host, int port, int timeoutSec)
{
    int type  = m_udpMode ? SOCK_DGRAM  : SOCK_STREAM;
    int proto = m_udpMode ? IPPROTO_UDP : IPPROTO_TCP;
    SOCKET s = socket(AF_INET, type, proto);
    if (s == INVALID_SOCKET) {
        PostError(WSAGetLastError(), L"target socket() failed");
        return false;
    }

    DWORD tv = (DWORD)(timeoutSec * 1000);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

    if (!m_udpMode) {
        BOOL nodelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    }

    // Resolve target
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = type;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        PostError(WSAGetLastError(), L"Failed to resolve target: " + Utils::ToWide(host));
        closesocket(s);
        return false;
    }
    sockaddr_in addr = {};
    memcpy(&addr, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    // Non-blocking connect with timeout
    // timeoutSec==0 means "no-timeout mode": 3s per attempt, no error posted on failure
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    ::connect(s, (sockaddr*)&addr, sizeof(addr));

    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_SET(s, &wfds);
    FD_ZERO(&efds); FD_SET(s, &efds);
    int effectiveSec = (timeoutSec == 0) ? 3 : timeoutSec;
    timeval timeout { effectiveSec, 0 };
    int r = select(0, nullptr, &wfds, &efds, &timeout);

    if (r <= 0 || FD_ISSET(s, &efds)) {
        if (timeoutSec > 0) {
            PostError(r == 0 ? WSAETIMEDOUT : WSAGetLastError(),
                      r == 0 ? L"Target connection timed out" : L"Target connection failed");
        }
        closesocket(s);
        return false;
    }

    // Back to blocking
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);

    m_targetSock = s;
    m_targetConnected = true;
    PostMessage(m_notifyWnd, WM_RELAY_TARGET_CONN, 0, 0);
    return true;
}

// ---- AcceptLoop ------------------------------------------------

void RelayNetwork::AcceptLoop(int listenPort, std::string targetHost, int targetPort, int timeoutSec)
{
    int type  = m_udpMode ? SOCK_DGRAM  : SOCK_STREAM;
    int proto = m_udpMode ? IPPROTO_UDP : IPPROTO_TCP;
    SOCKET listenSock = socket(AF_INET, type, proto);
    if (listenSock == INVALID_SOCKET) {
        PostError(WSAGetLastError(), L"socket() failed");
        return;
    }

    BOOL reuse = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    if (!m_udpMode) {
        BOOL nodelay = TRUE;
        setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    }

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)listenPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        PostError(WSAGetLastError(), L"bind() failed on port " + std::to_wstring(listenPort));
        closesocket(listenSock);
        return;
    }

    if (m_udpMode)
    {
        // ---- UDP relay mode ----
        m_listenSock = listenSock;
        m_listening  = true;
        PostMessage(m_notifyWnd, WM_RELAY_LISTENING, (WPARAM)listenPort, 0);

        // Connect to target (UDP just sets the default dest)
        if (!ConnectToTarget(targetHost, targetPort, timeoutSec)) {
            closesocket(listenSock);
            m_listenSock = INVALID_SOCKET;
            m_listening  = false;
            return;
        }

        // Start target recv in separate thread
        std::thread targetRecvThread([this]() { TargetRecvLoop(); });

        // Client recv loop on this thread
        constexpr int BUF = 65536;
        std::vector<BYTE> buf(BUF);
        m_udpClientAddr = {};

        while (!m_stopRequested)
        {
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
            if (!m_clientConnected ||
                memcmp(&fromAddr.sin_addr, &m_udpClientAddr.sin_addr, sizeof(fromAddr.sin_addr)) != 0 ||
                fromAddr.sin_port != m_udpClientAddr.sin_port)
            {
                m_udpClientAddr   = fromAddr;
                m_clientConnected = true;
                wchar_t ipBuf[64] = {};
                InetNtopW(AF_INET, &fromAddr.sin_addr, ipBuf, 64);
                wchar_t combined[80];
                swprintf_s(combined, L"%s:%d", ipBuf, ntohs(fromAddr.sin_port));
                PostMessage(m_notifyWnd, WM_RELAY_CLIENT_CONN, 0, (LPARAM)new std::wstring(combined));
            }

            bool forwarded = false;
            if (m_autoForwardC2T && m_targetConnected) {
                std::lock_guard<std::mutex> lk(m_targetSendMutex);
                int sent = ::send(m_targetSock, (char*)buf.data(), n, 0);
                forwarded = (sent > 0);
            }

            BYTE* copy = new BYTE[n];
            memcpy(copy, buf.data(), n);
            WPARAM wp = (WPARAM)n;
            if (forwarded) wp |= RELAY_FLAG_FORWARDED;
            PostMessage(m_notifyWnd, WM_RELAY_CLIENT_DATA, wp, (LPARAM)copy);
        }

        // Wait for target recv thread
        m_targetConnected = false;
        if (m_targetSock != INVALID_SOCKET) {
            shutdown(m_targetSock, SD_BOTH);
            closesocket(m_targetSock);
            m_targetSock = INVALID_SOCKET;
        }
        targetRecvThread.join();
    }
    else
    {
        // ---- TCP relay mode ----
        if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
            PostError(WSAGetLastError(), L"listen() failed");
            closesocket(listenSock);
            return;
        }

        m_listenSock = listenSock;
        m_listening  = true;
        PostMessage(m_notifyWnd, WM_RELAY_LISTENING, (WPARAM)listenPort, 0);

        // Accept loop - one client at a time
        while (!m_stopRequested)
        {
            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(listenSock, &rfds);
            timeval tv { 0, 200000 };
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

            // Set timeouts + nodelay
            DWORD tvMs = (DWORD)(timeoutSec * 1000);
            setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO,  (char*)&tvMs, sizeof(tvMs));
            setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO,  (char*)&tvMs, sizeof(tvMs));
            BOOL nd = TRUE;
            setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));

            m_clientSock      = clientSock;
            m_clientConnected = true;
            PostMessage(m_notifyWnd, WM_RELAY_CLIENT_CONN, 0,
                        (LPARAM)new std::wstring(GetPeerAddrStr(clientSock)));

            // Connect to target - with indefinite retry in no-timeout mode (timeoutSec==0)
            bool targetOk = ConnectToTarget(targetHost, targetPort, timeoutSec);
            if (!targetOk && timeoutSec == 0 && !m_stopRequested) {
                PostMessage(m_notifyWnd, WM_RELAY_ERROR, 0,
                            (LPARAM)new std::wstring(L"Target not available, waiting (no-timeout mode)..."));
                while (!m_stopRequested && !targetOk) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    if (m_stopRequested) break;
                    targetOk = ConnectToTarget(targetHost, targetPort, 0);
                }
            }
            if (!targetOk) {
                closesocket(clientSock);
                m_clientSock      = INVALID_SOCKET;
                m_clientConnected = false;
                PostMessage(m_notifyWnd, WM_RELAY_CLIENT_DISCONN, 0, 0);
                continue;
            }

            // Start two recv loops
            std::thread targetRecvThread([this]() { TargetRecvLoop(); });
            ClientRecvLoop(); // blocks until client disconnects

            // Clean up - client disconnected
            m_clientConnected = false;
            if (m_targetSock != INVALID_SOCKET) {
                shutdown(m_targetSock, SD_BOTH);
                closesocket(m_targetSock);
                m_targetSock = INVALID_SOCKET;
            }
            m_targetConnected = false;

            targetRecvThread.join();

            if (m_clientSock != INVALID_SOCKET) {
                closesocket(m_clientSock);
                m_clientSock = INVALID_SOCKET;
            }

            if (!m_stopRequested) {
                PostMessage(m_notifyWnd, WM_RELAY_CLIENT_DISCONN, 0, 0);
                PostMessage(m_notifyWnd, WM_RELAY_TARGET_DISCONN, 0, 0);
            }
        }
    }

    m_listening  = false;
    m_listenSock = INVALID_SOCKET;
    closesocket(listenSock);

    if (!m_stopRequested)
        PostMessage(m_notifyWnd, WM_RELAY_STOPPED, 0, 0);
}

// ---- ClientRecvLoop --------------------------------------------

void RelayNetwork::ClientRecvLoop()
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!m_stopRequested && m_clientConnected)
    {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(m_clientSock, &rfds);
        timeval tv { 0, 100000 }; // 100ms poll
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
        if (r == 0) continue;

        int n = recv(m_clientSock, (char*)buf.data(), BUF, 0);
        if (n <= 0) break;

        bool forwarded = false;
        if (m_autoForwardC2T && m_targetConnected) {
            std::lock_guard<std::mutex> lk(m_targetSendMutex);
            int total = n, sent = 0;
            while (sent < total) {
                int s = ::send(m_targetSock, (char*)buf.data() + sent, total - sent, 0);
                if (s == SOCKET_ERROR) break;
                sent += s;
            }
            forwarded = (sent == total);
        }

        BYTE* copy = new BYTE[n];
        memcpy(copy, buf.data(), n);
        WPARAM wp = (WPARAM)n;
        if (forwarded) wp |= RELAY_FLAG_FORWARDED;
        PostMessage(m_notifyWnd, WM_RELAY_CLIENT_DATA, wp, (LPARAM)copy);
    }
}

// ---- TargetRecvLoop --------------------------------------------

void RelayNetwork::TargetRecvLoop()
{
    constexpr int BUF = 65536;
    std::vector<BYTE> buf(BUF);

    while (!m_stopRequested && m_targetConnected)
    {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(m_targetSock, &rfds);
        timeval tv { 0, 100000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r < 0) break;
        if (r == 0) continue;

        int n = recv(m_targetSock, (char*)buf.data(), BUF, 0);
        if (n <= 0) break;

        bool forwarded = false;
        if (m_autoForwardT2C && m_clientConnected) {
            if (m_udpMode) {
                std::lock_guard<std::mutex> lk(m_clientSendMutex);
                sendto(m_listenSock, (char*)buf.data(), n, 0,
                       (sockaddr*)&m_udpClientAddr, sizeof(m_udpClientAddr));
                forwarded = true;
            } else {
                std::lock_guard<std::mutex> lk(m_clientSendMutex);
                int total = n, sent = 0;
                while (sent < total) {
                    int s = ::send(m_clientSock, (char*)buf.data() + sent, total - sent, 0);
                    if (s == SOCKET_ERROR) break;
                    sent += s;
                }
                forwarded = (sent == total);
            }
        }

        BYTE* copy = new BYTE[n];
        memcpy(copy, buf.data(), n);
        WPARAM wp = (WPARAM)n;
        if (forwarded) wp |= RELAY_FLAG_FORWARDED;
        PostMessage(m_notifyWnd, WM_RELAY_TARGET_DATA, wp, (LPARAM)copy);
    }

    // Target disconnected
    if (m_targetConnected.exchange(false)) {
        PostMessage(m_notifyWnd, WM_RELAY_TARGET_DISCONN, 0, 0);
    }
}

// ---- ForwardToTarget -------------------------------------------

bool RelayNetwork::ForwardToTarget(const std::vector<BYTE>& data)
{
    if (!m_targetConnected || data.empty()) return false;

    std::lock_guard<std::mutex> lk(m_targetSendMutex);
    if (m_udpMode) {
        return ::send(m_targetSock, (char*)data.data(), (int)data.size(), 0) > 0;
    }

    int total = (int)data.size(), sent = 0;
    while (sent < total) {
        int n = ::send(m_targetSock, (char*)data.data() + sent, total - sent, 0);
        if (n == SOCKET_ERROR) return false;
        sent += n;
    }
    return true;
}

// ---- ForwardToClient -------------------------------------------

bool RelayNetwork::ForwardToClient(const std::vector<BYTE>& data)
{
    if (!m_clientConnected || data.empty()) return false;

    if (m_udpMode) {
        std::lock_guard<std::mutex> lk(m_clientSendMutex);
        return sendto(m_listenSock, (char*)data.data(), (int)data.size(), 0,
                      (sockaddr*)&m_udpClientAddr, sizeof(m_udpClientAddr)) > 0;
    }

    std::lock_guard<std::mutex> lk(m_clientSendMutex);
    int total = (int)data.size(), sent = 0;
    while (sent < total) {
        int n = ::send(m_clientSock, (char*)data.data() + sent, total - sent, 0);
        if (n == SOCKET_ERROR) return false;
        sent += n;
    }
    return true;
}
