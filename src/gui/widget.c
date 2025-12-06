/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Widget System - Core Widget Implementation
 */

#include <string.h>
#include <stdio.h>

#include "astonia.h"
#include "widget.h"
#include "widget_manager.h"
#include "game/game.h"

// Widget chrome constants
#define TITLEBAR_HEIGHT    20
#define RESIZE_HANDLE_SIZE 8

// Widget ID counter for unique IDs
static int next_widget_id = 1;

// =============================================================================
// Widget Core API
// =============================================================================

Widget *widget_create(WidgetType type, int x, int y, int width, int height)
{
	Widget *widget;

	// Allocate widget
	widget = xmalloc(sizeof(Widget), MEM_GUI);
	if (!widget) {
		fail("widget_create: failed to allocate widget");
		return NULL;
	}

	// Zero initialize
	bzero(widget, sizeof(Widget));

	// Set identity
	widget->id = next_widget_id++;
	widget->type = type;
	snprintf(widget->name, sizeof(widget->name), "widget_%d", widget->id);

	// Set position and size
	widget->x = x;
	widget->y = y;
	widget->width = width;
	widget->height = height;

	// Set constraints (no limits by default)
	widget->min_width = 0;
	widget->min_height = 0;
	widget->max_width = -1; // -1 means unlimited
	widget->max_height = -1;

	// Set initial state
	widget->visible = 1;
	widget->enabled = 1;
	widget->focused = 0;
	widget->dirty = 1; // New widgets need initial draw
	widget->hover = 0;
	widget->pressed = 0;

	// Window chrome disabled by default
	widget->has_titlebar = 0;
	widget->draggable = 0;
	widget->resizable = 0;
	widget->minimizable = 0;
	widget->closable = 0;
	widget->minimized = 0;
	widget->modal = 0;

	// Z-order (0 = default)
	widget->z_order = 0;

	// Theming
	widget->skin_id = 0;

	// No virtual functions set (child types will set these)
	widget->render = NULL;
	widget->on_mouse_down = NULL;
	widget->on_mouse_up = NULL;
	widget->on_mouse_move = NULL;
	widget->on_mouse_wheel = NULL;
	widget->on_key_down = NULL;
	widget->on_key_up = NULL;
	widget->on_text_input = NULL;
	widget->on_focus_gain = NULL;
	widget->on_focus_lost = NULL;
	widget->on_resize = NULL;
	widget->on_destroy = NULL;
	widget->update = NULL;

	// No user data
	widget->user_data = NULL;
	widget->user_data_size = 0;

	return widget;
}

void widget_destroy(Widget *widget)
{
	Widget *child, *next_child;

	if (!widget) {
		return;
	}

	// Call custom destroy handler
	if (widget->on_destroy) {
		widget->on_destroy(widget);
	}

	// Destroy all children recursively
	child = widget->first_child;
	while (child) {
		next_child = child->next_sibling;
		widget_destroy(child);
		child = next_child;
	}

	// Remove from parent
	if (widget->parent) {
		widget_remove_child(widget->parent, widget);
	}

	// Free user data if allocated
	if (widget->user_data && widget->user_data_size > 0) {
		xfree(widget->user_data);
		widget->user_data = NULL;
	}

	// Free widget
	xfree(widget);
}

// =============================================================================
// Widget Hierarchy
// =============================================================================

int widget_add_child(Widget *parent, Widget *child)
{
	if (!parent || !child) {
		return 0;
	}

	// Remove from old parent if any
	if (child->parent) {
		widget_remove_child(child->parent, child);
	}

	// Set parent
	child->parent = parent;
	child->next_sibling = NULL;

	// Add to parent's child list
	if (!parent->first_child) {
		// First child
		parent->first_child = child;
		parent->last_child = child;
		child->prev_sibling = NULL;
	} else {
		// Append to end
		parent->last_child->next_sibling = child;
		child->prev_sibling = parent->last_child;
		parent->last_child = child;
	}

	// Mark parent dirty
	widget_mark_dirty(parent);

	// Request z-order rebuild
	WidgetManager *mgr = widget_manager_get();
	if (mgr) {
		mgr->needs_z_resort = 1;
	}

	return 1;
}

