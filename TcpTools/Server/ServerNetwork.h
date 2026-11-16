#pragma once
#include "pch.h"
#include "Utils.h"
#include "NetAddr.h"
#include "HandoffQueue.h"

namespace Server {

// ============================================================
//  ServerNetwork - Winsock2 TCP/UDP server (IPv4 and IPv6)
//
//  Threads (all joinable members, joined before reuse / destruction):
//    - accept thread : bind + listen + accept (TCP: one client is served inside
//                      RecvLoop, further clients are accepted and parked without
//                      being read) or recvfrom (UDP)
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

// Everything the network threads tell the window goes through ONE HandoffQueue
// (ordered, bounded, a single wake-up message per burst). Stopped is always the
// last event of a run, also when the start failed.
struct NetEvent {
    enum class Kind { Listening, Stopped, Connected, Disconnected, Data, SendComplete, Error, Waiting };
    Kind               kind        {Kind::Error};
    int                value       {0};      // Listening: port; Error: Winsock error; Connected / Waiting: clients still waiting
    std::wstring       text;                 // Connected / Waiting: client address; Error: message
    std::vector<BYTE>  data;                 // Data
    CycleStats         stats;                // Data / SendComplete: snapshot taken on the network thread
    size_t             bytes       {0};      // SendComplete: bytes sent
    bool               fromWaiting {false};  // Connected: the client had been waiting for its turn
};

class ServerNetwork
{
public:
    ServerNetwork(HWND notifyWnd, UINT wakeMessage, bool echoMode = false);
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

    // The window drains this when it receives its wake-up message.
    HandoffQueue<NetEvent>& Events() { return m_events; }

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
    struct Parked { SOCKET sock; std::wstring addr; };
    void  ServeTcp(SOCKET sock);
    void  RecvLoop(SOCKET clientSock, SOCKET listenSock, std::deque<Parked>& parked);
    bool  AcceptClient(SOCKET listenSock, Parked& out);
    void  SendWorkerLoop();
    void  ProcessJob(SendJob& job);
    void  CloseOwned(SOCKET& member);
    void  ClearQueue();
    void  PostError(int wsaErr, const std::wstring& msg);
    void  AppendLogFile(const std::vector<BYTE>& data);
    // Queues an event for the window; `weight` = payload bytes. May wait while the
    // window is behind, but never past a stop request.
    bool  Emit(NetEvent&& ev, size_t weight = 0);

    HandoffQueue<NetEvent> m_events;
    std::atomic<bool>  m_echoMode        {false};
    std::wstring       m_logFile;
    bool               m_logErrorPosted  {false};   // accept thread only

    // Guarded by m_sockMutex
    SOCKET             m_listenSock      {INVALID_SOCKET};
    SOCKET             m_clientSock      {INVALID_SOCKET};
    NetAddr::Endpoint  m_udpPeer         {};
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
