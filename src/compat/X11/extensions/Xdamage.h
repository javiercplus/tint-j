/* Stub: prevents real X11/extensions/Xdamage.h */
#ifndef COMPAT_STUB_XDAMAGE_H
#define COMPAT_STUB_XDAMAGE_H
#include "compat/compat.h"

// XDamage stubs
static inline int XDamageQueryExtension(void *dpy, int *event_base, int *error_base) {
    (void)dpy;
    if (event_base) *event_base = 0;
    if (error_base) *error_base = 0;
    return 1;
}
#endif
