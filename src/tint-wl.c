/**************************************************************************
 * tint-wl.c
 *
 * Wayland panel using the full tint2 rendering engine.
 *
 * Architecture:
 *   1. Initialize the Wayland backend
 *   2. Initialize the compatibility layer (X11 → backend redirection)
 *   3. Load tint2 config
 *   4. Initialize the panel (taskbar, clock, launcher, etc.)
 *   5. Run the event loop using the backend
 *
 * The tint2 panel.c / area.c / taskbar / clock code works unmodified
 * because all X11 calls are intercepted by the compat layer.
 *
 * Copyright (C) 2026 tint-j project
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/select.h>
#include <poll.h>
#include <time.h>
#include <X11/Xlocale.h>

#include "compat/compat.h"
#include "compat/server.h"
#include "config.h"
#include "panel.h"
#include "task.h"
#include "taskbar.h"
#include "systraybar.h"
#include "launcher.h"
#include "tooltip.h"
#include "timer.h"
#include "window.h"

// ============================================================================
// Global state
// ============================================================================

static volatile sig_atomic_t running = 1;
// signal_pending is declared extern in panel.h, defined in panel.c

// Externals expected by panel.c/tint.c
// Most globals are defined in their respective .c files.
// We only define stubs for things NOT compiled.

Window net_sel_win = 0;
gboolean systray_enabled = FALSE;
int systray_profile = 0;
int refresh_systray = 0;

// ============================================================================
// Signal handling
// ============================================================================

static void signal_handler(int sig)
{
	// signal_pending is defined in panel.c
	extern volatile sig_atomic_t signal_pending;
	signal_pending = sig;
	running = 0;
}

// ============================================================================
// Event loop
// ============================================================================

// Forward declaration
static void draw_area_to_cairo(cairo_t *cr, Area *a, int offset_x, int offset_y);

static void draw_panels(void)
{
	if (!panels || num_panels <= 0)
		return;

	for (int i = 0; i < num_panels; i++) {
		Panel *panel = &panels[i];
		if (!panel->area.on_screen)
			continue;

		// Ensure areas are scheduled for redraw
				schedule_redraw(&panel->area);
				set_panel_background(panel);

				// Re-layout and draw all areas into their pixmaps
				render_panel(panel);

		// Create Wayland buffer for the main window and copy rendered pixmaps
				BkWindow *bw = win_lookup(panel->main_win);
				if (!bw)
					continue;

				int pw = panel->area.width;
				int ph = panel->area.height;

				cairo_surface_t *cs = bk_cairo_surface_create_for_window(bw, pw, ph);
				if (!cs)
					continue;

		cairo_t *cr = cairo_create(cs);

				// DEBUG: fill with solid color to verify visibility
				cairo_set_source_rgba(cr, 0.2, 0.2, 0.3, 0.95);
				cairo_paint(cr);

				// Walk the area tree and blit each area's pixmap onto the window buffer
		draw_area_to_cairo(cr, &panel->area, 0, 0);

		cairo_destroy(cr);
		cairo_surface_destroy(cs);

		// Commit to Wayland
		bk_window_present(bw);
	}
}

static void draw_area_to_cairo(cairo_t *cr, Area *a, int offset_x, int offset_y)
{
	if (!a->on_screen)
		return;

	// Draw this area's pixmap onto the composite surface
		if (a->pix && a->width > 0 && a->height > 0) {
			BkPixmap *bp = pix_lookup(a->pix);
			if (bp) {
				cairo_surface_t *asurf = bk_cairo_surface_create_for_pixmap(bp, a->width, a->height);
				if (asurf) {
					cairo_save(cr);
					cairo_translate(cr, a->posx, a->posy);
					cairo_set_source_surface(cr, asurf, 0, 0);
					cairo_rectangle(cr, 0, 0, a->width, a->height);
					cairo_fill(cr);
					cairo_restore(cr);
					cairo_surface_destroy(asurf);
				}
			}
		}

	// Draw children
	for (GList *l = a->children; l; l = l->next) {
		Area *child = (Area *)l->data;
		draw_area_to_cairo(cr, child, offset_x + a->posx, offset_y + a->posy);
	}
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char *argv[])
{
	// Set up signal handlers
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGUSR1, signal_handler);

	// Initialize locale
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "POSIX");

	// Flush stdout/stderr per line
	setlinebuf(stdout);
	setlinebuf(stderr);

	// ---- Step 1: Initialize Wayland backend ----
	fprintf(stderr, "tint-wl: Initializing Wayland backend...\n");
	if (backend_init(-1, &argc, &argv) != 0) {
		fprintf(stderr, "tint-wl: Failed to initialize Wayland backend\n");
		return 1;
	}

	// ---- Step 2: Initialize compat layer ----
	fprintf(stderr, "tint-wl: Initializing compatibility layer...\n");
	if (compat_init() != 0) {
		fprintf(stderr, "tint-wl: Failed to initialize compat layer\n");
		backend_cleanup();
		return 1;
	}

	// Set up the global server pseudo-state
	server.display = server_display;
	server.root_win = server_root_win;
	server.gc = server_gc;
	server.depth = server_depth;
	server.screen = DefaultScreen(server.display);
	server.viewports = NULL;
	server.real_transparency = TRUE;
	server.disable_transparency = FALSE;
	server.composite_manager = 0;
	server.got_root_win = TRUE;

	server_init_atoms();
	server_init_visual();
	get_monitors();
	get_desktops();

	// ---- Step 3: Initialize tint2 subsystems ----
	fprintf(stderr, "tint-wl: Initializing tint2 subsystems...\n");

	default_config();
	default_timeout();
	default_systray();
	default_clock();
	default_launcher();
	default_taskbar();
	default_tooltip();
	default_execp();
	default_panel();

	// Process command line
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			i++;
			if (config_path) free(config_path);
			config_path = strdup(argv[i]);
		} else if (i + 1 == argc && argv[i][0] != '-') {
			if (config_path) free(config_path);
			config_path = strdup(argv[i]);
		}
	}

	// Load config
		if (!config_read()) {
					fprintf(stderr, "tint-wl: Could not read config, using built-in defaults\n");
					g_free(panel_items_order);
					panel_items_order = strdup("TC");  // Taskbar + Clock only
					panel_config.area.width = 100;
					panel_config.area.height = 32;
					panel_config.monitor = -1;
				} else {
						// Override: disable systray and launcher on Wayland (not supported yet)
						fprintf(stderr, "tint-wl: Config loaded, stripping unsupported items for Wayland\n");
						// Remove 'L' and 'S' from panel_items_order
						char *p = panel_items_order;
						char *q = panel_items_order;
						while (*p) {
							if (*p != 'L' && *p != 'S')
								*q++ = *p;
							p++;
						}
						*q = '\0';
					}
					systray_enabled = FALSE;

	// Initialize Imlib2 (without X11 display)
	imlib_context_set_display(NULL);
	imlib_context_set_visual(NULL);
	imlib_context_set_colormap(NULL);

	// Load default icon
	default_icon = NULL;
	const gchar *const *data_dirs = g_get_system_data_dirs();
	for (int i = 0; data_dirs && data_dirs[i] != NULL; i++) {
		gchar *path = g_build_filename(data_dirs[i], "tint2", "default_icon.png", NULL);
		if (g_file_test(path, G_FILE_TEST_EXISTS))
			default_icon = imlib_load_image(path);
		g_free(path);
	}

	// ---- Step 4: Initialize the panel ----
	fprintf(stderr, "tint-wl: Creating panel...\n");
		fflush(stderr);

		init_panel();

		fprintf(stderr, "tint-wl: Panel created, panels=%p num=%d\n", (void*)panels, num_panels);
		fflush(stderr);

	if (snapshot_path) {
		// Snapshot mode: render once and exit
		fprintf(stderr, "tint-wl: Snapshot mode not fully supported\n");
		goto cleanup;
	}

	// Do initial draw
	fprintf(stderr, "tint-wl: Initial panel rendering...\n");
	draw_panels();

	// ---- Step 5: Main event loop ----
	fprintf(stderr, "tint-wl: Entering event loop...\n");

	XEvent e;
	struct timeval tv_last_draw;
		gettimeofday(&tv_last_draw, NULL);
		gboolean panel_refresh = FALSE;

	while (running) {
		// Check for timeout-based redraws (clock updates)
		struct timeval tv_now;
				gettimeofday(&tv_now, NULL);
				// Set refresh flag on second boundary (for clock)
				if (tv_now.tv_sec > tv_last_draw.tv_sec) {
					tv_last_draw = tv_now;
					panel_refresh = TRUE;
				}
				// Redraw if needed (max once per second)
				if (panel_refresh) {
					panel_refresh = FALSE;
					draw_panels();
				}

		// Poll for events from backend (blocking with timeout)
		int fd = bk_get_fd();
		if (fd >= 0) {
			struct pollfd pfd = { .fd = fd, .events = POLLIN };
			// Sleep for max 50ms to allow timeout updates and clock to refresh
			poll(&pfd, 1, 50);
		} else {
			// Fallback if no fd
			usleep(50000);
		}

		// Process timeouts
		update_next_timeout();

		// Flush pending backend output
		bk_flush();

		// Poll for events from backend
		BkEvent *bev;
		while ((bev = bk_poll_event()) != NULL) {
			switch (bev->type) {
			case BK_EVENT_CONFIGURE:
				// Panel reconfigured
				panel_refresh = TRUE;
				break;

			case BK_EVENT_MOUSE_PRESS:
			case BK_EVENT_MOUSE_RELEASE:
			case BK_EVENT_MOUSE_MOTION:
			case BK_EVENT_ENTER:
			case BK_EVENT_LEAVE:
				// These are translated to X11 events and handled by the event loop
				break;

			case BK_EVENT_DESKTOP_CHANGED:
				server.desktop = bk_get_current_desktop();
				panel_refresh = TRUE;
				break;

			case BK_EVENT_TOPLEVEL_ADDED:
			case BK_EVENT_TOPLEVEL_REMOVED:
			case BK_EVENT_TOPLEVEL_CHANGED:
				// Taskbar needs refresh
				panel_refresh = TRUE;
				break;

			case BK_EVENT_DESTROY:
				running = 0;
				break;

			default:
				break;
			}
			bk_event_free(bev);
		}

		// Process X11-style events (translated from backend by XPending/XNextEvent)
		if (XPending(server.display) > 0) {
			XNextEvent(server.display, &e);

			Panel *panel = get_panel(e.xany.window);

			switch (e.type) {
			case Expose:
				panel_refresh = TRUE;
				break;

			case ButtonPress: {
				Area *area = click_area(panel, e.xbutton.x, e.xbutton.y);
				if (panel && panel_config.mouse_effects)
					mouse_over(area, 1);
				break;
			}

			case ButtonRelease: {
				if (panel) {
					// Handle task clicks
					Task *task = click_task(panel, e.xbutton.x, e.xbutton.y);
					if (task && e.xbutton.button == 1) {
						// Activate task
						activate_window(task->win);
					}
				}
				Area *area = click_area(panel, e.xbutton.x, e.xbutton.y);
				if (panel && panel_config.mouse_effects)
					mouse_over(area, 0);
				break;
			}

			case MotionNotify: {
				Area *area = click_area(panel, e.xmotion.x, e.xmotion.y);
				if (panel && panel_config.mouse_effects)
					mouse_over(area, 0);
				break;
			}

			case EnterNotify:
				if (panel && panel_autohide)
					autohide_trigger_show(panel);
				break;

			case LeaveNotify:
				if (panel && panel_autohide)
					autohide_trigger_hide(panel);
				break;

			case PropertyNotify:
				// Desktop/window changes; handled by polling
				if (e.xproperty.window == server.root_win) {
					panel_refresh = TRUE;
				}
				break;

			case ConfigureNotify:
				// Panel geometry changed
				panel_refresh = TRUE;
				break;

			case DestroyNotify:
				running = 0;
				break;

			default:
				break;
			}
		}

		// Process timeouts
		static double last_timeout_check = 0;
		double now_sec = get_time();
		if (now_sec - last_timeout_check > 0.05) {
			callback_timeout_expired();
			last_timeout_check = now_sec;
		}

		// Check signal pending
		extern volatile sig_atomic_t signal_pending;
		if (signal_pending) {
			fprintf(stderr, "tint-wl: Signal %d received, exiting\n", signal_pending);
			running = 0;
		}
	}

cleanup:
	fprintf(stderr, "tint-wl: Cleaning up...\n");

	cleanup_panel();
	cleanup_systray();
	cleanup_tooltip();
	cleanup_clock();
	cleanup_launcher();
#ifdef ENABLE_BATTERY
	cleanup_battery();
#endif
	cleanup_execp();
	cleanup_config();
	cleanup_timeout();
	cleanup_server();

	if (default_icon) {
		imlib_context_set_image(default_icon);
		imlib_free_image();
		default_icon = NULL;
	}

	compat_cleanup();
	backend_cleanup();

	fprintf(stderr, "tint-wl: Exited cleanly\n");
	return 0;
}
