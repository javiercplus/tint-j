/**************************************************************************
 * compat.h
 *
 * X11 → Wayland Compatibility Layer
 *
 * Standalone header that provides all X11 types, constants, and function
 * declarations needed by the tint2 codebase. No real X11 headers are included.
 *
 * Include this header INSTEAD of <X11/Xlib.h> when building for Wayland.
 *
 * Copyright (C) 2026 tint-j project
 **************************************************************************/

#ifndef COMPAT_H
#define COMPAT_H

#include <glib.h>
#include <cairo.h>

// Define COMPAT_NO_IMLIB2 so backend.h doesn't include Imlib2.h
// (which would create a circular include via X11 stubs)
#define COMPAT_NO_IMLIB2
#include "backend/backend.h"
#undef COMPAT_NO_IMLIB2

// ============================================================================
// Type aliases (defined BEFORE Imlib2.h which uses these types)
// ============================================================================

typedef struct CompatDisplay CompatDisplay;
typedef CompatDisplay Display;
typedef BkVisual   Visual;   // BkVisual, not BkVisual* (Visual* = BkVisual*)
typedef void      *GC;
typedef void      *Colormap;
typedef unsigned long Atom;
typedef unsigned long Drawable;  // Same as Window/Pixmap (XID)
typedef unsigned int Cursor;
typedef unsigned long Time;
typedef unsigned long XID;
typedef int        Screen;
typedef int        Bool;

// Window and Pixmap: use integer IDs with a lookup table in compat.c
// This preserves X11 semantics (Window/Pixmap are value types, used as
// GHashTable keys and compared with ==)
typedef unsigned long Window;
typedef unsigned long Pixmap;
#define True  1
#define False 0
#define None  0L
#define CurrentTime 0L

// XImage is used by Imlib2.h but we don't need it
typedef struct _XImage XImage;

// XRender types (used by common.c for pixmap operations)
typedef struct { unsigned short red, green, blue, alpha; } XRenderColor;
typedef unsigned long Picture;
typedef void *XRenderPictFormat;

// Damage type (used by systraybar.h)
typedef unsigned long Damage;

// Now include Imlib2.h — all types it needs (Display, Visual, etc.) are defined
#include <Imlib2.h>

// ============================================================================
// CompatDisplay: wraps the backend as a pseudo-X11 Display
// ============================================================================

struct CompatDisplay {
	BkDisplay *bk;
	int default_screen;
	Window root_win;
	GC gc;
	int depth;
	Visual *default_visual;
	Colormap default_colormap;
	GHashTable *atom_table;
	unsigned long next_atom_id;
	// Lookup tables (stored here to avoid BSS corruption)
	GHashTable *window_table;
	GHashTable *window_rev_table;
	GHashTable *pixmap_table;
	unsigned long next_window_id;
	unsigned long next_pixmap_id;
};

// ============================================================================
// Basic types (X11/X.h compatible)
// ============================================================================

#define ALL_DESKTOPS 0xFFFFFFFF

// ============================================================================
// Atom type constants (from X11/Xatom.h)
// ============================================================================

#define XA_PRIMARY       ((Atom)1)
#define XA_SECONDARY     ((Atom)2)
#define XA_ARC           ((Atom)3)
#define XA_ATOM          ((Atom)4)
#define XA_BITMAP        ((Atom)5)
#define XA_CARDINAL      ((Atom)6)
#define XA_COLORMAP      ((Atom)7)
#define XA_CURSOR        ((Atom)8)
#define XA_DRAWABLE      ((Atom)10)
#define XA_FONT          ((Atom)11)
#define XA_INTEGER       ((Atom)13)
#define XA_PIXMAP        ((Atom)14)
#define XA_POINT         ((Atom)15)
#define XA_RECTANGLE     ((Atom)16)
#define XA_WINDOW        ((Atom)18)
#define XA_WM_HINTS      ((Atom)19)
#define XA_STRING        ((Atom)31)
#define XA_VISUALID      ((Atom)32)

