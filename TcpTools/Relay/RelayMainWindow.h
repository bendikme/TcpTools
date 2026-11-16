#pragma once
#include "pch.h"
#include "Utils.h"
#include "RelayNetwork.h"
#include "TextView.h"
#include "HandoffQueue.h"

// ============================================================
//  RelayMainWindow  -  TCP/UDP Relay (man-in-the-middle) UI
// ============================================================

class RelayMainWindow
{
public:
    static bool RegisterClass(HINSTANCE hInst);
    // Creates the relay as a child page of `parent` (the tab frame).
    static HWND Create(HINSTANCE hInst, HWND parent);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static constexpr const wchar_t* WndClassName = L"TcpRelayMainWindow";

public:
    // ---- Log types (public for PassesFilter helper) ----
    enum class LogType {
        Info = 0,           // general info
        ClientConn,         // 1 - client connected
        ClientDisconn,      // 2 - client disconnected
        TargetConn,         // 3 - target connected
        TargetDisconn,      // 4 - target disconnected
        DataC2T,            // 5 - data Client -> Target
        DataT2C,            // 6 - data Target -> Client
        Error,              // 7
        Listening,          // 8
        Stopped             // 9
    };
    static constexpr int LOG_TYPE_COUNT = 10;

    struct LogEntry {
        int            seq       {0};
        std::wstring   time;
        LogType        type      {LogType::Info};
        std::wstring   direction;     // "C\u2192T", "T\u2192C", or "-"
        size_t         size      {0};
        std::wstring   preview;
        bool           forwarded {false};
    };

    // ---- Handlers ----
private:
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void    OnCreate(HWND hWnd);
    void    OnSize(HWND hWnd, int cx, int cy);
    void    OnCommand(HWND hWnd, int id, int code, HWND hCtl);
    LRESULT OnNotify(HWND hWnd, NMHDR* nm);
    void    OnContextMenuLog(HWND hWnd, int x, int y);

    // ---- Actions ----
    void DoStartRelay();
    void DoStopRelay();
    void DoForwardC2T();
    void DoForwardT2C();
    void DoDropC2T();
    void DoDropT2C();
    void SetRelayUI(bool listening);
    void UpdateBufferLabel(bool c2t);
    void UpdateStats();
    void RefreshDiagnostics();
    void SwitchBottomTab(int tab);
    void RefreshC2TView();
    void RefreshT2CView();
    void ShowViews(bool c2t);
    void AppendView(bool c2t, const BYTE* data, size_t len);
    bool FlushHeld(bool c2t);
    void DiscardHeld(bool c2t, const wchar_t* why);
    void OnRelayEvents();
    void FlushSlice();
    void SyncLogList();
    void UpdateClientIndicator();
    void ResetSessionUI();
    void UpdateStatusText();
    void UpdateTitle();
    void CreateTooltips();
    void PositionHelpButton();

    // ---- Network events ----
    void OnRelayListening(int port);
    void OnRelayClientConn(const std::wstring& addr, int waiting, bool fromWait);
    void OnRelayClientWaiting(const std::wstring& addr, int waiting, bool left);
    void OnRelayClientDisconn();
    void OnRelayTargetConn();
    void OnRelayTargetDisconn();
    void OnRelayTargetConnecting();
    void OnRelayData(bool c2t, const BYTE* data, int len, bool forwarded);
    void OnRelayError(int err, const std::wstring& msg);
    void OnRelayStopped();

    // ---- Log management (virtual ListView) ----
    void AddLogEntry(LogType type, const std::wstring& dir, size_t size,
                     const std::wstring& preview, bool forwarded);
    void RefreshLogView();
    void ClearLog();

    // ---- Members ----
    HWND        m_hWnd     {nullptr};
    HINSTANCE   m_hInst    {nullptr};
    // Network threads -> window: every notification, in order, drained in slices.
    // Declared before m_network so it outlives the threads.
    HandoffQueue<RelayEvent> m_queue;
    std::unique_ptr<RelayNetwork> m_network;

    // Config controls
    HWND m_hEditListenPort  {nullptr};
    HWND m_hComboTarget     {nullptr};
    HWND m_hEditTargetPort  {nullptr};
    HWND m_hComboProtocol   {nullptr};
    HWND m_hEditTimeout     {nullptr};
    HWND m_hChkNoTimeout    {nullptr};  // "No timeout" checkbox
    HWND m_hBtnStart        {nullptr};
    HWND m_hBtnStop         {nullptr};
    HWND m_hStaticStatus    {nullptr};
    HWND m_hStaticClientInd {nullptr};
    HWND m_hStaticTargetInd {nullptr};

