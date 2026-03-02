#pragma once
#include "pch.h"
#include "ServerNetwork.h"
#include "HexView.h"
#include "Utils.h"
#include "resource.h"

// ============================================================
//  Log entry
// ============================================================
enum class LogType { Listening, ClientConn, ClientDisconn, Sent, Received, Error, Stopped, Info };

struct LogEntry {
    LogType          type;
    std::wstring     timestamp;
    std::wstring     preview;
    std::vector<BYTE>data;
    size_t           byteCount{0};
    CycleStats       stats;
    double           latencyMs{-1}; // send-to-first-recv latency (ms)
};

// ============================================================
//  ServerMainWindow
// ============================================================
class ServerMainWindow
{
public:
    static bool  RegisterClass(HINSTANCE hInst);
    static HWND  Create(HINSTANCE hInst, int nCmdShow,
                        int defaultPort = 9001, bool autoStart = false, bool echoMode = false,
                        const std::wstring& logFile = {});
    static const wchar_t* ClassName() { return L"TcpServerMainWnd"; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static ServerMainWindow* FromHwnd(HWND hWnd);

    // Lifecycle
    void OnCreate(HWND hWnd, HINSTANCE hInst, int defaultPort, bool autoStart, bool echoMode, const std::wstring& logFile);
    void OnDestroy();
    void OnSize(int w, int h);
    void OnCommand(WORD id, WORD notify, HWND hCtrl);
    void OnGetMinMaxInfo(MINMAXINFO* mm);
    void OnNotify(NMHDR* nm);
    void OnContextMenuLog(POINT pt);

    // Network message handlers
    void OnNetListening(int port);
    void OnNetStopped();
    void OnNetClientConnected(std::wstring* pAddrStr);
    void OnNetClientDisconnected();
    void OnNetDataReceived(size_t len, BYTE* pData);
    void OnNetSendComplete(size_t bytesSent, CycleStats* pStats);
    void OnNetError(int wsaErr, std::wstring* pMsg);

    // UI Actions
    void DoStartServer();
    void DoStopServer();
    void DoSend();
    void DoLoadFile();
    void DoReloadLogEntry(int idx);
    void DoSaveLastRecv();

    // UI Helpers
    void SetServerUI(bool listening, bool clientConnected);
    void UpdateStats(const CycleStats& s);
    void ClearStats();
    void AddLogEntry(LogEntry&& e);
    void AppendReceiveText(const std::vector<BYTE>& data);
    std::vector<BYTE> GetSendData();
    NetConfig         BuildNetConfig();
    SeparatorMode     GetSeparatorMode();
    void              RefreshSendView();
    void              RefreshRecvView();
    void              RefreshDiagnostics();
    void              RefreshLogView();

    // Layout helpers
    void LayoutControls(int w, int h);
    HWND MakeLabel(const wchar_t* text, int x, int y, int w, int h);
    HWND MakeEdit(int id, int x, int y, int w, int h, DWORD extra = 0);
    HWND MakeButton(int id, const wchar_t* text, int x, int y, int w, int h);
    HWND MakeCheck(int id, const wchar_t* text, int x, int y, int w, int h);
    HWND MakeGroupBox(int id, const wchar_t* text, int x, int y, int w, int h);

    // ---- Members ----
    HWND      m_hWnd          {nullptr};
    HINSTANCE m_hInst         {nullptr};
    HFONT     m_hFont         {nullptr};
    HFONT     m_hFontMono     {nullptr};

    // Server panel
    HWND    m_hEditPort       {nullptr};
    HWND    m_hEditTimeout    {nullptr};
    HWND    m_hBtnStart       {nullptr};
    HWND    m_hBtnStop        {nullptr};
    HWND    m_hChkEcho        {nullptr};
    HWND    m_hStaticStatus   {nullptr};
    HWND    m_hStaticClient   {nullptr};
    HWND    m_hComboProtocol  {nullptr};  // TCP / UDP
    HWND    m_hLblPort        {nullptr};
    HWND    m_hLblTimeout     {nullptr};

    // Send panel
    HWND    m_hEditSend       {nullptr};
    HWND    m_hHexSend        {nullptr};
    HWND    m_hBtnSend        {nullptr};
    HWND    m_hBtnLoadFile    {nullptr};
    HWND    m_hBtnClearSend   {nullptr};
    HWND    m_hChkHexSend     {nullptr};
    HWND    m_hChkAutoClearSend {nullptr};
    HWND    m_hComboSep       {nullptr};
    HWND    m_hEditPreFix     {nullptr};
    HWND    m_hEditSufFix     {nullptr};
    HWND    m_hLblPre         {nullptr};
    HWND    m_hLblSuf         {nullptr};

    // Receive panel
    HWND    m_hEditRecv       {nullptr};
    HWND    m_hHexRecv        {nullptr};
    HWND    m_hBtnClearRecv   {nullptr};
    HWND    m_hBtnCopyRecv    {nullptr};
    HWND    m_hBtnSaveRecv    {nullptr};
    HWND    m_hChkHexRecv     {nullptr};
    HWND    m_hChkAutoClearRecv {nullptr};

    // Statistics panel
    HWND    m_hStatCon        {nullptr};
    HWND    m_hStatSendStart  {nullptr};
    HWND    m_hStatSendEnd    {nullptr};
    HWND    m_hStatRecvStart  {nullptr};
    HWND    m_hStatRecvEnd    {nullptr};
    HWND    m_hStatBytesSent  {nullptr};
    HWND    m_hStatBytesRecv  {nullptr};
    HWND    m_hStatLatency    {nullptr};
    HWND    m_hStatTotal      {nullptr};
    HWND    m_hStatThrough    {nullptr};
    HWND    m_hStatTotalSent  {nullptr};
    HWND    m_hStatTotalRecv  {nullptr};
    HWND    m_hBtnClearStats  {nullptr};

    // Log
    HWND    m_hListLog        {nullptr};
    HWND    m_hBtnClearLog    {nullptr};
    HWND    m_hComboLogFilter {nullptr};

    // Diagnostics
    HWND    m_hStaticDiagInfo {nullptr};
    HWND    m_hBtnRefreshDiag {nullptr};

    // Bottom tab control (Log / Diagnostics / Statistics)
    HWND    m_hTabBottom      {nullptr};
    HIMAGELIST m_hImgListLog  {nullptr};
    int     m_bottomTab       {0};  // 0=Log, 1=Diagnostics, 2=Statistics
    void    SwitchBottomTab(int tab);

    // Group boxes
    HWND    m_hGrpServer      {nullptr};
    HWND    m_hGrpSend        {nullptr};
    HWND    m_hGrpRecv        {nullptr};

    // State
    std::unique_ptr<ServerNetwork> m_net;
    std::vector<LogEntry>          m_log;
    std::vector<int>               m_logVisible; // indices into m_log passing current filter
    std::vector<BYTE>              m_sendData;
    std::vector<BYTE>              m_recvData;
    std::vector<BYTE>              m_lastRecvData;  // only last transmission
    bool                           m_hexSendMode   {false};
    bool                           m_hexRecvMode   {false};
    bool                           m_listening     {false};
    bool                           m_clientConn    {false};
    float                          m_splitRatio    {0.5f};
    int                            m_logFilter     {0}; // 0=All, 1=Data, 2=Connection, 3=Errors
    size_t                         m_totalBytesSent {0};
    size_t                         m_totalBytesRecv {0};
};
