/**************************************************************************
 * backend.h
 *
 * Abstract backend interface for tint2.
 * Supports X11 (via Xlib) and Wayland (via libwayland-client, wlroots protocols).
 *
 * All backend-specific types are opaque. The core tint2 code only uses
 * these abstract types and function pointers.
 *
 * Copyright (C) 2024 tint-j project
 **************************************************************************/

#ifndef BACKEND_H
#define BACKEND_H

#include <glib.h>
#include <cairo.h>
#include <pango/pangocairo.h>

// Forward-declare Imlib_Image to avoid circular X11 dependency
// when building with the compat layer (Wayland).
// The full Imlib2.h is included by the backend implementations.
#ifndef COMPAT_NO_IMLIB2
#include <Imlib2.h>
#else
// Must match Imlib2.h's typedef: typedef void *Imlib_Image;
typedef void *Imlib_Image;
#endif

// ============================================================================
// Backend type enumeration
// ============================================================================

typedef enum {
	BACKEND_X11 = 0,
	BACKEND_WAYLAND,
} BackendType;

// ============================================================================
// Opaque backend types
// ============================================================================

typedef struct BkDisplay    BkDisplay;    // wraps Display* (X11) or wl_display* (Wayland)
typedef struct BkWindow     BkWindow;     // wraps Window (X11) or wl_surface+zwlr_layer_surface (Wayland)
typedef struct BkPixmap     BkPixmap;     // wraps Pixmap (X11) or wl_buffer+shm (Wayland)
typedef struct BkMonitor    BkMonitor;    // monitor/screen info
typedef struct BkTopLevel   BkTopLevel;   // a client window/toplevel
typedef struct BkSystray    BkSystray;    // system tray context
typedef struct BkVisual     BkVisual;     // visual info for rendering

// ============================================================================
// Monitor information
// ============================================================================

struct BkMonitor {
	int x, y;
	int width, height;
	int refresh_mhz;        // refresh rate in millihertz
	int phys_width, phys_height; // physical size in mm
	gboolean primary;
	gchar **names;
	gchar *model;
	gchar *manufacturer;
};

// ============================================================================
// TopLevel (client window) information
// ============================================================================

typedef enum {
	BK_TOPLEVEL_STATE_NORMAL = 0,
	BK_TOPLEVEL_STATE_ACTIVE,
	BK_TOPLEVEL_STATE_ICONIFIED,
	BK_TOPLEVEL_STATE_URGENT,
} BkTopLevelState;

struct BkTopLevel {
	BkWindow    *window;
	char        *title;
	char        *app_id;         // WM_CLASS (X11) or app_id (Wayland)
	int          desktop;        // workspace/desktop number, or -1 for all
	int          monitor;        // monitor index
	int          x, y;
	int          width, height;
	BkTopLevelState state;
	gboolean     skip_taskbar;
	gboolean     is_active;
	// icon data (raw pixel array)
	int          icon_width, icon_height;
	gulong      *icon_data;
	gsize        icon_data_len;
	void        *handle;         // backend-specific handle
};

// ============================================================================
// Layer for panel windows
// ============================================================================

typedef enum {
	BK_LAYER_BACKGROUND = 0,
	BK_LAYER_BOTTOM,
	BK_LAYER_NORMAL,
	BK_LAYER_TOP,
	BK_LAYER_OVERLAY,
} BkLayer;

// ============================================================================
// Anchor edges for struts / exclusive zone (Wayland layer shell)
// ============================================================================

typedef enum {
	BK_ANCHOR_TOP    = (1 << 0),
	BK_ANCHOR_BOTTOM = (1 << 1),
	BK_ANCHOR_LEFT   = (1 << 2),
	BK_ANCHOR_RIGHT  = (1 << 3),
} BkAnchor;

// ============================================================================
// Event types
// ============================================================================

