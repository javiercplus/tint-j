/**************************************************************************
 * wl_backend.c
 *
 * Wayland backend implementation for tint2.
 * Uses raw libwayland-client with wlr-layer-shell, 
 * wlr-foreign-toplevel-management, and xdg-output protocols.
 *
 * Designed for wlroots-based compositors (Sway, labwc, river, etc.).
 *
 * Copyright (C) 2024 tint-j project
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <cairo.h>
#include <Imlib2.h>

// Generated protocol headers
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include "backend/backend.h"
#include "wl_backend.h"

// ==========================================================================
// Forward declaration
// ==========================================================================

static void wl_backend_cleanup(void);

// ==========================================================================
// Global state
// ==========================================================================

WlDisplay *wl = NULL;

// ==========================================================================
// Toplevel tracking for taskbar
// ============================================================================

typedef struct WlTopLevel {
	struct zwlr_foreign_toplevel_handle_v1 *handle;
	char *title;
	char *app_id;
	gboolean maximized;
	gboolean minimized;
	gboolean activated;
	gboolean fullscreen;
	int monitor;
} WlTopLevel;

#define WL_MAX_TOPLEVELS 256
static WlTopLevel *wl_toplevels[WL_MAX_TOPLEVELS];
static int wl_num_toplevels = 0;

// ============================================================================
// Pending event
// ============================================================================

static GQueue *wl_event_queue = NULL;

static void wl_queue_event(BkEvent *ev)
{
	if (!wl_event_queue) {
		wl_event_queue = g_queue_new();
	}
	g_queue_push_tail(wl_event_queue, ev);
}

// ============================================================================
// Helper: SHM buffer creation
// ============================================================================

static int create_shm_fd(off_t size)
{
	int fd = memfd_create("tint2-shm", MFD_CLOEXEC);
	if (fd < 0) return -1;
	if (ftruncate(fd, size) < 0) { close(fd); return -1; }
	return fd;
}

static struct wl_buffer *wl_create_shm_buffer(int width, int height,
                                               void **data_out, size_t *size_out)
{
	int stride = width * 4;
	size_t size = stride * height;
	int fd = create_shm_fd(size);
	if (fd < 0) return NULL;

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) { close(fd); return NULL; }

	struct wl_shm_pool *pool = wl_shm_create_pool(wl->shm, fd, size);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0,
		width, height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	*data_out = data;
	*size_out = size;
	return buf;
}

// ============================================================================
// Registry listener
// ============================================================================

static void registry_handle_global(void *data, struct wl_registry *reg,
                                    uint32_t name, const char *iface, uint32_t ver)
{
	(void)data;

	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		wl->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, ver < 4 ? ver : 4);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		wl->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		wl->seat = wl_registry_bind(reg, name, &wl_seat_interface, ver < 5 ? ver : 5);
	} else if (strcmp(iface, wl_output_interface.name) == 0) {
		if (wl->num_monitors < WL_MAX_MONITORS) {
			int i = wl->num_monitors++;
			wl->monitors[i].output = wl_registry_bind(reg, name, &wl_output_interface, ver < 3 ? ver : 3);
			wl->monitors[i].wl_name = name;
		}
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		wl->layer_shell = wl_registry_bind(reg, name,
			&zwlr_layer_shell_v1_interface, ver < 4 ? ver : 4);
		wl->has_layer_shell = TRUE;
	} else if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		wl->toplevel_manager = wl_registry_bind(reg, name,
			&zwlr_foreign_toplevel_manager_v1_interface, ver < 3 ? ver : 3);
		wl->has_toplevel_manager = TRUE;
	} else if (strcmp(iface, zxdg_output_manager_v1_interface.name) == 0) {
		wl->xdg_output_manager = wl_registry_bind(reg, name,
			&zxdg_output_manager_v1_interface, ver < 3 ? ver : 3);
		wl->has_xdg_output = TRUE;
	}
}

static void registry_handle_global_remove(void *data, struct wl_registry *reg,
                                           uint32_t name)
{
	(void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

// ============================================================================
// Seat listener
// ============================================================================

static const struct wl_pointer_listener pointer_listener;
static const struct wl_keyboard_listener keyboard_listener;

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	(void)data;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl->pointer) {
		wl->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(wl->pointer, &pointer_listener, NULL);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && wl->pointer) {
		wl_pointer_destroy(wl->pointer);
		wl->pointer = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl->keyboard) {
		wl->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(wl->keyboard, &keyboard_listener, NULL);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl->keyboard) {
		wl_keyboard_destroy(wl->keyboard);
		wl->keyboard = NULL;
	}
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data; (void)seat;
	g_free(wl->seat_name);
	wl->seat_name = g_strdup(name);
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

// ============================================================================
// Pointer listeners
// ============================================================================

static void pointer_handle_enter(void *data, struct wl_pointer *ptr,
                                  uint32_t serial, struct wl_surface *surface,
                                  wl_fixed_t x, wl_fixed_t y)
{
	(void)data; (void)ptr; (void)serial;
	wl->focused_surface = surface;
	wl->cursor_x = wl_fixed_to_int(x);
	wl->cursor_y = wl_fixed_to_int(y);
}

static void pointer_handle_leave(void *data, struct wl_pointer *ptr,
                                  uint32_t serial, struct wl_surface *surface)
{
	(void)data; (void)ptr; (void)serial; (void)surface;
}

static void pointer_handle_motion(void *data, struct wl_pointer *ptr,
                                   uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)data; (void)ptr; (void)time;
	wl->cursor_x = wl_fixed_to_int(x);
	wl->cursor_y = wl_fixed_to_int(y);
}

static void pointer_handle_button(void *data, struct wl_pointer *ptr,
                                   uint32_t serial, uint32_t time,
                                   uint32_t button, uint32_t state)
{
	(void)data; (void)ptr; (void)time;
	wl->pointer_serial = serial;
	wl->pointer_button = button;

	for (GList *l = wl->panel_windows; l; l = l->next) {
		BkWindow *win = (BkWindow *)l->data;
		if (win->surface != wl->focused_surface) continue;
		BkEvent *ev = calloc(1, sizeof(BkEvent));
		ev->window = win;
		ev->mouse_x = wl->cursor_x;
		ev->mouse_y = wl->cursor_y;
		ev->mouse_button = button;
		ev->type = (state == WL_POINTER_BUTTON_STATE_PRESSED)
			? BK_EVENT_MOUSE_PRESS : BK_EVENT_MOUSE_RELEASE;
		wl_queue_event(ev);
		return;
	}
}

static void pointer_handle_axis(void *data, struct wl_pointer *ptr,
                                 uint32_t time, uint32_t axis, wl_fixed_t value)
{
	(void)data; (void)ptr; (void)time;

	for (GList *l = wl->panel_windows; l; l = l->next) {
		BkWindow *win = (BkWindow *)l->data;
		if (win->surface != wl->focused_surface) continue;
		BkEvent *ev = calloc(1, sizeof(BkEvent));
		ev->type = BK_EVENT_MOUSE_SCROLL;
		ev->window = win;
		ev->mouse_x = wl->cursor_x;
		ev->mouse_y = wl->cursor_y;
		double val = wl_fixed_to_double(value);
		if (axis == 0) { ev->scroll_dy = val; ev->mouse_button = val > 0 ? BK_SCROLL_DOWN : BK_SCROLL_UP; }
		else { ev->scroll_dx = val; ev->mouse_button = val > 0 ? BK_SCROLL_RIGHT : BK_SCROLL_LEFT; }
		wl_queue_event(ev);
		return;
	}
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
};

// ============================================================================
// Keyboard listener
// ============================================================================

static void keyboard_handle_keymap(void *data, struct wl_keyboard *kb,
                                    uint32_t format, int fd, uint32_t size)
{
	(void)data; (void)kb; (void)format; (void)size;
	close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *kb,
                                   uint32_t serial, struct wl_surface *surface,
                                   struct wl_array *keys)
{
	(void)data; (void)kb; (void)serial; (void)surface; (void)keys;
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *kb,
                                   uint32_t serial, struct wl_surface *surface)
{
	(void)data; (void)kb; (void)serial; (void)surface;
}

static void keyboard_handle_key(void *data, struct wl_keyboard *kb,
                                 uint32_t serial, uint32_t time,
                                 uint32_t key, uint32_t state)
{
	(void)data; (void)kb; (void)serial; (void)time;
	BkEvent *ev = calloc(1, sizeof(BkEvent));
	ev->type = (state == WL_KEYBOARD_KEY_STATE_PRESSED)
		? BK_EVENT_KEY_PRESS : BK_EVENT_KEY_RELEASE;
	ev->key_code = key;
	wl_queue_event(ev);
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *kb,
                                       uint32_t serial, uint32_t mods_depressed,
                                       uint32_t mods_latched, uint32_t mods_locked,
                                       uint32_t group)
{
	(void)data; (void)kb; (void)serial;
	(void)mods_depressed; (void)mods_latched; (void)mods_locked; (void)group;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
};

// ============================================================================
// Output listener
// ============================================================================

static void output_handle_geometry(void *data, struct wl_output *output,
                                    int x, int y, int pw, int ph, int subp,
                                    const char *make, const char *model, int tr)
{
	(void)data; (void)output; (void)x; (void)y; (void)subp; (void)tr;
	for (int j = 0; j < wl->num_monitors; j++) {
		if (wl->monitors[j].output == output) {
			wl->monitors[j].info.phys_width = pw;
			wl->monitors[j].info.phys_height = ph;
			g_free(wl->monitors[j].info.manufacturer);
			wl->monitors[j].info.manufacturer = g_strdup(make);
			g_free(wl->monitors[j].info.model);
			wl->monitors[j].info.model = g_strdup(model);
			return;
		}
	}
}

static void output_handle_mode(void *data, struct wl_output *output,
                                uint32_t flags, int w, int h, int refresh)
{
	(void)data;
	for (int j = 0; j < wl->num_monitors; j++) {
		if (wl->monitors[j].output == output && (flags & WL_OUTPUT_MODE_CURRENT)) {
			wl->monitors[j].info.width = w;
			wl->monitors[j].info.height = h;
			wl->monitors[j].info.refresh_mhz = refresh;
			return;
		}
	}
}

static void output_handle_done(void *data, struct wl_output *output) { (void)data; (void)output; }
static void output_handle_scale(void *data, struct wl_output *output, int32_t f) { (void)data; (void)output; (void)f; }

static const struct wl_output_listener output_listener = {
	.geometry = output_handle_geometry,
	.mode = output_handle_mode,
	.done = output_handle_done,
	.scale = output_handle_scale,
};

// ============================================================================
// xdg-output listener
// ============================================================================

static void xdg_output_handle_logical_position(void *data, struct zxdg_output_v1 *xout,
                                                int32_t x, int32_t y)
{
	for (int i = 0; i < wl->num_monitors; i++) {
		if (wl->monitors[i].xdg_output == xout) {
			wl->monitors[i].info.x = x;
			wl->monitors[i].info.y = y;
			return;
		}
	}
}

static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xout,
                                            int32_t w, int32_t h)
{
	for (int i = 0; i < wl->num_monitors; i++) {
		if (wl->monitors[i].xdg_output == xout) {
			wl->monitors[i].info.width = w;
			wl->monitors[i].info.height = h;
			return;
		}
	}
}

static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xout) { (void)data; (void)xout; }
static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xout, const char *name)
{
	for (int i = 0; i < wl->num_monitors; i++) {
		if (wl->monitors[i].xdg_output == xout) {
			g_strfreev(wl->monitors[i].info.names);
			wl->monitors[i].info.names = g_new0(gchar*, 2);
			wl->monitors[i].info.names[0] = g_strdup(name);
			return;
		}
	}
}

static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xout,
                                           const char *desc) { (void)data; (void)xout; (void)desc; }

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xdg_output_handle_logical_position,
	.logical_size = xdg_output_handle_logical_size,
	.done = xdg_output_handle_done,
	.name = xdg_output_handle_name,
	.description = xdg_output_handle_description,
};

// ============================================================================
// Layer surface listener
// ============================================================================

static void layer_surface_handle_configure(void *data,
    struct zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t w, uint32_t h)
{
	BkWindow *win = (BkWindow *)data;
	win->configured_width = (int)w;
	win->configured_height = (int)h;
	win->configured = TRUE;
	
	zwlr_layer_surface_v1_ack_configure(surface, serial);

	if (w > 0 && h > 0 && (win->width != (int)w || win->height != (int)h)) {
		win->width = (int)w;
		win->height = (int)h;
		// Buffer will be recreated in cairo_surface_create_for_window
		if (win->buffer) { wl_buffer_destroy(win->buffer); win->buffer = NULL; }
		if (win->shm_data && win->shm_size > 0) { munmap(win->shm_data, win->shm_size); win->shm_data = NULL; win->shm_size = 0; }
		if (win->cairo_surface) { cairo_surface_destroy(win->cairo_surface); win->cairo_surface = NULL; }
	}

	BkEvent *ev = calloc(1, sizeof(BkEvent));
	ev->type = BK_EVENT_CONFIGURE;
	ev->window = win;
	ev->configure_width = (int)w;
	ev->configure_height = (int)h;
	wl_queue_event(ev);
}

static void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
	BkWindow *win = (BkWindow *)data; (void)surface;
	win->closed = TRUE;
	BkEvent *ev = calloc(1, sizeof(BkEvent));
	ev->type = BK_EVENT_DESTROY;
	ev->window = win;
	wl_queue_event(ev);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_handle_configure,
	.closed = layer_surface_handle_closed,
};

// ============================================================================
// Foreign toplevel handle listener
// ============================================================================

static int wl_find_toplevel(struct zwlr_foreign_toplevel_handle_v1 *h)
{
	for (int i = 0; i < wl_num_toplevels; i++)
		if (wl_toplevels[i] && wl_toplevels[i]->handle == h) return i;
	return -1;
}

static void toplevel_handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
                                   const char *title)
{
	(void)data;
	int i = wl_find_toplevel(h); if (i < 0) return;
	g_free(wl_toplevels[i]->title);
	wl_toplevels[i]->title = g_strdup(title);
	BkEvent *ev = calloc(1, sizeof(BkEvent));
	ev->type = BK_EVENT_TOPLEVEL_CHANGED;
	wl_queue_event(ev);
}

static void toplevel_handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
                                    const char *app_id)
{
	(void)data;
	int i = wl_find_toplevel(h); if (i < 0) return;
	g_free(wl_toplevels[i]->app_id);
	wl_toplevels[i]->app_id = g_strdup(app_id);
}

static void toplevel_handle_output_enter(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *output)
{
	(void)data;
	int i = wl_find_toplevel(h); if (i < 0) return;
	for (int j = 0; j < wl->num_monitors; j++) {
		if (wl->monitors[j].output == output) {
			wl_toplevels[i]->monitor = j;
			return;
		}
	}
}

static void toplevel_handle_output_leave(void *data,
    struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *output)
{
	(void)data; (void)h; (void)output;
}

static void toplevel_handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
                                   struct wl_array *state_array)
{
	(void)data;
	int i = wl_find_toplevel(h); if (i < 0) return;
	wl_toplevels[i]->maximized = FALSE;
	wl_toplevels[i]->minimized = FALSE;
	wl_toplevels[i]->activated = FALSE;
	wl_toplevels[i]->fullscreen = FALSE;

	uint32_t *states = state_array->data;
	size_t n = state_array->size / sizeof(uint32_t);
	for (size_t s = 0; s < n; s++) {
		switch (states[s]) {
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED: wl_toplevels[i]->maximized = TRUE; break;
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED: wl_toplevels[i]->minimized = TRUE; break;
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED: wl_toplevels[i]->activated = TRUE; break;
		case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN: wl_toplevels[i]->fullscreen = TRUE; break;
		}
	}

	BkEvent *ev = calloc(1, sizeof(BkEvent));
	ev->type = BK_EVENT_TOPLEVEL_CHANGED;
	wl_queue_event(ev);
}

static void toplevel_handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	(void)data; (void)h;
}

static void toplevel_handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	(void)data;
	int i = wl_find_toplevel(h); if (i < 0) return;
	BkEvent *ev = calloc(1, sizeof(BkEvent));
	ev->type = BK_EVENT_TOPLEVEL_REMOVED;
	wl_queue_event(ev);
	g_free(wl_toplevels[i]->title);
	g_free(wl_toplevels[i]->app_id);
	g_free(wl_toplevels[i]);
	wl_toplevels[i] = NULL;
	zwlr_foreign_toplevel_handle_v1_destroy(h);
}

static void toplevel_handle_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
                                    struct zwlr_foreign_toplevel_handle_v1 *parent)
{
	(void)data; (void)h; (void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_listener = {
	.title = toplevel_handle_title,
	.app_id = toplevel_handle_app_id,
	.output_enter = toplevel_handle_output_enter,
	.output_leave = toplevel_handle_output_leave,
	.state = toplevel_handle_state,
	.done = toplevel_handle_done,
	.closed = toplevel_handle_closed,
	.parent = toplevel_handle_parent,
};

// ============================================================================
// Foreign toplevel manager listener
// ============================================================================

static void toplevel_manager_handle_toplevel(void *data,
    struct zwlr_foreign_toplevel_manager_v1 *mgr,
    struct zwlr_foreign_toplevel_handle_v1 *handle)
{
	(void)data; (void)mgr;

	int idx = -1;
	for (int i = 0; i < WL_MAX_TOPLEVELS; i++) {
		if (!wl_toplevels[i]) { idx = i; break; }
	}
	if (idx < 0) return;

	wl_toplevels[idx] = calloc(1, sizeof(WlTopLevel));
	wl_toplevels[idx]->handle = handle;
	wl_toplevels[idx]->monitor = -1;
	if (idx >= wl_num_toplevels) wl_num_toplevels = idx + 1;

	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_handle_listener, NULL);

	BkEvent *ev = calloc(1, sizeof(BkEvent));
	ev->type = BK_EVENT_TOPLEVEL_ADDED;
	wl_queue_event(ev);
}

static void toplevel_manager_handle_finished(void *data,
    struct zwlr_foreign_toplevel_manager_v1 *mgr)
{
	(void)data; (void)mgr;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_listener = {
	.toplevel = toplevel_manager_handle_toplevel,
	.finished = toplevel_manager_handle_finished,
};

// ============================================================================
// Lifecycle
// ============================================================================

static int wl_backend_init(int *argc, char ***argv)
{
	(void)argc; (void)argv;

	const char *display_name = getenv("WAYLAND_DISPLAY");
	if (!display_name) display_name = "wayland-0";

	wl = calloc(1, sizeof(WlDisplay));
	if (!wl) return 1;

	wl->display = wl_display_connect(display_name);
	if (!wl->display) {
		fprintf(stderr, "tint2: Cannot connect to Wayland display '%s'.\n", display_name);
		free(wl); wl = NULL; return 1;
	}

	wl->connection_fd = wl_display_get_fd(wl->display);
	wl->running = TRUE;

	wl->registry = wl_display_get_registry(wl->display);
	wl_registry_add_listener(wl->registry, &registry_listener, NULL);
	wl_display_roundtrip(wl->display);

	if (!wl->compositor) { fprintf(stderr, "tint2: missing wl_compositor\n"); goto fail; }
	if (!wl->shm)        { fprintf(stderr, "tint2: missing wl_shm\n"); goto fail; }
	if (!wl->has_layer_shell) {
		fprintf(stderr, "tint2: wlr-layer-shell not available. Need Sway/labwc/river.\n");
		goto fail;
	}

	if (wl->seat) {
		wl_seat_add_listener(wl->seat, &seat_listener, NULL);
	}

	for (int i = 0; i < wl->num_monitors; i++) {
		wl_output_add_listener(wl->monitors[i].output, &output_listener, NULL);
		if (wl->xdg_output_manager) {
			wl->monitors[i].xdg_output = zxdg_output_manager_v1_get_xdg_output(
				wl->xdg_output_manager, wl->monitors[i].output);
			zxdg_output_v1_add_listener(wl->monitors[i].xdg_output,
				&xdg_output_listener, NULL);
		}
	}

	if (wl->toplevel_manager) {
		zwlr_foreign_toplevel_manager_v1_add_listener(wl->toplevel_manager,
			&toplevel_manager_listener, NULL);
	}

	wl_display_roundtrip(wl->display);

	fprintf(stderr, "tint2: Wayland backend initialized. %d monitors, "
	        "layer-shell=%s, toplevel-mgr=%s\n",
	        wl->num_monitors,
	        wl->has_layer_shell ? "yes" : "no",
	        wl->has_toplevel_manager ? "yes" : "no");
	return 0;

fail:
	wl_backend_cleanup();
	return 1;
}

static void wl_backend_cleanup(void)
{
	if (!wl) return;
	wl->running = FALSE;

	for (int i = 0; i < wl_num_toplevels; i++) {
		if (wl_toplevels[i]) {
			g_free(wl_toplevels[i]->title);
			g_free(wl_toplevels[i]->app_id);
			if (wl_toplevels[i]->handle)
				zwlr_foreign_toplevel_handle_v1_destroy(wl_toplevels[i]->handle);
			g_free(wl_toplevels[i]);
			wl_toplevels[i] = NULL;
		}
	}
	wl_num_toplevels = 0;

	for (int i = 0; i < wl->num_monitors; i++) {
		if (wl->monitors[i].xdg_output)
			zxdg_output_v1_destroy(wl->monitors[i].xdg_output);
		if (wl->monitors[i].output)
			wl_output_destroy(wl->monitors[i].output);
		g_strfreev(wl->monitors[i].info.names);
		g_free(wl->monitors[i].info.model);
		g_free(wl->monitors[i].info.manufacturer);
	}

	if (wl->pointer)   wl_pointer_destroy(wl->pointer);
	if (wl->keyboard)  wl_keyboard_destroy(wl->keyboard);
	if (wl->seat)      wl_seat_destroy(wl->seat);
	if (wl->toplevel_manager)
		zwlr_foreign_toplevel_manager_v1_destroy(wl->toplevel_manager);
	if (wl->xdg_output_manager)
		zxdg_output_manager_v1_destroy(wl->xdg_output_manager);
	if (wl->layer_shell)
		zwlr_layer_shell_v1_destroy(wl->layer_shell);
	if (wl->shm)        wl_shm_destroy(wl->shm);
	if (wl->compositor) wl_compositor_destroy(wl->compositor);
	if (wl->registry)   wl_registry_destroy(wl->registry);

	if (wl->display) {
		wl_display_flush(wl->display);
		wl_display_disconnect(wl->display);
	}

	g_free(wl->seat_name);
	g_list_free(wl->panel_windows);
	free(wl);
	wl = NULL;
}

static BackendType wl_backend_type(void) { return BACKEND_WAYLAND; }
static BkDisplay* wl_get_display(void) { return (BkDisplay*)wl; }

// ============================================================================
// Event loop
// ============================================================================

static int wl_get_fd(void) { return wl ? wl->connection_fd : -1; }

static void wl_flush(void) {
	if (wl) wl_display_flush(wl->display);
}

static gboolean wl_events_pending(void)
{
	if (!wl) return FALSE;
	struct pollfd pfd = { .fd = wl->connection_fd, .events = POLLIN };
	wl_display_flush(wl->display);
	return (poll(&pfd, 1, 0) > 0);
}

static BkEvent* wl_wait_event(void)
{
	if (!wl) return NULL;
	if (!g_queue_is_empty(wl_event_queue)) return g_queue_pop_head(wl_event_queue);

	while (wl->running) {
		if (wl_display_dispatch(wl->display) < 0) { wl->running = FALSE; return NULL; }
		if (!g_queue_is_empty(wl_event_queue)) return g_queue_pop_head(wl_event_queue);
	}
	return NULL;
}

static BkEvent* wl_poll_event(void)
{
	if (!wl) return NULL;
	if (wl_events_pending()) {
		wl_display_read_events(wl->display);
		if (wl_display_dispatch_pending(wl->display) < 0) { wl->running = FALSE; return NULL; }
	} else {
		wl_display_cancel_read(wl->display);
	}
	if (!g_queue_is_empty(wl_event_queue)) return g_queue_pop_head(wl_event_queue);
	return NULL;
}

static void wl_event_free(BkEvent *event)
{
	if (!event) return;
	g_free(event->systray_message_data);
	g_free(event);
}

static int wl_run(void) {
	while (wl && wl->running) {
		if (wl_display_dispatch(wl->display) < 0) break;
	}
	return 0;
}

// ============================================================================
// Window management
// ============================================================================

static BkWindow* wl_window_create(BkWindow *parent, int x, int y, int width, int height,
                                   int depth, BkVisual *visual, BkLayer layer,
                                   BkAnchor anchor, int exclusive_zone)
{
	(void)parent; (void)depth; (void)visual;
	if (!wl || !wl->layer_shell || !wl->compositor) return NULL;

	BkWindow *win = calloc(1, sizeof(BkWindow));
	win->wl = wl;
	win->width = width;
	win->height = height;

	win->surface = wl_compositor_create_surface(wl->compositor);

	int mon_idx = 0;
	if (wl->num_monitors > 0) {
		for (int i = 0; i < wl->num_monitors; i++) {
			BkMonitor *m = &wl->monitors[i].info;
			if (x >= m->x && x < m->x + m->width && y >= m->y && y < m->y + m->height) {
				mon_idx = i; break;
			}
		}
	}

	struct wl_output *output = wl->monitors[mon_idx].output;

	uint32_t wl_layer;
	switch (layer) {
	case BK_LAYER_BACKGROUND: wl_layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND; break;
	case BK_LAYER_BOTTOM:     wl_layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM; break;
	case BK_LAYER_TOP:        wl_layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP; break;
	case BK_LAYER_OVERLAY:    wl_layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; break;
	default:                  wl_layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM; break;
	}

	win->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		wl->layer_shell, win->surface, output, wl_layer, "panel");
	zwlr_layer_surface_v1_add_listener(win->layer_surface, &layer_surface_listener, win);

	uint32_t wl_anchor = 0;
	if (anchor & BK_ANCHOR_TOP)    wl_anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	if (anchor & BK_ANCHOR_BOTTOM) wl_anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	if (anchor & BK_ANCHOR_LEFT)   wl_anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	if (anchor & BK_ANCHOR_RIGHT)  wl_anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	zwlr_layer_surface_v1_set_anchor(win->layer_surface, wl_anchor);
	zwlr_layer_surface_v1_set_size(win->layer_surface, width, height);
	zwlr_layer_surface_v1_set_exclusive_zone(win->layer_surface, exclusive_zone);
	zwlr_layer_surface_v1_set_margin(win->layer_surface, 0, 0, 0, 0);
	zwlr_layer_surface_v1_set_keyboard_interactivity(win->layer_surface,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

	win->output = output;
	wl_surface_commit(win->surface);
	wl_display_roundtrip(wl->display);

	wl->panel_windows = g_list_append(wl->panel_windows, win);
	return win;
}

static void wl_window_destroy(BkWindow *win)
{
	if (!win) return;
	wl->panel_windows = g_list_remove(wl->panel_windows, win);
	if (win->cairo_surface) cairo_surface_destroy(win->cairo_surface);
	if (win->buffer) wl_buffer_destroy(win->buffer);
	if (win->shm_data && win->shm_size > 0) munmap(win->shm_data, win->shm_size);
	if (win->layer_surface) zwlr_layer_surface_v1_destroy(win->layer_surface);
	if (win->surface) wl_surface_destroy(win->surface);
	free(win);
}

static void wl_window_show(BkWindow *win) {
	if (!win || !win->buffer) return;
	wl_surface_attach(win->surface, win->buffer, 0, 0);
	wl_surface_damage_buffer(win->surface, 0, 0, win->width, win->height);
	wl_surface_commit(win->surface);
}

static void wl_window_hide(BkWindow *win) {
	if (!win) return;
	wl_surface_attach(win->surface, NULL, 0, 0);
	wl_surface_commit(win->surface);
}

static void wl_window_move(BkWindow *win, int x, int y) { (void)win; (void)x; (void)y; }

static void wl_window_resize(BkWindow *win, int w, int h) {
	if (!win) return;
	win->width = w; win->height = h;
	if (win->layer_surface) {
		zwlr_layer_surface_v1_set_size(win->layer_surface, w, h);
		wl_surface_commit(win->surface);
	}
}

static void wl_window_move_resize(BkWindow *win, int x, int y, int w, int h)
	{ wl_window_move(win, x, y); wl_window_resize(win, w, h); }
static void wl_window_set_title(BkWindow *win, const char *title) { (void)win; (void)title; }

static void wl_window_set_input_region(BkWindow *win, cairo_region_t *region)
{
	if (!win || !wl || !wl->compositor) return;
	struct wl_region *wlr = NULL;
	if (region) {
			wlr = wl_compositor_create_region(wl->compositor);
			int n = cairo_region_num_rectangles(region);
			for (int i = 0; i < n; i++) {
				cairo_rectangle_int_t r;
				cairo_region_get_rectangle(region, i, &r);
				wl_region_add(wlr, r.x, r.y, r.width, r.height);
			}
		}
	wl_surface_set_input_region(win->surface, wlr);
	if (wlr) wl_region_destroy(wlr);
}

static void wl_window_set_opacity(BkWindow *win, double o) { (void)win; (void)o; }
static unsigned long wl_window_get_id(BkWindow *win) { return win ? (unsigned long)(uintptr_t)win->surface : 0; }

// ============================================================================
// Pixmap
// ============================================================================

static BkPixmap* wl_pixmap_create(int w, int h, int depth) {
	(void)depth;
	BkPixmap *pix = calloc(1, sizeof(BkPixmap));
	pix->width = w; pix->height = h;
	pix->stride = w * 4;
	pix->data_size = pix->stride * h;
	pix->data = calloc(1, pix->data_size);
	pix->wl = wl;
	pix->surface = cairo_image_surface_create_for_data(pix->data, CAIRO_FORMAT_ARGB32, w, h, pix->stride);
	return pix;
}

static void wl_pixmap_destroy(BkPixmap *p) {
	if (!p) return;
	if (p->surface) cairo_surface_destroy(p->surface);
	free(p->data); free(p);
}

static void wl_pixmap_get_size(BkPixmap *p, int *w, int *h) {
	if (p) { *w = p->width; *h = p->height; }
	else { *w = 0; *h = 0; }
}

// ============================================================================
// Cairo
// ============================================================================

static cairo_surface_t* wl_cairo_surface_create_for_pixmap(BkPixmap *p, int w, int h)
	{ (void)w; (void)h; return p && p->surface ? cairo_surface_reference(p->surface) : NULL; }

static cairo_surface_t* wl_cairo_surface_create_for_window(BkWindow *win, int w, int h)
{
	if (!win) return NULL;

	// Always destroy old buffer and create fresh to avoid buffer release issues
	if (win->cairo_surface) { cairo_surface_destroy(win->cairo_surface); win->cairo_surface = NULL; }
	if (win->buffer) { wl_buffer_destroy(win->buffer); win->buffer = NULL; }
	if (win->shm_data && win->shm_size > 0) { munmap(win->shm_data, win->shm_size); win->shm_data = NULL; win->shm_size = 0; }

	win->buffer = wl_create_shm_buffer(w, h, &win->shm_data, &win->shm_size);
	if (!win->buffer) return NULL;

	win->cairo_surface = cairo_image_surface_create_for_data(
		win->shm_data, CAIRO_FORMAT_ARGB32, w, h, w * 4);
	win->width = w; win->height = h;
	return cairo_surface_reference(win->cairo_surface);
}

// ============================================================================
// Rendering
// ============================================================================

static void wl_pixmap_copy_area(BkPixmap *src, BkPixmap *dst,
                                 int sx, int sy, int w, int h, int dx, int dy)
{
	if (!src || !dst || !src->data || !dst->data) return;
	int sstride = src->stride, dstride = dst->stride;
	uint8_t *sp = (uint8_t*)src->data + sy * sstride + sx * 4;
	uint8_t *dp = (uint8_t*)dst->data + dy * dstride + dx * 4;
	for (int r = 0; r < h; r++) { memcpy(dp, sp, w * 4); sp += sstride; dp += dstride; }
}

static void wl_window_present(BkWindow *win)
{
	if (!win || !win->surface || !win->buffer) return;
	cairo_surface_flush(win->cairo_surface);
	wl_surface_attach(win->surface, win->buffer, 0, 0);
	wl_surface_damage_buffer(win->surface, 0, 0, win->width, win->height);
	wl_surface_commit(win->surface);
}

// ============================================================================
// Visual
// ============================================================================

static BkVisual* wl_get_default_visual(int depth) { (void)depth; return NULL; }
static void wl_visual_free(BkVisual *v) { free(v); }

// ============================================================================
// Monitor
// ============================================================================

static int wl_get_monitor_count(void) { return wl ? wl->num_monitors : 0; }

static BkMonitor* wl_get_monitor(int i) {
	if (!wl || i < 0 || i >= wl->num_monitors) return NULL;
	BkMonitor *m = calloc(1, sizeof(BkMonitor));
	memcpy(m, &wl->monitors[i].info, sizeof(BkMonitor));
	m->names = g_strdupv(wl->monitors[i].info.names);
	m->model = g_strdup(wl->monitors[i].info.model);
	m->manufacturer = g_strdup(wl->monitors[i].info.manufacturer);
	return m;
}

static void wl_monitor_free(BkMonitor *m) {
	if (!m) return;
	g_strfreev(m->names); g_free(m->model); g_free(m->manufacturer); free(m);
}

static BkMonitor** wl_get_monitors(int *c) {
	if (!wl) { *c = 0; return NULL; }
	*c = wl->num_monitors;
	BkMonitor **list = calloc(*c + 1, sizeof(BkMonitor*));
	for (int i = 0; i < *c; i++) list[i] = wl_get_monitor(i);
	return list;
}

static void wl_monitor_free_list(BkMonitor **m, int c) {
	if (!m) return;
	for (int i = 0; i < c; i++) wl_monitor_free(m[i]);
	free(m);
}

// ============================================================================
// Desktop
// ============================================================================

static int wl_get_desktop_count(void) { return 1; }
static int wl_get_current_desktop(void) { return 0; }
static void wl_set_current_desktop(int d) { (void)d; }
static char** wl_get_desktop_names(int *c) {
	char **n = calloc(2, sizeof(char*));
	n[0] = strdup("Workspace 1"); *c = 1; return n;
}

// ============================================================================
// TopLevel
// ============================================================================

static BkTopLevel** wl_get_toplevels(int *count) {
	if (!wl) { *count = 0; return NULL; }
	BkTopLevel **list = calloc(wl_num_toplevels + 1, sizeof(BkTopLevel*));
	int n = 0;
	for (int i = 0; i < wl_num_toplevels; i++) {
		if (!wl_toplevels[i]) continue;
		BkTopLevel *tl = calloc(1, sizeof(BkTopLevel));
		tl->title = g_strdup(wl_toplevels[i]->title ? wl_toplevels[i]->title : "");
		tl->app_id = g_strdup(wl_toplevels[i]->app_id ? wl_toplevels[i]->app_id : "");
		tl->handle = wl_toplevels[i]->handle;
		tl->desktop = 0;
		tl->monitor = wl_toplevels[i]->monitor;
		if (wl_toplevels[i]->minimized) tl->state = BK_TOPLEVEL_STATE_ICONIFIED;
		else if (wl_toplevels[i]->activated) tl->state = BK_TOPLEVEL_STATE_ACTIVE;
		tl->is_active = wl_toplevels[i]->activated;
		list[n++] = tl;
	}
	*count = n;
	return list;
}

static void wl_toplevel_free(BkTopLevel *tl) {
	if (!tl) return;
	g_free(tl->title); g_free(tl->app_id); g_free(tl->icon_data); g_free(tl->window); g_free(tl);
}

static void wl_toplevel_free_list(BkTopLevel **tls, int c) {
	if (!tls) return;
	for (int i = 0; i < c; i++) wl_toplevel_free(tls[i]);
	free(tls);
}

static void wl_toplevel_activate(BkTopLevel *tl) {
	if (!wl || !wl->seat || !tl) return;
	for (int i = 0; i < wl_num_toplevels; i++)
		if (wl_toplevels[i] && wl_toplevels[i]->handle == tl->handle)
			zwlr_foreign_toplevel_handle_v1_activate(wl_toplevels[i]->handle, wl->seat);
}

static void wl_toplevel_close(BkTopLevel *tl) {
	if (!wl || !tl) return;
	for (int i = 0; i < wl_num_toplevels; i++)
		if (wl_toplevels[i] && wl_toplevels[i]->handle == tl->handle)
			zwlr_foreign_toplevel_handle_v1_close(wl_toplevels[i]->handle);
}

static void wl_toplevel_minimize(BkTopLevel *tl) {
	if (!wl || !tl) return;
	for (int i = 0; i < wl_num_toplevels; i++)
		if (wl_toplevels[i] && wl_toplevels[i]->handle == tl->handle)
			zwlr_foreign_toplevel_handle_v1_set_minimized(wl_toplevels[i]->handle);
}

static void wl_toplevel_toggle_maximize(BkTopLevel *tl) {
	if (!wl || !tl) return;
	for (int i = 0; i < wl_num_toplevels; i++)
		if (wl_toplevels[i] && wl_toplevels[i]->handle == tl->handle) {
			if (wl_toplevels[i]->maximized)
				zwlr_foreign_toplevel_handle_v1_unset_maximized(wl_toplevels[i]->handle);
			else
				zwlr_foreign_toplevel_handle_v1_set_maximized(wl_toplevels[i]->handle);
		}
}

static void wl_toplevel_toggle_shade(BkTopLevel *tl) { (void)tl; }
static void wl_toplevel_set_desktop(BkTopLevel *tl, int d) { (void)tl; (void)d; }

static BkTopLevel* wl_get_active_toplevel(void) {
	if (!wl) return NULL;
	for (int i = 0; i < wl_num_toplevels; i++) {
		if (wl_toplevels[i] && wl_toplevels[i]->activated) {
			BkTopLevel *tl = calloc(1, sizeof(BkTopLevel));
			tl->title = g_strdup(wl_toplevels[i]->title);
			tl->app_id = g_strdup(wl_toplevels[i]->app_id);
			tl->handle = wl_toplevels[i]->handle;
			tl->is_active = TRUE;
			return tl;
		}
	}
	return NULL;
}

static Imlib_Image wl_toplevel_get_icon(BkTopLevel *tl, int sz) { (void)tl; (void)sz; return NULL; }
static void wl_toplevel_get_geometry(BkTopLevel *tl, int *x, int *y, int *w, int *h)
	{ *x = *y = *w = *h = 0; (void)tl; }
static int wl_toplevel_get_pid(BkTopLevel *tl) { (void)tl; return 0; }
static gboolean wl_toplevel_is_iconified(BkTopLevel *tl) { return tl && tl->state == BK_TOPLEVEL_STATE_ICONIFIED; }
static gboolean wl_toplevel_is_urgent(BkTopLevel *tl) { (void)tl; return FALSE; }
static gboolean wl_toplevel_is_hidden(BkTopLevel *tl) { return tl ? tl->skip_taskbar : TRUE; }
static gboolean wl_toplevel_is_active(BkTopLevel *tl) { return tl ? tl->is_active : FALSE; }

// ============================================================================
// Systray (stubs)
// ============================================================================

static int wl_systray_init(int m) { (void)m; return 0; }
static void wl_systray_cleanup(void) {}
static gboolean wl_systray_is_icon(BkWindow *w) { (void)w; return FALSE; }
static BkSystrayIcon* wl_systray_find_icon(BkWindow *w) { (void)w; return NULL; }
static GSList* wl_systray_get_icons(void) { return NULL; }
static int wl_systray_embed_icon(BkSystrayIcon *i, BkWindow *p) { (void)i; (void)p; return 0; }
static void wl_systray_remove_icon(BkSystrayIcon *i) { (void)i; }
static void wl_systray_send_message(BkWindow *ic, long m, long d1, long d2, long d3)
	{ (void)ic; (void)m; (void)d1; (void)d2; (void)d3; }
static Imlib_Image wl_systray_get_icon_image(BkSystrayIcon *ic) { (void)ic; return NULL; }
static void wl_systray_icon_free(BkSystrayIcon *ic) { (void)ic; }

// ============================================================================
// Transparency, cursor, DnD, misc
// ============================================================================

static gboolean wl_has_real_transparency(void) { return TRUE; }
static BkPixmap* wl_get_root_pixmap(void) { return NULL; }
static void wl_window_set_cursor(BkWindow *win, const char *name) { (void)win; (void)name; }
static void wl_get_cursor_position(int *x, int *y) {
	*x = wl ? wl->cursor_x : 0; *y = wl ? wl->cursor_y : 0;
}
static void wl_dnd_start(BkWindow *src, const char *data) { (void)src; (void)data; }
static int wl_get_dpi(void) { return 96; }
static unsigned long wl_get_root_id(void) { return 0; }
static gboolean wl_is_panel_window(BkWindow *win) {
	return win && wl && g_list_find(wl->panel_windows, win) != NULL;
}

// ============================================================================
// VTable
// ============================================================================

const BackendVT wl_backend_vt = {
	.init = wl_backend_init,
	.cleanup = wl_backend_cleanup,
	.type = wl_backend_type,
	.get_display = wl_get_display,
	.get_fd = wl_get_fd,
	.flush = wl_flush,
	.events_pending = wl_events_pending,
	.wait_event = wl_wait_event,
	.poll_event = wl_poll_event,
	.event_free = wl_event_free,
	.run = wl_run,
	.window_create = wl_window_create,
	.window_destroy = wl_window_destroy,
	.window_show = wl_window_show,
	.window_hide = wl_window_hide,
	.window_move = wl_window_move,
	.window_resize = wl_window_resize,
	.window_move_resize = wl_window_move_resize,
	.window_set_title = wl_window_set_title,
	.window_set_input_region = wl_window_set_input_region,
	.window_set_opacity = wl_window_set_opacity,
	.window_get_id = wl_window_get_id,
	.pixmap_create = wl_pixmap_create,
	.pixmap_destroy = wl_pixmap_destroy,
	.pixmap_get_size = wl_pixmap_get_size,
	.cairo_surface_create_for_pixmap = wl_cairo_surface_create_for_pixmap,
	.cairo_surface_create_for_window = wl_cairo_surface_create_for_window,
	.pixmap_copy_area = wl_pixmap_copy_area,
	.window_present = wl_window_present,
	.get_default_visual = wl_get_default_visual,
	.visual_free = wl_visual_free,
	.get_monitor_count = wl_get_monitor_count,
	.get_monitor = wl_get_monitor,
	.monitor_free = wl_monitor_free,
	.get_monitors = wl_get_monitors,
	.monitor_free_list = wl_monitor_free_list,
	.get_desktop_count = wl_get_desktop_count,
	.get_current_desktop = wl_get_current_desktop,
	.set_current_desktop = wl_set_current_desktop,
	.get_desktop_names = wl_get_desktop_names,
	.get_toplevels = wl_get_toplevels,
	.toplevel_free = wl_toplevel_free,
	.toplevel_free_list = wl_toplevel_free_list,
	.toplevel_activate = wl_toplevel_activate,
	.toplevel_close = wl_toplevel_close,
	.toplevel_minimize = wl_toplevel_minimize,
	.toplevel_toggle_maximize = wl_toplevel_toggle_maximize,
	.toplevel_toggle_shade = wl_toplevel_toggle_shade,
	.toplevel_set_desktop = wl_toplevel_set_desktop,
	.get_active_toplevel = wl_get_active_toplevel,
	.toplevel_get_icon = wl_toplevel_get_icon,
	.toplevel_get_geometry = wl_toplevel_get_geometry,
	.toplevel_get_pid = wl_toplevel_get_pid,
	.toplevel_is_iconified = wl_toplevel_is_iconified,
	.toplevel_is_urgent = wl_toplevel_is_urgent,
	.toplevel_is_hidden = wl_toplevel_is_hidden,
	.toplevel_is_active = wl_toplevel_is_active,
	.systray_init = wl_systray_init,
	.systray_cleanup = wl_systray_cleanup,
	.systray_is_icon = wl_systray_is_icon,
	.systray_find_icon = wl_systray_find_icon,
	.systray_get_icons = wl_systray_get_icons,
	.systray_embed_icon = wl_systray_embed_icon,
	.systray_remove_icon = wl_systray_remove_icon,
	.systray_send_message = wl_systray_send_message,
	.systray_get_icon_image = wl_systray_get_icon_image,
	.systray_icon_free = wl_systray_icon_free,
	.has_real_transparency = wl_has_real_transparency,
	.get_root_pixmap = wl_get_root_pixmap,
	.window_set_cursor = wl_window_set_cursor,
	.get_cursor_position = wl_get_cursor_position,
	.dnd_start = wl_dnd_start,
	.get_dpi = wl_get_dpi,
	.get_root_id = wl_get_root_id,
	.is_panel_window = wl_is_panel_window,
};
