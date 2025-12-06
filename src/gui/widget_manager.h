/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Widget Manager - Centralized Widget Management
 *
 * Manages the widget hierarchy, rendering order, input routing, focus
 * management, and drag-drop operations.
 */

#ifndef WIDGET_MANAGER_H
#define WIDGET_MANAGER_H

#include "widget.h"

// Widget manager singleton structure
struct widget_manager {
	// === Widget Hierarchy ===
	Widget *root; // Root widget (full screen container)
	Widget *focused; // Currently focused widget
	Widget *hovered; // Widget under mouse cursor

	// === Z-Order Management ===
	Widget **z_order_list; // Widgets sorted by z-order for rendering
	int z_order_capacity; // Capacity of z_order_list
	int z_order_count; // Number of widgets in z_order_list

	// === Dirty Tracking ===
	unsigned int needs_full_redraw : 1; // Full screen redraw needed
	unsigned int needs_z_resort : 1; // Z-order list needs resorting

	// === Drag & Drop State ===
	Widget *dragging_widget; // Widget being dragged (window chrome)
	int drag_offset_x, drag_offset_y; // Offset from widget origin to mouse

	Widget *dragging_item; // Item/content being dragged (for inventory, etc.)
	void *drag_data; // Custom drag data
	int drag_data_type; // Type identifier for drag data

	// === Resize State ===
	Widget *resizing_widget; // Widget being resized
	int resize_handle; // Which resize handle (0-7)
	int resize_start_x, resize_start_y; // Mouse position at resize start
	int resize_start_width, resize_start_height; // Widget size at resize start

	// === Modal Dialog Support ===
	Widget *modal_widget; // Current modal widget (blocks others)

	// === Performance Tracking ===
	int frame_count; // Total frames rendered
	int widget_count; // Total widgets in hierarchy
	unsigned int last_update_time; // Last update time (SDL_GetTicks())
};

// =============================================================================
// Widget Manager Core API
// =============================================================================

/**
 * Initialize the widget manager
 * Creates the root widget and sets up initial state
 *
 * @param screen_width Screen width in pixels
 * @param screen_height Screen height in pixels
 * @return 1 on success, 0 on failure
 */
DLL_EXPORT int widget_manager_init(int screen_width, int screen_height);

/**
 * Cleanup the widget manager
 * Destroys all widgets and frees resources
 */
DLL_EXPORT void widget_manager_cleanup(void);

/**
 * Get the global widget manager instance
 *
 * @return Widget manager pointer, or NULL if not initialized
 */
DLL_EXPORT WidgetManager *widget_manager_get(void);

/**
 * Get the root widget
 *
 * @return Root widget pointer
 */
DLL_EXPORT Widget *widget_manager_get_root(void);

// =============================================================================
// Widget Manager Rendering
// =============================================================================

/**
 * Render all widgets
 * Renders widgets in z-order from back to front
 */
DLL_EXPORT void widget_manager_render(void);

/**
 * Update all widgets
 * Updates animations, timers, and other time-based state
 *
 * @param dt Delta time in milliseconds since last update
 */
DLL_EXPORT void widget_manager_update(int dt);

/**
 * Request full screen redraw
 * Marks all widgets as dirty
 */
DLL_EXPORT void widget_manager_request_redraw(void);

// =============================================================================
// Widget Manager Input Routing
// =============================================================================

/**
 * Handle mouse event
 * Routes event to appropriate widget based on position and focus
 *
 * @param x Mouse X position (screen coordinates)
 * @param y Mouse Y position (screen coordinates)
 * @param button Mouse button (MOUSE_BUTTON_*)
 * @param action Mouse action (MOUSE_ACTION_*)
 * @return 1 if event was handled, 0 otherwise
 */
DLL_EXPORT int widget_manager_handle_mouse(int x, int y, int button, int action);

/**
 * Handle mouse wheel event
 *
 * @param x Mouse X position (screen coordinates)
 * @param y Mouse Y position (screen coordinates)
 * @param delta Wheel delta (positive = up, negative = down)
 * @return 1 if event was handled, 0 otherwise
 */
DLL_EXPORT int widget_manager_handle_mouse_wheel(int x, int y, int delta);

/**
 * Handle keyboard event
 * Routes event to focused widget
 *
 * @param key Key code
 * @param down 1 for key down, 0 for key up
 * @return 1 if event was handled, 0 otherwise
 */
DLL_EXPORT int widget_manager_handle_key(int key, int down);

/**
 * Handle text input event
 * Routes to focused widget for text entry
 *
 * @param character Character code
 * @return 1 if event was handled, 0 otherwise
 */
DLL_EXPORT int widget_manager_handle_text(int character);

// =============================================================================
// Widget Manager Focus Management
// =============================================================================

/**
 * Set focused widget
 * Removes focus from previous widget and sets focus to new widget
 *
 * @param widget Widget to focus (NULL to clear focus)
 */
DLL_EXPORT void widget_manager_set_focus(Widget *widget);

