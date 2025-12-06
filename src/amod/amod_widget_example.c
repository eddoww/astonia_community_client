/*
 * Widget System Example Mod
 *
 * This example demonstrates how to use the widget system in a mod.
 * It creates a custom window with buttons and labels that can be
 * toggled with the F9 key.
 *
 * Build: make amod
 *
 * Features demonstrated:
 * - Creating a window with title bar, drag, resize, minimize, close
 * - Adding child widgets (labels, buttons)
 * - Handling mouse events (button clicks)
 * - Handling keyboard events (toggle window)
 * - Custom update logic
 * - Finding widgets by name
 * - Showing/hiding widgets
 */

#include <stdio.h>
#include <string.h>

#include "amod.h"

// ============================================================================
// Widget References
// ============================================================================

static Widget *example_window = NULL;
static Widget *click_counter_label = NULL;
static int click_count = 0;

// ============================================================================
// Event Handlers
// ============================================================================

// Called when the "Click Me!" button is pressed
static int on_click_button(Widget *self, int x, int y, int button)
{
	(void)self;
	(void)x;
	(void)y;

	if (button == MOUSE_BUTTON_LEFT) {
		click_count++;

		// Update the counter label text by finding it and marking dirty
		if (click_counter_label) {
			widget_mark_dirty(click_counter_label);
		}

		addline("Button clicked %d times!", click_count);
		return 1; // Event handled
	}
	return 0;
}

// Called when the "Reset" button is pressed
static int on_reset_button(Widget *self, int x, int y, int button)
{
	(void)self;
	(void)x;
	(void)y;

	if (button == MOUSE_BUTTON_LEFT) {
		click_count = 0;
		if (click_counter_label) {
			widget_mark_dirty(click_counter_label);
		}
		addline("Counter reset!");
		return 1;
	}
	return 0;
}

// Called when the "Close" button is pressed
static int on_close_button(Widget *self, int x, int y, int button)
{
	(void)self;
	(void)x;
	(void)y;

	if (button == MOUSE_BUTTON_LEFT) {
		if (example_window) {
			widget_set_visible(example_window, 0);
		}
		addline("Window hidden. Press F9 to show again.");
		return 1;
	}
	return 0;
}

// Custom render function for the counter label
static void render_counter_label(Widget *self)
{
	int sx, sy;
	widget_get_screen_position(self, &sx, &sy);

	// Draw background
	render_rect(sx, sy, sx + self->width, sy + self->height, darkgraycolor);

	// Draw counter text
	char buf[64];
	snprintf(buf, sizeof(buf), "Clicks: %d", click_count);
	render_text(sx + 5, sy + 5, whitecolor, RENDER_TEXT_SMALL, buf);
}

// ============================================================================
// Window Creation
// ============================================================================

static void create_example_window(void)
{
	Widget *root = widget_manager_get_root();
	if (!root) {
		warn("Widget manager not initialized!");
		return;
	}

	// Check if window already exists
	if (example_window) {
		return;
	}

	// -------------------------------------------------------------------------
	// Create main window container
	// -------------------------------------------------------------------------
	example_window = widget_create(WIDGET_TYPE_CONTAINER, 100, 100, 250, 180);
	if (!example_window) {
		warn("Failed to create example window!");
		return;
	}

	// Set window name (used for state persistence and lookup)
	widget_set_name(example_window, "example_mod_window");

	// Enable window chrome: title bar, draggable, resizable, minimizable, closable
	widget_set_window_chrome(example_window, 1, 1, 1, 1, 1);
	widget_set_title(example_window, "Widget Example");

	// Set minimum size constraints (prevents resizing too small)
	example_window->min_width = 200;
	example_window->min_height = 150;

	// Add to root widget
	widget_add_child(root, example_window);

	// -------------------------------------------------------------------------
	// Create title label
	// -------------------------------------------------------------------------
	Widget *title_label = widget_create(WIDGET_TYPE_LABEL, 10, 30, 230, 20);
	if (title_label) {
		widget_set_name(title_label, "title_label");
		widget_add_child(example_window, title_label);
	}

	// -------------------------------------------------------------------------
	// Create click counter label with custom render
	// -------------------------------------------------------------------------
	click_counter_label = widget_create(WIDGET_TYPE_LABEL, 10, 55, 230, 25);
	if (click_counter_label) {
		widget_set_name(click_counter_label, "counter_label");
		click_counter_label->render = render_counter_label;
		widget_add_child(example_window, click_counter_label);
	}

	// -------------------------------------------------------------------------
	// Create "Click Me!" button
	// -------------------------------------------------------------------------
	Widget *click_button = widget_create(WIDGET_TYPE_BUTTON, 10, 90, 100, 30);
	if (click_button) {
		widget_set_name(click_button, "click_button");
		click_button->on_mouse_down = on_click_button;
		widget_add_child(example_window, click_button);
	}

	// -------------------------------------------------------------------------
	// Create "Reset" button
	// -------------------------------------------------------------------------
	Widget *reset_button = widget_create(WIDGET_TYPE_BUTTON, 120, 90, 70, 30);
	if (reset_button) {
		widget_set_name(reset_button, "reset_button");
		reset_button->on_mouse_down = on_reset_button;
		widget_add_child(example_window, reset_button);
	}

	// -------------------------------------------------------------------------
	// Create "Close" button
	// -------------------------------------------------------------------------
	Widget *close_button = widget_create(WIDGET_TYPE_BUTTON, 10, 130, 230, 30);
	if (close_button) {
		widget_set_name(close_button, "close_button");
		close_button->on_mouse_down = on_close_button;
		widget_add_child(example_window, close_button);
	}

	note("Example window created. Press F9 to toggle visibility.");
}