// ============================================================================
// Event types (matching X11 values)
// ============================================================================

enum {
	KeyPress         = 2,
	KeyRelease       = 3,
	ButtonPress      = 4,
	ButtonRelease    = 5,
	MotionNotify     = 6,
	EnterNotify      = 7,
	LeaveNotify      = 8,
	FocusIn          = 9,
	FocusOut         = 10,
	KeymapNotify     = 11,
	Expose           = 12,
	GraphicsExpose   = 13,
	NoExpose         = 14,
	VisibilityNotify = 15,
	CreateNotify     = 16,
	DestroyNotify    = 17,
	UnmapNotify      = 18,
	MapNotify        = 19,
	MapRequest       = 20,
	ReparentNotify   = 21,
	ConfigureNotify  = 22,
	ConfigureRequest = 23,
	GravityNotify    = 24,
	ResizeRequest    = 25,
	CirculateNotify  = 26,
	CirculateRequest = 27,
	PropertyNotify   = 28,
	SelectionClear   = 29,
	SelectionRequest = 30,
	SelectionNotify  = 31,
	ColormapNotify   = 32,
	ClientMessage    = 33,
	MappingNotify    = 34,
	LASTEvent        = 35,
};

// Event masks
#define NoEventMask              0L
#define KeyPressMask             (1L<<0)
#define KeyReleaseMask           (1L<<1)
#define ButtonPressMask          (1L<<2)
#define ButtonReleaseMask        (1L<<3)
#define EnterWindowMask          (1L<<4)
#define LeaveWindowMask          (1L<<5)
#define PointerMotionMask        (1L<<6)
#define PointerMotionHintMask    (1L<<7)
#define Button1MotionMask        (1L<<8)
#define Button2MotionMask        (1L<<9)
#define Button3MotionMask        (1L<<10)
#define Button4MotionMask        (1L<<11)
#define Button5MotionMask        (1L<<12)
#define ButtonMotionMask         (1L<<13)
#define KeymapStateMask          (1L<<14)
#define ExposureMask             (1L<<15)
#define VisibilityChangeMask     (1L<<16)
#define StructureNotifyMask      (1L<<17)
#define ResizeRedirectMask       (1L<<18)
#define SubstructureNotifyMask   (1L<<19)
#define SubstructureRedirectMask (1L<<20)
#define FocusChangeMask          (1L<<21)
#define PropertyChangeMask       (1L<<22)
#define ColormapChangeMask       (1L<<23)
#define OwnerGrabButtonMask      (1L<<24)

// Button masks
#define Button1Mask (1<<8)
#define Button2Mask (1<<9)
#define Button3Mask (1<<10)
#define Button4Mask (1<<11)
#define Button5Mask (1<<12)

// Modifier masks
#define ShiftMask   (1<<0)
#define LockMask    (1<<1)
#define ControlMask (1<<2)
#define Mod1Mask    (1<<3)
#define Mod2Mask    (1<<4)
#define Mod3Mask    (1<<5)
#define Mod4Mask    (1<<6)
#define Mod5Mask    (1<<7)

// Window attributes mask
#define CWBackPixmap       (1L<<0)
#define CWBackPixel        (1L<<1)
#define CWBorderPixmap     (1L<<2)
#define CWBorderPixel      (1L<<3)
#define CWBitGravity       (1L<<4)
#define CWWinGravity       (1L<<5)
#define CWBackingStore     (1L<<6)
#define CWBackingPlanes    (1L<<7)
#define CWBackingPixel     (1L<<8)
#define CWOverrideRedirect (1L<<9)
#define CWSaveUnder        (1L<<10)
#define CWEventMask        (1L<<11)
#define CWDontPropagate    (1L<<12)
#define CWColormap         (1L<<13)
#define CWCursor           (1L<<14)

