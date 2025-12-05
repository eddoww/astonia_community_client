/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Widget Manager - Centralized Widget Management Implementation
 */

#include <string.h>
#include <stdio.h>
#include <SDL2/SDL.h>

#include "astonia.h"
#include "widget.h"
#include "widget_manager.h"
#include "game/game.h"

// Global widget manager instance
static WidgetManager *g_widget_manager = NULL;

// =============================================================================
// Internal Helper Functions
// =============================================================================

/**
 * Recursively add widget and children to z-order list
 */
static void add_to_z_order_recursive(WidgetManager *mgr, Widget *widget)
{
	Widget *child;

	if (!widget) {
		return;
	}

	// Expand z-order list if needed
	if (mgr->z_order_count >= mgr->z_order_capacity) {
		int new_capacity = mgr->z_order_capacity * 2;
		Widget **new_list = xrealloc(mgr->z_order_list, new_capacity * sizeof(Widget *), MEM_GUI);
		if (!new_list) {
			fail("add_to_z_order_recursive: failed to expand z-order list");
			return;
		}
		mgr->z_order_list = new_list;
		mgr->z_order_capacity = new_capacity;
	}

	// Add this widget
	mgr->z_order_list[mgr->z_order_count++] = widget;

	// Add children
	for (child = widget->first_child; child; child = child->next_sibling) {
		add_to_z_order_recursive(mgr, child);
	}
}

/**
 * Z-order comparison for qsort
 */
static int z_order_compare(const void *a, const void *b)
{
	Widget *wa = *(Widget **)a;
	Widget *wb = *(Widget **)b;

	return wa->z_order - wb->z_order;
}

/**
 * Count total widgets in hierarchy
 */
static int count_widgets_recursive(Widget *widget)
{
	Widget *child;
	int count = 1; // Count this widget

	for (child = widget->first_child; child; child = child->next_sibling) {
		count += count_widgets_recursive(child);
	}

	return count;
}

// =============================================================================
// Widget Manager Core API
// =============================================================================

int widget_manager_init(int screen_width, int screen_height)
{
	if (g_widget_manager) {
		note("widget_manager_init: already initialized");
		return 0;
	}

	// Allocate manager
	g_widget_manager = xmalloc(sizeof(WidgetManager), MEM_GUI);
	if (!g_widget_manager) {
		fail("widget_manager_init: failed to allocate widget manager");
		return 0;
	}

	// Zero initialize
	bzero(g_widget_manager, sizeof(WidgetManager));

	// Create root widget (full screen - needed for widget_find_at_position to work)
	// The root check in handle_mouse prevents it from blocking game clicks
	g_widget_manager->root = widget_create(WIDGET_TYPE_CONTAINER, 0, 0, screen_width, screen_height);
	if (!g_widget_manager->root) {
		fail("widget_manager_init: failed to create root widget");
		xfree(g_widget_manager);
		g_widget_manager = NULL;
		return 0;
	}

	strcpy(g_widget_manager->root->name, "root");

	// Initialize z-order list
	g_widget_manager->z_order_capacity = 256;
	g_widget_manager->z_order_list = xmalloc(g_widget_manager->z_order_capacity * sizeof(Widget *), MEM_GUI);
	if (!g_widget_manager->z_order_list) {
		fail("widget_manager_init: failed to allocate z-order list");
		widget_destroy(g_widget_manager->root);
		xfree(g_widget_manager);
		g_widget_manager = NULL;
		return 0;
	}
	g_widget_manager->z_order_count = 0;

	// Initialize state
	g_widget_manager->focused = NULL;
	g_widget_manager->hovered = NULL;
	g_widget_manager->needs_full_redraw = 1;
	g_widget_manager->needs_z_resort = 1;
	g_widget_manager->dragging_widget = NULL;
	g_widget_manager->dragging_item = NULL;
	g_widget_manager->drag_data = NULL;
	g_widget_manager->drag_data_type = 0;
	g_widget_manager->resizing_widget = NULL;
	g_widget_manager->resize_handle = -1;
	g_widget_manager->modal_widget = NULL;
	g_widget_manager->frame_count = 0;
	g_widget_manager->widget_count = 1; // Root widget
	g_widget_manager->last_update_time = SDL_GetTicks();

	note("Widget manager initialized (%dx%d)", screen_width, screen_height);

	return 1;
}

