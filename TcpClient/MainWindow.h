#pragma once
#include "pch.h"
#include "Network.h"
#include "HexView.h"
#include "Utils.h"
#include "resource.h"

// ============================================================
//  Log entry
// ============================================================
enum class LogType { Connected, Disconnected, Sent, Received, Error, Info };

struct LogEntry {
    LogType          type;
    std::wstring     timestamp;
    std::wstring     preview;      // short display string
    std::vector<BYTE>data;         // raw bytes (payload)
    size_t           byteCount{0};
    CycleStats       stats;        // timing (for Sent entries)
    double           latencyMs{-1};// send-to-first-recv latency (ms)
};

// ============================================================
//  MainWindow
// ============================================================
class MainWindow
{
public:
    static bool  RegisterClass(HINSTANCE hInst);
    static HWND  Create(HINSTANCE hInst, int nCmdShow,
                        const std::wstring& defaultIP   = L"127.0.0.1",
                        int                 defaultPort = 9001,
                        bool                autoConnect = false);
    static const wchar_t* ClassName() { return L"TcpClientMainWnd"; }

private:
    // ---- Window Proc -------------------------------------------
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static MainWindow* FromHwnd(HWND hWnd);

    // ---- Lifecycle ---------------------------------------------
    void OnCreate(HWND hWnd, HINSTANCE hInst,
                  const std::wstring& defaultIP, int defaultPort, bool autoConnect);
    void OnDestroy();
    void OnSize(int w, int h);
    void OnCommand(WORD id, WORD notify, HWND hCtrl);
    void OnGetMinMaxInfo(MINMAXINFO* mm);
    void OnNotify(NMHDR* nm);
    void OnContextMenuLog(POINT pt);

    // ---- Network message handlers ------------------------------
    void OnNetConnected();
    void OnNetDisconnected();
    void OnNetDataReceived(size_t len, BYTE* pData);   // takes ownership of pData
    void OnNetSendComplete(size_t bytesSent, CycleStats* pStats);
    void OnNetError(int wsaErr, std::wstring* pMsg);

    // ---- UI Actions --------------------------------------------
    void DoConnect();
    void DoDisconnect();
    void DoSend();
    void DoLoadFile();
    void DoReloadLogEntry(int idx);
    void DoSaveLastRecv();    // save last transmission to file

    // ---- UI Helpers --------------------------------------------
    void SetConnectionUI(bool connected);
    void UpdateStats(const CycleStats& s);
    void ClearStats();
    void AddLogEntry(LogEntry&& e);
    void AppendReceiveText(const std::vector<BYTE>& data);
    std::vector<BYTE> GetSendData();   // reads send textbox or hex area
    NetConfig         BuildNetConfig();
    SeparatorMode     GetSeparatorMode();
    void              RefreshSendView();
    void              RefreshRecvView();
    void              RefreshDiagnostics();
    void              RefreshLogView();
    void              CreateTooltips();
    void              PositionHelpButton();

    // ---- Layout ------------------------------------------------
    void LayoutControls(int w, int h);
    HWND MakeLabel(const wchar_t* text, int x, int y, int w, int h);
    HWND MakeEdit(int id, int x, int y, int w, int h, DWORD extra = 0);
    HWND MakeButton(int id, const wchar_t* text, int x, int y, int w, int h);
    HWND MakeCheck(int id, const wchar_t* text, int x, int y, int w, int h);
    HWND MakeGroupBox(int id, const wchar_t* text, int x, int y, int w, int h);

    // ---- Member variables --------------------------------------
    HWND    m_hWnd          {nullptr};
    HINSTANCE m_hInst       {nullptr};
    HFONT   m_hFont         {nullptr};   // UI font
    HFONT   m_hFontMono     {nullptr};   // mono font for recv

    // Connection panel
    HWND    m_hComboHost    {nullptr};
    HWND    m_hEditPort     {nullptr};
    HWND    m_hEditTimeout  {nullptr};
    HWND    m_hBtnConnect   {nullptr};
    HWND    m_hBtnDisconnect{nullptr};
    HWND    m_hChkKeepAlive {nullptr};
    HWND    m_hChkAutoDisc  {nullptr};
    HWND    m_hChkAutoConnect{nullptr}; // auto-connect before send
    HWND    m_hStaticStatus {nullptr};
    HWND    m_hComboProtocol{nullptr};  // TCP / UDP