// GC masks
#define GCFunction          (1L<<0)
#define GCPlaneMask         (1L<<1)
#define GCForeground        (1L<<2)
#define GCBackground        (1L<<3)
#define GCLineWidth         (1L<<4)
#define GCLineStyle         (1L<<5)
#define GCCapStyle          (1L<<6)
#define GCJoinStyle         (1L<<7)
#define GCFillStyle         (1L<<8)
#define GCFillRule          (1L<<9)
#define GCTile              (1L<<10)
#define GCStipple           (1L<<11)
#define GCTileStipXOrigin   (1L<<12)
#define GCTileStipYOrigin   (1L<<13)
#define GCFont              (1L<<14)
#define GCSubwindowMode     (1L<<15)
#define GCGraphicsExposures (1L<<16)
#define GCClipXOrigin       (1L<<17)
#define GCClipYOrigin       (1L<<18)
#define GCClipMask          (1L<<19)
#define GCDashOffset        (1L<<20)
#define GCDashList          (1L<<21)
#define GCArcMode           (1L<<22)

// Property modes
#define PropModeReplace 0
#define PropModePrepend 1
#define PropModeAppend  2

// Property state
#define PropertyNewValue 0
#define PropertyDelete   1

// Map state
#define IsUnmapped   0
#define IsUnviewable 1
#define IsViewable   2

// Window classes
#define InputOutput 1
#define InputOnly   2
#define CopyFromParent 0

// Window size hints
#define USPosition  (1L << 0)
#define USSize      (1L << 1)
#define PPosition   (1L << 2)
#define PSize       (1L << 3)
#define PMinSize    (1L << 4)
#define PMaxSize    (1L << 5)
#define PResizeInc  (1L << 6)
#define PAspect     (1L << 7)
#define PBaseSize   (1L << 8)
#define PWinGravity (1L << 9)

// WM Hints flags
#define InputHint       (1L << 0)
#define StateHint       (1L << 1)
#define IconPixmapHint  (1L << 2)
#define IconWindowHint  (1L << 3)
#define IconPositionHint (1L << 4)
#define IconMaskHint    (1L << 5)
#define WindowGroupHint (1L << 7)
#define UrgencyHint     (1L << 8)

// WM states
#define WithdrawnState 0
#define NormalState    1
#define IconicState    3

// Grab modes
#define GrabModeSync  0
#define GrabModeAsync 1

// Cursor shapes
#define XC_left_ptr 68

// Byte order (from X11/X.h)
#define LSBFirst 0
#define MSBFirst 1

// Success/error codes
#define Success 0

// ============================================================================
// X11 Struct Types (needed by tint2 codebase)
// ============================================================================

typedef struct {
	int x, y;
	int width, height;
	int border_width;
	int depth;
	Visual *visual;
	Window root;
	int c_class;
	int bit_gravity;
	int win_gravity;
	int backing_store;
	unsigned long backing_planes;
	unsigned long backing_pixel;
	int save_under;
	Colormap colormap;
	int map_is_installed;
	int map_state;
	long all_event_masks;
	long your_event_mask;
	long do_not_propagate_mask;
	int override_redirect;
	Screen *screen;
} XWindowAttributes;

typedef struct {
	Pixmap background_pixmap;
	unsigned long background_pixel;
	Pixmap border_pixmap;
	unsigned long border_pixel;
	int bit_gravity;
	int win_gravity;
	int backing_store;
	unsigned long backing_planes;
	unsigned long backing_pixel;
	int save_under;
	long event_mask;
	long do_not_propagate_mask;
	int override_redirect;
	Colormap colormap;
	Cursor cursor;
} XSetWindowAttributes;

typedef struct {
	int flags;
	int x, y;
	int width, height;
	int min_width, min_height;
	int max_width, max_height;
	int width_inc, height_inc;
	struct {
		int x;
		int y;
	} min_aspect, max_aspect;
	int base_width, base_height;
	int win_gravity;
} XSizeHints;

typedef struct {
	long flags;
	Window icon_window;
	int icon_x, icon_y;
	Pixmap icon_mask;
	unsigned long input;
	int initial_state;
	Pixmap icon_pixmap;
	Window window_group;
} XWMHints;