int widget_remove_child(Widget *parent, Widget *child)
{
	if (!parent || !child || child->parent != parent) {
		return 0;
	}

	// Remove from sibling list
	if (child->prev_sibling) {
		child->prev_sibling->next_sibling = child->next_sibling;
	} else {
		// Was first child
		parent->first_child = child->next_sibling;
	}

	if (child->next_sibling) {
		child->next_sibling->prev_sibling = child->prev_sibling;
	} else {
		// Was last child
		parent->last_child = child->prev_sibling;
	}

	// Clear links
	child->parent = NULL;
	child->prev_sibling = NULL;
	child->next_sibling = NULL;

	// Mark parent dirty
	widget_mark_dirty(parent);

	// Request z-order rebuild
	WidgetManager *mgr = widget_manager_get();
	if (mgr) {
		mgr->needs_z_resort = 1;
	}

	return 1;
}

Widget *widget_find_child(Widget *parent, const char *name, int recursive)
{
	Widget *child, *found;

	if (!parent || !name) {
		return NULL;
	}

	// Search direct children
	for (child = parent->first_child; child; child = child->next_sibling) {
		if (strcmp(child->name, name) == 0) {
			return child;
		}
	}

	// Recursive search
	if (recursive) {
		for (child = parent->first_child; child; child = child->next_sibling) {
			found = widget_find_child(child, name, 1);
			if (found) {
				return found;
			}
		}
	}

	return NULL;
}

Widget *widget_get_root(Widget *widget)
{
	if (!widget) {
		return NULL;
	}

	while (widget->parent) {
		widget = widget->parent;
	}

	return widget;
}

// =============================================================================
// Widget State
// =============================================================================

void widget_set_visible(Widget *widget, int visible)
{
	if (!widget) {
		return;
	}

	if (widget->visible != visible) {
		widget->visible = visible;
		widget_mark_dirty(widget);
	}
}

void widget_set_enabled(Widget *widget, int enabled)
{
	if (!widget) {
		return;
	}

	if (widget->enabled != enabled) {
		widget->enabled = enabled;
		widget_mark_dirty(widget);
	}
}

void widget_set_focus(Widget *widget)
{
	widget_manager_set_focus(widget);
}

void widget_mark_dirty(Widget *widget)
{
	if (!widget) {
		return;
	}

	// Mark this widget and all parents dirty
	while (widget) {
		widget->dirty = 1;
		widget = widget->parent;
	}
}

void widget_bring_to_front(Widget *widget)
{
	Widget *sibling;
	int max_z = 0;

	if (!widget || !widget->parent) {
		return;
	}

	// Find max z-order among siblings
	for (sibling = widget->parent->first_child; sibling; sibling = sibling->next_sibling) {
		if (sibling != widget && sibling->z_order > max_z) {
			max_z = sibling->z_order;
		}
	}

	// Set to max + 1
	widget->z_order = max_z + 1;

	// Request z-order resort
	WidgetManager *mgr = widget_manager_get();
	if (mgr) {
		mgr->needs_z_resort = 1;
	}

	widget_mark_dirty(widget);
}

void widget_send_to_back(Widget *widget)
{
	Widget *sibling;
	int min_z = 0;

	if (!widget || !widget->parent) {
		return;
	}

	// Find min z-order among siblings
	for (sibling = widget->parent->first_child; sibling; sibling = sibling->next_sibling) {
		if (sibling != widget && sibling->z_order < min_z) {
			min_z = sibling->z_order;
		}
	}

	// Set to min - 1
	widget->z_order = min_z - 1;

	// Request z-order resort
	WidgetManager *mgr = widget_manager_get();
	if (mgr) {
		mgr->needs_z_resort = 1;
	}

	widget_mark_dirty(widget);
}

// =============================================================================
// Widget Layout & Positioning
// =============================================================================

void widget_set_position(Widget *widget, int x, int y)
{
	if (!widget) {
		return;
	}

	if (widget->x != x || widget->y != y) {
		widget->x = x;
		widget->y = y;
		widget_mark_dirty(widget);
	}
}

void widget_set_size(Widget *widget, int width, int height)
{
	if (!widget) {
		return;
	}

	// Apply constraints
	if (widget->min_width > 0 && width < widget->min_width) {
		width = widget->min_width;
	}
	if (widget->max_width > 0 && width > widget->max_width) {
		width = widget->max_width;
	}
	if (widget->min_height > 0 && height < widget->min_height) {
		height = widget->min_height;
	}
	if (widget->max_height > 0 && height > widget->max_height) {
		height = widget->max_height;
	}

	if (widget->width != width || widget->height != height) {
		widget->width = width;
		widget->height = height;

		// Call resize handler
		if (widget->on_resize) {
			widget->on_resize(widget, width, height);
		}

		widget_mark_dirty(widget);
	}
}

