/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Widget System - Core Widget Structures and API
 *
 * Modern widget-based GUI system with hierarchical structure, event handling,
 * and window chrome support.
 */

#ifndef WIDGET_H
#define WIDGET_H

#include "dll.h"

// Forward declarations
typedef struct widget Widget;
typedef struct widget_manager WidgetManager;

// Widget type identifiers
typedef enum {
	WIDGET_TYPE_BASE = 0,
	WIDGET_TYPE_CONTAINER,
	WIDGET_TYPE_BUTTON,
	WIDGET_TYPE_LABEL,
	WIDGET_TYPE_TEXTINPUT,
	WIDGET_TYPE_ITEMSLOT,
	WIDGET_TYPE_SCROLLCONTAINER,
	WIDGET_TYPE_GRID,
	WIDGET_TYPE_PROGRESSBAR,
	WIDGET_TYPE_TOOLTIP,
	WIDGET_TYPE_VIEWPORT,
	WIDGET_TYPE_CHAT,
	WIDGET_TYPE_INVENTORY,
	WIDGET_TYPE_EQUIPMENT,
	WIDGET_TYPE_SKILLS,
	WIDGET_TYPE_TRADING,
	WIDGET_TYPE_CHARLOOK,
	WIDGET_TYPE_MINIMAP,
	WIDGET_TYPE_QUESTLOG,
	WIDGET_TYPE_HELP,
	WIDGET_TYPE_STATBARS,
	WIDGET_TYPE_HOTBAR,
	WIDGET_TYPE_TELEPORTER,
	WIDGET_TYPE_COLORPICKER,
	WIDGET_TYPE_SLIDER,
	WIDGET_TYPE_VOLUME,
	WIDGET_TYPE_CUSTOM
} WidgetType;

// Mouse button identifiers
typedef enum {
	MOUSE_BUTTON_LEFT = 1,
	MOUSE_BUTTON_MIDDLE = 2,
	MOUSE_BUTTON_RIGHT = 3,
	MOUSE_BUTTON_WHEEL_UP = 4,
	MOUSE_BUTTON_WHEEL_DOWN = 5
} MouseButton;

// Mouse action types
typedef enum { MOUSE_ACTION_DOWN = 1, MOUSE_ACTION_UP = 2, MOUSE_ACTION_MOVE = 3 } MouseAction;

/**
 * Core Widget Structure
 *
 * Base widget that all other widgets inherit from. Uses composition pattern
 * where specific widget types embed this as their first member.
 */
struct widget {
	// === Identity ===
	int id; // Unique widget ID
	WidgetType type; // Widget type identifier
	char name[64]; // Debug/reference name

	// === Hierarchy ===
	Widget *parent; // Parent widget (NULL for root)
	Widget *first_child; // First child in linked list
	Widget *last_child; // Last child (for efficient append)
	Widget *next_sibling; // Next sibling in parent's child list
	Widget *prev_sibling; // Previous sibling in parent's child list

	// === Layout & Positioning ===
	int x, y; // Position relative to parent
	int width, height; // Current size
	int min_width, min_height; // Minimum size constraints
	int max_width, max_height; // Maximum size constraints (-1 = unlimited)

	// === State Flags ===
	unsigned int visible : 1; // Widget is visible
	unsigned int enabled : 1; // Widget accepts input
	unsigned int focused : 1; // Widget has input focus
	unsigned int dirty : 1; // Needs redraw
	unsigned int hover : 1; // Mouse is over widget
	unsigned int pressed : 1; // Mouse button pressed on widget
	unsigned int focusable : 1; // Widget can receive keyboard focus (tab navigation)

	// === Tab Navigation ===
	int tab_index; // Tab order (lower = earlier, -1 = not in tab order)

	// === Window Chrome (optional per widget) ===
	unsigned int has_titlebar : 1; // Show title bar
	unsigned int draggable : 1; // Can drag to move
	unsigned int resizable : 1; // Can resize
	unsigned int minimizable : 1; // Can minimize
	unsigned int closable : 1; // Can close
	unsigned int minimized : 1; // Currently minimized
	unsigned int modal : 1; // Modal widget (blocks others)
	char title[128]; // Title bar text