typedef struct {
	char *res_name;
	char *res_class;
} XClassHint;

typedef struct {
	int dummy;
} XGCValues;

// ============================================================================
// XEvent structures (matching X11 layout)
// ============================================================================

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
} XAnyEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	Window root, subwindow;
	Time time;
	int x, y;
	int x_root, y_root;
	unsigned int state;
	unsigned int button;
	int same_screen;
} XButtonEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	Window root, subwindow;
	Time time;
	int x, y;
	int x_root, y_root;
	unsigned int state;
	char is_hint;
	int same_screen;
} XMotionEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	Window root, subwindow;
	Time time;
	int x, y;
	int x_root, y_root;
	int mode;
	int detail;
	int same_screen;
	int focus;
	unsigned int state;
} XCrossingEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	int x, y;
	int width, height;
	int count;
} XExposeEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	Atom atom;
	Time time;
	int state;
} XPropertyEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	int x, y;
	int width, height;
	int border_width;
	Window above;
	int override_redirect;
} XConfigureEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	Atom message_type;
	int format;
	long data[5];
} XClientMessageEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	Window parent;
	int x, y;
	int override_redirect;
} XReparentEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
} XDestroyWindowEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	int from_configure;
} XUnmapEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window requestor;
	Atom selection;
	Atom target;
	Atom property;
	Time time;
} XSelectionEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window owner;
	Window requestor;
	Atom selection;
	Atom target;
	Atom property;
	Time time;
} XSelectionRequestEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	int width, height;
} XResizeRequestEvent;

typedef struct {
	int type;
	unsigned long serial;
	int send_event;
	Display *display;
	Window window;
	int x, y;
	int width, height;
	int border_width;
	Window above;
	int detail;
	unsigned long value_mask;
} XConfigureRequestEvent;

typedef struct {
	int type;
	Display *display;
	unsigned long serial;
	unsigned char error_code;
	unsigned char request_code;
	unsigned char minor_code;
	XID resourceid;
} XErrorEvent;

// Event union
typedef union XEvent {
	int type;
	XAnyEvent xany;
	XButtonEvent xbutton;
	XMotionEvent xmotion;
	XCrossingEvent xcrossing;
	XExposeEvent xexpose;
	XPropertyEvent xproperty;
	XConfigureEvent xconfigure;
	XClientMessageEvent xclient;
	XReparentEvent xreparent;
	XDestroyWindowEvent xdestroywindow;
	XUnmapEvent xunmap;
	XSelectionEvent xselection;
	XSelectionRequestEvent xselectionrequest;
	XResizeRequestEvent xresizerequest;
	XConfigureRequestEvent xconfigurerequest;
	long pad[24];
} XEvent;

// ============================================================================
// X11 Functions → Backend wrappers
// ============================================================================

// Display
Display     *XOpenDisplay(const char *display_name);
int          XCloseDisplay(Display *display);
int          ConnectionNumber(Display *display);
int          DefaultScreen(Display *display);
Window       XRootWindow(Display *display, int screen);
#define RootWindow(d, s) XRootWindow(d, s)

// Atoms
Atom         XInternAtom(Display *display, const char *atom_name, int only_if_exists);
char        *XGetAtomName(Display *display, Atom atom);

// Windows
Window       XCreateWindow(Display *display, Window parent,
                           int x, int y, unsigned int width, unsigned int height,
                           unsigned int border_width, int depth,
                           unsigned int class, Visual *visual,
                           unsigned long valuemask, XSetWindowAttributes *attributes);
int          XDestroyWindow(Display *display, Window w);
int          XMapWindow(Display *display, Window w);
int          XUnmapWindow(Display *display, Window w);
int          XMapSubwindows(Display *display, Window w);
int          XUnmapSubwindows(Display *display, Window w);
int          XMoveResizeWindow(Display *display, Window w, int x, int y,
                               unsigned int width, unsigned int height);