typedef enum {
	BK_EVENT_NONE = 0,
	BK_EVENT_EXPOSE,          // window needs redraw
	BK_EVENT_MOUSE_MOTION,    // mouse moved
	BK_EVENT_MOUSE_PRESS,     // button pressed
	BK_EVENT_MOUSE_RELEASE,   // button released
	BK_EVENT_MOUSE_SCROLL,    // scroll wheel
	BK_EVENT_KEY_PRESS,       // key pressed
	BK_EVENT_KEY_RELEASE,     // key released
	BK_EVENT_ENTER,           // mouse entered window
	BK_EVENT_LEAVE,           // mouse left window
	BK_EVENT_CONFIGURE,       // window resized/moved
	BK_EVENT_DESTROY,         // window destroyed
	BK_EVENT_CLIENT_MESSAGE,  // custom message
	BK_EVENT_PROPERTY_NOTIFY, // property changed
	BK_EVENT_TOPLEVEL_ADDED,       // new client window appeared
	BK_EVENT_TOPLEVEL_REMOVED,     // client window closed
	BK_EVENT_TOPLEVEL_CHANGED,     // client window properties changed
	BK_EVENT_MONITOR_CHANGED,      // monitor configuration changed
	BK_EVENT_DESKTOP_CHANGED,      // current desktop changed
	BK_EVENT_SYSTRAY_ICON_ADDED,   // new system tray icon
	BK_EVENT_SYSTRAY_ICON_REMOVED, // system tray icon removed
	BK_EVENT_SYSTRAY_MESSAGE,      // system tray message
} BkEventType;

// ============================================================================
// Mouse button identifiers
// ============================================================================

typedef enum {
	BK_BUTTON_LEFT = 1,
	BK_BUTTON_MIDDLE = 2,
	BK_BUTTON_RIGHT = 3,
	BK_SCROLL_UP = 4,
	BK_SCROLL_DOWN = 5,
	BK_SCROLL_LEFT = 6,
	BK_SCROLL_RIGHT = 7,
} BkButton;

// ============================================================================
// Modifier mask
// ============================================================================

typedef enum {
	BK_MOD_SHIFT   = (1 << 0),
	BK_MOD_CTRL    = (1 << 1),
	BK_MOD_ALT     = (1 << 2),
	BK_MOD_META    = (1 << 3),
} BkModifier;

// ============================================================================
// Generic event structure
// ============================================================================

typedef struct BkEvent {
	BkEventType type;
	BkWindow   *window;       // window that received the event

	// Mouse events
	int         mouse_x, mouse_y;
	BkButton    mouse_button;
	int         mouse_modifiers;

	// Scroll events
	double      scroll_dx, scroll_dy;

	// Configure events
	int         configure_x, configure_y;
	int         configure_width, configure_height;

	// TopLevel events
	BkTopLevel *toplevel;

	// Systray events
	BkWindow   *systray_icon;
	char       *systray_message_data;
	long        systray_message_l[5];

	// Keyboard
	unsigned int key_code;
	unsigned int key_state;
} BkEvent;

// ============================================================================
// Systray icon info
// ============================================================================

typedef struct BkSystrayIcon {
	BkWindow  *window;
	char      *name;
	int        width, height;
	int        x, y;
	gboolean   mapped;
	BkPixmap  *pixmap;
} BkSystrayIcon;

// ============================================================================
// Backend virtual function table (vtable)
// ============================================================================