void widget_manager_cleanup(void)
{
	if (!g_widget_manager) {
		return;
	}

	// Destroy all widgets (root will recursively destroy children)
	if (g_widget_manager->root) {
		widget_destroy(g_widget_manager->root);
		g_widget_manager->root = NULL;
	}

	// Free z-order list
	if (g_widget_manager->z_order_list) {
		xfree(g_widget_manager->z_order_list);
		g_widget_manager->z_order_list = NULL;
	}

	// Free manager
	xfree(g_widget_manager);
	g_widget_manager = NULL;

	note("Widget manager cleaned up");
}

WidgetManager *widget_manager_get(void)
{
	return g_widget_manager;
}

Widget *widget_manager_get_root(void)
{
	if (!g_widget_manager) {
		return NULL;
	}

	return g_widget_manager->root;
}

// =============================================================================
// Widget Manager Rendering
// =============================================================================

/**
 * Check if a widget is effectively visible (including parent chain)
 */
static int is_widget_effectively_visible(Widget *widget)
{
	Widget *w;

	if (!widget) {
		return 0;
	}

	// Check this widget and all parents
	for (w = widget; w; w = w->parent) {
		if (!w->visible) {
			return 0;
		}
		// Also check if parent is minimized
		if (w->parent && w->parent->minimized) {
			return 0;
		}
	}

	return 1;
}

/**
 * Render a single widget and its children
 */
static void render_widget_recursive(Widget *widget)
{
	Widget *child;

	if (!widget || !widget->visible) {
		return;
	}

	// Render this widget
	if (widget->render) {
		widget->render(widget);
	}

	// Render window chrome if enabled
	if (widget->has_titlebar && !widget->minimized) {
		widget_render_chrome(widget);
	}

	// Render children (they're in z-order in the list)
	for (child = widget->first_child; child; child = child->next_sibling) {
		render_widget_recursive(child);
	}

	// Clear dirty flag
	widget->dirty = 0;
}

void widget_manager_render(void)
{
	int i;

	if (!g_widget_manager || !g_widget_manager->root) {
		return;
	}

	// Rebuild z-order list if needed
	if (g_widget_manager->needs_z_resort) {
		widget_manager_rebuild_z_order();
	}

	// Render widgets in z-order (back to front)
	for (i = 0; i < g_widget_manager->z_order_count; i++) {
		Widget *widget = g_widget_manager->z_order_list[i];
		// Check effective visibility (widget and all parents must be visible)
		if (is_widget_effectively_visible(widget)) {
			// Render content (skip if minimized)
			if (!widget->minimized && widget->render) {
				widget->render(widget);
			}

			// Render window chrome (always render if has titlebar, even when minimized)
			if (widget->has_titlebar) {
				widget_render_chrome(widget);
			}
		}
	}

	// Render dragged item if any
	if (g_widget_manager->dragging_item && g_widget_manager->drag_data) {
		// TODO: Render dragged item at mouse cursor
		// This will be implemented when we have ItemSlot widget
	}

	g_widget_manager->needs_full_redraw = 0;
	g_widget_manager->frame_count++;
}

void widget_manager_update(int dt)
{
	int i;

	if (!g_widget_manager) {
		return;
	}

	// Update all widgets
	for (i = 0; i < g_widget_manager->z_order_count; i++) {
		Widget *widget = g_widget_manager->z_order_list[i];
		if (widget->update) {
			widget->update(widget, dt);
		}
	}

	g_widget_manager->last_update_time = SDL_GetTicks();
}

void widget_manager_request_redraw(void)
{
	Widget *widget;

	if (!g_widget_manager || !g_widget_manager->root) {
		return;
	}

	g_widget_manager->needs_full_redraw = 1;

	// Mark all widgets dirty
	widget = g_widget_manager->root;
	widget_mark_dirty(widget);
}

// =============================================================================
// Widget Manager Input Routing
// =============================================================================

