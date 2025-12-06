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
#include "widgets/widget_tooltip.h"
#include "game/game.h"
#include "gui/gui_private.h" // for mousex, mousey
#include "sdl/sdl.h" // for cursor functions

// Global widget manager instance
static WidgetManager *g_widget_manager = NULL;

// =============================================================================
// Forward Declarations
// =============================================================================

static void widget_manager_save_state(void);

/**
 * Map resize handle index to cursor type
 * Handle indexes: 0=top-left, 1=top, 2=top-right, 3=right,
 *                 4=bottom-right, 5=bottom, 6=bottom-left, 7=left
 */
static int resize_handle_to_cursor(int handle)
{
	switch (handle) {
	case 0: // top-left
	case 4: // bottom-right
		return SDL_CUR_SIZENWSE;
	case 2: // top-right
	case 6: // bottom-left
		return SDL_CUR_SIZENESW;
	case 1: // top
	case 5: // bottom
		return SDL_CUR_SIZENS;
	case 3: // right
	case 7: // left
		return SDL_CUR_SIZEWE;
	default:
		return SDL_CUR_ARROW;
	}
}

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

	// Tooltip management - create internal tooltip widget
	// NOTE: We do NOT add it to the widget hierarchy so it doesn't participate in hit testing
	// We manage rendering and updates manually
	g_widget_manager->tooltip_widget = widget_tooltip_create(0, 0);
	if (g_widget_manager->tooltip_widget) {
		widget_set_visible(g_widget_manager->tooltip_widget, 0);
		widget_tooltip_set_delay(g_widget_manager->tooltip_widget, 0); // We handle delay ourselves
		// Don't add to root - we manage it separately to avoid hit-testing issues
	}
	g_widget_manager->tooltip_target = NULL;
	g_widget_manager->tooltip_hover_start = 0;
	g_widget_manager->tooltip_visible = 0;
	g_widget_manager->mouse_x = 0;
	g_widget_manager->mouse_y = 0;

	// Double-click detection
	g_widget_manager->last_click_widget = NULL;
	g_widget_manager->last_click_time = 0;
	g_widget_manager->last_click_x = 0;
	g_widget_manager->last_click_y = 0;
	g_widget_manager->last_click_button = 0;

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

	// Save widget state before cleanup
	widget_manager_save_state();

	// Destroy internal tooltip widget (not part of hierarchy)
	if (g_widget_manager->tooltip_widget) {
		widget_destroy(g_widget_manager->tooltip_widget);
		g_widget_manager->tooltip_widget = NULL;
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

	// Render internal tooltip LAST so it's always on top
	if (g_widget_manager->tooltip_widget && g_widget_manager->tooltip_widget->visible) {
		if (g_widget_manager->tooltip_widget->render) {
			g_widget_manager->tooltip_widget->render(g_widget_manager->tooltip_widget);
		}
	}

	g_widget_manager->needs_full_redraw = 0;
	g_widget_manager->frame_count++;
}

/**
 * Update hover state based on current mouse position
 * Called every frame to ensure hover tracking works even without explicit mouse events
 */
