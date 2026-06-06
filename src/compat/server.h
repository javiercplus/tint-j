/**************************************************************************
 * compat/server.h
 *
 * Wayland-compatible server interface for tint2.
 * Replaces the X11-based src/server.h when building for Wayland.
 *
 * Copyright (C) 2026 tint-j project
 **************************************************************************/

#ifndef COMPAT_SERVER_H
#define COMPAT_SERVER_H

#include "compat/compat.h"

// ---------------------------------------------------------------------------
// XSettings client (stubbed for Wayland)
// Functions are no-ops, implemented in compat.c
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Monitor (backend-based)
// ---------------------------------------------------------------------------
typedef struct Monitor {
	int x;
	int y;
	int width;
	int height;
	gboolean primary;
	gchar **names;
} Monitor;

// ---------------------------------------------------------------------------
// Server state (replaces X11 Server struct)
// ---------------------------------------------------------------------------
typedef struct GlobalAtom {
	Atom _XROOTPMAP_ID;
	Atom _XROOTMAP_ID;
	Atom _NET_CURRENT_DESKTOP;
	Atom _NET_NUMBER_OF_DESKTOPS;
	Atom _NET_DESKTOP_NAMES;
	Atom _NET_DESKTOP_GEOMETRY;
	Atom _NET_DESKTOP_VIEWPORT;
	Atom _NET_WORKAREA;
	Atom _NET_ACTIVE_WINDOW;
	Atom _NET_WM_WINDOW_TYPE;
	Atom _NET_WM_STATE_SKIP_PAGER;
	Atom _NET_WM_STATE_SKIP_TASKBAR;
	Atom _NET_WM_STATE_STICKY;
	Atom _NET_WM_STATE_DEMANDS_ATTENTION;
	Atom _NET_WM_WINDOW_TYPE_DOCK;
	Atom _NET_WM_WINDOW_TYPE_DESKTOP;
	Atom _NET_WM_WINDOW_TYPE_TOOLBAR;
	Atom _NET_WM_WINDOW_TYPE_MENU;
	Atom _NET_WM_WINDOW_TYPE_SPLASH;
	Atom _NET_WM_WINDOW_TYPE_DIALOG;
	Atom _NET_WM_WINDOW_TYPE_NORMAL;
	Atom _NET_WM_DESKTOP;
	Atom WM_STATE;
	Atom _NET_WM_STATE;
	Atom _NET_WM_STATE_MAXIMIZED_VERT;
	Atom _NET_WM_STATE_MAXIMIZED_HORZ;
	Atom _NET_WM_STATE_SHADED;
	Atom _NET_WM_STATE_HIDDEN;
	Atom _NET_WM_STATE_BELOW;
	Atom _NET_WM_STATE_ABOVE;
	Atom _NET_WM_STATE_MODAL;
	Atom _NET_CLIENT_LIST;
	Atom _NET_WM_NAME;
	Atom _NET_WM_VISIBLE_NAME;
	Atom _NET_WM_STRUT;
	Atom _NET_WM_ICON;
	Atom _NET_WM_ICON_GEOMETRY;
	Atom _NET_WM_ICON_NAME;
	Atom _NET_CLOSE_WINDOW;
	Atom UTF8_STRING;
	Atom _NET_SUPPORTING_WM_CHECK;
	Atom _NET_WM_CM_S0;
	Atom _NET_WM_STRUT_PARTIAL;
	Atom WM_NAME;
	Atom __SWM_VROOT;
	Atom _MOTIF_WM_HINTS;
	Atom WM_HINTS;
	Atom _NET_SYSTEM_TRAY_SCREEN;
	Atom _NET_SYSTEM_TRAY_OPCODE;
	Atom MANAGER;
	Atom _NET_SYSTEM_TRAY_MESSAGE_DATA;
	Atom _NET_SYSTEM_TRAY_ORIENTATION;
	Atom _NET_SYSTEM_TRAY_ICON_SIZE;
	Atom _NET_SYSTEM_TRAY_PADDING;
	Atom _XEMBED;
	Atom _XEMBED_INFO;
	Atom _NET_WM_PID;
	Atom _XSETTINGS_SCREEN;
	Atom _XSETTINGS_SETTINGS;
	Atom XdndAware;
	Atom XdndEnter;
	Atom XdndPosition;
	Atom XdndStatus;
	Atom XdndDrop;
	Atom XdndLeave;
	Atom XdndSelection;
	Atom XdndTypeList;
	Atom XdndActionCopy;
	Atom XdndFinished;
	Atom TARGETS;
} GlobalAtom;

typedef struct Viewport {
	int x;
	int y;
	int width;
	int height;
} Viewport;

typedef struct CompatServer {
	Display *display;
	Window root_win;
	Window composite_manager;
	gboolean real_transparency;
	gboolean disable_transparency;
	int desktop;
	int screen;
	int depth;
	int num_desktops;
	int num_monitors;
	Viewport *viewports;
	Monitor *monitors;
	gboolean got_root_win;
	Visual *visual;
	Visual *visual32;
	Pixmap root_pmap;
	GC gc;
	Colormap colormap;
	Colormap colormap32;
	GlobalAtom atom;
} CompatServer;

extern CompatServer server;
extern gboolean primary_monitor_first;

// ---------------------------------------------------------------------------
// Server functions (reimplemented for Wayland)
// ---------------------------------------------------------------------------

void cleanup_server(void);
void send_event32(Window win, Atom at, long data1, long data2, long data3);
int get_property32(Window win, Atom at, Atom type);
void *server_get_property(Window win, Atom at, Atom type, int *num_results);
void server_init_atoms(void);
void server_init_visual(void);
void get_root_pixmap(void);
void get_monitors(void);
void get_desktops(void);
void server_get_number_of_desktops(void);
GSList *get_desktop_names(void);
int get_current_desktop(void);
void change_desktop(int desktop);

#endif // COMPAT_SERVER_H
