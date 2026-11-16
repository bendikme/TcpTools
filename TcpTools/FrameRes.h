#pragma once

// ============================================================
//  Frame (AppFrame) resource and command IDs.
//  Each tool keeps its own resource.h in its own folder.
// ============================================================

#define IDC_FRAME_TABS          100

// Tab menu commands (popup menus, returned by TrackPopupMenu)
#define IDM_NEW_CLIENT          40001
#define IDM_NEW_SERVER          40002
#define IDM_NEW_RELAY           40003
#define IDM_CLOSE_TAB           40004
#define IDM_HELP_TOOL           40005
#define IDM_ABOUT               40006

// Frame-internal messages
#define WM_FRAME_PAGE_TITLE     (WM_APP + 210)   // lParam = page HWND whose title changed

// Timers
#define IDT_UNREAD_LABELS       1                // repaints the tab labels whose event count changed
