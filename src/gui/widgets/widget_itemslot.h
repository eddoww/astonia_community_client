/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * ItemSlot Widget - Display and interact with game items
 */

#ifndef WIDGET_ITEMSLOT_H
#define WIDGET_ITEMSLOT_H

#include "../widget.h"

// Drag source type for item slots
#define DRAG_TYPE_ITEM_INV       1 // Inventory item
#define DRAG_TYPE_ITEM_EQUIP     2 // Equipment item
#define DRAG_TYPE_ITEM_CONTAINER 3 // Container item
#define DRAG_TYPE_ITEM_GROUND    4 // Ground item

// ItemSlot-specific data
typedef struct {
	int item_index; // Index into game's item array (-1 for empty)
	int item_sprite; // Cached sprite ID for rendering
	int item_count; // Stack count (for stackable items)
	int item_price; // Price (for vendor containers)

	// Visual state
	int selected; // Slot is selected
	int highlighted; // Slot is highlighted (drag target)

	// Drag & drop
	int drag_source_type; // Type of drag source (DRAG_TYPE_*)
	int allow_drag; // Allow dragging from this slot
	int allow_drop; // Allow dropping into this slot

	// Callbacks
	void (*on_click)(Widget *self, int button); // Click callback
	void (*on_double_click)(Widget *self); // Double-click callback
	void (*on_right_click)(Widget *self); // Right-click callback
	void (*on_drag_start)(Widget *self); // Drag start callback
	void (*on_drop)(Widget *self, int source_type, int source_index); // Drop callback

	// Tooltip data
	char tooltip_text[256]; // Tooltip text (item description)
	int show_tooltip; // Show tooltip on hover

	// Colors
	unsigned short bg_color;
	unsigned short border_color;
	unsigned short select_color;
	unsigned short highlight_color;

	// F-key indicator
	int fkey_slot; // F-key slot (0-3, -1 for none)
} ItemSlotData;

/**
 * Create an item slot widget
 *
 * @param x X position
 * @param y Y position
 * @param size Slot size (width and height)
 * @return ItemSlot widget
 */
DLL_EXPORT Widget *widget_itemslot_create(int x, int y, int size);

/**
 * Set item in slot
 *
 * @param slot ItemSlot widget
 * @param item_index Index into game's item array (-1 for empty)
 * @param sprite Item sprite ID
 * @param count Stack count
 */
DLL_EXPORT void widget_itemslot_set_item(Widget *slot, int item_index, int sprite, int count);

/**
 * Set item price (for vendor display)
 *
 * @param slot ItemSlot widget
 * @param price Item price in silver
 */
DLL_EXPORT void widget_itemslot_set_price(Widget *slot, int price);

/**
 * Set selected state
 *
 * @param slot ItemSlot widget
 * @param selected 1 to select, 0 to deselect
 */
DLL_EXPORT void widget_itemslot_set_selected(Widget *slot, int selected);

/**
 * Set highlighted state (for drag target)
 *
 * @param slot ItemSlot widget
 * @param highlighted 1 to highlight, 0 to remove
 */
DLL_EXPORT void widget_itemslot_set_highlighted(Widget *slot, int highlighted);

/**
 * Enable/disable drag from this slot
 *
 * @param slot ItemSlot widget
 * @param allow 1 to allow, 0 to disallow
 * @param source_type Drag source type (DRAG_TYPE_*)
 */
DLL_EXPORT void widget_itemslot_set_drag_source(Widget *slot, int allow, int source_type);

/**
 * Enable/disable drop into this slot
 *
 * @param slot ItemSlot widget
 * @param allow 1 to allow, 0 to disallow
 */
DLL_EXPORT void widget_itemslot_set_drop_target(Widget *slot, int allow);

/**
 * Set click callbacks
 *
 * @param slot ItemSlot widget
 * @param on_click Single click callback
 * @param on_double_click Double click callback
 * @param on_right_click Right click callback
 */
DLL_EXPORT void widget_itemslot_set_click_callbacks(
    Widget *slot, void (*on_click)(Widget *, int), void (*on_double_click)(Widget *), void (*on_right_click)(Widget *));

/**
 * Set drag/drop callbacks
 *
 * @param slot ItemSlot widget
 * @param on_drag_start Drag start callback
 * @param on_drop Drop callback
 */
DLL_EXPORT void widget_itemslot_set_drag_callbacks(
    Widget *slot, void (*on_drag_start)(Widget *), void (*on_drop)(Widget *, int, int));

/**
 * Set tooltip text
 *
 * @param slot ItemSlot widget
 * @param text Tooltip text (item name/description)
 */
DLL_EXPORT void widget_itemslot_set_tooltip(Widget *slot, const char *text);

/**
 * Set F-key indicator
 *
 * @param slot ItemSlot widget
 * @param fkey F-key slot (0-3 for F1-F4, -1 for none)
 */
DLL_EXPORT void widget_itemslot_set_fkey(Widget *slot, int fkey);

/**
 * Get item index from slot
 *
 * @param slot ItemSlot widget
 * @return Item index, or -1 if empty
 */
DLL_EXPORT int widget_itemslot_get_item(Widget *slot);

/**
 * Check if slot is empty
 *
 * @param slot ItemSlot widget
 * @return 1 if empty, 0 if has item
 */
DLL_EXPORT int widget_itemslot_is_empty(Widget *slot);

#endif // WIDGET_ITEMSLOT_H