void widget_set_bounds(Widget *widget, int x, int y, int width, int height)
{
	if (!widget) {
		return;
	}

	widget_set_position(widget, x, y);
	widget_set_size(widget, width, height);
}

void widget_get_screen_position(Widget *widget, int *screen_x, int *screen_y)
{
	int x = 0, y = 0;

	if (!widget) {
		if (screen_x) {
			*screen_x = 0;
		}
		if (screen_y) {
			*screen_y = 0;
		}
		return;
	}

	// Walk up parent chain, accumulating offsets
	while (widget) {
		x += widget->x;
		y += widget->y;
		widget = widget->parent;
	}

	if (screen_x) {
		*screen_x = x;
	}
	if (screen_y) {
		*screen_y = y;
	}
}

void widget_local_to_screen(Widget *widget, int local_x, int local_y, int *screen_x, int *screen_y)
{
	int sx, sy;

	widget_get_screen_position(widget, &sx, &sy);

	if (screen_x) {
		*screen_x = sx + local_x;
	}
	if (screen_y) {
		*screen_y = sy + local_y;
	}
}

void widget_screen_to_local(Widget *widget, int screen_x, int screen_y, int *local_x, int *local_y)
{
	int sx, sy;

	widget_get_screen_position(widget, &sx, &sy);

	if (local_x) {
		*local_x = screen_x - sx;
	}
	if (local_y) {
		*local_y = screen_y - sy;
	}
}

// =============================================================================
// Widget Hit Testing
// =============================================================================

int widget_hit_test(Widget *widget, int local_x, int local_y)
{
	int min_y, max_y;

	if (!widget || !widget->visible) {
		return 0;
	}

	// Account for title bar if present
	min_y = widget->has_titlebar ? -TITLEBAR_HEIGHT : 0;
	max_y = widget->height;

	return (local_x >= 0 && local_x < widget->width && local_y >= min_y && local_y < max_y);
}

Widget *widget_find_at_position(Widget *root, int screen_x, int screen_y)
{
	Widget *child, *found;
	int local_x, local_y;

	if (!root || !root->visible) {
		return NULL;
	}

	// Convert to local coordinates
	widget_screen_to_local(root, screen_x, screen_y, &local_x, &local_y);

	// Test if inside this widget
	if (!widget_hit_test(root, local_x, local_y)) {
		return NULL;
	}

	// Search children in reverse z-order (front to back)
	// Note: We need to search in reverse order to find topmost widget
	// For now, do a simple iteration (z-order sorting will be in widget_manager)
	for (child = root->first_child; child; child = child->next_sibling) {
		found = widget_find_at_position(child, screen_x, screen_y);
		if (found) {
			return found;
		}
	}

	// No child found, return this widget
	return root;
}

// =============================================================================
// Widget Window Chrome
// =============================================================================

void widget_set_title(Widget *widget, const char *title)
{
	if (!widget || !title) {
		return;
	}

	strncpy(widget->title, title, sizeof(widget->title) - 1);
	widget->title[sizeof(widget->title) - 1] = '\0';

	widget_mark_dirty(widget);
}

void widget_set_name(Widget *widget, const char *name)
{
	if (!widget || !name) {
		return;
	}

	strncpy(widget->name, name, sizeof(widget->name) - 1);
	widget->name[sizeof(widget->name) - 1] = '\0';
}

void widget_set_window_chrome(
    Widget *widget, int has_titlebar, int draggable, int resizable, int minimizable, int closable)
{
	if (!widget) {
		return;
	}

	widget->has_titlebar = has_titlebar;
	widget->draggable = draggable;
	widget->resizable = resizable;
	widget->minimizable = minimizable;
	widget->closable = closable;

	widget_mark_dirty(widget);
}

void widget_set_minimized(Widget *widget, int minimized)
{
	if (!widget) {
		return;
	}

	if (widget->minimized != minimized) {
		widget->minimized = minimized;
		widget_mark_dirty(widget);
	}
}

// =============================================================================
// Widget Rendering Helpers
// =============================================================================

