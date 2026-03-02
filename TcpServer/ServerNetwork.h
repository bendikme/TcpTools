#pragma once
#include "pch.h"
#include "Utils.h"

// ============================================================
//  ServerNetwork - Winsock2 TCP/UDP server (listen / accept / send / recv)
//
//  Architecture:
//    - AcceptLoop thread: listen + accept (TCP) or recvfrom (UDP)
//    - RecvLoop: per-client recv (TCP only, runs in AcceptLoop)
//    - SendWorkerLoop: persistent send thread, processes queued sends
//    - UI thread: QueueSend() pushes to queue (non-blocking)
//
//  State machine:
//    Idle  -[ StartListening() ]->  Listening
//    Listening  -[ client connects ]->  ClientConnected
//    ClientConnected -[ client disconnects | DisconnectClient() ]-> Listening
//    Any  -[ StopServer() ]->  Idle
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

// Messages posted to the UI window:
//  WM_NET_LISTENING        wParam=port             lParam=nullptr
//  WM_NET_STOPPED          wParam=0                lParam=nullptr
//  WM_NET_CONNECTED        wParam=0                lParam=wchar_t* client "ip:port" (heap)
//  WM_NET_DISCONNECTED     wParam=0                lParam=nullptr
//  WM_NET_DATA_RECEIVED    wParam=len              lParam=BYTE*   (heap, must free)
//  WM_NET_SEND_COMPLETE    wParam=bytesSent        lParam=CycleStats* (heap, must free)
//  WM_NET_ERROR            wParam=wsaErr           lParam=wchar_t*    (heap, must free)

class ServerNetwork
{
public:
    explicit ServerNetwork(HWND notifyWnd, bool echoMode = false);
    ~ServerNetwork();

    bool  StartListening(int port, int timeoutSec = 5, bool udpMode = false);
    void  StopServer();
    bool  Send(const std::vector<BYTE>& data, const NetConfig& cfg);
    void  DisconnectClient();

    bool  IsListening()       const { return m_listening.load(); }
    bool  IsClientConnected() const { return m_clientConnected.load(); }
    SOCKET GetClientSocket()  const { return m_clientSock; }
    SOCKET GetListenSocket()  const { return m_listenSock; }

    void  SetEchoMode(bool echo)    { m_echoMode = echo; }
    bool  GetEchoMode()       const { return m_echoMode; }

    // For test/CLI: write received data to a log file
    void  SetLogFile(const std::wstring& path) { m_logFile = path; }

private:
    void  AcceptLoop(int port, int timeoutSec);
    void  RecvLoop(SOCKET clientSock);
    void  PostError(int wsaErr, const std::wstring& msg);
    void  AppendLogFile(const std::vector<BYTE>& data);

    HWND               m_notifyWnd       {nullptr};
    bool               m_echoMode        {false};
    std::wstring       m_logFile;

    SOCKET             m_listenSock      {INVALID_SOCKET};
    SOCKET             m_clientSock      {INVALID_SOCKET};

    std::atomic<bool>  m_listening       {false};
    std::atomic<bool>  m_clientConnected {false};
    std::atomic<bool>  m_stopRequested   {false};
    bool               m_udpMode         {false};
    sockaddr_in        m_udpClientAddr   {};

    std::mutex         m_sendMutex;
    CycleStats         m_stats;
    std::mutex         m_statsMutex;
};