    // C2T panel
    HWND m_hChkAutoC2T      {nullptr};
    HWND m_hBtnFwdC2T       {nullptr};
    HWND m_hBtnDropC2T      {nullptr};
    HWND m_hStaticBufC2T    {nullptr};
    HWND m_hHexC2T          {nullptr};
    HWND m_hChkHexC2T       {nullptr};  // "Hex View" toggle
    HWND m_hEditTextC2T     {nullptr};  // normal text view

    // T2C panel
    HWND m_hChkAutoT2C      {nullptr};
    HWND m_hBtnFwdT2C       {nullptr};
    HWND m_hBtnDropT2C      {nullptr};
    HWND m_hStaticBufT2C    {nullptr};
    HWND m_hHexT2C          {nullptr};
    HWND m_hChkHexT2C       {nullptr};  // "Hex View" toggle
    HWND m_hEditTextT2C     {nullptr};  // normal text view

    // Log / tab
    HWND m_hTab             {nullptr};
    HWND m_hListLog         {nullptr};
    HWND m_hComboFilter     {nullptr};
    HWND m_hBtnClearLog     {nullptr};

    // Fonts
    HFONT m_hFontMono       {nullptr};
    HFONT m_hFontUI         {nullptr};

    // Statistics labels
    HWND m_hStatC2TBytes    {nullptr};
    HWND m_hStatC2TPkts     {nullptr};
    HWND m_hStatT2CBytes    {nullptr};
    HWND m_hStatT2CPkts     {nullptr};
    HWND m_hStatClients     {nullptr};
    HWND m_hStatTarget      {nullptr};
    HWND m_hBtnClearStats   {nullptr};

    // Diagnostics
    HWND m_hStaticDiagInfo  {nullptr};
    HWND m_hBtnRefreshDiag  {nullptr};

    // Image list
    HIMAGELIST m_hImgListLog{nullptr};

    // Group boxes
    HWND m_hGrpConfig       {nullptr};
    HWND m_hGrpC2T          {nullptr};
    HWND m_hGrpT2C          {nullptr};

    // Log data (virtual ListView)
    std::vector<LogEntry> m_log;
    std::vector<int>      m_logVisible;
    int                   m_logSeq      {0};
    int                   m_filterIndex {0}; // 0=All
    int                   m_bottomTab   {0}; // 0=Log, 1=Diagnostics, 2=Statistics

    // Held packets (not forwarded yet), one entry per received chunk /
    // datagram so UDP datagram boundaries survive manual forwarding.
    std::deque<std::vector<BYTE>> m_pendC2T;
    std::deque<std::vector<BYTE>> m_pendT2C;
    size_t                m_pendBytesC2T  {0};
    size_t                m_pendBytesT2C  {0};

    // Data shown in the text / hex view of each direction: the traffic seen,
    // newest last, capped (RELAY_VIEW_CAP). Both views show the same bytes.
    std::vector<BYTE>     m_viewC2T;
    std::vector<BYTE>     m_viewT2C;
    // Received in the slice being handled, shown with one append at its end.
    std::vector<BYTE>     m_sliceC2T;
    std::vector<BYTE>     m_sliceT2C;
    // A view that was not updated because the page is hidden: rebuilt on show.
    bool                  m_viewStaleC2T  {false};
    bool                  m_viewStaleT2C  {false};
    bool                  m_bufDirtyC2T   {false};   // buffer label wanted, set per slice
    bool                  m_bufDirtyT2C   {false};
    bool                  m_inSlice       {false};
    bool                  m_stoppedInSlice{false};

    // Statistics
    size_t m_bytesC2T       {0};
    size_t m_bytesT2C       {0};
    size_t m_packetsC2T     {0};
    size_t m_packetsT2C     {0};

    // Connection info
    std::wstring m_clientAddr;
    std::wstring m_clientIndBase {L"○ Client: -"};  // indicator text without the waiting count
    int          m_waitingCount  {0};     // TCP clients parked behind the current one
    std::wstring m_targetAddr;   // stored when target connects
    bool         m_isListening    {false};
    bool         m_clientUp       {false};  // as told by the network events
    bool         m_targetUp       {false};
    std::wstring m_lastError;
    std::wstring m_proto {L"TCP"}; // protocol of the current / last relay
    std::wstring m_targetText;    // "host:port" of the running relay
    std::wstring m_listenText;

    // View mode flags for data panels
    bool         m_hexModeC2T    {false};  // false = text view, true = hex view
    bool         m_hexModeT2C    {false};
    HWND         m_hTooltip      {nullptr};
    HWND         m_hBtnHelp      {nullptr};
    bool         m_tooltipsEnabled {false};

    // Connection history
    std::vector<std::wstring> m_targetHistory;
    void    PopulateTargetCombo();
    void    OnTargetComboSelChange();
    void    DoParseComboParts();
};
