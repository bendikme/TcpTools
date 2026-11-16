#pragma once
#include "pch.h"
#include "Utils.h"
#include "NetAddr.h"
#include "HandoffQueue.h"

namespace Client {

// ============================================================
//  Network layer - Winsock2 TCP/UDP client (worker threads)
//
//  Architecture:
//    - Core::Run: resolve + connect + recv loop (one worker thread per session, joined)
//    - Core::SendLoop: send thread of the session, processes queued sends
//    - UI thread: QueueSend() pushes to queue (non-blocking)
//    - Every notification to the window goes through one HandoffQueue<NetEvent>
// ============================================================

// Separator / framing modes
enum class SeparatorMode : int {
    None = 0,       // raw bytes
    STX_ETX,        // 0x02 ... 0x03
    CR_LF,          // \r\n appended
    LF,             // \n  appended
    CR,             // \r  appended
    NUL,            // 0x00 appended
    Custom          // user-defined prefix/suffix bytes
};

// Timing snapshot for one send/receive cycle
struct CycleStats {
    Utils::TimePoint connectTime    {};
    Utils::TimePoint sendStart      {};
    Utils::TimePoint sendEnd        {};
    Utils::TimePoint recvStart      {};
    Utils::TimePoint recvEnd        {};
    size_t           bytesSent      {0};
    size_t           bytesReceived  {0};
    bool             valid          {false};

    double ConnectToSendMs()  const { return valid ? Utils::ElapsedMs(connectTime,  sendStart) : -1; }
    double SendDurationMs()   const { return valid ? Utils::ElapsedMs(sendStart,    sendEnd)   : -1; }
    double SendToFirstRecvMs()const { return valid ? Utils::ElapsedMs(sendEnd,      recvStart) : -1; }
    double RecvDurationMs()   const { return valid ? Utils::ElapsedMs(recvStart,    recvEnd)   : -1; }
    double TotalMs()          const { return valid ? Utils::ElapsedMs(sendStart,    recvEnd)   : -1; }
    double ThroughputKBps()   const {
        double t = RecvDurationMs();
        if (t <= 0 || bytesReceived == 0) return 0;
        return (bytesReceived / 1024.0) / (t / 1000.0);
    }
};

struct NetConfig {
    std::string      host;                  // UTF-8, without brackets for IPv6
    int              port          {0};
    int              timeoutSec    {5};
    bool             keepAlive     {true};  // keep connection open after send
    bool             autoDisconnect{false}; // end the session after the reply (see AUTODISC_*)
    bool             udpMode       {false}; // true = UDP, false = TCP
    SeparatorMode    separator     {SeparatorMode::None};
    std::vector<BYTE>customPrefix;          // for Custom mode
    std::vector<BYTE>customSuffix;
};

struct SendRequest {
    std::vector<BYTE> data;
    NetConfig cfg;
};

// What the network threads tell the window. Everything goes through ONE
// HandoffQueue, so events keep their order and the window gets a single
// wake-up message (WM_NET_QUEUE) however much has arrived.
struct NetEvent {
    enum class Kind {
        Connected,      // the session is up
        Disconnected,   // last event of every session; code 1 = never connected, flag = ended by Auto-Disconnect
        Data,           // data = payload; latMs >= 0 when this chunk is the first reply to a send
        SendComplete,   // bytesSent, stats; flag = Auto-Disconnect is now winding the session down
        Error           // code = Winsock error, text = readable message
    };
    Kind              kind {Kind::Error};
    int               code {0};
    bool              flag {false};
    size_t            bytesSent {0};
    double            latMs {-1};
    std::wstring      text;
    std::vector<BYTE> data;
    CycleStats        stats;
};

// Auto-Disconnect: after the send the session stays open to receive the reply and
// ends when the peer closes, or when data has arrived and then nothing more for
// AUTODISC_QUIET_MS, or when nothing at all arrives for AUTODISC_NO_REPLY_MS.
// No half-close is done before that: some devices treat a FIN as an abort.
constexpr int AUTODISC_QUIET_MS    = 250;
constexpr int AUTODISC_NO_REPLY_MS = 1000;

// Lifetime: the destructor always joins the worker. That is quick because name
// lookup and connect are cancellable and every blocking call is woken by the stop
// request. After destruction nothing is posted.
class TcpNetwork
{
public:
    // The window is woken with WM_NET_QUEUE and calls Drain().
    explicit TcpNetwork(HWND notifyWnd);
    ~TcpNetwork();

    // UI thread. False when a session is still running.
    bool     Connect(const NetConfig& cfg);
    // Asks the session to end (also cancels a name lookup or connect in progress).
    // Returns at once; a Disconnected event follows.
    void     Disconnect();

    // UI thread: the queued events, oldest first, up to about maxBytes of payload
    // (at least one event). The queue wakes the window again when more is left.
    std::vector<NetEvent> Drain(size_t maxBytes);

    // Queue a send request (non-blocking, processed by the session's send thread)
    void     QueueSend(std::vector<BYTE> data, const NetConfig& cfg);

    // Direct send (cyclic thread) - blocks until sent or the session ends.
    bool     Send(const std::vector<BYTE>& data, const NetConfig& cfg);

    bool     IsConnected() const;
    // True from Connect() until the session thread has finished.
    bool     IsActive() const;
    // Runs fn(socket) while the socket cannot be closed. fn must not block.
    bool     WithSocket(const std::function<void(SOCKET)>& fn);

    struct Core;
private:
    std::shared_ptr<Core> m_core;
    std::thread           m_worker;
};

} // namespace Client
