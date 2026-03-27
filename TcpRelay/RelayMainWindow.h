#pragma once
#include "pch.h"
#include "Utils.h"
#include "RelayNetwork.h"

// ============================================================
//  RelayMainWindow  -  TCP/UDP Relay (man-in-the-middle) UI
// ============================================================

class RelayMainWindow
{
public:
    static bool RegisterClass(HINSTANCE hInst);
    static HWND Create(HINSTANCE hInst, int nCmdShow);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static RelayMainWindow* s_instance;
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
    void UpdateTitle();
    void CreateTooltips();

    // ---- Network events ----
    void OnRelayListening(int port);
    void OnRelayClientConn(const std::wstring& addr);
    void OnRelayClientDisconn();
    void OnRelayTargetConn();
    void OnRelayTargetDisconn();
    void OnRelayTargetConnecting();
    void OnRelayClientData(const BYTE* data, int len, bool forwarded);
    void OnRelayTargetData(const BYTE* data, int len, bool forwarded);
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

    // Buffers (pending data when paused)
    std::vector<BYTE>     m_bufferC2T;
    std::vector<BYTE>     m_bufferT2C;
    int                   m_bufC2TPackets {0};
    int                   m_bufT2CPackets {0};

    // Statistics
    size_t m_bytesC2T       {0};
    size_t m_bytesT2C       {0};
    size_t m_packetsC2T     {0};
    size_t m_packetsT2C     {0};

    // Connection info
    std::wstring m_clientAddr;
    std::wstring m_targetAddr;   // stored when target connects
    bool         m_isListening    {false};

    // View mode flags for data panels
    bool         m_hexModeC2T    {false};  // false = text view, true = hex view
    bool         m_hexModeT2C    {false};
    HWND         m_hTooltip      {nullptr};

    // Connection history
    std::vector<std::wstring> m_targetHistory;
    void    PopulateTargetCombo();
    void    OnTargetComboSelChange();
    void    DoParseComboParts();
};