static void update_hover_state(void)
{
	Widget *target;
	int cursor;

	if (!g_widget_manager || !g_widget_manager->root) {
		return;
	}

	// Track mouse position for tooltip
	g_widget_manager->mouse_x = mousex;
	g_widget_manager->mouse_y = mousey;

	// Find widget at current mouse position
	target = widget_find_at_position(g_widget_manager->root, mousex, mousey);

	// Treat root as "no target" for hover purposes
	if (target == g_widget_manager->root) {
		target = NULL;
	}

	// Update hover state if target changed
	if (g_widget_manager->hovered != target) {
		// Call on_mouse_leave for the widget losing hover
		if (g_widget_manager->hovered) {
			g_widget_manager->hovered->hover = 0;
			widget_mark_dirty(g_widget_manager->hovered);

			// Call on_mouse_leave callback
			if (g_widget_manager->hovered->on_mouse_leave) {
				g_widget_manager->hovered->on_mouse_leave(g_widget_manager->hovered);
			}
		}

		g_widget_manager->hovered = target;

		// Hide tooltip when hover target changes - always hide regardless of tooltip_visible flag
		if (g_widget_manager->tooltip_widget) {
			widget_set_visible(g_widget_manager->tooltip_widget, 0);
		}
		g_widget_manager->tooltip_visible = 0;
		g_widget_manager->tooltip_target = NULL;

		if (target) {
			target->hover = 1;
			widget_mark_dirty(target);

			// Call on_mouse_enter callback
			if (target->on_mouse_enter) {
				target->on_mouse_enter(target);
			}

			// Start tooltip hover timer if widget has tooltip
			if (target->tooltip_text[0] != '\0') {
				g_widget_manager->tooltip_target = target;
				g_widget_manager->tooltip_hover_start = SDL_GetTicks();
			}
		}
	}

	// Determine appropriate cursor based on state
	// Use -1 to indicate "no widget cursor needed, let game handle it"
	cursor = -1;

	if (g_widget_manager->resizing_widget) {
		// Currently resizing - show resize cursor
		cursor = resize_handle_to_cursor(g_widget_manager->resize_handle);
	} else if (g_widget_manager->dragging_widget) {
		// Currently dragging - show move cursor
		cursor = SDL_CUR_SIZEALL;
	} else if (target) {
		// Check for resize handle hover
		if (target->resizable && !target->minimized) {
			int handle = widget_get_resize_handle(target, mousex, mousey);
			if (handle >= 0) {
				cursor = resize_handle_to_cursor(handle);
			}
		}

		// Check for title bar hover (for draggable widgets)
		if (cursor == -1 && target->has_titlebar && target->draggable) {
			int wx, wy;
			widget_get_screen_position(target, &wx, &wy);
			if (mousey >= wy - 20 && mousey < wy) {
				// In title bar - check not on close/minimize buttons
				int on_button = 0;
				if (target->closable) {
					int close_x = wx + target->width - 16;
					if (mousex >= close_x && mousex < close_x + 14) {
						on_button = 1;
					}
				}
				if (!on_button && target->minimizable) {
					int min_x = wx + target->width - (target->closable ? 32 : 16);
					if (mousex >= min_x && mousex < min_x + 14) {
						on_button = 1;
					}
				}
				if (!on_button) {
					cursor = SDL_CUR_SIZEALL;
				}
			}
		}

		// Cursor based on widget type
		if (cursor == -1) {
			switch (target->type) {
			case WIDGET_TYPE_BUTTON:
				cursor = SDL_CUR_HAND;
				break;
			case WIDGET_TYPE_TEXTINPUT:
				cursor = SDL_CUR_IBEAM;
				break;
			default:
				// No special cursor for this widget type - let game handle it
				break;
			}
		}
	}

	// Only set cursor if we have a specific widget cursor to show
	// Otherwise let the game's cursor logic take over
	if (cursor >= 0) {
		sdl_set_cursor(cursor);
	}
}