int          XResizeWindow(Display *display, Window w, unsigned int width, unsigned int height);
int          XMoveWindow(Display *display, Window w, int x, int y);
int          XLowerWindow(Display *display, Window w);
int          XRaiseWindow(Display *display, Window w);
int          XIconifyWindow(Display *display, Window w, int screen);
int          XSelectInput(Display *display, Window w, long event_mask);
int          XChangeWindowAttributes(Display *display, Window w,
                                     unsigned long valuemask, XSetWindowAttributes *attributes);
int          XSetWindowBackgroundPixmap(Display *display, Window w, Pixmap pixmap);
int          XGetWindowAttributes(Display *display, Window w, XWindowAttributes *attr);
int          XGetGeometry(Display *display, Drawable d, Window *root_return,
                          int *x_return, int *y_return,
                          unsigned int *width_return, unsigned int *height_return,
                          unsigned int *border_width_return, unsigned int *depth_return);
int          XTranslateCoordinates(Display *display,
                                   Window src_w, Window dest_w,
                                   int src_x, int src_y,
                                   int *dest_x_return, int *dest_y_return,
                                   Window *child_return);
int          XQueryPointer(Display *display, Window w,
                           Window *root_return, Window *child_return,
                           int *root_x_return, int *root_y_return,
                           int *win_x_return, int *win_y_return,
                           unsigned int *mask_return);
int          XGetTransientForHint(Display *display, Window w, Window *prop_window_return);

// Pixmaps
Pixmap       XCreatePixmap(Display *display, Drawable d,
                           unsigned int width, unsigned int height,
                           unsigned int depth);
int          XFreePixmap(Display *display, Pixmap pixmap);

// Drawing
int          XCopyArea(Display *display, Drawable src, Drawable dst, GC gc,
                       int src_x, int src_y,
                       unsigned int width, unsigned int height,
                       int dst_x, int dst_y);
int          XFillRectangle(Display *display, Drawable d, GC gc,
                            int x, int y, unsigned int width, unsigned int height);
int          XSetTSOrigin(Display *display, GC gc, int x, int y);

// Cairo integration
// cairo_xlib_surface_create is redirected at link time via --wrap
// The declaration must match libcairo's to avoid implicit declaration warnings
cairo_surface_t *cairo_xlib_surface_create(Display *dpy, Drawable drawable,
                                           Visual *visual, int width, int height);
cairo_surface_t *__wrap_cairo_xlib_surface_create(Display *dpy, Drawable drawable,
                                                   Visual *visual, int width, int height);

// Properties
int          XChangeProperty(Display *display, Window w,
                             Atom property, Atom type,
                             int format, int mode,
                             const unsigned char *data, int nelements);
int          XDeleteProperty(Display *display, Window w, Atom property);
int          XGetWindowProperty(Display *display, Window w, Atom property,
                                long long_offset, long long_length,
                                int delete, Atom req_type,
                                Atom *actual_type_return, int *actual_format_return,
                                unsigned long *nitems_return,
                                unsigned long *bytes_after_return,
                                unsigned char **prop_return);

// Window metadata
int          XStoreName(Display *display, Window w, const char *window_name);
int          XSetIconName(Display *display, Window w, const char *icon_name);
int          XSetWMHints(Display *display, Window w, XWMHints *wm_hints);
int          XSetWMNormalHints(Display *display, Window w, XSizeHints *hints);
int          XSetClassHint(Display *display, Window w, XClassHint *class_hint);
int          XSetTransientForHint(Display *display, Window w, Window prop_window);
XWMHints    *XGetWMHints(Display *display, Window w);
XClassHint  *XAllocClassHint(void);

// Selections
Window       XGetSelectionOwner(Display *display, Atom selection);
int          XConvertSelection(Display *display, Atom selection, Atom target,
                               Atom property, Window requestor, Time time);

// Events
int          XPending(Display *display);
int          XNextEvent(Display *display, XEvent *event_return);
int          XSendEvent(Display *display, Window w, int propagate,
                        long event_mask, XEvent *event_send);