/**
 * Get currently focused widget
 *
 * @return Focused widget, or NULL if no widget has focus
 */
DLL_EXPORT Widget *widget_manager_get_focus(void);

/**
 * Move focus to next widget (tab navigation)
 *
 * @param reverse If 1, move backwards (shift-tab)
 */
DLL_EXPORT void widget_manager_focus_next(int reverse);

// =============================================================================
// Widget Manager Z-Order
// =============================================================================

/**
 * Bring widget to front
 * Sets widget to highest z-order among siblings
 *
 * @param widget Widget to bring to front
 */
DLL_EXPORT void widget_manager_bring_to_front(Widget *widget);

/**
 * Send widget to back
 * Sets widget to lowest z-order among siblings
 *
 * @param widget Widget to send to back
 */
DLL_EXPORT void widget_manager_send_to_back(Widget *widget);

/**
 * Resort z-order list
 * Call this after modifying widget z-order values
 */
void widget_manager_resort_z_order(void);

/**
 * Rebuild z-order list
 * Call this after adding/removing widgets
 */
void widget_manager_rebuild_z_order(void);

// =============================================================================
// Widget Manager Drag & Drop
// =============================================================================

/**
 * Start dragging a widget (window chrome drag)
 *
 * @param widget Widget to drag
 * @param offset_x X offset from widget origin to mouse
 * @param offset_y Y offset from widget origin to mouse
 */
void widget_manager_start_drag(Widget *widget, int offset_x, int offset_y);

/**
 * Stop dragging current widget
 */
void widget_manager_stop_drag(void);

/**
 * Start dragging item/content (for inventory drag-drop)
 *
 * @param source_widget Widget that initiated the drag
 * @param data Custom drag data
 * @param data_type Type identifier for drag data
 */
DLL_EXPORT void widget_manager_start_item_drag(Widget *source_widget, void *data, int data_type);

/**
 * Stop dragging item/content
 *
 * @param target_widget Widget where item was dropped (NULL if cancelled)
 * @return Drag data pointer (caller should handle it)
 */
DLL_EXPORT void *widget_manager_stop_item_drag(Widget *target_widget);

/**
 * Check if an item is currently being dragged
 *
 * @return 1 if item drag is active, 0 otherwise
 */
DLL_EXPORT int widget_manager_is_item_dragging(void);

/**
 * Get current drag data
 *
 * @param out_type Output: drag data type (can be NULL)
 * @return Drag data pointer, or NULL if not dragging
 */
DLL_EXPORT void *widget_manager_get_drag_data(int *out_type);

// =============================================================================
// Widget Manager Resize
// =============================================================================

/**
 * Start resizing a widget
 *
 * @param widget Widget to resize
 * @param handle Resize handle index (0-7)
 * @param mouse_x Mouse X position at resize start
 * @param mouse_y Mouse Y position at resize start
 */
void widget_manager_start_resize(Widget *widget, int handle, int mouse_x, int mouse_y);

/**
 * Stop resizing current widget
 */
void widget_manager_stop_resize(void);

// =============================================================================
// Widget Manager Modal Dialogs
// =============================================================================

/**
 * Set modal widget
 * Modal widget blocks input to all other widgets
 *
 * @param widget Modal widget (NULL to clear modal)
 */
DLL_EXPORT void widget_manager_set_modal(Widget *widget);

/**
 * Get current modal widget
 *
 * @return Modal widget, or NULL if no modal active
 */
DLL_EXPORT Widget *widget_manager_get_modal(void);

// =============================================================================
// Widget Manager Utilities
// =============================================================================

/**
 * Find widget by ID
 *
 * @param id Widget ID to search for
 * @return Widget if found, NULL otherwise
 */
DLL_EXPORT Widget *widget_manager_find_by_id(int id);

/**
 * Find widget by name
 *
 * @param name Widget name to search for
 * @return Widget if found, NULL otherwise
 */
DLL_EXPORT Widget *widget_manager_find_by_name(const char *name);

/**
 * Get total widget count
 *
 * @return Number of widgets in hierarchy
 */
DLL_EXPORT int widget_manager_get_widget_count(void);

/**
 * Print widget hierarchy for debugging
 *
 * @param root Root widget to print from (NULL = use manager root)
 * @param indent Current indentation level
 */
void widget_manager_print_hierarchy(Widget *root, int indent);

// =============================================================================
// Widget State Persistence
// =============================================================================

/**
 * Load widget positions and state from disk
 * Should be called after widgets are created and initialized
 * Widgets are matched by name, so ensure widgets have unique names
 */
DLL_EXPORT void widget_manager_load_state(void);

/**
 * Load state for a single widget by name
 * Searches the state file for an entry matching the widget's name
 * and applies position/size/visibility if found.
 *
 * @param widget Widget to load state for (must have a name set)
 * @return 1 if state was found and applied, 0 otherwise
 */
DLL_EXPORT int widget_load_state(Widget *widget);

#endif // WIDGET_MANAGER_H
