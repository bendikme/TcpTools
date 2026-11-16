#pragma once
#include "pch.h"
#include "Utils.h"

namespace Client {

// ============================================================
//  Network layer - Winsock2 TCP/UDP client (worker threads)
//
//  Architecture:
//    - Core::Run: connect + recv loop (one worker thread per session, joined)
//    - Core::SendLoop: send thread of the session, processes queued sends
//    - UI thread: QueueSend() pushes to queue (non-blocking)
//    - Recv thread posts latency via WM_NET_RECV_LATENCY (no mutex)
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
    std::string      host;
    int              port          {0};
    int              timeoutSec    {5};
    bool             keepAlive     {true};  // keep connection open after send
    bool             autoDisconnect{false}; // disconnect immediately after send
    bool             udpMode       {false}; // true = UDP, false = TCP
    SeparatorMode    separator     {SeparatorMode::None};
    std::vector<BYTE>customPrefix;          // for Custom mode
    std::vector<BYTE>customSuffix;
};

struct SendRequest {
    std::vector<BYTE> data;
    NetConfig cfg;
};

// Messages posted back to the UI window (lParam = heap-allocated payload, must free)
// WM_NET_CONNECTED      wParam=0            lParam=nullptr
// WM_NET_DISCONNECTED   wParam=0 (was connected) / 1 (connect failed or cancelled)
//                       Posted exactly once per Connect(), after everything else of
//                       that session, so the UI may call Connect() again when it arrives.
// WM_NET_SEND_COMPLETE  wParam=bytesSent    lParam=CycleStats*
// WM_NET_DATA_RECEIVED  wParam=dataLen      lParam=BYTE* (heap)
// WM_NET_RECV_LATENCY   wParam=latUs(lo32)  lParam=latUs(hi32)   (microseconds as int64)
// WM_NET_ERROR          wParam=wsaError     lParam=std::wstring* (heap msg)

// Lifetime: all threads belong to a shared Core that outlives this object when a
// worker is still inside a blocking name lookup, so destruction never waits for DNS
// and never leaves a thread touching freed memory. After destruction nothing is posted.
class TcpNetwork
{
public:
    explicit TcpNetwork(HWND notifyWnd);
    ~TcpNetwork();

    // UI thread. False when a session is still running.
    bool     Connect(const NetConfig& cfg);
    // Asks the session to end (also cancels a connect in progress). Returns at once;
    // WM_NET_DISCONNECTED follows.
    void     Disconnect();

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
