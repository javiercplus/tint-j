/* Stub: prevents real X11/Xft/Xft.h from being included. */
#ifndef COMPAT_STUB_XFT_H
#define COMPAT_STUB_XFT_H

#include "compat/compat.h"

// Xft stubs - no-op functions for font config
typedef void *XftFont;
typedef void *XftDraw;
typedef void *XftColor;

#define XftFontOpenName(dpy, screen, name) NULL
#define XftFontClose(dpy, font) ((void)0)
#define XftDrawCreate(dpy, drawable, visual, colormap) NULL
#define XftDrawDestroy(draw) ((void)0)
#define XftTextExtentsUtf8(dpy, font, str, len, extents) ((void)0)

#endif
