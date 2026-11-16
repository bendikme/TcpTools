#pragma once

// App
#define IDR_MAINMENU            101
#define IDI_APPICON             102

// Server controls
#define IDC_COMBO_PORT          1002
#define IDC_EDIT_TIMEOUT        1003
#define IDC_BTN_STARTSERVER     1004
#define IDC_BTN_STOPSERVER      1005
#define IDC_CHK_ECHO            1006
#define IDC_STATIC_CONNSTATUS   1008
#define IDC_STATIC_CLIENTINFO   1009

// Send controls
#define IDC_EDIT_SEND           1010
#define IDC_BTN_SEND            1011
#define IDC_BTN_LOADFILE        1012
#define IDC_BTN_CLEARSEND       1013
#define IDC_CHK_HEXVIEW         1014
#define IDC_COMBO_SEPARATOR     1015
#define IDC_STATIC_HEXVIEW      1016
#define IDC_EDIT_CUSTOMPRE      1017
#define IDC_EDIT_CUSTOMSUF      1018

// Receive controls
#define IDC_EDIT_RECV           1020
#define IDC_BTN_CLEARRECV       1021
#define IDC_BTN_COPYRECV        1022
#define IDC_CHK_HEXVIEW_RECV    1023
#define IDC_STATIC_HEXRECV      1024
#define IDC_BTN_SAVERECV        1025   // save last transmission to file

// Statistics
#define IDC_STATIC_STAT_CONNECT     1030
#define IDC_STATIC_STAT_SEND_START  1031
#define IDC_STATIC_STAT_SEND_END    1032
#define IDC_STATIC_STAT_RECV_START  1033
#define IDC_STATIC_STAT_RECV_END    1034
#define IDC_STATIC_STAT_BYTES_SENT  1035
#define IDC_STATIC_STAT_BYTES_RECV  1036
#define IDC_STATIC_STAT_LATENCY     1037
#define IDC_STATIC_STAT_TOTAL       1038
#define IDC_STATIC_STAT_THROUGHPUT  1039
#define IDC_BTN_CLEARSTATS          1040

// Log
#define IDC_LIST_LOG            1050
#define IDC_BTN_CLEARLOG        1051

// Group boxes
#define IDC_GRP_SERVER          1060
#define IDC_GRP_SEND            1061
#define IDC_GRP_RECEIVE         1062
#define IDC_GRP_STATS           1063
#define IDC_GRP_LOG             1064
#define IDC_GRP_DIAG            1065

// Auto-clear checkboxes
#define IDC_CHK_AUTOCLEAR_SEND  1070
#define IDC_CHK_AUTOCLEAR_RECV  1071

// Diagnostics
#define IDC_STATIC_DIAG_INFO    1080
#define IDC_BTN_REFRESHDIAG     1081

// Log filter
#define IDC_COMBO_LOGFILTER     1090

// Tab control
#define IDC_TAB_BOTTOM          1095

// Protocol
#define IDC_COMBO_PROTOCOL      1100

// Context menu
#define IDM_LOG_RELOAD          2001
#define IDM_LOG_COPY            2002
#define IDM_LOG_CLEAR           2003

// Custom Window Messages
#define WM_NET_CONNECTED        (WM_APP + 1)   // client accepted
#define WM_NET_DISCONNECTED     (WM_APP + 2)   // client disconnected
#define WM_NET_DATA_RECEIVED    (WM_APP + 3)   // data from client
#define WM_NET_ERROR            (WM_APP + 4)
#define WM_NET_SEND_COMPLETE    (WM_APP + 5)
#define WM_NET_RECV_LATENCY     (WM_APP + 8)   // latency data from recv thread
#define IDC_BTN_HELP            1110

#define WM_NET_LISTENING        (WM_APP + 6)   // server started listening
#define WM_NET_STOPPED          (WM_APP + 7)   // server stopped

// The one wake-up of the network hand-off queue (HandoffQueue<NetEvent>); the
// WM_NET_* messages above are no longer posted, their ids stay reserved.
#define WM_SERVER_EVENTS        (WM_APP + 30)