typedef struct BackendVT {
	// ------------------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------------------

	// Initialize the backend. Returns 0 on success.
	int  (*init)(int *argc, char ***argv);

	// Shutdown and free all backend resources.
	void (*cleanup)(void);

	// Get the backend type.
	BackendType (*type)(void);

	// Get the display connection.
	BkDisplay* (*get_display)(void);

	// ------------------------------------------------------------------------
	// Event loop
	// ------------------------------------------------------------------------

	// Get the file descriptor for the event loop (for polling with select/poll).
	int  (*get_fd)(void);

	// Flush pending output.
	void (*flush)(void);

	// Check if events are pending.
	gboolean (*events_pending)(void);

	// Wait for and process the next event. Returns the event, or NULL if no event.
	// The caller must free the returned event with bk_event_free().
	BkEvent* (*wait_event)(void);

	// Poll for events (non-blocking). Returns the next event, or NULL.
	BkEvent* (*poll_event)(void);

	// Free an event returned by wait_event or poll_event.
	void (*event_free)(BkEvent *event);

	// Run the main event loop (not used by tint2, which has its own loop).
	// Returns the exit code.
	int  (*run)(void);

	// ------------------------------------------------------------------------
	// Window (panel surface) management
	// ------------------------------------------------------------------------

	// Create a window suitable for a panel (dock or layer shell).
	// parent: parent window or NULL for root.
	// x, y, width, height: geometry.
	// depth: color depth.
	// visual: desired visual or NULL for default.
	// layer: stacking layer.
	// anchor: which edges to anchor (Wayland layer shell) or 0.
	// exclusive_zone: size of the exclusive zone for struts (Wayland) or 0.
	// Returns the new window.
	BkWindow* (*window_create)(BkWindow *parent,
	                           int x, int y, int width, int height,
	                           int depth, BkVisual *visual,
	                           BkLayer layer,
	                           BkAnchor anchor,
	                           int exclusive_zone);

	// Destroy a window.
	void (*window_destroy)(BkWindow *win);

	// Show (map) a window.
	void (*window_show)(BkWindow *win);

	// Hide (unmap) a window.
	void (*window_hide)(BkWindow *win);

	// Move a window.
	void (*window_move)(BkWindow *win, int x, int y);

	// Resize a window.
	void (*window_resize)(BkWindow *win, int width, int height);

	// Move and resize a window in one operation.
	void (*window_move_resize)(BkWindow *win, int x, int y, int width, int height);

	// Set the window title/name.
	void (*window_set_title)(BkWindow *win, const char *title);

	// Set the input region of the window (for mouse input pass-through).
	// region: NULL to accept all input, or a list of rectangles.
	void (*window_set_input_region)(BkWindow *win, cairo_region_t *region);

	// Set window opacity (0.0 = fully transparent, 1.0 = fully opaque).
	void (*window_set_opacity)(BkWindow *win, double opacity);

	// Get the window's unique ID (for hashing/comparison).
	unsigned long (*window_get_id)(BkWindow *win);

	// ------------------------------------------------------------------------
	// Pixmap / Buffer management
	// ------------------------------------------------------------------------

	// Create a pixmap/buffer for off-screen rendering.
	// width, height: dimensions in pixels.
	// depth: color depth (32 for ARGB).
	BkPixmap* (*pixmap_create)(int width, int height, int depth);

	// Destroy a pixmap/buffer.
	void (*pixmap_destroy)(BkPixmap *pixmap);

	// Get the size of a pixmap.
	void (*pixmap_get_size)(BkPixmap *pixmap, int *width, int *height);

	// ------------------------------------------------------------------------
	// Cairo integration
	// ------------------------------------------------------------------------

	// Create a cairo surface for drawing into a pixmap.
	cairo_surface_t* (*cairo_surface_create_for_pixmap)(BkPixmap *pixmap,
	                                                    int width, int height);

	// Create a cairo surface for drawing directly into a window.
	cairo_surface_t* (*cairo_surface_create_for_window)(BkWindow *win,
	                                                    int width, int height);

	// ------------------------------------------------------------------------
	// Rendering
	// ------------------------------------------------------------------------

	// Copy an area from src pixmap to dst pixmap.
	void (*pixmap_copy_area)(BkPixmap *src, BkPixmap *dst,
	                         int src_x, int src_y,
	                         int width, int height,
	                         int dst_x, int dst_y);

	// Present (commit) the window contents to the display.
	// In X11 this flushes, in Wayland this commits the surface.
	void (*window_present)(BkWindow *win);

	// ------------------------------------------------------------------------
	// Visual / Color management
	// ------------------------------------------------------------------------

	// Get the default visual for the display.
	BkVisual* (*get_default_visual)(int depth);

	// Free a visual.
	void (*visual_free)(BkVisual *visual);

	// ------------------------------------------------------------------------
	// Monitor management
	// ------------------------------------------------------------------------

	// Get the number of monitors.
	int (*get_monitor_count)(void);

	// Get monitor info by index.
	BkMonitor* (*get_monitor)(int index);

	// Free a monitor info structure.
	void (*monitor_free)(BkMonitor *monitor);

	// Get monitors list (NULL-terminated). Caller frees with monitor_free_list.
	BkMonitor** (*get_monitors)(int *count);

	// Free monitor list.
	void (*monitor_free_list)(BkMonitor **monitors, int count);

	// ------------------------------------------------------------------------
	// Desktop / Workspace management
	// ------------------------------------------------------------------------

	// Get the number of desktops/workspaces.
	int (*get_desktop_count)(void);

	// Get the current desktop number (0-based).
	int (*get_current_desktop)(void);

	// Switch to a desktop.
	void (*set_current_desktop)(int desktop);

	// Get desktop names. Returns NULL-terminated string array. Caller frees each string.
	char** (*get_desktop_names)(int *count);

	// ------------------------------------------------------------------------
	// TopLevel (client window) management
	// ------------------------------------------------------------------------

	// Get all toplevels. Returns NULL-terminated array. Caller frees.
	BkTopLevel** (*get_toplevels)(int *count);

	// Free a toplevel info structure.
	void (*toplevel_free)(BkTopLevel *tl);

	// Free a toplevel list.
	void (*toplevel_free_list)(BkTopLevel **tls, int count);

	// Activate a toplevel (bring to front, focus).
	void (*toplevel_activate)(BkTopLevel *tl);

	// Close a toplevel.
	void (*toplevel_close)(BkTopLevel *tl);

	// Minimize/iconify a toplevel.
	void (*toplevel_minimize)(BkTopLevel *tl);

	// Toggle maximize.
	void (*toplevel_toggle_maximize)(BkTopLevel *tl);

	// Toggle shade.
	void (*toplevel_toggle_shade)(BkTopLevel *tl);

	// Move a toplevel to a different desktop.
	void (*toplevel_set_desktop)(BkTopLevel *tl, int desktop);

	// Get the currently active (focused) toplevel.
	BkTopLevel* (*get_active_toplevel)(void);

	// Get icon for a toplevel. Returns Imlib_Image that caller must free.
	Imlib_Image (*toplevel_get_icon)(BkTopLevel *tl, int size);

	// Get toplevel coordinates (absolute position and size including decorations).
	void (*toplevel_get_geometry)(BkTopLevel *tl, int *x, int *y, int *width, int *height);

	// Get toplevel PID.
	int  (*toplevel_get_pid)(BkTopLevel *tl);

	// Check if a toplevel is iconified/minimized.
	gboolean (*toplevel_is_iconified)(BkTopLevel *tl);

	// Check if a toplevel is urgent.
	gboolean (*toplevel_is_urgent)(BkTopLevel *tl);

	// Check if a toplevel is hidden (skip taskbar).
	gboolean (*toplevel_is_hidden)(BkTopLevel *tl);

	// Check if a toplevel is the active window.
	gboolean (*toplevel_is_active)(BkTopLevel *tl);

	// ------------------------------------------------------------------------
	// System Tray
	// ------------------------------------------------------------------------

	// Initialize the system tray protocol.
	// monitor: monitor index for the systray.
	// orientation: 0 = horizontal, 1 = vertical.
	// Returns 0 on success.
	int  (*systray_init)(int monitor);

	// Cleanup the system tray.
	void (*systray_cleanup)(void);

	// Check if a window is a systray icon.
	gboolean (*systray_is_icon)(BkWindow *win);

	// Get a systray icon by its window.
	BkSystrayIcon* (*systray_find_icon)(BkWindow *win);

	// Get the list of all systray icons.
	GSList* (*systray_get_icons)(void);

	// Reparent/embed a systray icon into a panel window.
	// Returns 0 on success.
	int  (*systray_embed_icon)(BkSystrayIcon *icon, BkWindow *panel);

	// Remove a systray icon.
	void (*systray_remove_icon)(BkSystrayIcon *icon);

	// Send a systray message.
	void (*systray_send_message)(BkWindow *icon, long message,
	                             long data1, long data2, long data3);

	// Get the systray icon's rendered image. Caller frees with imlib_free_image.
	Imlib_Image (*systray_get_icon_image)(BkSystrayIcon *icon);

	// Free a systray icon info structure.
	void (*systray_icon_free)(BkSystrayIcon *icon);

	// ------------------------------------------------------------------------
	// Transparency / Compositing / Root background
	// ------------------------------------------------------------------------

	// Check if real transparency is available (compositor running).
	gboolean (*has_real_transparency)(void);

	// Get the root background pixmap (for fake transparency), or NULL.
	BkPixmap* (*get_root_pixmap)(void);

	// ------------------------------------------------------------------------
	// Cursor
	// ------------------------------------------------------------------------

	// Set the cursor for a window.
	// cursor_name: X cursor name or Wayland cursor name.
	void (*window_set_cursor)(BkWindow *win, const char *cursor_name);

	// Get the current cursor position.
	void (*get_cursor_position)(int *x, int *y);

	// ------------------------------------------------------------------------
	// Drag and Drop
	// ------------------------------------------------------------------------

	// Start a drag operation (for launcher icons).
	// FIXME: Define DnD interface properly
	void (*dnd_start)(BkWindow *source, const char *data);

	// ------------------------------------------------------------------------
	// Miscellaneous
	// ------------------------------------------------------------------------

	// Get the screen resolution / DPI.
	int  (*get_dpi)(void);

	// Get a unique identifier for the root/parent window.
	unsigned long (*get_root_id)(void);

	// Check if a given window is one of our panel windows.
	gboolean (*is_panel_window)(BkWindow *win);

} BackendVT;

