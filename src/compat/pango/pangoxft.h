/* Stub: prevents real pango/pangoxft.h from being included.
 * For Wayland we use pangocairo instead. */

#ifndef COMPAT_STUB_PANGOXFT_H
#define COMPAT_STUB_PANGOXFT_H

// Minimal type definitions to satisfy includes
typedef int XTrapezoid;
typedef int XftGlyphSpec;
typedef int XftGlyphFontSpec;

// No-op function macros
#define pango_xft_get_context(dpy, scr) NULL
#define pango_xft_set_default_substitute(dpy, scr, func, data) ((void)0)
#define pango_xft_substitute_changed(dpy, scr) ((void)0)
#define pango_xft_shutdown_display(dpy, scr) ((void)0)
#define pango_xft_render_layout_line(draw, color, line, x, y) ((void)0)
#define pango_xft_picture_set_argb(pict, dpy, scr, argb) ((void)0)
#define pango_xft_font_get_font(font) NULL
#define pango_xft_font_get_display(font) NULL
#define pango_xft_font_get_screen(font) 0
#define pango_xft_font_lock_face(font) NULL
#define pango_xft_font_unlock_face(font) ((void)0)
#define pango_xft_font_map_new() NULL

#endif
