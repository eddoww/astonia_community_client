/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Widget System Demo/Test Integration
 *
 * This file demonstrates the new widget system by creating test widgets
 * that render on top of the existing GUI.
 */

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../astonia.h"
#include "gui.h"
#include "gui_private.h"
#include "widget.h"
#include "widget_manager.h"
#include "widgets/widget_container.h"
#include "widgets/widget_button.h"
#include "widgets/widget_label.h"
#include "widgets/widget_progressbar.h"
#include "widgets/widget_textinput.h"
#include "widgets/widget_tooltip.h"
#include "../game/game.h"

// Demo widget references
static Widget *demo_container = NULL;
static Widget *demo_button1 = NULL;
static Widget *demo_button2 = NULL;
static Widget *demo_button3 = NULL;
static Widget *demo_label = NULL;
static Widget *demo_progressbar = NULL;
static Widget *demo_textinput = NULL;

static int demo_initialized = 0;
static int demo_enabled = 0; // Toggle with F11
static int click_count = 0;

// Button callbacks
static void on_button1_click(Widget *button, void *param)
{
	click_count++;
	char text[64];
	snprintf(text, sizeof(text), "Widget Test - Clicked %d times", click_count);
	widget_label_set_text(demo_label, text);

	// Update progress bar
	float progress = (click_count % 10) * 10.0f;
	widget_progressbar_set_value(demo_progressbar, progress);
}

static void on_button2_click(Widget *button, void *param)
{
	// Toggle text input visibility
	widget_set_visible(demo_textinput, !demo_textinput->visible);

	// Update layout to reposition widgets
	if (demo_container) {
		widget_container_update_layout(demo_container);
	}
}

static void on_button3_click(Widget *button, void *param)
{
	// Reset counter and progress bar
	click_count = 0;
	widget_label_set_text(demo_label, "Widget System Test");
	widget_progressbar_set_value(demo_progressbar, 0.0f);
}

static void on_textinput_submit(Widget *input, const char *text, void *param)
{
	char label_text[128];
	snprintf(label_text, sizeof(label_text), "You entered: %s", text);
	widget_label_set_text(demo_label, label_text);
	widget_textinput_clear(input);
}

/**
 * Initialize the widget demo
 * Creates test widgets positioned in the top-right area
 * Note: widget_manager must already be initialized before calling this
 */
void widget_demo_init(void)
{
	if (demo_initialized) {
		return;
	}

	// Verify widget manager is initialized
	if (!widget_manager_get()) {
		return;
	}

	// Create container for demo widgets (top-right area)
	demo_container = widget_container_create(550, 50, 240, 300);
	if (!demo_container) {
		return;
	}
	widget_container_set_layout(demo_container, LAYOUT_VERTICAL);
	widget_container_set_spacing(demo_container, 10, 8); // padding=10, spacing=8
	widget_container_set_background(demo_container, IRGB(5, 5, 7), 1); // Enable dark background

	// Enable window chrome (title bar, dragging, resizing, minimizing)
	widget_set_window_chrome(
	    demo_container, 1, 1, 1, 1, 1); // has_titlebar, draggable, resizable, minimizable, closable
	widget_set_title(demo_container, "Widget Demo");
	widget_set_name(demo_container, "widget_demo"); // Name for state persistence

	// Add container to root widget so it gets rendered
	Widget *root = widget_manager_get_root();
	if (root) {
		widget_add_child(root, demo_container);
	}

	// Create label
	demo_label = widget_label_create(0, 0, 220, 20, "Widget System Test");
	if (demo_label) {
		widget_label_set_alignment(demo_label, LABEL_ALIGN_CENTER);
		widget_label_set_color(demo_label, IRGB(28, 28, 31));
		widget_add_child(demo_container, demo_label);
	}

	// Create button 1
	demo_button1 = widget_button_create(0, 0, 220, 30, "Click Me!");
	if (demo_button1) {
		widget_button_set_callback(demo_button1, on_button1_click, NULL);
		widget_add_child(demo_container, demo_button1);
	}

	// Create button 2
	demo_button2 = widget_button_create(0, 0, 220, 30, "Toggle Text Input");
	if (demo_button2) {
		widget_button_set_callback(demo_button2, on_button2_click, NULL);
		widget_add_child(demo_container, demo_button2);
	}

	// Create button 3 (Hover me for tooltip - uses built-in tooltip support)
	demo_button3 = widget_button_create(0, 0, 220, 30, "Hover Me!");
	if (demo_button3) {
		widget_button_set_callback(demo_button3, on_button3_click, NULL);
		// Use the new built-in tooltip support instead of manual tooltip widget
		widget_set_tooltip_text(demo_button3,
		    "Hover tooltip test!\nClick to reset counter\nDrag the title bar to move\nResize from edges/corners");
		widget_set_tooltip_delay(demo_button3, 300);
		widget_add_child(demo_container, demo_button3);
	}

	// Create progress bar
	demo_progressbar = widget_progressbar_create(0, 0, 220, 20, PROGRESSBAR_HORIZONTAL);
	if (demo_progressbar) {
		widget_progressbar_set_range(demo_progressbar, 0.0f, 100.0f);
		widget_progressbar_set_fill_color(demo_progressbar, IRGB(15, 25, 15));
		widget_add_child(demo_container, demo_progressbar);
	}

	// Create text input (initially hidden)
	demo_textinput = widget_textinput_create(0, 0, 220, 25);
	if (demo_textinput) {
		widget_textinput_set_placeholder(demo_textinput, "Type here...");
		widget_textinput_set_submit_callback(demo_textinput, on_textinput_submit, NULL);
		widget_textinput_set_max_length(demo_textinput, 32);
		widget_add_child(demo_container, demo_textinput);
		widget_set_visible(demo_textinput, 0); // Start hidden
	}

	// Note: Tooltips are now handled automatically by the widget_manager
	// when widget_set_tooltip_text() is used on widgets

	// Update container layout now that all children are added
	widget_container_update_layout(demo_container);

	// Rebuild z-order list to ensure all widgets are included
	widget_manager_rebuild_z_order();

	demo_initialized = 1;
	demo_enabled = 1; // Enable by default for testing
}

