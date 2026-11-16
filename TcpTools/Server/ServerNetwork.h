#pragma once
#include "pch.h"
#include "Utils.h"

namespace Server {

// ============================================================
//  ServerNetwork - Winsock2 TCP/UDP server (IPv4 only)
//
//  Threads (all joinable members, joined before reuse / destruction):
//    - accept thread : bind + listen + accept (TCP, one client at a time, receives
//                      inside RecvLoop) or recvfrom (UDP)
//    - send worker   : persistent; processes QueueSend() jobs so the UI thread
//                      never blocks on a stalled peer
//
//  Socket ownership: a socket is closed only by the thread that created it
//  (the accept thread), via CloseOwned(). Other threads only call shutdown()
//  to wake a blocked call, always under m_sockMutex while the handle is valid.
//  CloseOwned() waits for an in-flight send (m_sendMutex) before closing, so a
//  send never touches a closed or recycled handle.
//
//  State machine:
//    Idle  -[ StartListening() ]->  Listening
//    Listening  -[ client connects ]->  ClientConnected
//    ClientConnected -[ client disconnects | DisconnectClient() ]-> Listening
//    Any  -[ StopServer() ]->  Idle
//  UDP has no connection: the "client" is the sender of the last datagram.
// ============================================================

// Separator / framing (shared with TcpClient concept, same enum)
enum class SeparatorMode : int {
    None = 0,
    STX_ETX,
    CR_LF,
    LF,
    CR,
    NUL,
    Custom
};

struct CycleStats {
    Utils::TimePoint connectTime    {};
    Utils::TimePoint sendStart      {};
    Utils::TimePoint sendEnd        {};
    Utils::TimePoint recvStart      {};
    Utils::TimePoint recvEnd        {};
    size_t           bytesSent      {0};
    size_t           bytesReceived  {0};
    bool             valid          {false};
    bool             isEcho         {false};  // completion of an echo reply, not of a QueueSend job
    bool             ok             {true};   // false: the send failed (bytesSent may be partial)
    unsigned long long id           {0};      // QueueSend id (0 for echo)

    double ConnectToSendMs()   const { return valid ? Utils::ElapsedMs(connectTime, sendStart)  : -1; }
    double SendDurationMs()    const { return valid ? Utils::ElapsedMs(sendStart,   sendEnd)    : -1; }
    double SendToFirstRecvMs() const { return valid ? Utils::ElapsedMs(sendEnd,     recvStart)  : -1; }
    double RecvDurationMs()    const { return valid ? Utils::ElapsedMs(recvStart,   recvEnd)    : -1; }
    double TotalMs()           const { return valid ? Utils::ElapsedMs(sendStart,   recvEnd)    : -1; }
    double ThroughputKBps()    const {
        double t = RecvDurationMs();
        if (t <= 0 || bytesReceived == 0) return 0;
        return (bytesReceived / 1024.0) / (t / 1000.0);
    }
};

struct NetConfig {
    std::string      host;          // unused on server side
    int              port          {9001};
    int              timeoutSec    {5};
    bool             keepAlive     {true};
    bool             autoDisconnect{false};
    bool             udpMode       {false}; // true = UDP, false = TCP
    SeparatorMode    separator     {SeparatorMode::None};
    std::vector<BYTE>customPrefix;
    std::vector<BYTE>customSuffix;
};

// Messages posted to the UI window (PostMessage only, never SendMessage):
//  WM_NET_LISTENING        wParam=port             lParam=nullptr
//  WM_NET_STOPPED          wParam=0                lParam=nullptr   (always the last message of a run, also when start failed)
//  WM_NET_CONNECTED        wParam=0                lParam=std::wstring* client "ip:port" (heap, must delete)
//  WM_NET_DISCONNECTED     wParam=0                lParam=nullptr
//  WM_NET_DATA_RECEIVED    wParam=len              lParam=BYTE*   (heap new[], must delete[])
//  WM_NET_SEND_COMPLETE    wParam=bytesSent        lParam=CycleStats* (heap, must delete)
//  WM_NET_ERROR            wParam=wsaErr           lParam=std::wstring* (heap, must delete)

class ServerNetwork
{
public:
    explicit ServerNetwork(HWND notifyWnd, bool echoMode = false);
    ~ServerNetwork();

    bool  StartListening(int port, int timeoutSec = 5, bool udpMode = false);
    // Stops listening and waits (<= ~250 ms) for the accept thread. Posts WM_NET_STOPPED
    // (from the accept thread) when a run was active.
    void  StopServer();
    // Stops everything and detaches from the window: nothing is posted afterwards.
    void  Shutdown();
    // Queue a payload for the connected client (UDP: the last sender). Never blocks.
    // Returns false when there is no client or the queue is full. `id` comes back in CycleStats::id.
    bool  QueueSend(const std::vector<BYTE>& data, const NetConfig& cfg, unsigned long long id);
    void  DisconnectClient();

    bool  IsRunning()         const { return m_running.load(); }
    bool  IsListening()       const { return m_listening.load(); }
    bool  IsClientConnected() const { return m_clientConnected.load(); }

    // Call fn(socket) while the socket is guaranteed to stay open; false if there is none.
    bool  WithListenSocket(const std::function<void(SOCKET)>& fn);
    bool  WithClientSocket(const std::function<void(SOCKET)>& fn);

    CycleStats GetStats();

    void  SetEchoMode(bool echo)    { m_echoMode = echo; }
    bool  GetEchoMode()       const { return m_echoMode; }

    // For test/CLI: append received bytes to this file. Set before StartListening().
    void  SetLogFile(const std::wstring& path) { m_logFile = path; }

private:
    struct SendJob {
        std::vector<BYTE>  data;
        NetConfig          cfg;
        unsigned long long id {0};
    };

    void  AcceptThreadMain(int port, int timeoutSec);
    void  ServeUdp(SOCKET sock);
    void  ServeTcp(SOCKET sock, int timeoutSec);
    void  RecvLoop(SOCKET clientSock);
    void  SendWorkerLoop();
    void  ProcessJob(SendJob& job);
    void  CloseOwned(SOCKET& member);
    void  ClearQueue();
    void  PostError(int wsaErr, const std::wstring& msg);
    void  AppendLogFile(const std::vector<BYTE>& data);
    bool  Post(UINT msg, WPARAM w, LPARAM l);

    std::atomic<HWND>  m_notifyWnd       {nullptr};
    std::atomic<bool>  m_echoMode        {false};
    std::wstring       m_logFile;
    bool               m_logErrorPosted  {false};   // accept thread only

    // Guarded by m_sockMutex
    SOCKET             m_listenSock      {INVALID_SOCKET};
    SOCKET             m_clientSock      {INVALID_SOCKET};
    sockaddr_in        m_udpPeer         {};
    std::mutex         m_sockMutex;

    std::atomic<bool>  m_running         {false};   // accept thread active
    std::atomic<bool>  m_listening       {false};
    std::atomic<bool>  m_clientConnected {false};
    std::atomic<bool>  m_stopRequested   {false};
    std::atomic<bool>  m_udpMode         {false};
    std::atomic<DWORD> m_sndTimeoutMs    {5000};

    std::mutex         m_lifeMutex;      // serialises Start / Stop / Shutdown
    std::thread        m_acceptThread;

    std::mutex         m_sendMutex;      // one writer at a time; held across the send call
    std::thread        m_sendThread;
    std::mutex         m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<SendJob>m_queue;
    bool               m_workerQuit      {false};

    CycleStats         m_stats;
    std::mutex         m_statsMutex;
};

} // namespace Server
