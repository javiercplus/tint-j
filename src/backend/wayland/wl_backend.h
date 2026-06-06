/**************************************************************************
 * wl_backend.h
 *
 * Wayland backend internal header.
 *
 * Copyright (C) 2024 tint-j project
 **************************************************************************/

#ifndef WL_BACKEND_H
#define WL_BACKEND_H

#include <wayland-client.h>
#include <cairo.h>
#include <sys/mman.h>

// Generated protocol headers
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include "backend/backend.h"

// Maximum number of monitors
#define WL_MAX_MONITORS 16

typedef struct {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;

	// wlr-layer-shell (for panels)
	struct zwlr_layer_shell_v1 *layer_shell;

	// xdg-output (for monitor info)
	struct zxdg_output_manager_v1 *xdg_output_manager;

	// wlr-foreign-toplevel-management (for taskbar)
	struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager;

	// Seat
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	char *seat_name;

	// Monitors
	int num_monitors;
	struct {
		struct wl_output *output;
		struct zxdg_output_v1 *xdg_output;
		BkMonitor info;
		int wl_name;
	} monitors[WL_MAX_MONITORS];

	// Connection fd
	int connection_fd;

	// Running flag
	gboolean running;

	// Compositor supports: layer-shell, foreign-toplevel, xdg-output
	gboolean has_layer_shell;
	gboolean has_toplevel_manager;
	gboolean has_xdg_output;

	// Cursor position
	int cursor_x, cursor_y;

	// Serial for grabs
	uint32_t pointer_serial;
	uint32_t pointer_button;

	struct wl_surface *focused_surface;

	// Panel windows registry
	GList *panel_windows;
} WlDisplay;

// Wayland window (panel surface)
struct BkWindow {
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_output *output;
	int configured_width;
	int configured_height;
	int pending_width;
	int pending_height;
	gboolean configured;
	gboolean closed;
	// Buffer
	struct wl_buffer *buffer;
	cairo_surface_t *cairo_surface;
	void *shm_data;
	size_t shm_size;
	int width, height;
	WlDisplay *wl;
};

// Wayland pixmap
struct BkPixmap {
	cairo_surface_t *surface;
	int width, height;
	int stride;
	void *data;
	size_t data_size;
	WlDisplay *wl;
};

// Global display
extern WlDisplay *wl;

#endif // WL_BACKEND_H