static void destroy_example_window(void)
{
	if (example_window) {
		widget_destroy(example_window);
		example_window = NULL;
		click_counter_label = NULL;
	}
}

// ============================================================================
// Mod Lifecycle Functions
// ============================================================================

DLL_EXPORT char *amod_version(void)
{
	return "Widget Example Mod 1.0";
}

DLL_EXPORT void amod_init(void)
{
	// Called when mod is loaded
}

DLL_EXPORT void amod_exit(void)
{
	// Called when mod is unloaded - clean up widgets
	destroy_example_window();
}

DLL_EXPORT void amod_gamestart(void)
{
	// Called when game starts - create our window
	create_example_window();
	addline("Widget Example Mod loaded. Press F9 to toggle window.");
}

DLL_EXPORT void amod_frame(void)
{
	// Called every frame - can be used for animations or updates
}

DLL_EXPORT void amod_tick(void)
{
	// Called every game tick
}

// ============================================================================
// Input Handling
// ============================================================================

DLL_EXPORT int amod_keydown(int key)
{
	// F9 key (SDL scancode) - toggle window visibility
	// Note: Key codes may vary, check SDL documentation
	if (key == 0x42) { // F9
		if (example_window) {
			int visible = example_window->visible;
			widget_set_visible(example_window, !visible);
			if (!visible) {
				widget_bring_to_front(example_window);
			}
			addline("Example window %s", visible ? "hidden" : "shown");
			return 1; // Consume the key event
		}
	}
	return 0;
}

DLL_EXPORT int amod_keyup(int key)
{
	(void)key;
	return 0;
}

DLL_EXPORT int amod_mouse_click(int x, int y, int what)
{
	(void)x;
	(void)y;
	(void)what;
	return 0;
}

DLL_EXPORT void amod_mouse_move(int x, int y)
{
	(void)x;
	(void)y;
}

DLL_EXPORT void amod_update_hover_texts(void) {}

// ============================================================================
// Client Command Handling
// ============================================================================

DLL_EXPORT int amod_client_cmd(const char *buf)
{
	// Handle #widget command to toggle window
	if (!strncmp(buf, "#widget", 7)) {
		if (example_window) {
			int visible = example_window->visible;
			widget_set_visible(example_window, !visible);
			if (!visible) {
				widget_bring_to_front(example_window);
			}
		}
		return 1;
	}

	// Handle #widgetinfo command to show widget info
	if (!strncmp(buf, "#widgetinfo", 11)) {
		int count = widget_manager_get_widget_count();
		addline("Total widgets: %d", count);

		if (example_window) {
			int sx, sy;
			widget_get_screen_position(example_window, &sx, &sy);
			addline("Example window: pos=(%d,%d) size=(%d,%d) visible=%d", sx, sy, example_window->width,
			    example_window->height, example_window->visible);
		}
		return 1;
	}

	return 0;
}

// ============================================================================
// Optional: Server Message Processing
// ============================================================================

DLL_EXPORT int amod_process(const char *buf)
{
	(void)buf;
	return 0;
}

DLL_EXPORT int amod_prefetch(const char *buf)
{
	(void)buf;
	return 0;
}

DLL_EXPORT int amod_display_skill_line(int v, int base, int curr, int cn, char *buf)
{
	(void)v;
	(void)base;
	(void)curr;
	(void)cn;
	(void)buf;
	return 0;
}

DLL_EXPORT int amod_is_playersprite(int sprite)
{
	(void)sprite;
	return 0;
}
