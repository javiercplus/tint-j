/**************************************************************************
 * backend.c
 *
 * Backend selection and initialization.
 * Auto-detects X11 vs Wayland and loads the appropriate backend.
 *
 * When compiled as TINT2_FULL, both X11 and Wayland backends are available.
 * When compiled standalone (tint-wl), only Wayland is available.
 *
 * Copyright (C) 2024 tint-j project
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend/backend.h"

const BackendVT *backend = NULL;

#ifdef TINT2_FULL
extern const BackendVT x11_backend_vt;
#endif
#ifdef ENABLE_WAYLAND
extern const BackendVT wl_backend_vt;
#endif

int backend_init(BackendType type_hint, int *argc, char ***argv)
{
	const char *env_backend = getenv("TINT2_BACKEND");

	if (env_backend) {
		if (strcmp(env_backend, "wayland") == 0 || strcmp(env_backend, "wl") == 0) {
			type_hint = BACKEND_WAYLAND;
		} else if (strcmp(env_backend, "x11") == 0) {
			type_hint = BACKEND_X11;
		}
	}

	// Auto-detect: prefer Wayland if WAYLAND_DISPLAY is set, else fallback to X11
	if ((int)type_hint < 0) {
		const char *wl_display = getenv("WAYLAND_DISPLAY");
		const char *xdg_session = getenv("XDG_SESSION_TYPE");

		if (wl_display || (xdg_session && strcmp(xdg_session, "wayland") == 0)) {
			type_hint = BACKEND_WAYLAND;
		} else {
			type_hint = BACKEND_X11;
		}
	}

	switch (type_hint) {
	case BACKEND_X11:
#ifdef TINT2_FULL
		backend = &x11_backend_vt;
		break;
#else
		fprintf(stderr, "tint2: X11 backend not compiled in (use full tint2 binary).\n");
		return 1;
#endif
	case BACKEND_WAYLAND:
#ifdef ENABLE_WAYLAND
		backend = &wl_backend_vt;
		break;
#else
		fprintf(stderr, "tint2: Wayland backend not compiled in.\n");
		return 1;
#endif
	default:
		fprintf(stderr, "tint2: Unknown backend type %d\n", type_hint);
		return 1;
	}

	if (!backend || backend->init(argc, argv) != 0) {
#ifdef TINT2_FULL
		// If Wayland failed and we're auto-detecting, try X11 fallback
		if (type_hint == BACKEND_WAYLAND && env_backend == NULL) {
			fprintf(stderr, "tint2: Wayland backend failed, trying X11 fallback...\n");
			backend = &x11_backend_vt;
			if (backend->init(argc, argv) == 0) {
				fprintf(stderr, "tint2: Using X11 backend (fallback)\n");
				return 0;
			}
		}
#endif
		fprintf(stderr, "tint2: Backend initialization failed.\n");
		backend = NULL;
		return 1;
	}

	fprintf(stderr, "tint2: Using %s backend\n",
	        backend->type() == BACKEND_X11 ? "X11" : "Wayland");
	return 0;
}

void backend_cleanup(void)
{
	if (backend) {
		backend->cleanup();
		backend = NULL;
	}
}
