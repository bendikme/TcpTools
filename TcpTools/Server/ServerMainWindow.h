#pragma once
#include "pch.h"
#include "ServerNetwork.h"
#include "HexView.h"
#include "TextView.h"
#include "Utils.h"
#include "resource.h"

namespace Server {

// ============================================================
//  Log entry
// ============================================================
enum class LogType { Listening, ClientConn, ClientDisconn, Sent, Received, Error, Stopped, Info };

struct LogEntry {
    LogType          type;
    unsigned long long seq{0};      // running number shown in the "#" column (survives trimming)
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
    // Creates the server as a child page of `parent` (the tab frame).
    static HWND  Create(HINSTANCE hInst, HWND parent,
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
    void OnNotify(NMHDR* nm);
    void OnContextMenuLog(POINT pt);

    // Network message handlers
    void OnNetEvents();                              // WM_SERVER_EVENTS: drains one slice of the hand-off queue
    void OnNetListening(int port);
    void OnNetStopped();
    void OnNetClientConnected(const NetEvent& ev);
    void OnNetClientDisconnected();
    void OnNetWaiting(const NetEvent& ev);
    void OnNetDataRun(std::vector<NetEvent*>& run);  // consecutive data chunks, shown with one update
    void OnNetSendComplete(const NetEvent& ev);
    void OnNetError(const std::wstring& msg);
    void UpdateClientLabel();

    // UI Actions
    bool StartServer(int port, int timeoutSec, bool udp, bool echo);
    void DoStartServer();
    void DoStopServer();
    void DoSend();
    void DoLoadFile();
    void DoReloadLogEntry(int idx);
    void DoSaveLastRecv();
    void DoToggleSendHex();

    // UI Helpers
    void SetServerUI(bool listening, bool clientConnected);
    void UpdateToolState();
    void SetPageTitle(const std::wstring& detail);
    void UpdateStats(const CycleStats& s);
    void ClearStats();
    void AddLogEntry(LogEntry&& e);
    void FlushLogView();
    void RebuildLogVisible();
    void TrimLog();
    LogEntry* FindLogBySeq(unsigned long long seq, int* visibleIdx);
    void AppendReceiveText(const std::vector<BYTE>& data, bool flush = false);
    void RebuildReceiveView();
    void RebuildTextView();
    void ResetRecvTextState();
    void CapRecvBuffers();
    void SetSendEditText(const std::wstring& text);
    void ShowSendBytes(const std::vector<BYTE>& bytes, const std::wstring& note);
    bool GetSendData(std::vector<BYTE>& out);
    bool ParseHexField(const std::wstring& text, const wchar_t* what, std::vector<BYTE>& out);
    bool BuildNetConfig(NetConfig& cfg);
    SeparatorMode     GetSeparatorMode();
    void              RefreshRecvView();
    void              RefreshDiagnostics();
    void              RefreshLogView();
    void              CreateTooltips();
    void              ShowStatusNote(const std::wstring& text);

    // Layout helpers
    void RelayoutToClient();
    void LayoutControls(int w, int h);      // w/h in 96-DPI units
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
    HWND    m_hComboPort      {nullptr};
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
    size_t                         m_logBytes      {0};   // payload bytes held by m_log
    unsigned long long             m_nextSeq       {1};
    std::vector<BYTE>              m_sendData;      // raw payload pinned by Load File / log reload; cleared when the user edits
    std::vector<BYTE>              m_recvData;
    std::vector<BYTE>              m_lastRecvData;  // received since the last Send / Clear
    std::vector<BYTE>              m_recvTail;      // incomplete UTF-8 sequence not yet shown in the text view
    bool                           m_recvLastCR    {false};
    bool                           m_hexSendMode   {false};
    bool                           m_hexRecvMode   {false};
    bool                           m_ignoreEditChange {false}; // suppress EN_CHANGE during programmatic edits
    HWND                           m_hTooltip         {nullptr};
    HWND                           m_hBtnHelp         {nullptr};
    bool                           m_tooltipsEnabled  {false};
    bool                           m_starting      {false};  // Start clicked, WM_NET_LISTENING not yet seen
    bool                           m_listening     {false};
    bool                           m_clientConn    {false};
    bool                           m_udpActive     {false};  // protocol of the current / last run
    float                          m_splitRatio    {0.5f};
    int                            m_logFilter     {0}; // 0=All, 1=Data, 2=Connection, 3=Errors
    size_t                         m_totalBytesSent {0};
    size_t                         m_totalBytesRecv {0};
    std::wstring                   m_peerText;
    int                            m_port          {0};
    int                            m_waiting       {0};      // TCP clients parked behind the current one

    // Receive views: the visible one is current, the hidden one is rebuilt when shown.
    bool                           m_textStale     {false};
    bool                           m_hexStale      {false};

    // Log list updates are batched per drained slice of network events.
    int                            m_logBatch      {0};
    int                            m_logNotify     {0};      // entries added since the host was told
    bool                           m_logDirty      {false};
    bool                           m_logRebuild    {false};  // the list needs a full refresh (log was trimmed)

    // Connection history
    std::vector<std::wstring>      m_portHistory;
    void    PopulatePortCombo();
};

} // namespace Server