// ============================================================================
// Global backend instance
// ============================================================================

extern const BackendVT *backend;

// ============================================================================
// Backend selection and initialization
// ============================================================================

// Initialize the appropriate backend. Call once at startup.
// backend_type can be BACKEND_X11, BACKEND_WAYLAND, or -1 for auto-detect.
// Returns 0 on success, non-zero on failure.
int backend_init(BackendType type_hint, int *argc, char ***argv);

// Shutdown the backend.
void backend_cleanup(void);

// ============================================================================
// Convenience inline accessors
// ============================================================================

static inline int bk_get_fd(void)
	{ return backend->get_fd(); }

static inline void bk_flush(void)
	{ backend->flush(); }

static inline gboolean bk_events_pending(void)
	{ return backend->events_pending(); }

static inline BkEvent* bk_wait_event(void)
	{ return backend->wait_event(); }

static inline BkEvent* bk_poll_event(void)
	{ return backend->poll_event(); }

static inline void bk_event_free(BkEvent *event)
	{ backend->event_free(event); }

static inline BkWindow* bk_window_create(BkWindow *parent,
                                          int x, int y, int w, int h,
                                          int depth, BkVisual *visual,
                                          BkLayer layer,
                                          BkAnchor anchor, int exclusive_zone)
	{ return backend->window_create(parent, x, y, w, h, depth, visual,
	                                layer, anchor, exclusive_zone); }

