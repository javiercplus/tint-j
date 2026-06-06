/**************************************************************************
 * x11_backend.c
 *
 * X11 backend implementation for tint2.
 * Wraps Xlib calls into the abstract BackendVT interface.
 *
 * Copyright (C) 2024 tint-j project
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>
#include <X11/cursorfont.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <Imlib2.h>

#include "backend/backend.h"
#include "x11_backend.h"

// ============================================================================
// Global X11 display singleton
// ============================================================================

static X11Display *xd = NULL;

// ============================================================================
// Lifecycle
// ============================================================================

static int x11_backend_init(int *argc, char ***argv)
{
	const char *display_name = getenv("DISPLAY");
	if (!display_name) {
		fprintf(stderr, "tint2: DISPLAY not set, cannot initialize X11 backend.\n");
		return 1;
	}

	xd = calloc(1, sizeof(X11Display));
	if (!xd)
		return 1;

	xd->display = XOpenDisplay(display_name);
	if (!xd->display) {
		fprintf(stderr, "tint2: Cannot open X11 display '%s'.\n", display_name);
		free(xd);
		xd = NULL;
		return 1;
	}

	xd->screen = DefaultScreen(xd->display);
	xd->depth = DefaultDepth(xd->display, xd->screen);
	xd->root_win = RootWindow(xd->display, xd->screen);
	xd->visual = DefaultVisual(xd->display, xd->screen);
	xd->colormap = DefaultColormap(xd->display, xd->screen);

	// Try to get ARGB visual for transparency
	XVisualInfo vinfo_template;
	vinfo_template.screen = xd->screen;
	vinfo_template.depth = 32;
	vinfo_template.class = TrueColor;
	int nitems;
	XVisualInfo *vinfo = XGetVisualInfo(xd->display, VisualScreenMask | VisualDepthMask | VisualClassMask,
	                                    &vinfo_template, &nitems);
	if (vinfo) {
		xd->visual32 = vinfo->visual;
		xd->colormap32 = XCreateColormap(xd->display, xd->root_win, xd->visual32, AllocNone);
		XFree(vinfo);
	}

	// GC for fake transparency
	XGCValues gcv;
	xd->gc = XCreateGC(xd->display, xd->root_win, 0, &gcv);

	// Check for compositor
	xd->composite_manager = (XGetSelectionOwner(xd->display,
		XInternAtom(xd->display, "_NET_WM_CM_S0", False)) != None);
	xd->real_transparency = xd->composite_manager;

	// Get connection fd for polling
	xd->connection_fd = ConnectionNumber(xd->display);

	// Initialize atoms
	x11_init_atoms(xd);

	// Detect monitors
		x11_detect_monitors(xd);

		// Detect root pixmap for fake transparency
		x11_detect_root_pixmap(xd);

	// Ignore X errors
	XSetErrorHandler(NULL);

	return 0;
}

static void x11_backend_cleanup(void)
{
	if (!xd)
		return;

	if (xd->panel_windows)
		free(xd->panel_windows);
	if (xd->monitors) {
		for (int i = 0; i < xd->num_monitors; ++i) {
			g_strfreev(xd->monitors[i].names);
			g_free(xd->monitors[i].model);
			g_free(xd->monitors[i].manufacturer);
		}
		free(xd->monitors);
	}

	if (xd->colormap32)
		XFreeColormap(xd->display, xd->colormap32);
	if (xd->gc)
		XFreeGC(xd->display, xd->gc);

	XCloseDisplay(xd->display);
	free(xd);
	xd = NULL;
}

static BackendType x11_backend_type(void)
	{ return BACKEND_X11; }

static BkDisplay* x11_get_display(void)
	{ return (BkDisplay*)xd; }

// ============================================================================
// Event loop
// ============================================================================

// Forward declaration
static BkEvent* x11_translate_event(XEvent *xev);
static BkWindow* x11_window_find_or_create(Window xid);

static int x11_get_fd(void)
{
	return xd ? xd->connection_fd : -1;
}

static void x11_flush(void)
{
	if (xd)
		XFlush(xd->display);
}

static gboolean x11_events_pending(void)
{
	if (!xd)
		return FALSE;
	return (XPending(xd->display) > 0);
}

static BkEvent* x11_wait_event(void)
{
	if (!xd)
		return NULL;

	XEvent xev;
	XNextEvent(xd->display, &xev);
	return x11_translate_event(&xev);
}

static BkEvent* x11_poll_event(void)
{
	if (!xd || !XPending(xd->display))
		return NULL;

	XEvent xev;
	XNextEvent(xd->display, &xev);
	return x11_translate_event(&xev);
}

static void x11_event_free(BkEvent *event)
{
	if (!event)
		return;
	g_free(event->toplevel);
	g_free(event->systray_message_data);
	g_free(event);
}

static int x11_run(void)
{
	// tint2 runs its own event loop, not this
	return 0;
}

// ============================================================================
// Event translation: XEvent -> BkEvent
// ============================================================================

BkEvent* x11_translate_event(XEvent *xev)
{
	BkEvent *ev = calloc(1, sizeof(BkEvent));

	switch (xev->type) {
	case Expose:
		ev->type = BK_EVENT_EXPOSE;
		break;
	case MotionNotify:
		ev->type = BK_EVENT_MOUSE_MOTION;
		ev->mouse_x = xev->xmotion.x;
		ev->mouse_y = xev->xmotion.y;
		break;
	case ButtonPress:
		ev->type = BK_EVENT_MOUSE_PRESS;
		ev->mouse_x = xev->xbutton.x;
		ev->mouse_y = xev->xbutton.y;
		ev->mouse_button = xev->xbutton.button;
		ev->mouse_modifiers = xev->xbutton.state;
		break;
	case ButtonRelease:
		ev->type = BK_EVENT_MOUSE_RELEASE;
		ev->mouse_x = xev->xbutton.x;
		ev->mouse_y = xev->xbutton.y;
		ev->mouse_button = xev->xbutton.button;
		ev->mouse_modifiers = xev->xbutton.state;
		break;
	case EnterNotify:
		ev->type = BK_EVENT_ENTER;
		break;
	case LeaveNotify:
		ev->type = BK_EVENT_LEAVE;
		break;
	case ConfigureNotify:
		ev->type = BK_EVENT_CONFIGURE;
		ev->configure_x = xev->xconfigure.x;
		ev->configure_y = xev->xconfigure.y;
		ev->configure_width = xev->xconfigure.width;
		ev->configure_height = xev->xconfigure.height;
		break;
	case DestroyNotify:
		ev->type = BK_EVENT_DESTROY;
		break;
	case ClientMessage:
		ev->type = BK_EVENT_CLIENT_MESSAGE;
		memcpy(ev->systray_message_l, xev->xclient.data.l, sizeof(long) * 5);
		break;
	case PropertyNotify:
		ev->type = BK_EVENT_PROPERTY_NOTIFY;
		break;
	default:
		// Unknown event, mark as NONE
		ev->type = BK_EVENT_NONE;
		break;
	}

	// Set window
	if (xev->xany.window) {
		ev->window = x11_window_find_or_create(xev->xany.window);
	}

	return ev;
}

// ============================================================================
// Window management
// ============================================================================

static BkWindow* x11_window_create(BkWindow *parent,
                                    int x, int y, int width, int height,
                                    int depth, BkVisual *visual,
                                    BkLayer layer,
                                    BkAnchor anchor,
                                    int exclusive_zone)
{
	if (!xd)
		return NULL;

	Window parent_xid = parent ? parent->xid : xd->root_win;
	Visual *vis = visual ? visual->visual : (depth == 32 ? xd->visual32 : xd->visual);
	Colormap cmap = (vis == xd->visual32 && xd->colormap32) ? xd->colormap32 : xd->colormap;
	int actual_depth = depth ? depth : xd->depth;

	XSetWindowAttributes attrs;
	unsigned long valuemask = 0;

	attrs.background_pixel = 0;
	attrs.border_pixel = 0;
	attrs.colormap = cmap;
	valuemask |= CWBorderPixel | CWColormap;

	if (actual_depth == 32) {
		attrs.background_pixmap = None;
		valuemask |= CWBackPixmap;
	}

	Window xid = XCreateWindow(xd->display, parent_xid,
	                           x, y, width, height, 0,
	                           actual_depth, InputOutput, vis,
	                           valuemask, &attrs);

	// Set WM properties
	Atom window_type = XInternAtom(xd->display, "_NET_WM_WINDOW_TYPE", False);
	Atom window_type_dock = XInternAtom(xd->display, "_NET_WM_WINDOW_TYPE_DOCK", False);
	XChangeProperty(xd->display, xid, window_type, XA_ATOM, 32,
	                PropModeReplace, (unsigned char*)&window_type_dock, 1);

	// Set layer using strut if needed
	if (layer == BK_LAYER_TOP) {
		Atom state_above = XInternAtom(xd->display, "_NET_WM_STATE_ABOVE", False);
		Atom state = XInternAtom(xd->display, "_NET_WM_STATE", False);
		XChangeProperty(xd->display, xid, state, XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)&state_above, 1);
	} else if (layer == BK_LAYER_BOTTOM) {
		Atom state_below = XInternAtom(xd->display, "_NET_WM_STATE_BELOW", False);
		Atom state = XInternAtom(xd->display, "_NET_WM_STATE", False);
		XChangeProperty(xd->display, xid, state, XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)&state_below, 1);
	}

	// Set WM_CLASS
	XClassHint class_hint;
	class_hint.res_name = "tint2";
	class_hint.res_class = "Tint2";
	XSetClassHint(xd->display, xid, &class_hint);

	// Select events
	XSelectInput(xd->display, xid,
	             ExposureMask | ButtonPressMask | ButtonReleaseMask |
	             PointerMotionMask | EnterWindowMask | LeaveWindowMask |
	             StructureNotifyMask | PropertyChangeMask);

	BkWindow *win = calloc(1, sizeof(BkWindow));
	win->xid = xid;
	win->x11 = xd;

	return win;
}

static void x11_window_destroy(BkWindow *win)
{
	if (!win || !xd)
		return;
	XDestroyWindow(xd->display, win->xid);
	free(win);
}

static void x11_window_show(BkWindow *win)
{
	if (!win || !xd)
		return;
	XMapWindow(xd->display, win->xid);
}

static void x11_window_hide(BkWindow *win)
{
	if (!win || !xd)
		return;
	XUnmapWindow(xd->display, win->xid);
}

static void x11_window_move(BkWindow *win, int x, int y)
{
	if (!win || !xd)
		return;
	XMoveWindow(xd->display, win->xid, x, y);
}

static void x11_window_resize(BkWindow *win, int width, int height)
{
	if (!win || !xd)
		return;
	XResizeWindow(xd->display, win->xid, width, height);
}

static void x11_window_move_resize(BkWindow *win, int x, int y, int width, int height)
{
	if (!win || !xd)
		return;
	XMoveResizeWindow(xd->display, win->xid, x, y, width, height);
}

static void x11_window_set_title(BkWindow *win, const char *title)
{
	if (!win || !xd || !title)
		return;
	XStoreName(xd->display, win->xid, title);

	// Also set _NET_WM_NAME
	Atom net_wm_name = XInternAtom(xd->display, "_NET_WM_NAME", False);
	Atom utf8_string = XInternAtom(xd->display, "UTF8_STRING", False);
	XChangeProperty(xd->display, win->xid, net_wm_name, utf8_string, 8,
	                PropModeReplace, (unsigned char*)title, strlen(title));
}

static void x11_window_set_input_region(BkWindow *win, cairo_region_t *region)
{
	if (!win || !xd)
		return;

	if (!region) {
		// Accept all input
		XShapeCombineMask(xd->display, win->xid, ShapeInput, 0, 0, None, ShapeSet);
	} else {
		// Convert cairo_region to X rectangles
		int n_rects = cairo_region_num_rectangles(region);
		XRectangle *xrects = calloc(n_rects, sizeof(XRectangle));
		for (int i = 0; i < n_rects; i++) {
			cairo_rectangle_int_t rect;
			cairo_region_get_rectangle(region, i, &rect);
			xrects[i].x = rect.x;
			xrects[i].y = rect.y;
			xrects[i].width = rect.width;
			xrects[i].height = rect.height;
		}
		XShapeCombineRectangles(xd->display, win->xid, ShapeInput, 0, 0,
		                        xrects, n_rects, ShapeSet, YXBanded);
		free(xrects);
	}
}

static void x11_window_set_opacity(BkWindow *win, double opacity)
{
	if (!win || !xd)
		return;

	unsigned long opac = (unsigned long)(opacity * 0xFFFFFFFF);
	Atom opacity_atom = XInternAtom(xd->display, "_NET_WM_WINDOW_OPACITY", False);
	XChangeProperty(xd->display, win->xid, opacity_atom, XA_CARDINAL, 32,
	                PropModeReplace, (unsigned char*)&opac, 1);
}

static unsigned long x11_window_get_id(BkWindow *win)
{
	return win ? (unsigned long)win->xid : 0;
}

// ============================================================================
// Pixmap management
// ============================================================================

static BkPixmap* x11_pixmap_create(int width, int height, int depth)
{
	if (!xd)
		return NULL;

	int d = depth ? depth : xd->depth;

	BkPixmap *pix = calloc(1, sizeof(BkPixmap));
	pix->xid = XCreatePixmap(xd->display, xd->root_win, width, height, d);
	pix->width = width;
	pix->height = height;
	pix->depth = d;
	pix->x11 = xd;

	return pix;
}

static void x11_pixmap_destroy(BkPixmap *pixmap)
{
	if (!pixmap || !xd)
		return;
	XFreePixmap(xd->display, pixmap->xid);
	free(pixmap);
}

static void x11_pixmap_get_size(BkPixmap *pixmap, int *width, int *height)
{
	if (pixmap) {
		*width = pixmap->width;
		*height = pixmap->height;
	} else {
		*width = 0;
		*height = 0;
	}
}

// ============================================================================
// Cairo integration
// ============================================================================

static cairo_surface_t* x11_cairo_surface_create_for_pixmap(BkPixmap *pixmap,
                                                             int width, int height)
{
	if (!pixmap || !xd)
		return NULL;

	Visual *vis = (pixmap->depth == 32 && xd->visual32) ? xd->visual32 : xd->visual;

	return cairo_xlib_surface_create(xd->display, pixmap->xid, vis,
	                                 width, height);
}

static cairo_surface_t* x11_cairo_surface_create_for_window(BkWindow *win,
                                                              int width, int height)
{
	if (!win || !xd)
		return NULL;

	return cairo_xlib_surface_create(xd->display, win->xid,
	                                 xd->visual, width, height);
}

// ============================================================================
// Rendering
// ============================================================================

static void x11_pixmap_copy_area(BkPixmap *src, BkPixmap *dst,
                                  int src_x, int src_y,
                                  int w, int h,
                                  int dst_x, int dst_y)
{
	if (!src || !dst || !xd)
		return;

	XCopyArea(xd->display, src->xid, dst->xid, xd->gc,
	          src_x, src_y, w, h, dst_x, dst_y);
}

static void x11_window_present(BkWindow *win)
{
	if (!win || !xd)
		return;
	XFlush(xd->display);
}

// ============================================================================
// Visual management
// ============================================================================

static BkVisual* x11_get_default_visual(int depth)
{
	if (!xd)
		return NULL;

	BkVisual *vis = calloc(1, sizeof(BkVisual));
	vis->depth = depth ? depth : xd->depth;
	vis->visual = (depth == 32 && xd->visual32) ? xd->visual32 : xd->visual;
	return vis;
}

static void x11_visual_free(BkVisual *visual)
{
	// X11 visuals are owned by the display, just free the wrapper
	free(visual);
}

// ============================================================================
// Monitor management
// ============================================================================

static int x11_get_monitor_count(void)
{
	return xd ? xd->num_monitors : 0;
}

static BkMonitor* x11_get_monitor(int index)
{
	if (!xd || index < 0 || index >= xd->num_monitors)
		return NULL;

	BkMonitor *m = calloc(1, sizeof(BkMonitor));
	memcpy(m, &xd->monitors[index], sizeof(BkMonitor));
	m->names = g_strdupv(xd->monitors[index].names);
	m->model = g_strdup(xd->monitors[index].model);
	m->manufacturer = g_strdup(xd->monitors[index].manufacturer);
	return m;
}

static void x11_monitor_free(BkMonitor *monitor)
{
	if (!monitor)
		return;
	g_strfreev(monitor->names);
	g_free(monitor->model);
	g_free(monitor->manufacturer);
	free(monitor);
}

static BkMonitor** x11_get_monitors(int *count)
{
	if (!xd) {
		*count = 0;
		return NULL;
	}

	*count = xd->num_monitors;
	BkMonitor **list = calloc(xd->num_monitors + 1, sizeof(BkMonitor*));
	for (int i = 0; i < xd->num_monitors; i++) {
		list[i] = x11_get_monitor(i);
	}
	list[xd->num_monitors] = NULL;
	return list;
}

static void x11_monitor_free_list(BkMonitor **monitors, int count)
{
	if (!monitors)
		return;
	for (int i = 0; i < count; i++)
		x11_monitor_free(monitors[i]);
	free(monitors);
}

// ============================================================================
// Desktop management
// ============================================================================

static int x11_get_desktop_count(void)
{
	return xd ? x11_get_property32(xd, xd->root_win, xd->_NET_NUMBER_OF_DESKTOPS, XA_CARDINAL) : 0;
}

static int x11_get_current_desktop(void)
{
	return xd ? x11_get_property32(xd, xd->root_win, xd->_NET_CURRENT_DESKTOP, XA_CARDINAL) : 0;
}

static void x11_set_current_desktop(int desktop)
{
	if (!xd)
		return;
	x11_send_event32(xd, xd->root_win, xd->_NET_CURRENT_DESKTOP, desktop, 2, 0);
}

static char** x11_get_desktop_names(int *count)
{
	if (!xd) {
		*count = 0;
		return NULL;
	}

	int num;
	unsigned char *prop = x11_get_property(xd, xd->root_win, xd->_NET_DESKTOP_NAMES,
	                                        XInternAtom(xd->display, "UTF8_STRING", False), &num);
	if (!prop) {
		*count = 0;
		return NULL;
	}

	// Parse the property: it's a sequence of null-terminated strings
	// We don't know num_desktops for sure, so we scan
	GArray *names = g_array_new(TRUE, FALSE, sizeof(char*));
	const unsigned char *p = prop;
	const unsigned char *end = prop + num;
	while (p < end) {
		int len = strlen((const char*)p);
		if (len == 0) break;
		char *name = strdup((const char*)p);
		g_array_append_val(names, name);
		p += len + 1;
	}

	XFree(prop);

	*count = names->len;
	char **result = (char**)names->data;
	g_array_free(names, FALSE);
	return result;
}

// ============================================================================
// TopLevel management
// ============================================================================

static BkTopLevel** x11_get_toplevels(int *count)
{
	if (!xd) {
		*count = 0;
		return NULL;
	}

	int num;
	Window *wins = x11_get_property(xd, xd->root_win, xd->_NET_CLIENT_LIST, XA_WINDOW, &num);
	if (!wins) {
		*count = 0;
		return NULL;
	}

	BkTopLevel **list = calloc(num + 1, sizeof(BkTopLevel*));
	int actual_count = 0;

	for (int i = 0; i < num; i++) {
		BkTopLevel *tl = calloc(1, sizeof(BkTopLevel));

		// Create window wrapper
		tl->window = calloc(1, sizeof(BkWindow));
		tl->window->xid = wins[i];
		tl->window->x11 = xd;

		// Get title
		int n;
		char *title = x11_get_property(xd, wins[i], xd->_NET_WM_NAME, xd->UTF8_STRING, &n);
		if (!title)
			title = x11_get_property(xd, wins[i], xd->WM_NAME, XA_STRING, &n);
		tl->title = title ? strdup(title) : strdup("");
		if (title) XFree(title);

		// Get desktop
		tl->desktop = x11_get_property32(xd, wins[i], xd->_NET_WM_DESKTOP, XA_CARDINAL);

		// Get icon
		gulong *icon_data = x11_get_property(xd, wins[i], xd->_NET_WM_ICON, XA_CARDINAL, &n);
		if (icon_data) {
			tl->icon_data = malloc(n * sizeof(gulong));
			memcpy(tl->icon_data, icon_data, n * sizeof(gulong));
			tl->icon_data_len = n;
			XFree(icon_data);
		}

		// Get PID
		int pid_n;
		unsigned long *pid_data = x11_get_property(xd, wins[i], xd->_NET_WM_PID, XA_CARDINAL, &pid_n);
		if (pid_data) {
			if (pid_n > 0)
				// PID is stored in pid_data, but we need to access it
				// Actually _NET_WM_PID returns a CARDINAL, so pid_data is gulong array
				;
			XFree(pid_data);
		}

		// Get geometry
		int wx, wy, ww;
		Window child;
		XWindowAttributes wa;
		if (XGetWindowAttributes(xd->display, wins[i], &wa)) {
			XTranslateCoordinates(xd->display, wins[i], xd->root_win,
			                      -wa.border_width, -wa.border_width, &wx, &wy, &child);
			tl->x = wx;
			tl->y = wy;
			tl->width = wa.width + 2 * wa.border_width;
			tl->height = wa.height + 2 * wa.border_width;
		}

		// Check states
		int state_n;
		Atom *atoms = x11_get_property(xd, wins[i], xd->_NET_WM_STATE, XA_ATOM, &state_n);
		if (atoms) {
			for (int j = 0; j < state_n; j++) {
				if (atoms[j] == xd->_NET_WM_STATE_HIDDEN)
					tl->state = BK_TOPLEVEL_STATE_ICONIFIED;
				if (atoms[j] == xd->_NET_WM_STATE_SKIP_TASKBAR)
					tl->skip_taskbar = TRUE;
				if (atoms[j] == xd->_NET_WM_STATE_DEMANDS_ATTENTION)
					tl->state = BK_TOPLEVEL_STATE_URGENT;
			}
			XFree(atoms);
		}

		// Check window type
		int type_n;
		Atom *types = x11_get_property(xd, wins[i], xd->_NET_WM_WINDOW_TYPE, XA_ATOM, &type_n);
		if (types) {
			for (int j = 0; j < type_n; j++) {
				if (types[j] == xd->_NET_WM_WINDOW_TYPE_DOCK ||
				    types[j] == xd->_NET_WM_WINDOW_TYPE_DESKTOP ||
				    types[j] == xd->_NET_WM_WINDOW_TYPE_TOOLBAR ||
				    types[j] == xd->_NET_WM_WINDOW_TYPE_MENU ||
				    types[j] == xd->_NET_WM_WINDOW_TYPE_SPLASH) {
					tl->skip_taskbar = TRUE;
				}
			}
			XFree(types);
		}

		// Check if active
		Window active = x11_get_property32(xd, xd->root_win, xd->_NET_ACTIVE_WINDOW, XA_WINDOW);
		tl->is_active = (active == wins[i]);

		list[actual_count++] = tl;
	}

	XFree(wins);
	*count = actual_count;
	list[actual_count] = NULL;

	return list;
}

static void x11_toplevel_free(BkTopLevel *tl)
{
	if (!tl)
		return;
	g_free(tl->title);
	g_free(tl->app_id);
	g_free(tl->icon_data);
	g_free(tl->window);
	g_free(tl);
}

static void x11_toplevel_free_list(BkTopLevel **tls, int count)
{
	if (!tls)
		return;
	for (int i = 0; i < count; i++)
		x11_toplevel_free(tls[i]);
	free(tls);
}

static void x11_toplevel_activate(BkTopLevel *tl)
{
	if (!tl || !xd)
		return;
	x11_send_event32(xd, tl->window->xid, xd->_NET_ACTIVE_WINDOW, 2, CurrentTime, 0);
}

static void x11_toplevel_close(BkTopLevel *tl)
{
	if (!tl || !xd)
		return;
	x11_send_event32(xd, tl->window->xid, xd->_NET_CLOSE_WINDOW, 0, 2, 0);
}

static void x11_toplevel_minimize(BkTopLevel *tl)
{
	if (!tl || !xd)
		return;
	XIconifyWindow(xd->display, tl->window->xid, xd->screen);
}

static void x11_toplevel_toggle_maximize(BkTopLevel *tl)
{
	if (!tl || !xd)
		return;
	x11_send_event32(xd, tl->window->xid, xd->_NET_WM_STATE, 2,
	                 xd->_NET_WM_STATE_MAXIMIZED_VERT, 0);
	x11_send_event32(xd, tl->window->xid, xd->_NET_WM_STATE, 2,
	                 xd->_NET_WM_STATE_MAXIMIZED_HORZ, 0);
}

static void x11_toplevel_toggle_shade(BkTopLevel *tl)
{
	if (!tl || !xd)
		return;
	x11_send_event32(xd, tl->window->xid, xd->_NET_WM_STATE, 2,
	                 xd->_NET_WM_STATE_SHADED, 0);
}

static void x11_toplevel_set_desktop(BkTopLevel *tl, int desktop)
{
	if (!tl || !xd)
		return;
	x11_send_event32(xd, tl->window->xid, xd->_NET_WM_DESKTOP, desktop, 2, 0);
}

static BkTopLevel* x11_get_active_toplevel(void)
{
	if (!xd)
		return NULL;

	Window active = x11_get_property32(xd, xd->root_win, xd->_NET_ACTIVE_WINDOW, XA_WINDOW);
	if (!active)
		return NULL;

	// Create minimal toplevel with just the active window
	BkTopLevel *tl = calloc(1, sizeof(BkTopLevel));
	tl->window = calloc(1, sizeof(BkWindow));
	tl->window->xid = active;
	tl->window->x11 = xd;
	tl->is_active = TRUE;
	return tl;
}

static Imlib_Image x11_toplevel_get_icon(BkTopLevel *tl, int size)
{
	// tint2 already handles icon loading via imlib2; this is a stub
	(void)size;
	return NULL;
}

static void x11_toplevel_get_geometry(BkTopLevel *tl, int *x, int *y, int *w, int *h)
{
	if (!tl || !xd) {
		*x = *y = *w = *h = 0;
		return;
	}
	*x = tl->x;
	*y = tl->y;
	*w = tl->width;
	*h = tl->height;
}

static int x11_toplevel_get_pid(BkTopLevel *tl)
{
	if (!tl || !xd)
		return 0;
	return x11_get_property32(xd, tl->window->xid, xd->_NET_WM_PID, XA_CARDINAL);
}

static gboolean x11_toplevel_is_iconified(BkTopLevel *tl)
{
	if (!tl)
		return FALSE;
	return tl->state == BK_TOPLEVEL_STATE_ICONIFIED;
}

static gboolean x11_toplevel_is_urgent(BkTopLevel *tl)
{
	if (!tl)
		return FALSE;
	return tl->state == BK_TOPLEVEL_STATE_URGENT;
}

static gboolean x11_toplevel_is_hidden(BkTopLevel *tl)
{
	if (!tl)
		return TRUE;
	return tl->skip_taskbar;
}

static gboolean x11_toplevel_is_active(BkTopLevel *tl)
{
	if (!tl)
		return FALSE;
	return tl->is_active;
}

// ============================================================================
// System Tray (stubs - actual implementation will connect to existing code)
// ============================================================================

static int x11_systray_init(int monitor)
{
	(void)monitor;
	// The actual systray implementation is in systray/systraybar.c
	// This is a compatibility wrapper
	return 0;
}

static void x11_systray_cleanup(void)
{
}

static gboolean x11_systray_is_icon(BkWindow *win)
{
	(void)win;
	return FALSE;
}

static BkSystrayIcon* x11_systray_find_icon(BkWindow *win) { (void)win; return NULL; }
static GSList* x11_systray_get_icons(void) { return NULL; }
static int x11_systray_embed_icon(BkSystrayIcon *icon, BkWindow *panel)
	{ (void)icon; (void)panel; return 0; }
static void x11_systray_remove_icon(BkSystrayIcon *icon) { (void)icon; }
static void x11_systray_send_message(BkWindow *icon, long msg, long d1, long d2, long d3)
	{ (void)icon; (void)msg; (void)d1; (void)d2; (void)d3; }
static Imlib_Image x11_systray_get_icon_image(BkSystrayIcon *icon)
	{ (void)icon; return NULL; }
static void x11_systray_icon_free(BkSystrayIcon *icon) { (void)icon; }

// ============================================================================
// Transparency
// ============================================================================

static gboolean x11_has_real_transparency(void)
{
	return xd ? xd->real_transparency : FALSE;
}

static BkPixmap* x11_get_root_pixmap(void)
{
	if (!xd || xd->root_pmap == None)
		return NULL;

	BkPixmap *pix = calloc(1, sizeof(BkPixmap));
	pix->xid = xd->root_pmap;
	pix->x11 = xd;
	return pix;
}

// ============================================================================
// Cursor
// ============================================================================

static void x11_window_set_cursor(BkWindow *win, const char *cursor_name)
{
	if (!win || !xd)
		return;

	Cursor cursor = XCreateFontCursor(xd->display, XC_left_ptr);
	// To do: map cursor names to X cursor fonts
	// For now, always use left_ptr
	(void)cursor_name;

	XDefineCursor(xd->display, win->xid, cursor);
	XFreeCursor(xd->display, cursor);
}

static void x11_get_cursor_position(int *x, int *y)
{
	if (!xd) {
		*x = *y = 0;
		return;
	}

	Window root, child;
	int root_x, root_y, win_x, win_y;
	unsigned int mask;
	XQueryPointer(xd->display, xd->root_win, &root, &child,
	              &root_x, &root_y, &win_x, &win_y, &mask);
	*x = root_x;
	*y = root_y;
}

// ============================================================================
// DnD (stub)
// ============================================================================

static void x11_dnd_start(BkWindow *source, const char *data)
{
	(void)source;
	(void)data;
}

// ============================================================================
// Miscellaneous
// ============================================================================

static int x11_get_dpi(void)
{
	if (!xd)
		return 96;

	int width_px = DisplayWidth(xd->display, xd->screen);
	int width_mm = DisplayWidthMM(xd->display, xd->screen);
	if (width_mm > 0)
		return (int)((double)width_px * 25.4 / width_mm);
	return 96;
}

static unsigned long x11_get_root_id(void)
{
	return xd ? (unsigned long)xd->root_win : 0;
}

static gboolean x11_is_panel_window(BkWindow *win)
{
	if (!win || !xd)
		return FALSE;

	for (int i = 0; i < xd->num_panel_windows; i++) {
		if (xd->panel_windows[i] == win->xid)
			return TRUE;
	}
	return FALSE;
}

// ============================================================================
// Internal helper: find or create BkWindow from Window xid
// ============================================================================

static BkWindow* x11_window_find_or_create(Window xid)
{
	BkWindow *win = calloc(1, sizeof(BkWindow));
	win->xid = xid;
	win->x11 = xd;
	return win;
}

// ============================================================================
// Atom initialization
// ============================================================================

void x11_init_atoms(X11Display *x)
{
	x->_NET_CURRENT_DESKTOP = XInternAtom(x->display, "_NET_CURRENT_DESKTOP", False);
	x->_NET_NUMBER_OF_DESKTOPS = XInternAtom(x->display, "_NET_NUMBER_OF_DESKTOPS", False);
	x->_NET_DESKTOP_NAMES = XInternAtom(x->display, "_NET_DESKTOP_NAMES", False);
	x->_NET_DESKTOP_GEOMETRY = XInternAtom(x->display, "_NET_DESKTOP_GEOMETRY", False);
	x->_NET_DESKTOP_VIEWPORT = XInternAtom(x->display, "_NET_DESKTOP_VIEWPORT", False);
	x->_NET_WORKAREA = XInternAtom(x->display, "_NET_WORKAREA", False);
	x->_NET_ACTIVE_WINDOW = XInternAtom(x->display, "_NET_ACTIVE_WINDOW", False);
	x->_NET_WM_WINDOW_TYPE = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE", False);
	x->_NET_WM_STATE_SKIP_PAGER = XInternAtom(x->display, "_NET_WM_STATE_SKIP_PAGER", False);
	x->_NET_WM_STATE_SKIP_TASKBAR = XInternAtom(x->display, "_NET_WM_STATE_SKIP_TASKBAR", False);
	x->_NET_WM_STATE_DEMANDS_ATTENTION = XInternAtom(x->display, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
	x->_NET_WM_WINDOW_TYPE_DOCK = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE_DOCK", False);
	x->_NET_WM_WINDOW_TYPE_DESKTOP = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	x->_NET_WM_WINDOW_TYPE_TOOLBAR = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
	x->_NET_WM_WINDOW_TYPE_MENU = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE_MENU", False);
	x->_NET_WM_WINDOW_TYPE_SPLASH = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
	x->_NET_WM_WINDOW_TYPE_DIALOG = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	x->_NET_WM_WINDOW_TYPE_NORMAL = XInternAtom(x->display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
	x->_NET_WM_DESKTOP = XInternAtom(x->display, "_NET_WM_DESKTOP", False);
	x->WM_STATE = XInternAtom(x->display, "WM_STATE", False);
	x->_NET_WM_STATE = XInternAtom(x->display, "_NET_WM_STATE", False);
	x->_NET_WM_STATE_MAXIMIZED_VERT = XInternAtom(x->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	x->_NET_WM_STATE_MAXIMIZED_HORZ = XInternAtom(x->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	x->_NET_WM_STATE_SHADED = XInternAtom(x->display, "_NET_WM_STATE_SHADED", False);
	x->_NET_WM_STATE_HIDDEN = XInternAtom(x->display, "_NET_WM_STATE_HIDDEN", False);
	x->_NET_WM_STATE_BELOW = XInternAtom(x->display, "_NET_WM_STATE_BELOW", False);
	x->_NET_WM_STATE_ABOVE = XInternAtom(x->display, "_NET_WM_STATE_ABOVE", False);
	x->_NET_WM_STATE_MODAL = XInternAtom(x->display, "_NET_WM_STATE_MODAL", False);
	x->_NET_CLIENT_LIST = XInternAtom(x->display, "_NET_CLIENT_LIST", False);
	x->_NET_WM_NAME = XInternAtom(x->display, "_NET_WM_NAME", False);
	x->_NET_WM_VISIBLE_NAME = XInternAtom(x->display, "_NET_WM_VISIBLE_NAME", False);
	x->_NET_WM_STRUT = XInternAtom(x->display, "_NET_WM_STRUT", False);
	x->_NET_WM_ICON = XInternAtom(x->display, "_NET_WM_ICON", False);
	x->_NET_WM_ICON_NAME = XInternAtom(x->display, "_NET_WM_ICON_NAME", False);
	x->_NET_CLOSE_WINDOW = XInternAtom(x->display, "_NET_CLOSE_WINDOW", False);
	x->UTF8_STRING = XInternAtom(x->display, "UTF8_STRING", False);
	x->_NET_WM_CM_S0 = XInternAtom(x->display, "_NET_WM_CM_S0", False);
	x->_NET_WM_STRUT_PARTIAL = XInternAtom(x->display, "_NET_WM_STRUT_PARTIAL", False);
	x->WM_NAME = XInternAtom(x->display, "WM_NAME", False);
	x->__SWM_VROOT = XInternAtom(x->display, "__SWM_VROOT", False);
	x->_MOTIF_WM_HINTS = XInternAtom(x->display, "_MOTIF_WM_HINTS", False);
	x->WM_HINTS = XInternAtom(x->display, "WM_HINTS", False);

	char name[256];
	snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", x->screen);
	x->_NET_SYSTEM_TRAY_SCREEN = XInternAtom(x->display, name, False);
	x->_NET_SYSTEM_TRAY_OPCODE = XInternAtom(x->display, "_NET_SYSTEM_TRAY_OPCODE", False);
	x->MANAGER = XInternAtom(x->display, "MANAGER", False);
	x->_NET_SYSTEM_TRAY_MESSAGE_DATA = XInternAtom(x->display, "_NET_SYSTEM_TRAY_MESSAGE_DATA", False);
	x->_NET_SYSTEM_TRAY_ORIENTATION = XInternAtom(x->display, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	x->_NET_SYSTEM_TRAY_ICON_SIZE = XInternAtom(x->display, "_NET_SYSTEM_TRAY_ICON_SIZE", False);
	x->_NET_SYSTEM_TRAY_PADDING = XInternAtom(x->display, "_NET_SYSTEM_TRAY_PADDING", False);
	x->_XEMBED = XInternAtom(x->display, "_XEMBED", False);
	x->_XEMBED_INFO = XInternAtom(x->display, "_XEMBED_INFO", False);
	x->_NET_WM_PID = XInternAtom(x->display, "_NET_WM_PID", True);

	x->XdndAware = XInternAtom(x->display, "XdndAware", False);
	x->XdndEnter = XInternAtom(x->display, "XdndEnter", False);
	x->XdndPosition = XInternAtom(x->display, "XdndPosition", False);
	x->XdndStatus = XInternAtom(x->display, "XdndStatus", False);
	x->XdndDrop = XInternAtom(x->display, "XdndDrop", False);
	x->XdndLeave = XInternAtom(x->display, "XdndLeave", False);
	x->XdndSelection = XInternAtom(x->display, "XdndSelection", False);
	x->XdndTypeList = XInternAtom(x->display, "XdndTypeList", False);
	x->XdndActionCopy = XInternAtom(x->display, "XdndActionCopy", False);
	x->XdndFinished = XInternAtom(x->display, "XdndFinished", False);
	x->TARGETS = XInternAtom(x->display, "TARGETS", False);

	snprintf(name, sizeof(name), "_XSETTINGS_S%d", x->screen);
	x->_XSETTINGS_SCREEN = XInternAtom(x->display, name, False);
	x->_XSETTINGS_SETTINGS = XInternAtom(x->display, "_XSETTINGS_SETTINGS", False);
}

void x11_send_event32(X11Display *x, Window win, Atom at, long data1, long data2, long data3)
{
	XEvent event;
	event.xclient.type = ClientMessage;
	event.xclient.serial = 0;
	event.xclient.send_event = True;
	event.xclient.display = x->display;
	event.xclient.window = win;
	event.xclient.message_type = at;
	event.xclient.format = 32;
	event.xclient.data.l[0] = data1;
	event.xclient.data.l[1] = data2;
	event.xclient.data.l[2] = data3;
	event.xclient.data.l[3] = 0;
	event.xclient.data.l[4] = 0;

	XSendEvent(x->display, x->root_win, False,
	           SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

int x11_get_property32(X11Display *x, Window win, Atom at, Atom type)
{
	Atom type_ret;
	int format_ret = 0, data = 0;
	unsigned long nitems_ret = 0;
	unsigned long bafter_ret = 0;
	unsigned char *prop_value = 0;

	if (!win)
		return 0;

	int result = XGetWindowProperty(x->display, win, at, 0, 0x7fffffff,
	                                False, type, &type_ret, &format_ret,
	                                &nitems_ret, &bafter_ret, &prop_value);

	if (result == Success && prop_value) {
		data = ((gulong *)prop_value)[0];
		XFree(prop_value);
	}
	return data;
}

void *x11_get_property(X11Display *x, Window win, Atom at, Atom type, int *num_results)
{
	Atom type_ret;
	int format_ret = 0;
	unsigned long nitems_ret = 0;
	unsigned long bafter_ret = 0;
	unsigned char *prop_value;

	if (!win)
		return NULL;

	int result = XGetWindowProperty(x->display, win, at, 0, 0x7fffffff,
	                                False, type, &type_ret, &format_ret,
	                                &nitems_ret, &bafter_ret, &prop_value);

	if (num_results)
		*num_results = (int)nitems_ret;

	if (result == Success && prop_value)
		return prop_value;
	return NULL;
}

// ============================================================================
// Monitor detection
// ============================================================================

static int compare_monitors(const void *a, const void *b)
{
	const BkMonitor *m1 = a, *m2 = b;
	if (m1->primary && !m2->primary) return -1;
	if (!m1->primary && m2->primary) return 1;
	if (m1->x < m2->x) return -1;
	if (m1->x > m2->x) return 1;
	if (m1->y < m2->y) return -1;
	if (m1->y > m2->y) return 1;
	return 0;
}

void x11_detect_monitors(X11Display *x)
{
	// Try Xinerama first
	int num = 0;
	XineramaScreenInfo *xinerama_info = XineramaQueryScreens(x->display, &num);

	if (xinerama_info && num > 0) {
		x->num_monitors = num;
		x->monitors = calloc(num, sizeof(BkMonitor));

		for (int i = 0; i < num; i++) {
			x->monitors[i].x = xinerama_info[i].x_org;
			x->monitors[i].y = xinerama_info[i].y_org;
			x->monitors[i].width = xinerama_info[i].width;
			x->monitors[i].height = xinerama_info[i].height;
			x->monitors[i].names = g_new0(gchar*, 2);
			x->monitors[i].names[0] = g_strdup_printf("Monitor %d", i + 1);
			x->monitors[i].names[1] = NULL;
		}
		XFree(xinerama_info);
		return;
	}

	// No Xinerama, use screen size
	x->num_monitors = 1;
	x->monitors = calloc(1, sizeof(BkMonitor));
	x->monitors[0].width = DisplayWidth(x->display, x->screen);
	x->monitors[0].height = DisplayHeight(x->display, x->screen);
	x->monitors[0].names = g_new0(gchar*, 2);
	x->monitors[0].names[0] = g_strdup("Monitor 1");
	x->monitors[0].names[1] = NULL;
}

// ============================================================================
// Root pixmap detection (for fake transparency)
// ============================================================================

void x11_detect_root_pixmap(X11Display *x)
{
	Pixmap ret = None;

	Atom pixmap_atoms[] = {
		XInternAtom(x->display, "_XROOTPMAP_ID", False),
		XInternAtom(x->display, "_XROOTMAP_ID", False),
	};

	for (size_t i = 0; i < sizeof(pixmap_atoms) / sizeof(Atom); ++i) {
		unsigned long *res = x11_get_property(x, x->root_win, pixmap_atoms[i], XA_PIXMAP, NULL);
		if (res) {
			ret = *((Pixmap *)res);
			XFree(res);
			break;
		}
	}

	x->root_pmap = ret;

	if (x->root_pmap != None) {
		XGCValues gcv;
		gcv.ts_x_origin = 0;
		gcv.ts_y_origin = 0;
		gcv.fill_style = FillTiled;
		unsigned long mask = GCTileStipXOrigin | GCTileStipYOrigin | GCFillStyle | GCTile;
		gcv.tile = x->root_pmap;
		XChangeGC(x->display, x->gc, mask, &gcv);
	}
}

// ============================================================================
// VTable
// ============================================================================

const BackendVT x11_backend_vt = {
	.init                           = x11_backend_init,
	.cleanup                        = x11_backend_cleanup,
	.type                           = x11_backend_type,
	.get_display                    = x11_get_display,
	.get_fd                         = x11_get_fd,
	.flush                          = x11_flush,
	.events_pending                 = x11_events_pending,
	.wait_event                     = x11_wait_event,
	.poll_event                     = x11_poll_event,
	.event_free                     = x11_event_free,
	.run                            = x11_run,
	.window_create                  = x11_window_create,
	.window_destroy                 = x11_window_destroy,
	.window_show                    = x11_window_show,
	.window_hide                    = x11_window_hide,
	.window_move                    = x11_window_move,
	.window_resize                  = x11_window_resize,
	.window_move_resize             = x11_window_move_resize,
	.window_set_title               = x11_window_set_title,
	.window_set_input_region        = x11_window_set_input_region,
	.window_set_opacity             = x11_window_set_opacity,
	.window_get_id                  = x11_window_get_id,
	.pixmap_create                  = x11_pixmap_create,
	.pixmap_destroy                 = x11_pixmap_destroy,
	.pixmap_get_size                = x11_pixmap_get_size,
	.cairo_surface_create_for_pixmap = x11_cairo_surface_create_for_pixmap,
	.cairo_surface_create_for_window = x11_cairo_surface_create_for_window,
	.pixmap_copy_area               = x11_pixmap_copy_area,
	.window_present                 = x11_window_present,
	.get_default_visual             = x11_get_default_visual,
	.visual_free                    = x11_visual_free,
	.get_monitor_count              = x11_get_monitor_count,
	.get_monitor                    = x11_get_monitor,
	.monitor_free                   = x11_monitor_free,
	.get_monitors                   = x11_get_monitors,
	.monitor_free_list              = x11_monitor_free_list,
	.get_desktop_count              = x11_get_desktop_count,
	.get_current_desktop            = x11_get_current_desktop,
	.set_current_desktop            = x11_set_current_desktop,
	.get_desktop_names              = x11_get_desktop_names,
	.get_toplevels                  = x11_get_toplevels,
	.toplevel_free                  = x11_toplevel_free,
	.toplevel_free_list             = x11_toplevel_free_list,
	.toplevel_activate              = x11_toplevel_activate,
	.toplevel_close                 = x11_toplevel_close,
	.toplevel_minimize              = x11_toplevel_minimize,
	.toplevel_toggle_maximize       = x11_toplevel_toggle_maximize,
	.toplevel_toggle_shade          = x11_toplevel_toggle_shade,
	.toplevel_set_desktop           = x11_toplevel_set_desktop,
	.get_active_toplevel            = x11_get_active_toplevel,
	.toplevel_get_icon              = x11_toplevel_get_icon,
	.toplevel_get_geometry          = x11_toplevel_get_geometry,
	.toplevel_get_pid               = x11_toplevel_get_pid,
	.toplevel_is_iconified          = x11_toplevel_is_iconified,
	.toplevel_is_urgent             = x11_toplevel_is_urgent,
	.toplevel_is_hidden             = x11_toplevel_is_hidden,
	.toplevel_is_active             = x11_toplevel_is_active,
	.systray_init                   = x11_systray_init,
	.systray_cleanup                = x11_systray_cleanup,
	.systray_is_icon                = x11_systray_is_icon,
	.systray_find_icon              = x11_systray_find_icon,
	.systray_get_icons              = x11_systray_get_icons,
	.systray_embed_icon             = x11_systray_embed_icon,
	.systray_remove_icon            = x11_systray_remove_icon,
	.systray_send_message           = x11_systray_send_message,
	.systray_get_icon_image         = x11_systray_get_icon_image,
	.systray_icon_free              = x11_systray_icon_free,
	.has_real_transparency          = x11_has_real_transparency,
	.get_root_pixmap                = x11_get_root_pixmap,
	.window_set_cursor              = x11_window_set_cursor,
	.get_cursor_position            = x11_get_cursor_position,
	.dnd_start                      = x11_dnd_start,
	.get_dpi                        = x11_get_dpi,
	.get_root_id                    = x11_get_root_id,
	.is_panel_window                = x11_is_panel_window,
};