	// === Z-Order ===
	int z_order; // Z-order for sorting (higher = on top)

	// === Theming ===
	int skin_id; // Theme/skin identifier

	// === Tooltip ===
	char tooltip_text[256]; // Tooltip text (empty = no tooltip)
	int tooltip_delay; // Delay in ms before showing tooltip (default 500)

	// === Virtual Functions (polymorphic behavior) ===

	/**
	 * Render the widget
	 * Called during widget_manager_render()
	 */
	void (*render)(Widget *self);

	/**
	 * Handle mouse button down event
	 * @return 1 if event was handled (stops propagation), 0 otherwise
	 */
	int (*on_mouse_down)(Widget *self, int x, int y, int button);

	/**
	 * Handle mouse button up event
	 * @return 1 if event was handled, 0 otherwise
	 */
	int (*on_mouse_up)(Widget *self, int x, int y, int button);

	/**
	 * Handle mouse double-click event
	 * Called when two clicks occur within DOUBLE_CLICK_TIME_MS at similar position
	 * @return 1 if event was handled, 0 otherwise
	 */
	int (*on_double_click)(Widget *self, int x, int y, int button);

	/**
	 * Handle mouse move event
	 * @return 1 if event was handled, 0 otherwise
	 */
	int (*on_mouse_move)(Widget *self, int x, int y);

	/**
	 * Handle mouse wheel event
	 * @param delta Positive = scroll up, negative = scroll down
	 * @return 1 if event was handled, 0 otherwise
	 */
	int (*on_mouse_wheel)(Widget *self, int x, int y, int delta);

	/**
	 * Handle key down event
	 * @return 1 if event was handled, 0 otherwise
	 */
	int (*on_key_down)(Widget *self, int key);

	/**
	 * Handle key up event
	 * @return 1 if event was handled, 0 otherwise
	 */
	int (*on_key_up)(Widget *self, int key);

	/**
	 * Handle text input event (for printable characters)
	 * @return 1 if event was handled, 0 otherwise
	 */
	int (*on_text_input)(Widget *self, int character);

	/**
	 * Called when widget gains focus
	 */
	void (*on_focus_gain)(Widget *self);

	/**
	 * Called when widget loses focus
	 */
	void (*on_focus_lost)(Widget *self);

	/**
	 * Called when mouse enters widget area
	 */
	void (*on_mouse_enter)(Widget *self);

	/**
	 * Called when mouse leaves widget area
	 */
	void (*on_mouse_leave)(Widget *self);

	/**
	 * Called when widget is resized
	 */
	void (*on_resize)(Widget *self, int new_width, int new_height);

	/**
	 * Called when widget is being destroyed
	 * Use this to free custom data
	 */
	void (*on_destroy)(Widget *self);

	/**
	 * Update widget state (called each frame)
	 * @param dt Delta time in milliseconds
	 */
	void (*update)(Widget *self, int dt);

	// === User Data ===
	void *user_data; // Custom data pointer (widget-specific)
	int user_data_size; // Size of user_data (for memory tracking)
};

// =============================================================================
// Widget Core API
// =============================================================================

/**
 * Create a new widget
 *
 * @param type Widget type identifier
 * @param x X position relative to parent
 * @param y Y position relative to parent
 * @param width Widget width
 * @param height Widget height
 * @return New widget instance, or NULL on failure
 */
DLL_EXPORT Widget *widget_create(WidgetType type, int x, int y, int width, int height);

/**
 * Destroy a widget and all its children
 *
 * @param widget Widget to destroy
 */
DLL_EXPORT void widget_destroy(Widget *widget);

// =============================================================================
// Widget Hierarchy
// =============================================================================

/**
 * Add a child widget
 *
 * @param parent Parent widget
 * @param child Child widget to add
 * @return 1 on success, 0 on failure
 */
DLL_EXPORT int widget_add_child(Widget *parent, Widget *child);

/**
 * Remove a child widget (does not destroy it)
 *
 * @param parent Parent widget
 * @param child Child widget to remove
 * @return 1 on success, 0 on failure
 */
DLL_EXPORT int widget_remove_child(Widget *parent, Widget *child);

