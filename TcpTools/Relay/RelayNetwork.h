#pragma once
#include "pch.h"
#include "Utils.h"
#include "NetAddr.h"
#include "HandoffQueue.h"

// ============================================================
//  RelayNetwork - TCP/UDP relay (man-in-the-middle)
//
//  Threads (all joinable members, joined by StopRelay / destructor):
//    TCP: m_acceptThread = AcceptClientLoop (listen, relay one client at a
//         time, park further ones unread in m_waiting) and
//         m_targetThread = TargetConnectLoop (resolve + connect to the target,
//         receive from it, reconnect when it drops).
//    UDP: m_acceptThread = UdpLoop (one thread, select() on both sockets).
//  Every socket is non-blocking and is closed only by the thread that owns it,
//  so every loop notices a stop request within ~100 ms. Name lookup and
//  connect poll the stop flag (NetAddr), so Stop never waits for them.
//
//  Auto-forward decision (per packet, made on the receiving thread): the
//  packet is sent on at once when auto-forward is on AND nothing of that
//  direction is held (m_held*). Everything else is reported to the UI as
//  "not forwarded" and counted as held until the UI reports it forwarded or
//  dropped (ReleaseHeld). That keeps the order of a stream intact when
//  auto-forward is toggled while data flows. Forwarding happens here, on the
//  network thread; the window is only told afterwards.
//
//  Everything the window must know goes through ONE HandoffQueue<RelayEvent>
//  (owned by the window, which drains it in slices), so events keep their order.
// ============================================================

struct RelayEvent {
    enum class Kind {
        Listening,        // num = port
        ClientConn,       // text = peer, num = clients still waiting, fromWait
        ClientWaiting,    // text = peer, num = clients waiting now
        ClientWaitLeft,   // text = peer, num = clients waiting now (it gave up)
        ClientDisconn,
        TargetConnecting,
        TargetConn,
        TargetDisconn,
        Data,             // c2t, forwarded, data
        Error,            // num = Winsock error, text
        Stopped           // the relay ended by itself (fatal error); never after StopRelay()
    };
    Kind              kind     {Kind::Error};
    int               num      {0};
    bool              c2t      {false};
    bool              forwarded{false};
    bool              fromWait {false};
    std::wstring      text;
    std::vector<BYTE> data;
};

// How many further TCP clients are parked (unread, in order) while one is relayed.
constexpr size_t RELAY_MAX_WAITING = 16;

class RelayNetwork
{
public:
    explicit RelayNetwork(HandoffQueue<RelayEvent>& events);
    ~RelayNetwork();

    // targetHost without brackets (name, IPv4 or IPv6 text).
    bool StartRelay(int listenPort, const std::wstring& targetHost, int targetPort,
                    int timeoutSec, bool udpMode);
    // Stops and joins all threads (at most a few hundred ms). Idempotent.
    void StopRelay();

    // Manual forwarding (UI thread). Returns how many bytes were sent; less
    // than len means the peer is gone or not accepting data. For UDP, call
    // once per datagram.
    size_t ForwardToTarget(const BYTE* data, size_t len);
    size_t ForwardToClient(const BYTE* data, size_t len);

    // The UI tells how many held packets it forwarded or dropped.
    void ReleaseHeld(bool c2t, int packets) { (c2t ? m_heldC2T : m_heldT2C).fetch_sub(packets); }

    void SetAutoForwardC2T(bool on) { m_autoForwardC2T = on; }
    void SetAutoForwardT2C(bool on) { m_autoForwardT2C = on; }
    bool GetAutoForwardC2T() const  { return m_autoForwardC2T; }
    bool GetAutoForwardT2C() const  { return m_autoForwardT2C; }

    bool IsListening()       const { return m_listening.load(); }
    bool IsClientConnected() const { return m_clientConnected.load(); }
    bool IsTargetConnected() const { return m_targetConnected.load(); }

    bool IsUdpMode() const { return m_udpMode; }
    std::wstring GetProtocolStr() const { return m_udpMode ? L"UDP" : L"TCP"; }

    // Socket handles for diagnostics. Hold SockMutex() while using them: a
    // socket is closed under that mutex.
    std::mutex& SockMutex() { return m_sockMutex; }
    SOCKET GetListenSocket() const { return m_listenSock; }
    SOCKET GetClientSocket() const { return m_clientSock; }
    SOCKET GetTargetSocket() const { return m_targetSock; }

private:
    struct Waiting { SOCKET sock {INVALID_SOCKET}; std::wstring addr; };

    void AcceptClientLoop(int listenPort);
    void TargetConnectLoop(std::wstring host, int port);
    void UdpLoop(int listenPort, std::wstring host, int targetPort);
    SOCKET ConnectToTarget(const std::wstring& host, int port, std::wstring& lastError);
    bool   AcceptOne(SOCKET ls, Waiting& out);
    void   SweepWaiting();
    void   CloseWaiting();
    void ClientRecvLoop(SOCKET s, SOCKET ls);
    void TargetRecvLoop(SOCKET s);

    void HandleData(bool c2t, const BYTE* data, int n);
    void PostData(bool c2t, const BYTE* data, size_t n, bool forwarded);
    void Post(RelayEvent::Kind kind, int num = 0, std::wstring text = {}, bool fromWait = false);
    void PostError(int wsaErr, const std::wstring& msg);
    void FatalStop(int wsaErr, const std::wstring& msg);

    size_t SendToTarget(const BYTE* p, size_t n, int maxStallMs);
    size_t SendToClient(const BYTE* p, size_t n, int maxStallMs);
    size_t SendAll(SOCKET s, const BYTE* p, size_t n, int maxStallMs,
                   const std::atomic<bool>& closing);
    bool   SendDatagram(SOCKET s, const BYTE* p, size_t n, const NetAddr::Endpoint* to);

    void PublishSocket(std::atomic<SOCKET>& slot, std::mutex& sendMutex, SOCKET s);
    void CloseSocket(std::atomic<SOCKET>& slot, std::mutex* sendMutex,
                     std::atomic<bool>* closing);
    bool SleepInterruptible(int ms);

    HandoffQueue<RelayEvent>& m_events;
    std::atomic<SOCKET> m_listenSock     {INVALID_SOCKET};
    std::atomic<SOCKET> m_clientSock     {INVALID_SOCKET};
    std::atomic<SOCKET> m_targetSock     {INVALID_SOCKET};
    std::atomic<bool>  m_clientClosing   {false};
    std::atomic<bool>  m_targetClosing   {false};

    std::atomic<bool>  m_listening       {false};
    std::atomic<bool>  m_clientConnected {false};
    std::atomic<bool>  m_targetConnected {false};
    std::atomic<bool>  m_stopRequested   {false};
    std::atomic<bool>  m_stoppedPosted   {false};

    std::atomic<bool>  m_autoForwardC2T  {true};
    std::atomic<bool>  m_autoForwardT2C  {true};
    std::atomic<int>   m_heldC2T         {0};
    std::atomic<int>   m_heldT2C         {0};

    bool               m_udpMode         {false};
    int                m_timeoutSec      {0};     // 0 = no timeout
    NetAddr::Endpoint  m_udpClient;               // guarded by m_clientSendMutex

    std::deque<Waiting> m_waiting;                // accept thread only

    std::thread        m_acceptThread;
    std::thread        m_targetThread;

    std::mutex         m_sockMutex;        // guards closing of sockets / diagnostics
    std::mutex         m_clientSendMutex;  // serialises sends to the client (+ UDP address)
    std::mutex         m_targetSendMutex;  // serialises sends to the target
};
