#pragma once
#include "pch.h"
#include "Utils.h"

// ============================================================
//  RelayNetwork - TCP/UDP relay (man-in-the-middle)
//
//  Architecture:
//    - AcceptLoop thread: listen + accept (TCP) or recvfrom (UDP)
//    - ClientRecvLoop: receives from connected client
//    - TargetRecvLoop: receives from target server
//    - Auto-forward: when enabled, data is immediately forwarded
//    - When paused: data is posted to UI for manual forwarding
//
//  Messages posted to UI window (lParam heap-alloc'd, must free):
//    WM_RELAY_LISTENING        wParam=port
//    WM_RELAY_CLIENT_CONN      wParam=0, lParam=wchar_t* "ip:port"
//    WM_RELAY_CLIENT_DISCONN   wParam=0
//    WM_RELAY_TARGET_CONN      wParam=0
//    WM_RELAY_TARGET_DISCONN   wParam=0
//    WM_RELAY_CLIENT_DATA      wParam=len|flags, lParam=BYTE*
//    WM_RELAY_TARGET_DATA      wParam=len|flags, lParam=BYTE*
//    WM_RELAY_ERROR            wParam=err, lParam=wchar_t*
//    WM_RELAY_STOPPED          wParam=0
// ============================================================

// Flag in wParam high bit: data was auto-forwarded
constexpr WPARAM RELAY_FLAG_FORWARDED = 0x80000000;

class RelayNetwork
{
public:
    explicit RelayNetwork(HWND notifyWnd);
    ~RelayNetwork();

    bool StartRelay(int listenPort, const std::string& targetHost, int targetPort,
                    int timeoutSec, bool udpMode);
    void StopRelay();

    // Manual forwarding (called from UI when paused)
    bool ForwardToTarget(const std::vector<BYTE>& data);
    bool ForwardToClient(const std::vector<BYTE>& data);

    // Pause/resume auto-forwarding
    void SetAutoForwardC2T(bool on) { m_autoForwardC2T = on; }
    void SetAutoForwardT2C(bool on) { m_autoForwardT2C = on; }
    bool GetAutoForwardC2T() const  { return m_autoForwardC2T; }
    bool GetAutoForwardT2C() const  { return m_autoForwardT2C; }

    bool IsListening()       const { return m_listening.load(); }
    bool IsClientConnected() const { return m_clientConnected.load(); }
    bool IsTargetConnected() const { return m_targetConnected.load(); }

    bool IsUdpMode() const { return m_udpMode; }
    std::wstring GetProtocolStr() const { return m_udpMode ? L"UDP" : L"TCP"; }

    // Socket handles for diagnostics
    SOCKET GetListenSocket() const { return m_listenSock; }
    SOCKET GetClientSocket() const { return m_clientSock; }
    SOCKET GetTargetSocket() const { return m_targetSock; }

private:
    void AcceptLoop(int listenPort, std::string targetHost, int targetPort, int timeoutSec);
    void AcceptClientLoop(int listenPort, int timeoutSec);
    void TargetConnectLoop(std::string host, int port, int timeoutSec);
    bool ConnectToTarget(const std::string& host, int port, int timeoutSec);
    void ClientRecvLoop();
    void TargetRecvLoop();
    void PostError(int wsaErr, const std::wstring& msg);

    HWND               m_notifyWnd       {nullptr};
    SOCKET             m_listenSock      {INVALID_SOCKET};
    SOCKET             m_clientSock      {INVALID_SOCKET};
    SOCKET             m_targetSock      {INVALID_SOCKET};

    std::atomic<bool>  m_listening       {false};
    std::atomic<bool>  m_clientConnected {false};
    std::atomic<bool>  m_targetConnected {false};
    std::atomic<bool>  m_stopRequested   {false};

    std::atomic<bool>  m_autoForwardC2T  {true};
    std::atomic<bool>  m_autoForwardT2C  {true};

    bool               m_udpMode         {false};
    sockaddr_in        m_udpClientAddr   {};

    std::mutex         m_clientSendMutex;
    std::mutex         m_targetSendMutex;
};