/**
 * Find a child widget by name
 *
 * @param parent Parent widget to search
 * @param name Name to search for
 * @param recursive If true, search all descendants
 * @return Widget if found, NULL otherwise
 */
DLL_EXPORT Widget *widget_find_child(Widget *parent, const char *name, int recursive);

/**
 * Get the root widget (topmost parent)
 *
 * @param widget Any widget in the hierarchy
 * @return Root widget
 */
DLL_EXPORT Widget *widget_get_root(Widget *widget);

// =============================================================================
// Widget State
// =============================================================================

/**
 * Set widget visibility
 *
 * @param widget Widget to modify
 * @param visible 1 to show, 0 to hide
 */
DLL_EXPORT void widget_set_visible(Widget *widget, int visible);

/**
 * Set widget enabled state
 *
 * @param widget Widget to modify
 * @param enabled 1 to enable, 0 to disable
 */
DLL_EXPORT void widget_set_enabled(Widget *widget, int enabled);

/**
 * Set input focus to this widget
 *
 * @param widget Widget to focus (NULL to clear focus)
 */
DLL_EXPORT void widget_set_focus(Widget *widget);

/**
 * Mark widget as dirty (needs redraw)
 * Also marks all parents as dirty
 *
 * @param widget Widget to mark dirty
 */
DLL_EXPORT void widget_mark_dirty(Widget *widget);

/**
 * Bring widget to front (highest z-order)
 * Only affects siblings at the same hierarchy level
 *
 * @param widget Widget to bring to front
 */
DLL_EXPORT void widget_bring_to_front(Widget *widget);

/**
 * Send widget to back (lowest z-order)
 * Only affects siblings at the same hierarchy level
 *
 * @param widget Widget to send to back
 */
DLL_EXPORT void widget_send_to_back(Widget *widget);

// =============================================================================
// Widget Layout & Positioning
// =============================================================================

/**
 * Set widget position
 *
 * @param widget Widget to modify
 * @param x New X position relative to parent
 * @param y New Y position relative to parent
 */
DLL_EXPORT void widget_set_position(Widget *widget, int x, int y);

/**
 * Set widget size
 *
 * @param widget Widget to modify
 * @param width New width
 * @param height New height
 */
DLL_EXPORT void widget_set_size(Widget *widget, int width, int height);

/**
 * Set widget bounds (position and size)
 *
 * @param widget Widget to modify
 * @param x New X position
 * @param y New Y position
 * @param width New width
 * @param height New height
 */
DLL_EXPORT void widget_set_bounds(Widget *widget, int x, int y, int width, int height);

/**
 * Get widget absolute screen position
 *
 * @param widget Widget to query
 * @param screen_x Output: screen X coordinate
 * @param screen_y Output: screen Y coordinate
 */
DLL_EXPORT void widget_get_screen_position(Widget *widget, int *screen_x, int *screen_y);

/**
 * Convert widget-local coordinates to screen coordinates
 *
 * @param widget Widget
 * @param local_x Local X coordinate
 * @param local_y Local Y coordinate
 * @param screen_x Output: screen X coordinate
 * @param screen_y Output: screen Y coordinate
 */
DLL_EXPORT void widget_local_to_screen(Widget *widget, int local_x, int local_y, int *screen_x, int *screen_y);

/**
 * Convert screen coordinates to widget-local coordinates
 *
 * @param widget Widget
 * @param screen_x Screen X coordinate
 * @param screen_y Screen Y coordinate
 * @param local_x Output: local X coordinate
 * @param local_y Output: local Y coordinate
 */
DLL_EXPORT void widget_screen_to_local(Widget *widget, int screen_x, int screen_y, int *local_x, int *local_y);

// =============================================================================
// Widget Hit Testing
// =============================================================================

/**
 * Test if a point (in widget-local coordinates) is inside the widget
 *
 * @param widget Widget to test
 * @param local_x Local X coordinate
 * @param local_y Local Y coordinate
 * @return 1 if point is inside, 0 otherwise
 */
DLL_EXPORT int widget_hit_test(Widget *widget, int local_x, int local_y);

/**
 * Find the topmost widget at screen coordinates
 * Searches recursively through widget hierarchy
 *
 * @param root Root widget to search from
 * @param screen_x Screen X coordinate
 * @param screen_y Screen Y coordinate
 * @return Widget at position, or NULL if none found
 */
