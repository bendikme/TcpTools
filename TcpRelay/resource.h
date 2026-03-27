#pragma once

// ============================================================
//  Resource IDs for TcpRelay
// ============================================================

// Menus & App
#define IDR_MAINMENU            101
#define IDI_APPICON             102

// Relay configuration controls
#define IDC_EDIT_LISTEN_PORT    1001
#define IDC_COMBO_TARGET        1002
#define IDC_EDIT_TARGET_PORT    1003
#define IDC_COMBO_PROTOCOL      1004
#define IDC_EDIT_TIMEOUT        1005
#define IDC_BTN_START           1006
#define IDC_BTN_STOP            1007
#define IDC_STATIC_STATUS       1008
#define IDC_CHK_NO_TIMEOUT      1009  // "No timeout" checkbox

// Client -> Target direction panel
#define IDC_CHK_AUTO_C2T        1010
#define IDC_BTN_FWD_C2T         1011
#define IDC_BTN_DROP_C2T        1012
#define IDC_STATIC_BUF_C2T     1013
#define IDC_STATIC_HEX_C2T     1014  // HexView control
#define IDC_CHK_HEX_C2T        1015  // "Hex View" toggle checkbox
#define IDC_EDIT_TEXT_C2T      1016  // Text (normal) view

// Target -> Client direction panel
#define IDC_CHK_AUTO_T2C        1020
#define IDC_BTN_FWD_T2C         1021
#define IDC_BTN_DROP_T2C        1022
#define IDC_STATIC_BUF_T2C     1023
#define IDC_STATIC_HEX_T2C     1024  // HexView control
#define IDC_CHK_HEX_T2C        1025  // "Hex View" toggle checkbox
#define IDC_EDIT_TEXT_T2C      1026  // Text (normal) view

// Log
#define IDC_LIST_LOG            1050
#define IDC_BTN_CLEARLOG        1051
#define IDC_COMBO_LOGFILTER     1052

// Tab control
#define IDC_TAB_BOTTOM          1060

// Statistics labels
#define IDC_STATIC_STAT_C2T_BYTES   1070
#define IDC_STATIC_STAT_C2T_PKTS    1071
#define IDC_STATIC_STAT_T2C_BYTES   1072
#define IDC_STATIC_STAT_T2C_PKTS    1073
#define IDC_STATIC_STAT_CLIENTS     1074
#define IDC_STATIC_STAT_TARGET      1075
#define IDC_BTN_CLEARSTATS          1076

// Diagnostics
#define IDC_STATIC_DIAG_INFO    1080
#define IDC_BTN_REFRESHDIAG     1081

// Connection indicators
#define IDC_STATIC_CLIENT_IND   1085
#define IDC_STATIC_TARGET_IND   1086
#define IDC_STATIC_FILTER_LABEL 1087

// Group boxes
#define IDC_GRP_CONFIG          1090
#define IDC_GRP_C2T             1091
#define IDC_GRP_T2C             1092
#define IDC_GRP_LOG             1093
#define IDC_GRP_STATS           1094

// Context menu for log
#define IDM_LOG_COPY            2001
#define IDM_LOG_CLEAR           2002

// Custom Window Messages (WM_APP range)
#define WM_RELAY_LISTENING          (WM_APP + 1)
#define WM_RELAY_CLIENT_CONN        (WM_APP + 2)
#define WM_RELAY_CLIENT_DISCONN     (WM_APP + 3)
#define WM_RELAY_TARGET_CONN        (WM_APP + 4)
#define WM_RELAY_TARGET_DISCONN     (WM_APP + 5)
#define WM_RELAY_CLIENT_DATA        (WM_APP + 6)
#define WM_RELAY_TARGET_DATA        (WM_APP + 7)
#define WM_RELAY_ERROR              (WM_APP + 8)
#define WM_RELAY_STOPPED            (WM_APP + 9)
#define WM_RELAY_TARGET_CONNECTING  (WM_APP + 10)  // target is trying to reconnect
#define WM_PARSE_TARGET_COMBO       (WM_APP + 20)