int          XSync(Display *display, int discard);
int          XFlush(Display *display);
int          XUngrabPointer(Display *display, Time time);

// Server grab (no-ops in Wayland)
int          XGrabServer(Display *display);
int          XUngrabServer(Display *display);

// XRender (stubbed for Wayland)
int          XRenderComposite(Display *dpy, int op, Picture src, Picture mask,
                              Picture dst, int src_x, int src_y, int mask_x, int mask_y,
                              int dst_x, int dst_y, unsigned int width, unsigned int height);
int          XRenderFreePicture(Display *dpy, Picture picture);
int          XRenderFillRectangle(Display *dpy, int op, Picture dst,
                                  const XRenderColor *color, int x, int y,
                                  unsigned int width, unsigned int height);
Picture      XRenderCreatePicture(Display *dpy, Drawable drawable,
                                  const XRenderPictFormat *format,
                                  unsigned long valuemask, const void *attributes);
XRenderPictFormat *XRenderFindVisualFormat(Display *dpy, const Visual *visual);
XRenderPictFormat *XRenderFindStandardFormat(Display *dpy, int format);

// Error handling
typedef int (*XErrorHandler)(Display *, XErrorEvent *);
typedef int (*XIOErrorHandler)(Display *);
XErrorHandler  XSetErrorHandler(XErrorHandler handler);
XIOErrorHandler XSetIOErrorHandler(XIOErrorHandler handler);

// GC
GC           XCreateGC(Display *display, Drawable d, unsigned long valuemask, XGCValues *values);
int          XFreeGC(Display *display, GC gc);

// Memory
int          XFree(void *data);

// Visual / Colormap
Visual      *XDefaultVisual(Display *display, int screen);
Colormap     XDefaultColormap(Display *display, int screen);
#define DefaultVisual(d, s)    XDefaultVisual(d, s)
#define DefaultColormap(d, s)  XDefaultColormap(d, s)

// Cursor
Cursor       XCreateFontCursor(Display *display, unsigned int shape);

// XLocale
int          XSupportsLocale(void);

// ============================================================================
// Compat Layer State (replaces X11 server globals)
// ============================================================================

// These are the same globals used in server.h/server.c
extern CompatDisplay *server_display;
extern Window server_root_win;
extern GC server_gc;
extern Visual *server_visual;
extern int server_depth;
extern Colormap server_colormap;

// ============================================================================
// Compat-specific functions
// ============================================================================

int           compat_init(void);
void          compat_cleanup(void);
BkDisplay    *compat_get_bk_display(void);

// Helper: present a compat Window (looks up BkWindow and calls bk_window_present)
void          compat_window_present(Window w);
BkWindow     *win_lookup(Window w);
BkPixmap     *pix_lookup(Pixmap p);

// ============================================================================
// Imlib2 integration macros (defined AFTER Imlib2.h is included)
// These redirect X11-specific Imlib2 functions to our compat implementations
// ============================================================================

#define imlib_context_set_display(d)       ((void)(d))
#define imlib_context_set_visual(v)        ((void)(v))
#define imlib_context_set_colormap(c)      ((void)(c))
void compat_imlib_context_set_drawable(Drawable d);
#define imlib_context_set_drawable(d)      compat_imlib_context_set_drawable(d)

// render_image() is defined in common.c. It calls imlib_render_image_on_drawable()
// which we redirect below. We do NOT macro render_image itself.
// Note: imlib_render_image_on_drawable takes (int x, int y) — no drawable param.
void compat_imlib_render_image_on_drawable(int x, int y);
#define imlib_render_image_on_drawable(x, y) \
	compat_imlib_render_image_on_drawable(x, y)

Imlib_Image compat_imlib_create_image_from_drawable(Drawable d, int x, int y,
                                                     int width, int height,
                                                     int need_to_grab);
#define imlib_create_image_from_drawable(d, x, y, w, h, grab) \
	compat_imlib_create_image_from_drawable(d, x, y, w, h, grab)

#endif // COMPAT_H
