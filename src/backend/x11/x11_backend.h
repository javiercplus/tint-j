/**************************************************************************
 * x11_backend.h
 *
 * X11 backend internal header.
 *
 * Copyright (C) 2024 tint-j project
 **************************************************************************/

#ifndef X11_BACKEND_H
#define X11_BACKEND_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xdamage.h>
#include <cairo.h>
#include <cairo-xlib.h>

#include "backend/backend.h"

// X11-specific structures wrapping opaque types
typedef struct {
	Display *display;
	int      screen;
	int      depth;
	Window   root_win;
	Visual  *visual;
	Visual  *visual32;
	Colormap colormap;
	Colormap colormap32;
	GC       gc;
	gboolean composite_manager;
	gboolean real_transparency;
	gboolean disable_transparency;
	Pixmap   root_pmap;
	int      connection_fd;

	// Atoms cache (for systray and toplevel operations)
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
	Atom _NET_WM_ICON_NAME;
	Atom _NET_CLOSE_WINDOW;
	Atom UTF8_STRING;
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
	Atom _XSETTINGS_SCREEN;
	Atom _XSETTINGS_SETTINGS;

	// Monitors cache
	int        num_monitors;
	BkMonitor *monitors;

	// Panel windows registry
	Window *panel_windows;
	int     num_panel_windows;

#ifdef HAVE_SN
	void *sn_display;  // SnDisplay *
	GTree *pids;
#endif
} X11Display;

// Cast helpers (only used inside x11 backend)
#define X11_DISPLAY(d)  ((X11Display*)(d))
#define X11_WINDOW(w)   ((Window)(w ? *(Window*)(w) : 0))
#define X11_PIXMAP(p)   ((Pixmap)(p ? *(Pixmap*)(p) : 0))

// Actual structures
struct BkDisplay {
	X11Display x11;
};

struct BkWindow {
	Window xid;
	X11Display *x11;
};

struct BkPixmap {
	Pixmap xid;
	int width, height;
	int depth;
	X11Display *x11;
};

struct BkVisual {
	Visual *visual;
	int depth;
};

// X11-specific internal functions
void x11_init_atoms(X11Display *xd);
void *x11_get_property(X11Display *xd, Window win, Atom at, Atom type, int *num_results);
int  x11_get_property32(X11Display *xd, Window win, Atom at, Atom type);
void x11_send_event32(X11Display *xd, Window win, Atom at, long data1, long data2, long data3);
void x11_detect_monitors(X11Display *xd);
void x11_detect_root_pixmap(X11Display *xd);

#endif // X11_BACKEND_H