DLL_EXPORT Widget *widget_find_at_position(Widget *root, int screen_x, int screen_y);

// =============================================================================
// Widget Window Chrome
// =============================================================================

/**
 * Set widget title
 *
 * @param widget Widget to modify
 * @param title New title text
 */
DLL_EXPORT void widget_set_title(Widget *widget, const char *title);

/**
 * Set widget name (used for identification and state persistence)
 *
 * @param widget Widget to modify
 * @param name New name (max 63 characters)
 */
DLL_EXPORT void widget_set_name(Widget *widget, const char *name);

/**
 * Enable/disable window chrome features
 *
 * @param widget Widget to modify
 * @param has_titlebar Show title bar
 * @param draggable Allow dragging
 * @param resizable Allow resizing
 * @param minimizable Show minimize button
 * @param closable Show close button
 */
DLL_EXPORT void widget_set_window_chrome(
    Widget *widget, int has_titlebar, int draggable, int resizable, int minimizable, int closable);

/**
 * Minimize/restore a widget
 *
 * @param widget Widget to modify
 * @param minimized 1 to minimize, 0 to restore
 */
DLL_EXPORT void widget_set_minimized(Widget *widget, int minimized);

// =============================================================================
// Widget Tooltip
// =============================================================================

/**
 * Set tooltip text for a widget
 * When the mouse hovers over this widget, the tooltip will be shown
 *
 * @param widget Widget to modify
 * @param text Tooltip text (NULL or empty to disable)
 */
DLL_EXPORT void widget_set_tooltip_text(Widget *widget, const char *text);

/**
 * Set tooltip delay for a widget
 *
 * @param widget Widget to modify
 * @param delay_ms Delay in milliseconds before showing tooltip (default 500)
 */
DLL_EXPORT void widget_set_tooltip_delay(Widget *widget, int delay_ms);

// =============================================================================
// Widget Tab Navigation
// =============================================================================

/**
 * Set whether a widget can receive keyboard focus via tab navigation
 *
 * @param widget Widget to modify
 * @param focusable 1 if widget can receive focus, 0 otherwise
 */
DLL_EXPORT void widget_set_focusable(Widget *widget, int focusable);

/**
 * Set widget tab index for tab navigation order
 *
 * Lower indices are focused first. Widgets with the same tab_index
 * are navigated in tree order. Use -1 to exclude from tab order.
 *
 * @param widget Widget to modify
 * @param tab_index Tab order index (-1 = not in tab order)
 */
DLL_EXPORT void widget_set_tab_index(Widget *widget, int tab_index);

// =============================================================================
// Widget Rendering Helpers
// =============================================================================

/**
 * Render window chrome (title bar, borders, close button, etc.)
 * Called automatically by widgets with has_titlebar = 1
 *
 * @param widget Widget to render chrome for
 */
void widget_render_chrome(Widget *widget);

/**
 * Handle title bar dragging
 * Called automatically during mouse events
 *
 * @param widget Widget being dragged
 * @param screen_x Current mouse X position
 * @param screen_y Current mouse Y position
 * @return 1 if dragging was handled, 0 otherwise
 */
int widget_handle_titlebar_drag(Widget *widget, int screen_x, int screen_y);

/**
 * Handle resize dragging
 * Called automatically during mouse events
 *
 * @param widget Widget being resized
 * @param screen_x Current mouse X position
 * @param screen_y Current mouse Y position
 * @param handle Resize handle index (0-7)
 * @return 1 if resizing was handled, 0 otherwise
 */
int widget_handle_resize(Widget *widget, int screen_x, int screen_y, int handle);

/**
 * Get resize handle at position (for resizable widgets)
 *
 * @param widget Widget to test
 * @param screen_x Screen X position
 * @param screen_y Screen Y position
 * @return Handle index (0-7), or -1 if no handle at position
 *         0=top-left, 1=top, 2=top-right, 3=right, 4=bottom-right,
 *         5=bottom, 6=bottom-left, 7=left
 */
int widget_get_resize_handle(Widget *widget, int screen_x, int screen_y);

#endif // WIDGET_H
