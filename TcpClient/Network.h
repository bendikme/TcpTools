#pragma once
#include "pch.h"
#include "Utils.h"

// ============================================================
//  Network layer - Winsock2 TCP/UDP client (worker threads)
//
//  Architecture:
//    - WorkerThread: connect + recv loop (runs until disconnect)
//    - SendWorkerLoop: persistent send thread, processes queued sends
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
// WM_NET_DISCONNECTED   wParam=0            lParam=nullptr
// WM_NET_SEND_COMPLETE  wParam=bytesSent    lParam=CycleStats*
// WM_NET_DATA_RECEIVED  wParam=dataLen      lParam=BYTE* (heap)
// WM_NET_RECV_LATENCY   wParam=latUs(lo32)  lParam=latUs(hi32)   (microseconds as int64)
// WM_NET_ERROR          wParam=wsaError     lParam=wchar_t* (heap msg)

class TcpNetwork
{
public:
    explicit TcpNetwork(HWND notifyWnd);
    ~TcpNetwork();

    // Called from UI thread
    bool     Connect(const NetConfig& cfg);
    void     Disconnect();

    // Queue a send request (non-blocking, processed by send worker thread)
    void     QueueSend(std::vector<BYTE> data, const NetConfig& cfg);

    // Direct send (for cyclic thread or internal use — blocks until sent)
    bool     Send(const std::vector<BYTE>& data, const NetConfig& cfg);

    bool     IsConnected() const   { return m_connected.load(); }
    SOCKET   GetSocket()   const   { return m_sock; }

private:
    void     WorkerThread(NetConfig cfg);
    void     RecvLoop();
    void     SendWorkerLoop();
    void     PostError(int wsaErr, const std::wstring& msg);

    HWND               m_notifyWnd {nullptr};
    SOCKET             m_sock      {INVALID_SOCKET};
    std::atomic<bool>  m_connected {false};
    std::atomic<bool>  m_stopRecv  {false};
    std::thread        m_recvThread;
    std::mutex         m_sendMutex;       // serializes send() calls

    // Send queue (persistent send worker thread)
    std::queue<SendRequest> m_sendQueue;
    std::mutex              m_sendQueueMutex;
    std::condition_variable m_sendQueueCV;
    std::thread             m_sendThread;
    std::atomic<bool>       m_sendStop {false};

    CycleStats         m_stats;
    std::mutex         m_statsMutex;
    bool               m_udpMode    {false};
};