void widget_render_chrome(Widget *widget)
{
	int screen_x, screen_y;
	unsigned short title_bg_color, title_text_color, border_color;

	if (!widget || !widget->has_titlebar) {
		return;
	}

	// Get screen position
	widget_get_screen_position(widget, &screen_x, &screen_y);

	// Theme colors (dark medieval fantasy)
	title_bg_color = IRGB(8, 8, 10); // Dark blue-gray
	title_text_color = IRGB(25, 25, 28); // Light gray
	border_color = IRGB(15, 12, 10); // Brown-gray border

	// Draw title bar background
	render_rect(screen_x, screen_y - TITLEBAR_HEIGHT, screen_x + widget->width, screen_y, title_bg_color);

	// Draw title bar border
	render_line(
	    screen_x, screen_y - TITLEBAR_HEIGHT, screen_x + widget->width, screen_y - TITLEBAR_HEIGHT, border_color);
	render_line(screen_x, screen_y, screen_x + widget->width, screen_y, border_color);

	// Draw title text
	if (widget->title[0]) {
		render_text(screen_x + 5, screen_y - TITLEBAR_HEIGHT + 5, title_text_color,
		    RENDER_TEXT_LEFT | RENDER_TEXT_SMALL, widget->title);
	}

	// Draw close button if closable
	if (widget->closable) {
		int close_x = screen_x + widget->width - 16;
		int close_y = screen_y - TITLEBAR_HEIGHT + 2;

		// Draw close button background
		render_rect(close_x, close_y, close_x + 14, close_y + 14, IRGB(15, 5, 5));

		// Draw X
		render_line(close_x + 3, close_y + 3, close_x + 11, close_y + 11, IRGB(25, 25, 25));
		render_line(close_x + 11, close_y + 3, close_x + 3, close_y + 11, IRGB(25, 25, 25));
	}

	// Draw minimize button if minimizable
	if (widget->minimizable) {
		int min_x = screen_x + widget->width - (widget->closable ? 32 : 16);
		int min_y = screen_y - TITLEBAR_HEIGHT + 2;

		// Draw minimize button background
		render_rect(min_x, min_y, min_x + 14, min_y + 14, IRGB(10, 10, 12));

		// Draw minimize line
		render_line(min_x + 3, min_y + 10, min_x + 11, min_y + 10, IRGB(25, 25, 25));
	}

	// Draw window border (only when not minimized)
	if (!widget->minimized) {
		render_line(screen_x, screen_y, screen_x, screen_y + widget->height, border_color);
		render_line(
		    screen_x + widget->width, screen_y, screen_x + widget->width, screen_y + widget->height, border_color);
		render_line(
		    screen_x, screen_y + widget->height, screen_x + widget->width, screen_y + widget->height, border_color);
	}

	// Draw resize handles if resizable (make them very visible)
	if (widget->resizable && !widget->minimized) {
		int handle_size = 12; // Larger handle for easier grabbing
		unsigned short handle_color = IRGB(20, 20, 25); // Brighter color

		// Bottom-right corner - draw a visible triangle/grip pattern
		for (int i = 0; i < 3; i++) {
			int offset = i * 4;
			render_line(screen_x + widget->width - handle_size + offset, screen_y + widget->height,
			    screen_x + widget->width, screen_y + widget->height - handle_size + offset, handle_color);
		}

		// Also draw a filled rectangle in the corner for extra visibility
		render_rect(screen_x + widget->width - handle_size, screen_y + widget->height - handle_size,
		    screen_x + widget->width, screen_y + widget->height, IRGB(12, 12, 15));
	}
}

int widget_handle_titlebar_drag(Widget *widget, int screen_x, int screen_y)
{
	// This is handled by widget_manager
	return 0;
}

int widget_handle_resize(Widget *widget, int screen_x, int screen_y, int handle)
{
	// This is handled by widget_manager
	return 0;
}

int widget_get_resize_handle(Widget *widget, int screen_x, int screen_y)
{
	int wx, wy;
	int local_x, local_y;

	if (!widget || !widget->resizable || widget->minimized) {
		return -1;
	}

	// Get widget screen position
	widget_get_screen_position(widget, &wx, &wy);

	// Convert to widget-local coordinates
	local_x = screen_x - wx;
	local_y = screen_y - wy;

	// Check corner and edge handles (use larger size for easier grabbing)
	int handle_size = 12;

	// Bottom-right corner
	if (local_x >= widget->width - handle_size && local_x <= widget->width && local_y >= widget->height - handle_size &&
	    local_y <= widget->height) {
		return 4; // Bottom-right
	}

	// Bottom edge
	if (local_y >= widget->height - handle_size && local_y <= widget->height && local_x >= handle_size &&
	    local_x <= widget->width - handle_size) {
		return 5; // Bottom
	}

	// Right edge
	if (local_x >= widget->width - handle_size && local_x <= widget->width && local_y >= handle_size &&
	    local_y <= widget->height - handle_size) {
		return 3; // Right
	}

	return -1; // No handle
}