int widget_manager_handle_mouse(int x, int y, int button, int action)
{
	Widget *target;
	int local_x, local_y;
	int handled = 0;

	if (!g_widget_manager || !g_widget_manager->root) {
		return 0;
	}

	// Check for modal widget - blocks input to others
	if (g_widget_manager->modal_widget) {
		target = widget_find_at_position(g_widget_manager->modal_widget, x, y);
		if (!target) {
			return 1; // Block input
		}
	} else {
		// Find widget at position
		target = widget_find_at_position(g_widget_manager->root, x, y);
	}

	// Update hover state (do this before checking if target is NULL)
	if (g_widget_manager->hovered != target) {
		// Call on_mouse_move for the widget losing hover
		if (g_widget_manager->hovered) {
			g_widget_manager->hovered->hover = 0;
			widget_mark_dirty(g_widget_manager->hovered);
			if (g_widget_manager->hovered->on_mouse_move) {
				int hx, hy;
				widget_screen_to_local(g_widget_manager->hovered, x, y, &hx, &hy);
				g_widget_manager->hovered->on_mouse_move(g_widget_manager->hovered, hx, hy);
			}
		}
		g_widget_manager->hovered = target;
		if (target) {
			target->hover = 1;
			widget_mark_dirty(target);
			// Call on_mouse_move for the widget gaining hover
			if (target->on_mouse_move) {
				int tx, ty;
				widget_screen_to_local(target, x, y, &tx, &ty);
				target->on_mouse_move(target, tx, ty);
			}
		}
	}

	// Handle window dragging (check BEFORE target validation - drag continues even if mouse leaves widget)
	if (g_widget_manager->dragging_widget) {
		if (action == MOUSE_ACTION_MOVE) {
			int new_x = x - g_widget_manager->drag_offset_x;
			int new_y = y - g_widget_manager->drag_offset_y;
			widget_set_position(g_widget_manager->dragging_widget, new_x, new_y);
			return 1;
		} else if (action == MOUSE_ACTION_UP && button == MOUSE_BUTTON_LEFT) {
			widget_manager_stop_drag();
			return 1;
		}
	}

	// Handle window resizing (check BEFORE target validation - resize continues even if mouse leaves widget)
	if (g_widget_manager->resizing_widget) {
		if (action == MOUSE_ACTION_MOVE) {
			Widget *widget = g_widget_manager->resizing_widget;
			int dx = x - g_widget_manager->resize_start_x;
			int dy = y - g_widget_manager->resize_start_y;
			int new_width = g_widget_manager->resize_start_width;
			int new_height = g_widget_manager->resize_start_height;

			// Apply resize based on handle
			int handle = g_widget_manager->resize_handle;
			if (handle == 3 || handle == 4) { // Right or bottom-right
				new_width += dx;
			}
			if (handle == 4 || handle == 5) { // Bottom-right or bottom
				new_height += dy;
			}

			widget_set_size(widget, new_width, new_height);
			return 1;
		} else if (action == MOUSE_ACTION_UP && button == MOUSE_BUTTON_LEFT) {
			widget_manager_stop_resize();
			return 1;
		}
	}

	// Now validate target (after drag/resize checks, so those can continue even if mouse leaves widget)
	if (!target) {
		return 0;
	}

	// Don't block events if the only target is the root widget (should be transparent to clicks)
	if (target == g_widget_manager->root) {
		return 0;
	}

	// Convert to widget-local coordinates
	widget_screen_to_local(target, x, y, &local_x, &local_y);

	// Route event to widget
	if (action == MOUSE_ACTION_DOWN) {
		// Check for title bar interactions
		if (target->has_titlebar) {
			int wx, wy;
			widget_get_screen_position(target, &wx, &wy);
			if (y >= wy - 20 && y < wy) { // In title bar
				// Check for close button click
				if (target->closable) {
					int close_x = wx + target->width - 16;
					int close_y = wy - 20 + 2;
					if (x >= close_x && x < close_x + 14 && y >= close_y && y < close_y + 14) {
						widget_set_visible(target, 0);
						return 1;
					}
				}

				// Check for minimize button click
				if (target->minimizable) {
					int min_x = wx + target->width - (target->closable ? 32 : 16);
					int min_y = wy - 20 + 2;
					if (x >= min_x && x < min_x + 14 && y >= min_y && y < min_y + 14) {
						target->minimized = !target->minimized;
						widget_mark_dirty(target);
						return 1;
					}
				}

				// Start drag if not clicking a button
				if (target->draggable) {
					widget_manager_start_drag(target, x - wx, y - wy);
					widget_bring_to_front(target);
					return 1;
				}
			}
		}

		// Check for resize handle
		if (target->resizable && !target->minimized) {
			int handle = widget_get_resize_handle(target, x, y);
			if (handle >= 0) {
				widget_manager_start_resize(target, handle, x, y);
				return 1;
			}
		}

		// Bring to front on click
		widget_bring_to_front(target);

		// Set focus
		widget_manager_set_focus(target);

		// Call widget handler
		if (target->on_mouse_down) {
			handled = target->on_mouse_down(target, local_x, local_y, button);
		}

		target->pressed = 1;
		handled = 1; // Always block the event - we found a target widget
	} else if (action == MOUSE_ACTION_UP) {
		if (target->on_mouse_up) {
			handled = target->on_mouse_up(target, local_x, local_y, button);
		}

		target->pressed = 0;
		handled = 1; // Always block the event - we found a target widget
	} else if (action == MOUSE_ACTION_MOVE) {
		if (target->on_mouse_move) {
			handled = target->on_mouse_move(target, local_x, local_y);
		}
		// Don't force handled=1 for MOVE, allow widgets to decide
	}

	return handled;
}

