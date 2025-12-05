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
static Widget *demo_tooltip = NULL;

static int demo_initialized = 0;
static int demo_enabled = 0; // Toggle with F11
static int click_count = 0;
static int button3_hovered = 0; // Track if button3 is currently hovered

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
 */
void widget_demo_init(void)
{
	if (demo_initialized) {
		return;
	}

	// Initialize widget manager (800x600 is the default screen size for Astonia)
	if (widget_manager_init(800, 600) != 0) {
		// Can't use xlog here as it's not accessible, just return
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

	// Create button 3 (Hover me for tooltip)
	demo_button3 = widget_button_create(0, 0, 220, 30, "Hover Me!");
	if (demo_button3) {
		widget_button_set_callback(demo_button3, on_button3_click, NULL);
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

	// Create tooltip for button3
	demo_tooltip = widget_tooltip_create(0, 0);
	if (demo_tooltip) {
		widget_tooltip_set_text(demo_tooltip,
		    "Hover tooltip test!\nClick to reset counter\nDrag the title bar to move\nResize from edges/corners");
		widget_tooltip_set_delay(demo_tooltip, 300);

		// Add tooltip to root widget
		if (root) {
			widget_add_child(root, demo_tooltip);
		}
	}

	// Update container layout now that all children are added
	widget_container_update_layout(demo_container);

	// Rebuild z-order list to ensure all widgets (including tooltip) are included
	widget_manager_rebuild_z_order();

	demo_initialized = 1;
	demo_enabled = 1; // Enable by default for testing
}

/**
 * Cleanup widget demo
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
	if (demo_tooltip) {
		widget_destroy(demo_tooltip);
		demo_tooltip = NULL;
	}

	widget_manager_cleanup();

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

	if (demo_tooltip) {
		widget_set_visible(demo_tooltip, 0); // Hide tooltip when toggling
	}
}

/**
 * Update widget demo (call every frame)
 */
void widget_demo_update(int dt)
{
	if (!demo_initialized || !demo_enabled) {
		return;
	}

	// Update widget manager (handles animations, tooltips, etc.)
	widget_manager_update(dt);

	// Show tooltip when hovering over button3 (use state tracking to avoid resetting timer)
	if (demo_button3 && demo_tooltip && demo_enabled) {
		int screen_x, screen_y;
		widget_get_screen_position(demo_button3, &screen_x, &screen_y);

		int is_over = (mousex >= screen_x && mousex <= screen_x + demo_button3->width && mousey >= screen_y &&
		               mousey <= screen_y + demo_button3->height);

		if (is_over && !button3_hovered) {
			// Just started hovering - show tooltip (starts delay timer)
			widget_tooltip_show_at_mouse(demo_tooltip, mousex, mousey);
			button3_hovered = 1;
		} else if (is_over && button3_hovered) {
			// Still hovering - just update position (don't reset timer)
			if (demo_tooltip->visible) {
				widget_tooltip_update_position(demo_tooltip, mousex, mousey);
			}
		} else if (!is_over && button3_hovered) {
			// Stopped hovering - hide tooltip
			widget_tooltip_hide(demo_tooltip);
			button3_hovered = 0;
		}
	}
}

/**
 * Render widget demo (call in display())
 */
void widget_demo_render(void)
{
	if (!demo_initialized || !demo_enabled) {
		return;
	}

	// Render all widgets through the widget manager
	widget_manager_render();
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