static inline void bk_window_destroy(BkWindow *win)
	{ backend->window_destroy(win); }

static inline void bk_window_show(BkWindow *win)
	{ backend->window_show(win); }

static inline void bk_window_hide(BkWindow *win)
	{ backend->window_hide(win); }

static inline void bk_window_move(BkWindow *win, int x, int y)
	{ backend->window_move(win, x, y); }

static inline void bk_window_resize(BkWindow *win, int w, int h)
	{ backend->window_resize(win, w, h); }

static inline void bk_window_move_resize(BkWindow *win, int x, int y, int w, int h)
	{ backend->window_move_resize(win, x, y, w, h); }

static inline void bk_window_set_title(BkWindow *win, const char *title)
	{ backend->window_set_title(win, title); }

static inline void bk_window_set_input_region(BkWindow *win, cairo_region_t *r)
	{ backend->window_set_input_region(win, r); }

static inline void bk_window_set_opacity(BkWindow *win, double opacity)
	{ backend->window_set_opacity(win, opacity); }

static inline unsigned long bk_window_get_id(BkWindow *win)
	{ return backend->window_get_id(win); }

static inline BkPixmap* bk_pixmap_create(int w, int h, int depth)
	{ return backend->pixmap_create(w, h, depth); }

static inline void bk_pixmap_destroy(BkPixmap *pixmap)
	{ backend->pixmap_destroy(pixmap); }

static inline void bk_pixmap_get_size(BkPixmap *pixmap, int *w, int *h)
	{ backend->pixmap_get_size(pixmap, w, h); }

static inline cairo_surface_t* bk_cairo_surface_create_for_pixmap(BkPixmap *p, int w, int h)
	{ return backend->cairo_surface_create_for_pixmap(p, w, h); }

static inline cairo_surface_t* bk_cairo_surface_create_for_window(BkWindow *win, int w, int h)
	{ return backend->cairo_surface_create_for_window(win, w, h); }

static inline void bk_pixmap_copy_area(BkPixmap *src, BkPixmap *dst,
                                        int sx, int sy, int sw, int sh,
                                        int dx, int dy)
	{ backend->pixmap_copy_area(src, dst, sx, sy, sw, sh, dx, dy); }

static inline void bk_window_present(BkWindow *win)
	{ backend->window_present(win); }

static inline BkVisual* bk_get_default_visual(int depth)
	{ return backend->get_default_visual(depth); }

static inline int bk_get_monitor_count(void)
	{ return backend->get_monitor_count(); }

static inline BkMonitor* bk_get_monitor(int i)
	{ return backend->get_monitor(i); }

static inline void bk_monitor_free(BkMonitor *m)
	{ backend->monitor_free(m); }

static inline BkMonitor** bk_get_monitors(int *count)
	{ return backend->get_monitors(count); }

static inline int bk_get_desktop_count(void)
	{ return backend->get_desktop_count(); }

static inline int bk_get_current_desktop(void)
	{ return backend->get_current_desktop(); }

