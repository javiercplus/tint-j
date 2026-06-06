/**************************************************************************
 * compat.c
 *
 * X11 → Wayland Compatibility Layer Implementation
 *
 * Provides X11 API compatible functions that redirect to the Wayland backend.
 * Manages a pseudo-X11 display, atom table, and event translation.
 *
 * Copyright (C) 2026 tint-j project
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "compat/compat.h"
#include "area.h"

// ============================================================================
// Global state (simulates X11 server globals)
// ============================================================================

CompatDisplay *server_display = NULL;
Window server_root_win = 0;
GC server_gc = NULL;
Visual *server_visual = NULL;
int server_depth = 32;
Colormap server_colormap = NULL;

// Tables are now stored in server_display (struct CompatDisplay in compat.h)

// Window → backing pixmap table
// window_pixmap_table removed

BkWindow *win_lookup(Window w)
{
	if (!server_display || !server_display->window_table || w == 0) return NULL;
	return g_hash_table_lookup(server_display->window_table, GUINT_TO_POINTER(w));
}

BkPixmap *pix_lookup(Pixmap p)
{
	if (!server_display || !server_display->pixmap_table || p == 0) return NULL;
	return g_hash_table_lookup(server_display->pixmap_table, GUINT_TO_POINTER(p));
}

// Event queue
static GList *event_queue = NULL;

// ============================================================================
// Internal helpers
// ============================================================================

static Atom intern_atom(Display *display, const char *name)
{
	if (!display || !display->atom_table)
		return 0;
	gpointer val = g_hash_table_lookup(display->atom_table, name);
	if (val)
		return GPOINTER_TO_UINT(val);
	return 0;
}

static void queue_event(XEvent *ev)
{
	XEvent *copy = g_malloc(sizeof(XEvent));
	memcpy(copy, ev, sizeof(XEvent));
	event_queue = g_list_append(event_queue, copy);
}

static XEvent *dequeue_event(void)
{
	if (!event_queue)
		return NULL;
	XEvent *ev = (XEvent *)event_queue->data;
	event_queue = g_list_delete_link(event_queue, event_queue);
	return ev;
}

static unsigned int bk_button_to_x11(BkButton btn)
{
	switch (btn) {
	case BK_BUTTON_LEFT:   return 1;
	case BK_BUTTON_MIDDLE: return 2;
	case BK_BUTTON_RIGHT:  return 3;
	case BK_SCROLL_UP:     return 4;
	case BK_SCROLL_DOWN:   return 5;
	case BK_SCROLL_LEFT:   return 6;
	case BK_SCROLL_RIGHT:  return 7;
	default:               return 0;
	}
}

// ============================================================================
// Compat Init / Cleanup
// ============================================================================

int compat_init(void)
{
	server_display = g_new0(CompatDisplay, 1);
	server_display->bk = backend->get_display();
	server_display->default_screen = 0;
	server_display->depth = 32;

	server_root_win = (Window)0x1; // dummy non-NULL
	server_display->root_win = server_root_win;

	server_display->default_visual = bk_get_default_visual(32);
	server_visual = server_display->default_visual;

	server_display->atom_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		server_display->next_atom_id = 1;

		// Initialize lookup tables in server_display (heap-allocated, safe from BSS corruption)
		server_display->window_table = g_hash_table_new(g_direct_hash, g_direct_equal);
		server_display->window_rev_table = g_hash_table_new(g_direct_hash, g_direct_equal);
		server_display->pixmap_table = g_hash_table_new(g_direct_hash, g_direct_equal);
		server_display->next_window_id = 1;
		server_display->next_pixmap_id = 1;

	fprintf(stderr, "compat: initialized Wayland compatibility layer\n");
		fprintf(stderr, "compat: window_table=%p window_rev=%p pixmap=%p\n",
				        (void*)server_display->window_table, (void*)server_display->window_rev_table,
				        (void*)server_display->pixmap_table);
			return 0;
}

void compat_cleanup(void)
{
	g_list_free_full(event_queue, g_free);
	event_queue = NULL;

	if (server_display) {
		if (server_display->atom_table) {
			g_hash_table_destroy(server_display->atom_table);
			server_display->atom_table = NULL;
		}
		if (server_display->window_table) g_hash_table_destroy(server_display->window_table);
		if (server_display->window_rev_table) g_hash_table_destroy(server_display->window_rev_table);
		if (server_display->pixmap_table) g_hash_table_destroy(server_display->pixmap_table);
		g_free(server_display);
		server_display = NULL;
	}
	extern GHashTable *window_properties; // Fix for later in file
	if (window_properties) {
		g_hash_table_destroy(window_properties);
		window_properties = NULL;
	}
	server_root_win = 0;
	server_gc = NULL;
	server_visual = NULL;
	server_colormap = NULL;
}

BkDisplay *compat_get_bk_display(void)
{
	return server_display ? server_display->bk : NULL;
}

void compat_window_present(Window w)
{
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_present(bw);
}

// ============================================================================
// Display
// ============================================================================

Display *XOpenDisplay(const char *display_name)
{
	(void)display_name;
	if (server_display)
		return server_display;
	compat_init();
	return server_display;
}

int XCloseDisplay(Display *display)
{
	(void)display;
	return 0;
}

int ConnectionNumber(Display *display)
{
	(void)display;
	return bk_get_fd();
}

int DefaultScreen(Display *display)
{
	return display ? display->default_screen : 0;
}

Window XRootWindow(Display *display, int screen)
{
	(void)screen;
	return display ? display->root_win : 0;
}

// ============================================================================
// Atoms
// ============================================================================

Atom XInternAtom(Display *display, const char *atom_name, int only_if_exists)
{
	if (!display || !display->atom_table)
		return 0;

	gpointer existing = g_hash_table_lookup(display->atom_table, atom_name);
	if (existing)
		return GPOINTER_TO_UINT(existing);

	if (only_if_exists)
		return 0;

	Atom id = display->next_atom_id++;
	g_hash_table_insert(display->atom_table, g_strdup(atom_name), GUINT_TO_POINTER(id));
	return id;
}

char *XGetAtomName(Display *display, Atom atom)
{
	if (!display || !display->atom_table)
		return g_strdup("None");

	if (atom == 0)
		return g_strdup("None");

	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, display->atom_table);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (GPOINTER_TO_UINT(value) == atom)
			return g_strdup((const char *)key);
	}
	return g_strdup("Unknown");
}

// ============================================================================
// Windows
// ============================================================================

Window XCreateWindow(Display *display, Window parent,
                     int x, int y, unsigned int width, unsigned int height,
                     unsigned int border_width, int depth,
                     unsigned int class, Visual *visual,
                     unsigned long valuemask, XSetWindowAttributes *attributes)
{
	(void)display;
	(void)parent;
	(void)border_width;
	(void)depth;
	(void)class;
	(void)visual;
	(void)valuemask;
	(void)attributes;

	BkAnchor anchor = BK_ANCHOR_BOTTOM | BK_ANCHOR_LEFT | BK_ANCHOR_RIGHT;
	int exclusive_zone = height;

	BkWindow *win = bk_window_create(NULL, x, y, width, height,
	                                 32, NULL,
	                                 BK_LAYER_BOTTOM,
	                                 anchor,
	                                 exclusive_zone);
	if (!win) return 0;

		if (!server_display || !server_display->window_table || !server_display->window_rev_table) {
				fprintf(stderr, "compat: XCreateWindow: no tables! display=%p\n", (void*)server_display);
				bk_window_destroy(win);
				return 0;
			}

		Window id = server_display->next_window_id++;
	g_hash_table_insert(server_display->window_table, GUINT_TO_POINTER(id), win);
	g_hash_table_insert(server_display->window_rev_table, win, GUINT_TO_POINTER(id));
	return id;
}

int XDestroyWindow(Display *display, Window w)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) {
		bk_window_destroy(bw);
		g_hash_table_remove(server_display->window_table, GUINT_TO_POINTER(w));
		g_hash_table_remove(server_display->window_rev_table, bw);
	}
	return 0;
}

int XMapWindow(Display *display, Window w)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_show(bw);
	return 0;
}

int XUnmapWindow(Display *display, Window w)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_hide(bw);
	return 0;
}

int XMapSubwindows(Display *display, Window w)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_present(bw);
	return 0;
}

int XUnmapSubwindows(Display *display, Window w)
{
	(void)display;
	(void)w;
	return 0;
}

int XMoveResizeWindow(Display *display, Window w, int x, int y,
                      unsigned int width, unsigned int height)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_move_resize(bw, x, y, width, height);
	return 0;
}

int XResizeWindow(Display *display, Window w, unsigned int width, unsigned int height)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_resize(bw, width, height);
	return 0;
}

int XMoveWindow(Display *display, Window w, int x, int y)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_move(bw, x, y);
	return 0;
}

int XLowerWindow(Display *display, Window w)
{
	(void)display;
	(void)w;
	return 0;
}

int XRaiseWindow(Display *display, Window w)
{
	(void)display;
	(void)w;
	return 0;
}

int XIconifyWindow(Display *display, Window w, int screen)
{
	(void)display;
	(void)w;
	(void)screen;
	return 0;
}

int XSelectInput(Display *display, Window w, long event_mask)
{
	(void)display;
	(void)w;
	(void)event_mask;
	return 0;
}

int XChangeWindowAttributes(Display *display, Window w,
                            unsigned long valuemask, XSetWindowAttributes *attributes)
{
	(void)display;
	(void)w;
	(void)valuemask;
	(void)attributes;
	return 0;
}

int XSetWindowBackgroundPixmap(Display *display, Window w, Pixmap pixmap)
{
	(void)display;
	(void)w;
	(void)pixmap;
	return 0;
}

int XGetWindowAttributes(Display *display, Window w, XWindowAttributes *attr)
{
	(void)display;
	if (!w || !attr) return 0;
	memset(attr, 0, sizeof(*attr));
	attr->visual = server_visual;
	attr->depth = server_depth;
	attr->map_state = IsViewable;
	return 1;
}

int XGetGeometry(Display *display, Drawable d, Window *root_return,
                 int *x_return, int *y_return,
                 unsigned int *width_return, unsigned int *height_return,
                 unsigned int *border_width_return, unsigned int *depth_return)
{
	(void)display;

	if (root_return) *root_return = server_root_win;
	if (depth_return) *depth_return = server_depth;
	if (border_width_return) *border_width_return = 0;

	if (d == (Drawable)server_root_win || d == 0) {
		BkMonitor *mon = bk_get_monitor(0);
		if (mon) {
			if (x_return) *x_return = mon->x;
			if (y_return) *y_return = mon->y;
			if (width_return) *width_return = mon->width;
			if (height_return) *height_return = mon->height;
			bk_monitor_free(mon);
		} else {
			if (x_return) *x_return = 0;
			if (y_return) *y_return = 0;
			if (width_return) *width_return = 1920;
			if (height_return) *height_return = 1080;
		}
	} else {
		BkPixmap *pm = pix_lookup((Pixmap)d);
		int pw = 0, ph = 0;
		if (pm) bk_pixmap_get_size(pm, &pw, &ph);
		if (x_return) *x_return = 0;
		if (y_return) *y_return = 0;
		if (width_return) *width_return = pw;
		if (height_return) *height_return = ph;
	}
	return 1;
}

int XTranslateCoordinates(Display *display,
                          Window src_w, Window dest_w,
                          int src_x, int src_y,
                          int *dest_x_return, int *dest_y_return,
                          Window *child_return)
{
	(void)display;
	(void)src_w;
	(void)dest_w;
	if (dest_x_return) *dest_x_return = src_x;
	if (dest_y_return) *dest_y_return = src_y;
	if (child_return) *child_return = 0;
	return 1;
}

int XQueryPointer(Display *display, Window w,
                  Window *root_return, Window *child_return,
                  int *root_x_return, int *root_y_return,
                  int *win_x_return, int *win_y_return,
                  unsigned int *mask_return)
{
	(void)display;
	(void)w;

	bk_get_cursor_position(root_x_return, root_y_return);
	if (root_return) *root_return = server_root_win;
	if (child_return) *child_return = 0;
	if (win_x_return) *win_x_return = root_x_return ? *root_x_return : 0;
	if (win_y_return) *win_y_return = root_y_return ? *root_y_return : 0;
	if (mask_return) *mask_return = 0;
	return 1;
}

int XGetTransientForHint(Display *display, Window w, Window *prop_window_return)
{
	(void)display;
	(void)w;
	if (prop_window_return) *prop_window_return = 0;
	return 0;
}

// ============================================================================
// Pixmaps
// ============================================================================

Pixmap XCreatePixmap(Display *display, Drawable d,
                     unsigned int width, unsigned int height,
                     unsigned int depth)
{
	(void)display;
	(void)d;
	BkPixmap *pm = bk_pixmap_create(width, height, depth ? depth : 32);
	if (!pm) return 0;
	Pixmap id = server_display->next_pixmap_id++;
	g_hash_table_insert(server_display->pixmap_table, GUINT_TO_POINTER(id), pm);
	return id;
}

int XFreePixmap(Display *display, Pixmap pixmap)
{
	(void)display;
	BkPixmap *pm = pix_lookup(pixmap);
	if (pm) {
		bk_pixmap_destroy(pm);
		g_hash_table_remove(server_display->pixmap_table, GUINT_TO_POINTER(pixmap));
	}
	return 0;
}

// ============================================================================
// Pixmap copy / drawing
// ============================================================================

int XCopyArea(Display *display, Drawable src, Drawable dst, GC gc,
              int src_x, int src_y,
              unsigned int width, unsigned int height,
              int dst_x, int dst_y)
{
	(void)display;
	(void)gc;
	BkPixmap *psrc = pix_lookup((Pixmap)src);
	BkPixmap *pdst = pix_lookup((Pixmap)dst);
	if (!psrc || !pdst) return 0;
	bk_pixmap_copy_area(psrc, pdst, src_x, src_y, width, height, dst_x, dst_y);
	return 0;
}

int XFillRectangle(Display *display, Drawable d, GC gc,
                   int x, int y, unsigned int width, unsigned int height)
{
	(void)display;
	(void)gc;
	(void)d;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	return 0;
}

int XSetTSOrigin(Display *display, GC gc, int x, int y)
{
	(void)display;
	(void)gc;
	(void)x;
	(void)y;
	return 0;
}

// ============================================================================
// Cairo integration (linker-wrapped)
// ============================================================================

cairo_surface_t *__wrap_cairo_xlib_surface_create(Display *dpy, Drawable drawable,
                                                   Visual *visual, int width, int height)
{
	(void)dpy;
	(void)visual;
	BkPixmap *pm = pix_lookup((Pixmap)drawable);
	if (!pm) return NULL;
	return bk_cairo_surface_create_for_pixmap(pm, width, height);
}

// ============================================================================
// Imlib2 integration
// ============================================================================

static Drawable compat_imlib_drawable = 0;

void compat_imlib_context_set_drawable(Drawable d)
{
	compat_imlib_drawable = d;
}

void compat_imlib_render_image_on_drawable(int x, int y)
{
	BkPixmap *pm = pix_lookup((Pixmap)compat_imlib_drawable);
	if (!pm) return;

	int pw = 0, ph = 0;
	bk_pixmap_get_size(pm, &pw, &ph);

	cairo_surface_t *cs = bk_cairo_surface_create_for_pixmap(pm, pw, ph);
	if (!cs) return;

	cairo_t *cr = cairo_create(cs);

	Imlib_Image img = imlib_context_get_image();
	if (img) {
		imlib_context_set_image(img);
		int iw = imlib_image_get_width();
		int ih = imlib_image_get_height();
		DATA32 *data = imlib_image_get_data_for_reading_only();

		if (data) {
			cairo_surface_t *img_surface = cairo_image_surface_create_for_data(
				(unsigned char *)data, CAIRO_FORMAT_ARGB32, iw, ih, iw * 4);
			cairo_set_source_surface(cr, img_surface, x, y);
			cairo_paint(cr);
			cairo_surface_destroy(img_surface);
		}
	}

	cairo_destroy(cr);
	cairo_surface_destroy(cs);
}

Imlib_Image compat_imlib_create_image_from_drawable(Drawable d, int x, int y,
                                                     int width, int height,
                                                     int need_to_grab)
{
	(void)x;
	(void)y;
	(void)need_to_grab;

	BkPixmap *pm = pix_lookup((Pixmap)d);
	if (!pm) return NULL;

	// Create an empty ARGB32 image
	Imlib_Image img = imlib_create_image(width, height);
	if (!img) return NULL;

	imlib_context_set_image(img);
	imlib_image_set_has_alpha(1);
	DATA32 *pixels = imlib_image_get_data();
	if (pixels) {
		memset(pixels, 0, width * height * 4);
		imlib_image_put_back_data(pixels);
	}
	return img;
}

// ===================================================================
// Properties
// ===================================================================

typedef struct {
	Atom type;
	int format;
	unsigned char *data;
	int nelements;
} CompatProperty;

GHashTable *window_properties = NULL;  // Window → GHashTable<Atom→GList<CompatProperty*>>

#define WIN_TO_KEY(w) GUINT_TO_POINTER(w)

static GList *get_prop_list(Window w, Atom property)
{
	if (!window_properties) return NULL;
	GHashTable *prop_table = g_hash_table_lookup(window_properties, WIN_TO_KEY(w));
	if (!prop_table) return NULL;
	return g_hash_table_lookup(prop_table, GUINT_TO_POINTER(property));
}

static void set_prop(Window w, Atom property, Atom type, int format,
                     const unsigned char *data, int nelements)
{
	if (!window_properties) {
		window_properties = g_hash_table_new_full(g_direct_hash, g_direct_equal,
		                                          NULL, (GDestroyNotify)g_hash_table_destroy);
	}

	GHashTable *prop_table = g_hash_table_lookup(window_properties, WIN_TO_KEY(w));
	if (!prop_table) {
		prop_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
		                                   NULL, (GDestroyNotify)g_list_free);
		g_hash_table_insert(window_properties, WIN_TO_KEY(w), prop_table);
	}

	GList *old_list = g_hash_table_lookup(prop_table, GUINT_TO_POINTER(property));
	if (old_list) {
		CompatProperty *old = (CompatProperty *)old_list->data;
		g_free(old->data);
		g_free(old);
		g_hash_table_remove(prop_table, GUINT_TO_POINTER(property));
	}

	CompatProperty *prop = g_new(CompatProperty, 1);
	prop->type = type;
	prop->format = format;
	prop->nelements = nelements;
	prop->data = g_malloc(nelements * (format / 8));
	memcpy(prop->data, data, nelements * (format / 8));

	GList *list = g_list_append(NULL, prop);
	g_hash_table_insert(prop_table, GUINT_TO_POINTER(property), list);
}

int XChangeProperty(Display *display, Window w,
                    Atom property, Atom type,
                    int format, int mode,
                    const unsigned char *data, int nelements)
{
	(void)display;
	(void)mode;

	set_prop(w, property, type, format, data, nelements);

	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.xproperty.type = PropertyNotify;
	ev.xproperty.display = display;
	ev.xproperty.window = w;
	ev.xproperty.atom = property;
	ev.xproperty.state = PropertyNewValue;
	queue_event(&ev);

	return 0;
}

int XDeleteProperty(Display *display, Window w, Atom property)
{
	(void)display;
	if (!window_properties) return 0;

	GHashTable *prop_table = g_hash_table_lookup(window_properties, WIN_TO_KEY(w));
	if (!prop_table) return 0;

	GList *list = g_hash_table_lookup(prop_table, GUINT_TO_POINTER(property));
	if (list) {
		CompatProperty *prop = (CompatProperty *)list->data;
		g_free(prop->data);
		g_free(prop);
		g_hash_table_remove(prop_table, GUINT_TO_POINTER(property));
	}
	return 0;
}

int XGetWindowProperty(Display *display, Window w, Atom property,
                       long long_offset, long long_length,
                       int delete, Atom req_type,
                       Atom *actual_type_return, int *actual_format_return,
                       unsigned long *nitems_return,
                       unsigned long *bytes_after_return,
                       unsigned char **prop_return)
{
	(void)display;

	if (actual_type_return) *actual_type_return = 0;
	if (actual_format_return) *actual_format_return = 0;
	if (nitems_return) *nitems_return = 0;
	if (bytes_after_return) *bytes_after_return = 0;
	if (prop_return) *prop_return = NULL;

	GList *list = get_prop_list(w, property);
	if (!list) return 1;

	CompatProperty *prop = (CompatProperty *)list->data;

	if (actual_type_return) *actual_type_return = prop->type;
	if (actual_format_return) *actual_format_return = prop->format;

	long available = prop->nelements - long_offset;
	if (available <= 0) {
		if (nitems_return) *nitems_return = 0;
		return 0;
	}

	long count = MIN(available, long_length);
	if (nitems_return) *nitems_return = (unsigned long)count;
	if (bytes_after_return) *bytes_after_return = (unsigned long)(available - count);

	if (prop_return) {
		int bytes = count * (prop->format / 8);
		*prop_return = g_malloc(bytes);
		memcpy(*prop_return, prop->data + long_offset * (prop->format / 8), bytes);
	}

	if (delete) {
		XDeleteProperty(display, w, property);
	}

	return 0;
}

// ============================================================================
// Window properties
// ============================================================================

int XStoreName(Display *display, Window w, const char *window_name)
{
	(void)display;
	BkWindow *bw = win_lookup(w);
	if (bw) bk_window_set_title(bw, window_name);
	return 0;
}

int XSetIconName(Display *display, Window w, const char *icon_name)
{
	(void)display;
	(void)w;
	(void)icon_name;
	return 0;
}

int XSetWMHints(Display *display, Window w, XWMHints *wm_hints)
{
	(void)display;
	(void)w;
	(void)wm_hints;
	return 0;
}

int XSetWMNormalHints(Display *display, Window w, XSizeHints *hints)
{
	(void)display;
	(void)w;
	(void)hints;
	return 0;
}

int XSetClassHint(Display *display, Window w, XClassHint *class_hint)
{
	(void)display;
	(void)w;
	(void)class_hint;
	return 0;
}

int XSetTransientForHint(Display *display, Window w, Window prop_window)
{
	(void)display;
	(void)w;
	(void)prop_window;
	return 0;
}

XClassHint *XAllocClassHint(void)
{
	return g_new0(XClassHint, 1);
}

XWMHints *XGetWMHints(Display *display, Window w)
{
	(void)display;
	(void)w;
	return NULL;
}

// ============================================================================
// Selections
// ============================================================================

Window XGetSelectionOwner(Display *display, Atom selection)
{
	(void)display;
	(void)selection;
	return 0;
}

int XConvertSelection(Display *display, Atom selection, Atom target,
                      Atom property, Window requestor, Time time)
{
	(void)display;
	(void)selection;
	(void)target;
	(void)property;
	(void)requestor;
	(void)time;
	return 0;
}

// ============================================================================
// Event translation helper
// ============================================================================

static void translate_and_queue_event(BkEvent *bev)
{
	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.xany.display = server_display;

	// Look up Window ID from BkWindow pointer
	Window event_win = 0;
	if (bev->window && server_display && server_display->window_rev_table) {
		gpointer id = g_hash_table_lookup(server_display->window_rev_table, bev->window);
		if (id) event_win = GPOINTER_TO_UINT(id);
	}

	switch (bev->type) {
	case BK_EVENT_EXPOSE:
		ev.xexpose.type = Expose;
		ev.xexpose.window = event_win;
		ev.xexpose.width = bev->configure_width;
		ev.xexpose.height = bev->configure_height;
		break;

	case BK_EVENT_MOUSE_PRESS:
		ev.xbutton.type = ButtonPress;
		ev.xbutton.window = event_win;
		ev.xbutton.x = bev->mouse_x;
		ev.xbutton.y = bev->mouse_y;
		ev.xbutton.x_root = bev->mouse_x;
		ev.xbutton.y_root = bev->mouse_y;
		ev.xbutton.button = bk_button_to_x11(bev->mouse_button);
		ev.xbutton.state = 0;
		break;

	case BK_EVENT_MOUSE_RELEASE:
		ev.xbutton.type = ButtonRelease;
		ev.xbutton.window = event_win;
		ev.xbutton.x = bev->mouse_x;
		ev.xbutton.y = bev->mouse_y;
		ev.xbutton.x_root = bev->mouse_x;
		ev.xbutton.y_root = bev->mouse_y;
		ev.xbutton.button = bk_button_to_x11(bev->mouse_button);
		ev.xbutton.state = 0;
		break;

	case BK_EVENT_MOUSE_MOTION:
		ev.xmotion.type = MotionNotify;
		ev.xmotion.window = event_win;
		ev.xmotion.x = bev->mouse_x;
		ev.xmotion.y = bev->mouse_y;
		ev.xmotion.x_root = bev->mouse_x;
		ev.xmotion.y_root = bev->mouse_y;
		ev.xmotion.state = 0;
		break;

	case BK_EVENT_ENTER:
		ev.xcrossing.type = EnterNotify;
		ev.xcrossing.window = event_win;
		ev.xcrossing.x = bev->mouse_x;
		ev.xcrossing.y = bev->mouse_y;
		break;

	case BK_EVENT_LEAVE:
		ev.xcrossing.type = LeaveNotify;
		ev.xcrossing.window = event_win;
		ev.xcrossing.x = bev->mouse_x;
		ev.xcrossing.y = bev->mouse_y;
		break;

	case BK_EVENT_CONFIGURE:
		ev.xconfigure.type = ConfigureNotify;
		ev.xconfigure.window = event_win;
		ev.xconfigure.width = bev->configure_width;
		ev.xconfigure.height = bev->configure_height;
		break;

	case BK_EVENT_DESTROY:
		ev.xdestroywindow.type = DestroyNotify;
		ev.xdestroywindow.window = event_win;
		break;

	case BK_EVENT_DESKTOP_CHANGED:
		ev.xproperty.type = PropertyNotify;
		ev.xproperty.window = server_root_win;
		ev.xproperty.atom = intern_atom(server_display, "_NET_CURRENT_DESKTOP");
		ev.xproperty.state = PropertyNewValue;
		queue_event(&ev);

		ev.xproperty.atom = intern_atom(server_display, "_NET_NUMBER_OF_DESKTOPS");
		queue_event(&ev);

		ev.xproperty.atom = intern_atom(server_display, "_NET_CLIENT_LIST");
		queue_event(&ev);

		ev.xproperty.atom = intern_atom(server_display, "_NET_ACTIVE_WINDOW");
		queue_event(&ev);

		bk_event_free(bev);
		return;

	case BK_EVENT_TOPLEVEL_ADDED:
	case BK_EVENT_TOPLEVEL_REMOVED:
	case BK_EVENT_TOPLEVEL_CHANGED:
		ev.xproperty.type = PropertyNotify;
		ev.xproperty.window = server_root_win;
		ev.xproperty.atom = intern_atom(server_display, "_NET_CLIENT_LIST");
		ev.xproperty.state = PropertyNewValue;
		queue_event(&ev);

		ev.xproperty.atom = intern_atom(server_display, "_NET_ACTIVE_WINDOW");
		queue_event(&ev);

		bk_event_free(bev);
		return;

	case BK_EVENT_CLIENT_MESSAGE:
		ev.xclient.type = ClientMessage;
		ev.xclient.window = event_win;
		ev.xclient.message_type = 0;
		ev.xclient.format = 32;
		break;

	default:
		bk_event_free(bev);
		return;
	}

	queue_event(&ev);
	bk_event_free(bev);
}

// ============================================================================
// Event handling
// ============================================================================

int XPending(Display *display)
{
	(void)display;
	BkEvent *bev;
	while ((bev = bk_poll_event()) != NULL) {
		translate_and_queue_event(bev);
	}
	return event_queue ? 1 : 0;
}

int XNextEvent(Display *display, XEvent *event_return)
{
	(void)display;

	if (!event_queue)
		XPending(display);

	if (!event_queue) {
		// Block and wait
		BkEvent *bev = bk_wait_event();
		if (bev) {
			translate_and_queue_event(bev);
		}
	}

	XEvent *ev = dequeue_event();
	if (!ev) return 0;

	memcpy(event_return, ev, sizeof(XEvent));
	g_free(ev);
	return 0;
}

int XSendEvent(Display *display, Window w, int propagate,
               long event_mask, XEvent *event_send)
{
	(void)display;
	(void)w;
	(void)propagate;
	(void)event_mask;

	if (event_send && event_send->type == ClientMessage) {
		if (event_send->xclient.message_type ==
		    intern_atom(server_display, "_NET_ACTIVE_WINDOW")) {
			Window target = (Window)(long)event_send->xclient.data[0];
				BkWindow *target_bw = win_lookup(target);
				int count;
				BkTopLevel **tls = bk_get_toplevels(&count);
				if (tls) {
					for (int i = 0; tls[i]; i++) {
						if (tls[i]->window == target_bw) {
						bk_toplevel_activate(tls[i]);
						break;
					}
				}
				bk_toplevel_free_list(tls, count);
			}
		}
	}
	return 0;
}

int XSync(Display *display, int discard)
{
	(void)display;
	(void)discard;
	bk_flush();
	return 0;
}

int XFlush(Display *display)
{
	(void)display;
	bk_flush();
	return 0;
}

int XUngrabPointer(Display *display, Time time)
{
	(void)display;
	(void)time;
	return 0;
}

int XGrabServer(Display *display)
{
	(void)display;
	return 0;
}

int XUngrabServer(Display *display)
{
	(void)display;
	return 0;
}

int XRenderComposite(Display *dpy, int op, Picture src, Picture mask,
                     Picture dst, int src_x, int src_y, int mask_x, int mask_y,
                     int dst_x, int dst_y, unsigned int width, unsigned int height)
{
	(void)dpy; (void)op; (void)src; (void)mask; (void)dst;
	(void)src_x; (void)src_y; (void)mask_x; (void)mask_y;
	(void)dst_x; (void)dst_y; (void)width; (void)height;
	return 0;
}

int XRenderFreePicture(Display *dpy, Picture picture)
{
	(void)dpy; (void)picture;
	return 0;
}

int XRenderFillRectangle(Display *dpy, int op, Picture dst,
                         const XRenderColor *color, int x, int y,
                         unsigned int width, unsigned int height)
{
	(void)dpy; (void)op; (void)dst; (void)color;
	(void)x; (void)y; (void)width; (void)height;
	return 0;
}

Picture XRenderCreatePicture(Display *dpy, Drawable drawable,
                              const XRenderPictFormat *format,
                              unsigned long valuemask, const void *attributes)
{
	(void)dpy; (void)drawable; (void)format; (void)valuemask; (void)attributes;
	return 0;
}

XRenderPictFormat *XRenderFindVisualFormat(Display *dpy, const Visual *visual)
{
	(void)dpy; (void)visual;
	return NULL;
}

XRenderPictFormat *XRenderFindStandardFormat(Display *dpy, int format)
{
	(void)dpy; (void)format;
	return NULL;
}

// ============================================================================
// Error handling
// ============================================================================

XErrorHandler XSetErrorHandler(XErrorHandler handler)
{
	(void)handler;
	return NULL;
}

XIOErrorHandler XSetIOErrorHandler(XIOErrorHandler handler)
{
	(void)handler;
	return NULL;
}

// ============================================================================
// GC
// ============================================================================

GC XCreateGC(Display *display, Drawable d, unsigned long valuemask, XGCValues *values)
{
	(void)display;
	(void)d;
	(void)valuemask;
	(void)values;
	return (GC)0x2;
}

int XFreeGC(Display *display, GC gc)
{
	(void)display;
	(void)gc;
	return 0;
}

// ============================================================================
// Memory
// ============================================================================

int XFree(void *data)
{
	g_free(data);
	return 0;
}

// ============================================================================
// Visual / Colormap
// ============================================================================

Visual *XDefaultVisual(Display *display, int screen)
{
	(void)screen;
	return display ? display->default_visual : NULL;
}

Colormap XDefaultColormap(Display *display, int screen)
{
	(void)display;
	(void)screen;
	return NULL;
}

Cursor XCreateFontCursor(Display *display, unsigned int shape)
{
	(void)display;
	(void)shape;
	return (Cursor)1;
}

int XSupportsLocale(void)
{
	return 1;
}

// ============================================================================
// Systray stubs (XEMBED protocol not available on Wayland)
// ============================================================================

// Dummy systray struct and globals (references from panel.c)
typedef struct { Area area; } Systraybar;
Systraybar systray = {{0}};
int systray_max_icon_size = 0;
int systray_monitor = -1;

void default_systray(void) {}
void init_systray(void) {}
void cleanup_systray(void) {}
int systray_on_monitor(int i, int n) { (void)i; (void)n; return 0; }
void init_systray_panel(void *p) { (void)p; }
void refresh_systray_icons(void) {}
