/* Stub: prevents real cairo/cairo-xlib.h from being included.
 *
 * The compat layer provides cairo_xlib_surface_create via 
 * compat_cairo_surface_create_for_pixmap which is mapped through
 * a macro in compat.h. This stub just prevents the real
 * cairo-xlib.h from pulling in X11 headers and declaring the
 * real function (which would conflict with our macro).
 */
#ifndef COMPAT_STUB_CAIRO_XLIB_H
#define COMPAT_STUB_CAIRO_XLIB_H

// The compat.h macro #defines cairo_xlib_surface_create to redirect
// to compat_cairo_surface_create_for_pixmap.
// We just need to declare the types that cairo-xlib.h users expect.
// cairo_surface_t is from cairo.h which IS included.

#endif