/**
 * Cleanup widget demo
 * Note: widget_manager cleanup is handled externally, this only destroys demo widgets
 */
void widget_demo_cleanup(void)
{
	if (!demo_initialized) {
		return;
	}

	if (demo_container) {
		widget_destroy(demo_container);
		demo_container = NULL;
	}

	demo_initialized = 0;
	demo_enabled = 0;
}

/**
 * Toggle widget demo visibility
 */
void widget_demo_toggle(void)
{
	if (!demo_initialized) {
		widget_demo_init();
		return;
	}

	// If container was closed with X button, it's hidden but demo_enabled may still be 1
	// In that case, just show it again instead of toggling
	if (demo_container && !demo_container->visible) {
		widget_set_visible(demo_container, 1);
		demo_enabled = 1;
	} else {
		// Normal toggle
		demo_enabled = !demo_enabled;
		if (demo_container) {
			widget_set_visible(demo_container, demo_enabled);
		}
	}
}

/**
 * Update widget demo (call every frame)
 * Note: widget_manager_update() is called externally, this only handles demo-specific logic
 * Note: Tooltip handling is now automatic via widget_set_tooltip_text()
 */
void widget_demo_update(int dt)
{
	if (!demo_initialized || !demo_enabled) {
		return;
	}

	// Tooltips are now handled automatically by widget_manager
	// No manual tooltip code needed here anymore!
}

/**
 * Render widget demo (call in display())
 * Note: widget_manager_render() is called externally, this function is kept for compatibility
 */
void widget_demo_render(void)
{
	// Rendering is handled by widget_manager_render() called from gui_display.c
	// This function is now a no-op but kept for API compatibility
}

/**
 * Handle mouse button events
 * Returns 1 if event was handled by widgets, 0 otherwise
 */
int widget_demo_handle_mouse_button(int x, int y, int button, int down)
{
	if (!demo_initialized || !demo_enabled) {
		return 0;
	}

	// Forward to widget manager
	return widget_manager_handle_mouse(x, y, button, down ? MOUSE_ACTION_DOWN : MOUSE_ACTION_UP);
}

/**
 * Handle mouse motion
 */
void widget_demo_handle_mouse_motion(int x, int y)
{
	if (!demo_initialized || !demo_enabled) {
		return;
	}

	// Update widget manager with mouse position
	widget_manager_handle_mouse(x, y, 0, MOUSE_ACTION_MOVE);
}

/**
 * Handle keyboard events
 * Returns 1 if event was handled by widgets, 0 otherwise
 */
int widget_demo_handle_key(int key, int down)
{
	Widget *focused;

	if (!demo_initialized || !demo_enabled) {
		return 0;
	}

	// If a text input widget has focus, block ALL keypresses from reaching the game
	focused = widget_manager_get_focus();
	if (focused && focused->type == WIDGET_TYPE_TEXTINPUT) {
		// Let widget manager handle special keys (arrows, backspace, etc.)
		widget_manager_handle_key(key, down ? 1 : 0);
		return 1; // Always consume the key to prevent game from processing it
	}

	// Forward to widget manager for other widgets
	return widget_manager_handle_key(key, down ? 1 : 0);
}

/**
 * Handle text input events
 * Returns 1 if handled, 0 if not (allows old GUI to process)
 */
int widget_demo_handle_text_input(const char *text)
{
	Widget *focused;

	if (!demo_initialized || !demo_enabled || !text) {
		return 0;
	}

	// Only process text input if a text input widget has focus
	focused = widget_manager_get_focus();
	if (!focused || focused->type != WIDGET_TYPE_TEXTINPUT) {
		return 0; // Not handled, let old GUI process it
	}

	// Send each character to the widget manager
	for (int i = 0; text[i]; i++) {
		widget_manager_handle_text((int)(unsigned char)text[i]);
	}

	return 1; // Handled by widget
}

/**
 * Check if widget demo is enabled
 */
int widget_demo_is_enabled(void)
{
	return demo_initialized && demo_enabled;
}