static inline void bk_set_current_desktop(int d)
	{ backend->set_current_desktop(d); }

static inline char** bk_get_desktop_names(int *count)
	{ return backend->get_desktop_names(count); }

static inline BkTopLevel** bk_get_toplevels(int *count)
	{ return backend->get_toplevels(count); }

static inline void bk_toplevel_free(BkTopLevel *tl)
	{ backend->toplevel_free(tl); }

static inline void bk_toplevel_free_list(BkTopLevel **tls, int count)
	{ backend->toplevel_free_list(tls, count); }

static inline void bk_toplevel_activate(BkTopLevel *tl)
	{ backend->toplevel_activate(tl); }

static inline void bk_toplevel_close(BkTopLevel *tl)
	{ backend->toplevel_close(tl); }

static inline void bk_toplevel_minimize(BkTopLevel *tl)
	{ backend->toplevel_minimize(tl); }

static inline void bk_toplevel_toggle_maximize(BkTopLevel *tl)
	{ backend->toplevel_toggle_maximize(tl); }

static inline void bk_toplevel_toggle_shade(BkTopLevel *tl)
	{ backend->toplevel_toggle_shade(tl); }

static inline void bk_toplevel_set_desktop(BkTopLevel *tl, int d)
	{ backend->toplevel_set_desktop(tl, d); }

static inline BkTopLevel* bk_get_active_toplevel(void)
	{ return backend->get_active_toplevel(); }

static inline Imlib_Image bk_toplevel_get_icon(BkTopLevel *tl, int size)
	{ return backend->toplevel_get_icon(tl, size); }

static inline void bk_toplevel_get_geometry(BkTopLevel *tl, int *x, int *y, int *w, int *h)
	{ backend->toplevel_get_geometry(tl, x, y, w, h); }

static inline int bk_toplevel_get_pid(BkTopLevel *tl)
	{ return backend->toplevel_get_pid(tl); }

static inline gboolean bk_toplevel_is_iconified(BkTopLevel *tl)
	{ return backend->toplevel_is_iconified(tl); }

static inline gboolean bk_toplevel_is_urgent(BkTopLevel *tl)
	{ return backend->toplevel_is_urgent(tl); }

static inline gboolean bk_toplevel_is_hidden(BkTopLevel *tl)
	{ return backend->toplevel_is_hidden(tl); }

static inline gboolean bk_toplevel_is_active(BkTopLevel *tl)
	{ return backend->toplevel_is_active(tl); }

static inline int bk_systray_init(int monitor)
	{ return backend->systray_init(monitor); }

static inline void bk_systray_cleanup(void)
	{ backend->systray_cleanup(); }

static inline gboolean bk_systray_is_icon(BkWindow *win)
	{ return backend->systray_is_icon(win); }

static inline BkSystrayIcon* bk_systray_find_icon(BkWindow *win)
	{ return backend->systray_find_icon(win); }

static inline GSList* bk_systray_get_icons(void)
	{ return backend->systray_get_icons(); }

static inline int bk_systray_embed_icon(BkSystrayIcon *icon, BkWindow *panel)
	{ return backend->systray_embed_icon(icon, panel); }

static inline void bk_systray_remove_icon(BkSystrayIcon *icon)
	{ backend->systray_remove_icon(icon); }

static inline void bk_systray_send_message(BkWindow *icon, long msg,
                                            long d1, long d2, long d3)
	{ backend->systray_send_message(icon, msg, d1, d2, d3); }

static inline Imlib_Image bk_systray_get_icon_image(BkSystrayIcon *icon)
	{ return backend->systray_get_icon_image(icon); }

static inline void bk_systray_icon_free(BkSystrayIcon *icon)
	{ backend->systray_icon_free(icon); }

static inline gboolean bk_has_real_transparency(void)
	{ return backend->has_real_transparency(); }

static inline BkPixmap* bk_get_root_pixmap(void)
	{ return backend->get_root_pixmap(); }

static inline void bk_window_set_cursor(BkWindow *win, const char *name)
	{ backend->window_set_cursor(win, name); }

static inline void bk_get_cursor_position(int *x, int *y)
	{ backend->get_cursor_position(x, y); }

static inline int bk_get_dpi(void)
	{ return backend->get_dpi(); }

static inline unsigned long bk_get_root_id(void)
	{ return backend->get_root_id(); }

static inline gboolean bk_is_panel_window(BkWindow *win)
	{ return backend->is_panel_window(win); }

#endif // BACKEND_H