    // Send panel
    HWND    m_hEditSend     {nullptr};
    HWND    m_hHexSend      {nullptr};   // HexView
    HWND    m_hBtnSend      {nullptr};
    HWND    m_hBtnLoadFile  {nullptr};
    HWND    m_hBtnClearSend {nullptr};
    HWND    m_hChkHexSend   {nullptr};
    HWND    m_hChkAutoClearSend {nullptr};
    HWND    m_hComboSep     {nullptr};
    HWND    m_hEditPreFix   {nullptr};   // custom prefix
    HWND    m_hEditSufFix   {nullptr};   // custom suffix
    HWND    m_hLblPre       {nullptr};
    HWND    m_hLblSuf       {nullptr};
    HWND    m_hLblHost      {nullptr};
    HWND    m_hLblPort      {nullptr};
    HWND    m_hLblTimeout   {nullptr};

    // Receive panel
    HWND    m_hEditRecv     {nullptr};
    HWND    m_hHexRecv      {nullptr};   // HexView
    HWND    m_hBtnClearRecv {nullptr};
    HWND    m_hBtnCopyRecv  {nullptr};
    HWND    m_hBtnSaveRecv  {nullptr};   // save last transmission
    HWND    m_hChkHexRecv   {nullptr};
    HWND    m_hChkAutoClearRecv {nullptr};

    // Statistics panel
    HWND    m_hStatCon      {nullptr};
    HWND    m_hStatSendStart{nullptr};
    HWND    m_hStatSendEnd  {nullptr};
    HWND    m_hStatRecvStart{nullptr};
    HWND    m_hStatRecvEnd  {nullptr};
    HWND    m_hStatBytesSent{nullptr};
    HWND    m_hStatBytesRecv{nullptr};
    HWND    m_hStatLatency  {nullptr};
    HWND    m_hStatTotal    {nullptr};
    HWND    m_hStatThrough  {nullptr};
    HWND    m_hStatTotalSent{nullptr};
    HWND    m_hStatTotalRecv{nullptr};
    HWND    m_hBtnClearStats{nullptr};

    // Cyclic send
    HWND    m_hEditCyclicInterval {nullptr};
    HWND    m_hBtnCyclicStart     {nullptr};
    HWND    m_hLblCyclic          {nullptr};
    HWND    m_hStaticCyclicCount  {nullptr};
    bool                        m_cyclicRunning {false};
    std::atomic<bool>           m_cyclicStop    {false};
    std::atomic<uint32_t>       m_cyclicCount   {0};
    std::thread                 m_cyclicThread;

    // Log
    HWND    m_hListLog      {nullptr};
    HWND    m_hBtnClearLog  {nullptr};
    HWND    m_hComboLogFilter {nullptr};

    // Diagnostics
    HWND    m_hStaticDiagInfo {nullptr};
    HWND    m_hBtnRefreshDiag {nullptr};

    // Bottom tab control (Log / Diagnostics / Statistics)
    HWND    m_hTabBottom    {nullptr};
    HIMAGELIST m_hImgListLog {nullptr};
    int     m_bottomTab     {0};  // 0=Log, 1=Diagnostics, 2=Statistics
    void    SwitchBottomTab(int tab);

    // Group boxes (drawn first so children overlap correctly)
    HWND    m_hGrpConn      {nullptr};
    HWND    m_hGrpSend      {nullptr};
    HWND    m_hGrpRecv      {nullptr};

    // State
    std::unique_ptr<TcpNetwork> m_net;
    std::vector<LogEntry>       m_log;
    std::vector<int>            m_logVisible;   // indices into m_log passing current filter
    std::vector<BYTE>           m_sendData;      // current data loaded / typed
    std::vector<BYTE>           m_recvData;      // accumulated received data (all sessions)
    std::vector<BYTE>           m_lastRecvData;  // only last transmission (reset on each Send)
    bool                        m_hexSendMode  {false};
    bool                        m_hexRecvMode  {false};
    bool                        m_ignoreEditChange {false}; // suppress EN_CHANGE during programmatic edits
    HWND                        m_hTooltip         {nullptr};
    HWND                        m_hBtnHelp         {nullptr};
    bool                        m_tooltipsEnabled  {false};
    bool                        m_connected    {false};
    bool                        m_autoConnect     {false};
    bool                        m_pendingAutoSend   {false}; // set when auto-connect is triggered before send
    bool                        m_pendingCyclicStart{false}; // set when auto-connect is triggered before cyclic
    Utils::TimePoint            m_connectTime{};
    int                         m_logFilter    {0}; // 0=All, 1=Data, 2=Connection, 3=Errors
    size_t                      m_totalBytesSent {0};
    size_t                      m_totalBytesRecv {0};

    // Splitter ratios (fraction of client width for send pane)
    float   m_splitRatio    {0.5f};

    // Connection history
    std::vector<std::wstring> m_connHistory;
    void    PopulateHostCombo();
    void    OnHostComboSelChange();
    void    DoParseComboParts();
};