int widget_manager_handle_mouse_wheel(int x, int y, int delta)
{
	Widget *target;
	int local_x, local_y;

	if (!g_widget_manager || !g_widget_manager->root) {
		return 0;
	}

	// Find widget at position
	target = widget_find_at_position(g_widget_manager->root, x, y);
	if (!target || !target->enabled) {
		return 0;
	}

	// Convert to local coordinates
	widget_screen_to_local(target, x, y, &local_x, &local_y);

	// Call widget handler
	if (target->on_mouse_wheel) {
		return target->on_mouse_wheel(target, local_x, local_y, delta);
	}

	return 0;
}

int widget_manager_handle_key(int key, int down)
{
	Widget *target;

	if (!g_widget_manager) {
		return 0;
	}

	// Route to focused widget
	target = g_widget_manager->focused;
	if (!target || !target->enabled) {
		return 0;
	}

	// Call widget handler
	if (down && target->on_key_down) {
		return target->on_key_down(target, key);
	} else if (!down && target->on_key_up) {
		return target->on_key_up(target, key);
	}

	return 0;
}

int widget_manager_handle_text(int character)
{
	Widget *target;

	if (!g_widget_manager) {
		return 0;
	}

	// Route to focused widget
	target = g_widget_manager->focused;
	if (!target || !target->enabled) {
		return 0;
	}

	// Call widget handler
	if (target->on_text_input) {
		return target->on_text_input(target, character);
	}

	return 0;
}

// =============================================================================
// Widget Manager Focus Management
// =============================================================================

void widget_manager_set_focus(Widget *widget)
{
	if (!g_widget_manager) {
		return;
	}

	// Same widget, do nothing
	if (g_widget_manager->focused == widget) {
		return;
	}

	// Call old widget's focus lost handler
	if (g_widget_manager->focused) {
		g_widget_manager->focused->focused = 0;
		if (g_widget_manager->focused->on_focus_lost) {
			g_widget_manager->focused->on_focus_lost(g_widget_manager->focused);
		}
		widget_mark_dirty(g_widget_manager->focused);
	}

	// Set new focus
	g_widget_manager->focused = widget;

	// Call new widget's focus gain handler
	if (widget) {
		widget->focused = 1;
		if (widget->on_focus_gain) {
			widget->on_focus_gain(widget);
		}
		widget_mark_dirty(widget);
	}
}

Widget *widget_manager_get_focus(void)
{
	if (!g_widget_manager) {
		return NULL;
	}

	return g_widget_manager->focused;
}

void widget_manager_focus_next(int reverse)
{
	// TODO: Implement tab navigation
	// For now, do nothing
}

// =============================================================================
// Widget Manager Z-Order
// =============================================================================

void widget_manager_bring_to_front(Widget *widget)
{
	widget_bring_to_front(widget);
}

void widget_manager_send_to_back(Widget *widget)
{
	widget_send_to_back(widget);
}

void widget_manager_resort_z_order(void)
{
	if (!g_widget_manager) {
		return;
	}

	// Sort z-order list
	qsort(g_widget_manager->z_order_list, g_widget_manager->z_order_count, sizeof(Widget *), z_order_compare);

	g_widget_manager->needs_z_resort = 0;
}

void widget_manager_rebuild_z_order(void)
{
	if (!g_widget_manager || !g_widget_manager->root) {
		return;
	}

	// Clear list
	g_widget_manager->z_order_count = 0;

	// Rebuild from root
	add_to_z_order_recursive(g_widget_manager, g_widget_manager->root);

	// Sort by z-order
	widget_manager_resort_z_order();

	// Update widget count
	g_widget_manager->widget_count = g_widget_manager->z_order_count;
}

// =============================================================================
// Widget Manager Drag & Drop
// =============================================================================

void widget_manager_start_drag(Widget *widget, int offset_x, int offset_y)
{
	if (!g_widget_manager || !widget) {
		return;
	}

	g_widget_manager->dragging_widget = widget;
	g_widget_manager->drag_offset_x = offset_x;
	g_widget_manager->drag_offset_y = offset_y;
}