void widget_manager_update(int dt)
{
	int i;
	unsigned int now;

	if (!g_widget_manager) {
		return;
	}

	now = SDL_GetTicks();

	// Update hover state every frame based on current mouse position
	update_hover_state();

	// Handle automatic tooltip display
	if (g_widget_manager->tooltip_target && g_widget_manager->tooltip_widget) {
		Widget *target = g_widget_manager->tooltip_target;

		// Double-check that mouse is still over the target widget (safeguard against timing issues)
		Widget *current_hover =
		    widget_find_at_position(g_widget_manager->root, g_widget_manager->mouse_x, g_widget_manager->mouse_y);
		if (current_hover != target) {
			// Mouse moved away from target - hide tooltip and clear state
			widget_set_visible(g_widget_manager->tooltip_widget, 0);
			g_widget_manager->tooltip_visible = 0;
			g_widget_manager->tooltip_target = NULL;
		} else if (!g_widget_manager->tooltip_visible) {
			// Check if tooltip delay has elapsed
			unsigned int elapsed = now - g_widget_manager->tooltip_hover_start;
			int delay = target->tooltip_delay;

			if (elapsed >= (unsigned int)delay) {
				// Show tooltip
				widget_tooltip_set_text(g_widget_manager->tooltip_widget, target->tooltip_text);
				widget_tooltip_show_at_mouse(
				    g_widget_manager->tooltip_widget, g_widget_manager->mouse_x, g_widget_manager->mouse_y);
				widget_set_visible(g_widget_manager->tooltip_widget, 1);
				g_widget_manager->tooltip_visible = 1;
			}
		} else {
			// Update tooltip position if visible
			widget_tooltip_update_position(
			    g_widget_manager->tooltip_widget, g_widget_manager->mouse_x, g_widget_manager->mouse_y);
		}
	} else if (g_widget_manager->tooltip_visible && g_widget_manager->tooltip_widget) {
		// Hide tooltip if target is gone
		widget_set_visible(g_widget_manager->tooltip_widget, 0);
		g_widget_manager->tooltip_visible = 0;
	}

	// Update all widgets
	for (i = 0; i < g_widget_manager->z_order_count; i++) {
		Widget *widget = g_widget_manager->z_order_list[i];
		if (widget->update) {
			widget->update(widget, dt);
		}
	}

	// Update internal tooltip widget (not in z_order_list)
	if (g_widget_manager->tooltip_widget && g_widget_manager->tooltip_widget->update) {
		g_widget_manager->tooltip_widget->update(g_widget_manager->tooltip_widget, dt);
	}

	g_widget_manager->last_update_time = now;
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

	// Note: Hover state is now tracked in widget_manager_update() every frame
	// using the global mousex/mousey variables

	// Handle window dragging (check BEFORE target validation - drag continues even if mouse leaves widget)
	if (g_widget_manager->dragging_widget) {
		if (action == MOUSE_ACTION_MOVE) {
			Widget *drag_w = g_widget_manager->dragging_widget;
			int new_x = x - g_widget_manager->drag_offset_x;
			int new_y = y - g_widget_manager->drag_offset_y;

			// Apply screen boundary constraints
			// Keep at least 50 pixels of the widget visible on screen
			int min_visible = 50;
			int screen_w = g_widget_manager->root->width;
			int screen_h = g_widget_manager->root->height;
			int titlebar_h = drag_w->has_titlebar ? 20 : 0;

			// Clamp X: keep min_visible pixels on screen
			if (new_x < -drag_w->width + min_visible) {
				new_x = -drag_w->width + min_visible;
			}
			if (new_x > screen_w - min_visible) {
				new_x = screen_w - min_visible;
			}

			// Clamp Y: can't drag title bar off the top, keep min_visible on bottom
			if (new_y < titlebar_h) {
				new_y = titlebar_h;
			}
			if (new_y > screen_h - min_visible) {
				new_y = screen_h - min_visible;
			}

			widget_set_position(drag_w, new_x, new_y);
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

		// Check for double-click
		{
			unsigned int now = SDL_GetTicks();
			int is_double_click = 0;

			if (g_widget_manager->last_click_widget == target && g_widget_manager->last_click_button == button &&
			    (now - g_widget_manager->last_click_time) <= DOUBLE_CLICK_TIME_MS) {
				// Check if click position is close enough
				int dx = x - g_widget_manager->last_click_x;
				int dy = y - g_widget_manager->last_click_y;
				if (dx >= -DOUBLE_CLICK_DISTANCE && dx <= DOUBLE_CLICK_DISTANCE && dy >= -DOUBLE_CLICK_DISTANCE &&
				    dy <= DOUBLE_CLICK_DISTANCE) {
					is_double_click = 1;
				}
			}

			if (is_double_click) {
				// Call double-click handler
				if (target->on_double_click) {
					handled = target->on_double_click(target, local_x, local_y, button);
				}
				// Reset click tracking after double-click to prevent triple-click from registering
				g_widget_manager->last_click_widget = NULL;
				g_widget_manager->last_click_time = 0;
			} else {
				// Call regular mouse down handler
				if (target->on_mouse_down) {
					handled = target->on_mouse_down(target, local_x, local_y, button);
				}
				// Update click tracking for potential double-click
				g_widget_manager->last_click_widget = target;
				g_widget_manager->last_click_time = now;
				g_widget_manager->last_click_x = x;
				g_widget_manager->last_click_y = y;
				g_widget_manager->last_click_button = button;
			}
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

/**
 * Helper to collect focusable widgets recursively
 */
static void collect_focusable_widgets(Widget *widget, Widget **list, int *count, int max_count)
{
	Widget *child;

	if (!widget || !widget->visible || !widget->enabled) {
		return;
	}

	// Add this widget if it's focusable
	if (widget->focusable && *count < max_count) {
		list[(*count)++] = widget;
	}

	// Recurse into children
	for (child = widget->first_child; child; child = child->next_sibling) {
		collect_focusable_widgets(child, list, count, max_count);
	}
}

/**
 * Compare widgets for tab order sorting
 */
static int tab_order_compare(const void *a, const void *b)
{
	Widget *wa = *(Widget **)a;
	Widget *wb = *(Widget **)b;

	// Widgets with tab_index -1 go to the end
	if (wa->tab_index == -1 && wb->tab_index != -1) {
		return 1;
	}
	if (wa->tab_index != -1 && wb->tab_index == -1) {
		return -1;
	}

	// Sort by tab_index (lower first)
	if (wa->tab_index != wb->tab_index) {
		return wa->tab_index - wb->tab_index;
	}

	// Same tab_index: maintain original collection order (tree order)
	return 0;
}

void widget_manager_focus_next(int reverse)
{
	Widget *focusable[256]; // Max 256 focusable widgets
	int count = 0;
	int current_idx = -1;
	int next_idx;
	int i;

	if (!g_widget_manager || !g_widget_manager->root) {
		return;
	}

	// Collect all focusable widgets
	collect_focusable_widgets(g_widget_manager->root, focusable, &count, 256);

	if (count == 0) {
		return; // No focusable widgets
	}

	// Sort by tab order
	qsort(focusable, count, sizeof(Widget *), tab_order_compare);

	// Find current focused widget in the list
	if (g_widget_manager->focused) {
		for (i = 0; i < count; i++) {
			if (focusable[i] == g_widget_manager->focused) {
				current_idx = i;
				break;
			}
		}
	}

	// Calculate next index
	if (current_idx == -1) {
		// No current focus, start at beginning or end
		next_idx = reverse ? count - 1 : 0;
	} else if (reverse) {
		// Move backwards
		next_idx = current_idx - 1;
		if (next_idx < 0) {
			next_idx = count - 1; // Wrap to end
		}
	} else {
		// Move forwards
		next_idx = current_idx + 1;
		if (next_idx >= count) {
			next_idx = 0; // Wrap to beginning
		}
	}

	// Set focus to the next widget
	widget_manager_set_focus(focusable[next_idx]);
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

// =============================================================================
// Widget State Persistence
// =============================================================================

/**
 * Get the widget state file path
 * Returns a newly allocated string that must be freed with SDL_free
 */
static char *get_widget_state_path(void)
{
	char *base_path;
	char *full_path;
	size_t path_len;

	// Use SDL_GetPrefPath for cross-platform config directory
	base_path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
	if (!base_path) {
		// Fallback to current directory
		base_path = SDL_strdup("./");
		if (!base_path) {
			return NULL;
		}
	}

	// Allocate space for full path
	path_len = strlen(base_path) + strlen("widgets.json") + 1;
	full_path = SDL_malloc(path_len);
	if (!full_path) {
		SDL_free(base_path);
		return NULL;
	}

	// Construct full path
	SDL_snprintf(full_path, path_len, "%swidgets.json", base_path);
	SDL_free(base_path);

	return full_path;
}

/**
 * Recursively save widget state to JSON file
 */
static void save_widget_state_recursive(FILE *fp, Widget *widget, int *count, int *first)
{
	Widget *child;

	if (!widget || !fp) {
		return;
	}

	// Only save widgets that have titlebar (windows) or are draggable
	if (widget->has_titlebar || widget->draggable) {
		if (!(*first)) {
			fprintf(fp, ",\n");
		}
		*first = 0;

		fprintf(fp, "    {\n");
		fprintf(fp, "      \"name\": \"%s\",\n", widget->name);
		fprintf(fp, "      \"x\": %d,\n", widget->x);
		fprintf(fp, "      \"y\": %d,\n", widget->y);
		fprintf(fp, "      \"width\": %d,\n", widget->width);
		fprintf(fp, "      \"height\": %d,\n", widget->height);
		fprintf(fp, "      \"visible\": %s,\n", widget->visible ? "true" : "false");
		fprintf(fp, "      \"minimized\": %s\n", widget->minimized ? "true" : "false");
		fprintf(fp, "    }");
		(*count)++;
	}

	// Save children
	for (child = widget->first_child; child; child = child->next_sibling) {
		save_widget_state_recursive(fp, child, count, first);
	}
}

/**
 * Save widget positions and state to disk in JSON format
 * Called automatically on cleanup
 *
 * Format:
 * {
 *   "widgets": [
 *     {
 *       "name": "WindowName",
 *       "x": 100,
 *       "y": 200,
 *       "width": 300,
 *       "height": 400,
 *       "visible": true,
 *       "minimized": false
 *     }
 *   ]
 * }
 */
static void widget_manager_save_state(void)
{
	FILE *fp;
	char *path;
	int count = 0;
	int first = 1;

	if (!g_widget_manager || !g_widget_manager->root) {
		return;
	}

	path = get_widget_state_path();
	if (!path) {
		warn("widget_manager_save_state: failed to get state file path");
		return;
	}

	fp = fopen(path, "w");
	if (!fp) {
		warn("widget_manager_save_state: failed to open %s for writing", path);
		SDL_free(path);
		return;
	}

	// Write JSON header
	fprintf(fp, "{\n");
	fprintf(fp, "  \"widgets\": [\n");

	// Write widget data
	save_widget_state_recursive(fp, g_widget_manager->root, &count, &first);

	// Write JSON footer
	fprintf(fp, "\n  ]\n");
	fprintf(fp, "}\n");

	fclose(fp);
	note("Saved state for %d widgets to %s", count, path);
	SDL_free(path);
}

/**
 * Simple JSON parser for widget state
 * Handles basic JSON structure - not a full JSON parser
 */
static void parse_json_widget(const char *json_text, int *count)
{
	const char *ptr = json_text;
	char name[64] = {0};
	int x = 0, y = 0, width = 0, height = 0;
	int visible = 1, minimized = 0;

	// Parse JSON object looking for our fields
	while (*ptr) {
		// Skip whitespace
		while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r') {
			ptr++;
		}

		if (*ptr == '"') {
			ptr++; // Skip opening quote
			char key[32];
			int i = 0;

			// Read key name
			while (*ptr && *ptr != '"' && i < 31) {
				key[i++] = *ptr++;
			}
			key[i] = '\0';

			if (*ptr == '"') {
				ptr++; // Skip closing quote
			}

			// Skip to colon
			while (*ptr && *ptr != ':') {
				ptr++;
			}
			if (*ptr == ':') {
				ptr++;
			}

			// Skip whitespace
			while (*ptr == ' ' || *ptr == '\t') {
				ptr++;
			}

			// Parse value based on key
			if (strcmp(key, "name") == 0 && *ptr == '"') {
				ptr++; // Skip opening quote
				i = 0;
				while (*ptr && *ptr != '"' && i < 63) {
					name[i++] = *ptr++;
				}
				name[i] = '\0';
			} else if (strcmp(key, "x") == 0) {
				x = atoi(ptr);
			} else if (strcmp(key, "y") == 0) {
				y = atoi(ptr);
			} else if (strcmp(key, "width") == 0) {
				width = atoi(ptr);
			} else if (strcmp(key, "height") == 0) {
				height = atoi(ptr);
			} else if (strcmp(key, "visible") == 0) {
				visible = (strncmp(ptr, "true", 4) == 0) ? 1 : 0;
			} else if (strcmp(key, "minimized") == 0) {
				minimized = (strncmp(ptr, "true", 4) == 0) ? 1 : 0;
			}
		}

		// Check for end of object
		if (*ptr == '}') {
			// Apply widget state if we have a name
			if (name[0] != '\0') {
				Widget *widget = widget_manager_find_by_name(name);
				if (widget) {
					widget_set_position(widget, x, y);
					widget_set_size(widget, width, height);
					widget_set_visible(widget, visible);
					widget_set_minimized(widget, minimized);
					(*count)++;
				}
			}
			return;
		}

		ptr++;
	}
}

/**
 * Load and apply widget state from disk
 * Should be called after widgets are created
 */
DLL_EXPORT void widget_manager_load_state(void)
{
	FILE *fp;
	char *path;
	char *file_contents = NULL;
	long file_size;
	int count = 0;

	if (!g_widget_manager || !g_widget_manager->root) {
		return;
	}

	path = get_widget_state_path();
	if (!path) {
		note("widget_manager_load_state: failed to get state file path");
		return;
	}

	fp = fopen(path, "rb");
	if (!fp) {
		note("widget_manager_load_state: no saved state found at %s", path);
		SDL_free(path);
		return;
	}

	// Get file size
	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// Read entire file
	file_contents = SDL_malloc(file_size + 1);
	if (!file_contents) {
		warn("widget_manager_load_state: failed to allocate memory");
		fclose(fp);
		SDL_free(path);
		return;
	}

	if (fread(file_contents, 1, file_size, fp) != (size_t)file_size) {
		warn("widget_manager_load_state: failed to read file");
		SDL_free(file_contents);
		fclose(fp);
		SDL_free(path);
		return;
	}
	file_contents[file_size] = '\0';
	fclose(fp);

	// Parse JSON - look for widget objects
	const char *ptr = file_contents;
	while (*ptr) {
		// Find start of widget object
		if (*ptr == '{') {
			// Find end of this object
			const char *obj_start = ptr;
			int brace_count = 1;
			ptr++;

			while (*ptr && brace_count > 0) {
				if (*ptr == '{') {
					brace_count++;
				} else if (*ptr == '}') {
					brace_count--;
				}
				ptr++;
			}

			// Extract and parse this object
			if (brace_count == 0) {
				size_t obj_len = ptr - obj_start;
				char *obj_text = SDL_malloc(obj_len + 1);
				if (obj_text) {
					memcpy(obj_text, obj_start, obj_len);
					obj_text[obj_len] = '\0';
					parse_json_widget(obj_text, &count);
					SDL_free(obj_text);
				}
			}
		} else {
			ptr++;
		}
	}

	SDL_free(file_contents);
	note("Loaded state for %d widgets from %s", count, path);
	SDL_free(path);
}

/**
 * Load state for a single widget by name
 * Searches the state file for an entry matching the widget's name
 * and applies position/size/visibility if found.
 *
 * @param widget Widget to load state for (must have a name set)
 * @return 1 if state was found and applied, 0 otherwise
 */
DLL_EXPORT int widget_load_state(Widget *widget)
{
	FILE *fp;
	char *path;
	char *file_contents = NULL;
	long file_size;

	if (!widget || widget->name[0] == '\0') {
		return 0;
	}

	path = get_widget_state_path();
	if (!path) {
		return 0;
	}

	fp = fopen(path, "rb");
	if (!fp) {
		SDL_free(path);
		return 0;
	}

	// Get file size
	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// Read entire file
	file_contents = SDL_malloc(file_size + 1);
	if (!file_contents) {
		fclose(fp);
		SDL_free(path);
		return 0;
	}

	if (fread(file_contents, 1, file_size, fp) != (size_t)file_size) {
		SDL_free(file_contents);
		fclose(fp);
		SDL_free(path);
		return 0;
	}
	file_contents[file_size] = '\0';
	fclose(fp);
	SDL_free(path);

	// Search for this widget's name in the JSON
	const char *ptr = file_contents;
	int found = 0;

	while (*ptr && !found) {
		if (*ptr == '{') {
			const char *obj_start = ptr;
			int brace_count = 1;
			ptr++;

			while (*ptr && brace_count > 0) {
				if (*ptr == '{') {
					brace_count++;
				} else if (*ptr == '}') {
					brace_count--;
				}
				ptr++;
			}

			if (brace_count == 0) {
				// Parse this object to check if it matches our widget
				size_t obj_len = ptr - obj_start;
				char *obj_text = SDL_malloc(obj_len + 1);
				if (obj_text) {
					memcpy(obj_text, obj_start, obj_len);
					obj_text[obj_len] = '\0';

					// Extract name from this object
					char name[64] = {0};
					int x = 0, y = 0, width = 0, height = 0;
					int visible = 1, minimized = 0;
					const char *p = obj_text;

					while (*p) {
						while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
							p++;
						}

						if (*p == '"') {
							p++;
							char key[32];
							int i = 0;
							while (*p && *p != '"' && i < 31) {
								key[i++] = *p++;
							}
							key[i] = '\0';
							if (*p == '"') {
								p++;
							}
							while (*p && *p != ':') {
								p++;
							}
							if (*p == ':') {
								p++;
							}
							while (*p == ' ' || *p == '\t') {
								p++;
							}

							if (strcmp(key, "name") == 0 && *p == '"') {
								p++;
								i = 0;
								while (*p && *p != '"' && i < 63) {
									name[i++] = *p++;
								}
								name[i] = '\0';
							} else if (strcmp(key, "x") == 0) {
								x = atoi(p);
							} else if (strcmp(key, "y") == 0) {
								y = atoi(p);
							} else if (strcmp(key, "width") == 0) {
								width = atoi(p);
							} else if (strcmp(key, "height") == 0) {
								height = atoi(p);
							} else if (strcmp(key, "visible") == 0) {
								visible = (strncmp(p, "true", 4) == 0) ? 1 : 0;
							} else if (strcmp(key, "minimized") == 0) {
								minimized = (strncmp(p, "true", 4) == 0) ? 1 : 0;
							}
						}
						p++;
					}

					// Check if this matches our widget
					if (strcmp(name, widget->name) == 0) {
						widget_set_position(widget, x, y);
						widget_set_size(widget, width, height);
						widget_set_visible(widget, visible);
						widget_set_minimized(widget, minimized);
						found = 1;
					}

					SDL_free(obj_text);
				}
			}
		} else {
			ptr++;
		}
	}

	SDL_free(file_contents);
	return found;
}
