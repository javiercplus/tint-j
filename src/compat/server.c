/**************************************************************************
 * compat/server.c
 *
 * Wayland-compatible server implementation for tint2.
 * Replaces the X11-based src/server.c when building for Wayland.
 *
 * Uses the backend for monitor info, desktop info, and toplevel management.
 *
 * Copyright (C) 2026 tint-j project
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat/compat.h"
#include "compat/server.h"
#include "area.h"

CompatServer server;
gboolean primary_monitor_first = FALSE;

void cleanup_server(void)
{
	g_free(server.monitors);
	server.monitors = NULL;
}

void send_event32(Window win, Atom at, long data1, long data2, long data3)
{
	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = win;
	ev.xclient.message_type = at;
	ev.xclient.format = 32;
	ev.xclient.data[0] = data1;
	ev.xclient.data[1] = data2;
	ev.xclient.data[2] = data3;
	XSendEvent(server.display, win, False, SubstructureNotifyMask | SubstructureRedirectMask, &ev);
}

int get_property32(Window win, Atom at, Atom type)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(server.display, win, at, 0, 1, False, type,
	                       &actual_type, &actual_format,
	                       &nitems, &bytes_after, &data) != 0 || !data) {
		return 0;
	}

	int result = 0;
	if (nitems > 0 && actual_format == 32) {
		result = ((long *)data)[0];
	}
	XFree(data);
	return result;
}

void *server_get_property(Window win, Atom at, Atom type, int *num_results)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *data = NULL;

	long offset = 0;
	long length = 65536;

	if (XGetWindowProperty(server.display, win, at, offset, length / 4, False, type,
	                       &actual_type, &actual_format,
	                       &nitems, &bytes_after, &data) != 0 || !data) {
		if (num_results) *num_results = 0;
		return NULL;
	}

	if (num_results) *num_results = (int)nitems;
	return data;
}

void server_init_atoms(void)
{
#define INIT_ATOM(name) server.atom.name = XInternAtom(server.display, #name, False)

	INIT_ATOM(_XROOTPMAP_ID);
	INIT_ATOM(_XROOTMAP_ID);
	INIT_ATOM(_NET_CURRENT_DESKTOP);
	INIT_ATOM(_NET_NUMBER_OF_DESKTOPS);
	INIT_ATOM(_NET_DESKTOP_NAMES);
	INIT_ATOM(_NET_DESKTOP_GEOMETRY);
	INIT_ATOM(_NET_DESKTOP_VIEWPORT);
	INIT_ATOM(_NET_WORKAREA);
	INIT_ATOM(_NET_ACTIVE_WINDOW);
	INIT_ATOM(_NET_WM_WINDOW_TYPE);
	INIT_ATOM(_NET_WM_STATE_SKIP_PAGER);
	INIT_ATOM(_NET_WM_STATE_SKIP_TASKBAR);
	INIT_ATOM(_NET_WM_STATE_STICKY);
	INIT_ATOM(_NET_WM_STATE_DEMANDS_ATTENTION);
	INIT_ATOM(_NET_WM_WINDOW_TYPE_DOCK);
	INIT_ATOM(_NET_WM_WINDOW_TYPE_DESKTOP);
	INIT_ATOM(_NET_WM_WINDOW_TYPE_TOOLBAR);
	INIT_ATOM(_NET_WM_WINDOW_TYPE_MENU);
	INIT_ATOM(_NET_WM_WINDOW_TYPE_SPLASH);
	INIT_ATOM(_NET_WM_WINDOW_TYPE_DIALOG);
	INIT_ATOM(_NET_WM_WINDOW_TYPE_NORMAL);
	INIT_ATOM(_NET_WM_DESKTOP);
	INIT_ATOM(WM_STATE);
	INIT_ATOM(_NET_WM_STATE);
	INIT_ATOM(_NET_WM_STATE_MAXIMIZED_VERT);
	INIT_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ);
	INIT_ATOM(_NET_WM_STATE_SHADED);
	INIT_ATOM(_NET_WM_STATE_HIDDEN);
	INIT_ATOM(_NET_WM_STATE_BELOW);
	INIT_ATOM(_NET_WM_STATE_ABOVE);
	INIT_ATOM(_NET_WM_STATE_MODAL);
	INIT_ATOM(_NET_CLIENT_LIST);
	INIT_ATOM(_NET_WM_NAME);
	INIT_ATOM(_NET_WM_VISIBLE_NAME);
	INIT_ATOM(_NET_WM_STRUT);
	INIT_ATOM(_NET_WM_ICON);
	INIT_ATOM(_NET_WM_ICON_GEOMETRY);
	INIT_ATOM(_NET_WM_ICON_NAME);
	INIT_ATOM(_NET_CLOSE_WINDOW);
	INIT_ATOM(UTF8_STRING);
	INIT_ATOM(_NET_SUPPORTING_WM_CHECK);
	INIT_ATOM(_NET_WM_CM_S0);
	INIT_ATOM(_NET_WM_STRUT_PARTIAL);
	INIT_ATOM(WM_NAME);
	INIT_ATOM(__SWM_VROOT);
	INIT_ATOM(_MOTIF_WM_HINTS);
	INIT_ATOM(WM_HINTS);
	INIT_ATOM(_NET_SYSTEM_TRAY_SCREEN);
	INIT_ATOM(_NET_SYSTEM_TRAY_OPCODE);
	INIT_ATOM(MANAGER);
	INIT_ATOM(_NET_SYSTEM_TRAY_MESSAGE_DATA);
	INIT_ATOM(_NET_SYSTEM_TRAY_ORIENTATION);
	INIT_ATOM(_NET_SYSTEM_TRAY_ICON_SIZE);
	INIT_ATOM(_NET_SYSTEM_TRAY_PADDING);
	INIT_ATOM(_XEMBED);
	INIT_ATOM(_XEMBED_INFO);
	INIT_ATOM(_NET_WM_PID);
	INIT_ATOM(_XSETTINGS_SCREEN);
	INIT_ATOM(_XSETTINGS_SETTINGS);
	INIT_ATOM(XdndAware);
	INIT_ATOM(XdndEnter);
	INIT_ATOM(XdndPosition);
	INIT_ATOM(XdndStatus);
	INIT_ATOM(XdndDrop);
	INIT_ATOM(XdndLeave);
	INIT_ATOM(XdndSelection);
	INIT_ATOM(XdndTypeList);
	INIT_ATOM(XdndActionCopy);
	INIT_ATOM(XdndFinished);
	INIT_ATOM(TARGETS);

	// Also intern the EWMH string atoms used for property checks
	XInternAtom(server.display, "_NET_WM_STATE_SKIP_TASKBAR", False);
	XInternAtom(server.display, "_NET_WM_STATE_HIDDEN", False);
	XInternAtom(server.display, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
	XInternAtom(server.display, "_NET_CURRENT_DESKTOP", False);
	XInternAtom(server.display, "_NET_NUMBER_OF_DESKTOPS", False);
	XInternAtom(server.display, "_NET_CLIENT_LIST", False);
	XInternAtom(server.display, "_NET_ACTIVE_WINDOW", False);
	XInternAtom(server.display, "_NET_WM_DESKTOP", False);
	XInternAtom(server.display, "_NET_WM_STATE", False);
	XInternAtom(server.display, "_NET_WM_ICON", False);
	XInternAtom(server.display, "_NET_WM_NAME", False);
	XInternAtom(server.display, "_NET_WM_VISIBLE_NAME", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE_DOCK", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE_MENU", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	XInternAtom(server.display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
	XInternAtom(server.display, "_MOTIF_WM_HINTS", False);
	XInternAtom(server.display, "_NET_CLOSE_WINDOW", False);
	XInternAtom(server.display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	XInternAtom(server.display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	XInternAtom(server.display, "_NET_WM_STATE_SHADED", False);
	XInternAtom(server.display, "_NET_WM_STATE_STICKY", False);
	XInternAtom(server.display, "_NET_WM_PID", False);
	XInternAtom(server.display, "WM_STATE", False);
	XInternAtom(server.display, "WM_NAME", False);
	XInternAtom(server.display, "WM_HINTS", False);
	XInternAtom(server.display, "XdndAware", False);
	XInternAtom(server.display, "XdndEnter", False);
	XInternAtom(server.display, "XdndPosition", False);
	XInternAtom(server.display, "XdndStatus", False);
	XInternAtom(server.display, "XdndDrop", False);
	XInternAtom(server.display, "XdndLeave", False);
	XInternAtom(server.display, "XdndSelection", False);
	XInternAtom(server.display, "XdndTypeList", False);
	XInternAtom(server.display, "XdndFinished", False);
	XInternAtom(server.display, "TARGETS", False);
}

void server_init_visual(void)
{
	server.colormap = server_display->default_colormap;
	server.colormap32 = NULL;
	server.visual = server_display->default_visual;
	server.visual32 = NULL;
	server.depth = server_display->depth;
}

void get_root_pixmap(void)
{
	// No root pixmap in Wayland; real_transparency is always true
	server.real_transparency = TRUE;
	server.root_pmap = 0;
}

void get_monitors(void)
{
	int count;
	BkMonitor **bk_mons = bk_get_monitors(&count);
	if (!bk_mons || count == 0) {
		server.num_monitors = 1;
		server.monitors = g_new0(Monitor, 1);
		server.monitors[0].x = 0;
		server.monitors[0].y = 0;
		server.monitors[0].width = 1920;
		server.monitors[0].height = 1080;
		server.monitors[0].primary = TRUE;
		return;
	}

	server.num_monitors = count;
	server.monitors = g_new0(Monitor, count);

	for (int i = 0; i < count; i++) {
		server.monitors[i].x = bk_mons[i]->x;
		server.monitors[i].y = bk_mons[i]->y;
		server.monitors[i].width = bk_mons[i]->width;
		server.monitors[i].height = bk_mons[i]->height;
		server.monitors[i].primary = bk_mons[i]->primary;
		server.monitors[i].names = g_new0(gchar *, 1);
	}

	for (int i = 0; i < count; i++) {
		bk_monitor_free(bk_mons[i]);
	}
	g_free(bk_mons);

	fprintf(stderr, "compat: %d monitor(s) detected\n", server.num_monitors);
}

void get_desktops(void)
{
	server_get_number_of_desktops();
	server.desktop = get_current_desktop();

	if (server.num_desktops <= 0)
		server.num_desktops = 1;
	if (server.desktop < 0 || server.desktop >= server.num_desktops)
		server.desktop = 0;

	fprintf(stderr, "compat: %d desktop(s), current = %d\n",
	        server.num_desktops, server.desktop);
}

void server_get_number_of_desktops(void)
{
	server.num_desktops = bk_get_desktop_count();
	if (server.num_desktops <= 0)
		server.num_desktops = 1;
}

GSList *get_desktop_names(void)
{
	GSList *list = NULL;
	int count;
	char **names = bk_get_desktop_names(&count);
	if (names) {
		for (int i = 0; i < count && names[i]; i++) {
			list = g_slist_append(list, g_strdup(names[i]));
		}
		for (int i = 0; i < count; i++) {
			g_free(names[i]);
		}
		g_free(names);
	}
	if (!list) {
		for (int i = 0; i < server.num_desktops; i++) {
			list = g_slist_append(list, g_strdup_printf("Desktop %d", i + 1));
		}
	}
	return list;
}

int get_current_desktop(void)
{
	return bk_get_current_desktop();
}

void change_desktop(int desktop)
{
	bk_set_current_desktop(desktop);
	server.desktop = desktop;
}
