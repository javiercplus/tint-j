/* Pre-include header for Wayland compat build.
 *
 * Injected before every source file via -include.
 * Provides all X11 types, backend types, Imlib2, and compat macros
 * via compat.h.
 *
 * COMPAT_NO_IMLIB2 must be defined before compat.h to prevent
 * backend.h from directly including Imlib2.h (handled by compat.h).
 */
#ifndef COMPAT_PRE_H
#define COMPAT_PRE_H

#define COMPAT_NO_IMLIB2
#include "compat/compat.h"

#endif