void widget_manager_stop_drag(void)
{
	if (!g_widget_manager) {
		return;
	}

	g_widget_manager->dragging_widget = NULL;
}

void widget_manager_start_item_drag(Widget *source_widget, void *data, int data_type)
{
	if (!g_widget_manager) {
		return;
	}

	g_widget_manager->dragging_item = source_widget;
	g_widget_manager->drag_data = data;
	g_widget_manager->drag_data_type = data_type;
}

void *widget_manager_stop_item_drag(Widget *target_widget)
{
	void *data;

	if (!g_widget_manager) {
		return NULL;
	}

	data = g_widget_manager->drag_data;

	g_widget_manager->dragging_item = NULL;
	g_widget_manager->drag_data = NULL;
	g_widget_manager->drag_data_type = 0;

	return data;
}

int widget_manager_is_item_dragging(void)
{
	if (!g_widget_manager) {
		return 0;
	}

	return g_widget_manager->dragging_item != NULL;
}

void *widget_manager_get_drag_data(int *out_type)
{
	if (!g_widget_manager) {
		if (out_type) {
			*out_type = 0;
		}
		return NULL;
	}

	if (out_type) {
		*out_type = g_widget_manager->drag_data_type;
	}

	return g_widget_manager->drag_data;
}

// =============================================================================
// Widget Manager Resize
// =============================================================================

void widget_manager_start_resize(Widget *widget, int handle, int mouse_x, int mouse_y)
{
	if (!g_widget_manager || !widget) {
		return;
	}

	g_widget_manager->resizing_widget = widget;
	g_widget_manager->resize_handle = handle;
	g_widget_manager->resize_start_x = mouse_x;
	g_widget_manager->resize_start_y = mouse_y;
	g_widget_manager->resize_start_width = widget->width;
	g_widget_manager->resize_start_height = widget->height;
}

void widget_manager_stop_resize(void)
{
	if (!g_widget_manager) {
		return;
	}

	g_widget_manager->resizing_widget = NULL;
	g_widget_manager->resize_handle = -1;
}

// =============================================================================
// Widget Manager Modal Dialogs
// =============================================================================

void widget_manager_set_modal(Widget *widget)
{
	if (!g_widget_manager) {
		return;
	}

	g_widget_manager->modal_widget = widget;
}

Widget *widget_manager_get_modal(void)
{
	if (!g_widget_manager) {
		return NULL;
	}

	return g_widget_manager->modal_widget;
}

// =============================================================================
// Widget Manager Utilities
// =============================================================================

/**
 * Recursive search for widget by ID
 */
static Widget *find_widget_by_id_recursive(Widget *widget, int id)
{
	Widget *child, *found;

	if (!widget) {
		return NULL;
	}

	if (widget->id == id) {
		return widget;
	}

	for (child = widget->first_child; child; child = child->next_sibling) {
		found = find_widget_by_id_recursive(child, id);
		if (found) {
			return found;
		}
	}

	return NULL;
}

Widget *widget_manager_find_by_id(int id)
{
	if (!g_widget_manager || !g_widget_manager->root) {
		return NULL;
	}

	return find_widget_by_id_recursive(g_widget_manager->root, id);
}

Widget *widget_manager_find_by_name(const char *name)
{
	if (!g_widget_manager || !g_widget_manager->root || !name) {
		return NULL;
	}

	return widget_find_child(g_widget_manager->root, name, 1);
}

int widget_manager_get_widget_count(void)
{
	if (!g_widget_manager) {
		return 0;
	}

	return g_widget_manager->widget_count;
}

void widget_manager_print_hierarchy(Widget *root, int indent)
{
	Widget *child;
	int i;

	if (!root) {
		if (!g_widget_manager || !g_widget_manager->root) {
			return;
		}
		root = g_widget_manager->root;
		indent = 0;
	}

	// Print indentation
	for (i = 0; i < indent; i++) {
		printf("  ");
	}

	// Print widget info
	printf("%s (id=%d, type=%d, z=%d, %dx%d at %d,%d)%s%s\n", root->name, root->id, root->type, root->z_order,
	    root->width, root->height, root->x, root->y, root->visible ? "" : " [hidden]",
	    root->enabled ? "" : " [disabled]");

	// Print children
	for (child = root->first_child; child; child = child->next_sibling) {
		widget_manager_print_hierarchy(child, indent + 1);
	}
}
