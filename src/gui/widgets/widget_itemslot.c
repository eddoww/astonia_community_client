/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * ItemSlot Widget Implementation
 */

#include <string.h>
#include <stdio.h>
#include <SDL2/SDL.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_itemslot.h"
#include "../../game/game.h"

// Forward declarations
static void itemslot_render(Widget *self);
static int itemslot_on_mouse_down(Widget *self, int x, int y, int button);
static int itemslot_on_mouse_up(Widget *self, int x, int y, int button);
static int itemslot_on_mouse_move(Widget *self, int x, int y);
static void itemslot_on_destroy(Widget *self);

// Double-click detection
#define DOUBLE_CLICK_TIME 300 // milliseconds
static unsigned int last_click_time = 0;
static int last_click_slot_id = -1;

Widget *widget_itemslot_create(int x, int y, int size)
{
	Widget *widget;
	ItemSlotData *data;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_ITEMSLOT, x, y, size, size);
	if (!widget) {
		return NULL;
	}

	// Allocate itemslot-specific data
	data = xmalloc(sizeof(ItemSlotData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(ItemSlotData));

	// Initialize data
	data->item_index = -1; // Empty slot
	data->item_sprite = 0;
	data->item_count = 0;
	data->item_price = 0;
	data->selected = 0;
	data->highlighted = 0;
	data->drag_source_type = DRAG_TYPE_ITEM_INV;
	data->allow_drag = 1;
	data->allow_drop = 1;
	data->on_click = NULL;
	data->on_double_click = NULL;
	data->on_right_click = NULL;
	data->on_drag_start = NULL;
	data->on_drop = NULL;
	data->tooltip_text[0] = '\0';
	data->show_tooltip = 1;
	data->fkey_slot = -1;

	// Default colors (dark medieval theme)
	data->bg_color = IRGB(8, 8, 10);
	data->border_color = IRGB(12, 12, 14);
	data->select_color = IRGB(20, 15, 10); // Orange-brown when selected
	data->highlight_color = IRGB(15, 20, 10); // Green when drag target

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(ItemSlotData);

	// Set virtual functions
	widget->render = itemslot_render;
	widget->on_mouse_down = itemslot_on_mouse_down;
	widget->on_mouse_up = itemslot_on_mouse_up;
	widget->on_mouse_move = itemslot_on_mouse_move;
	widget->on_destroy = itemslot_on_destroy;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "itemslot_%d", widget->id);

	return widget;
}

void widget_itemslot_set_item(Widget *slot, int item_index, int sprite, int count)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	data->item_index = item_index;
	data->item_sprite = sprite;
	data->item_count = count;
	widget_mark_dirty(slot);
}

void widget_itemslot_set_price(Widget *slot, int price)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	data->item_price = price;
	widget_mark_dirty(slot);
}

void widget_itemslot_set_selected(Widget *slot, int selected)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	if (data->selected != selected) {
		data->selected = selected;
		widget_mark_dirty(slot);
	}
}

void widget_itemslot_set_highlighted(Widget *slot, int highlighted)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	if (data->highlighted != highlighted) {
		data->highlighted = highlighted;
		widget_mark_dirty(slot);
	}
}

void widget_itemslot_set_drag_source(Widget *slot, int allow, int source_type)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	data->allow_drag = allow;
	data->drag_source_type = source_type;
}

void widget_itemslot_set_drop_target(Widget *slot, int allow)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	data->allow_drop = allow;
}

void widget_itemslot_set_click_callbacks(
    Widget *slot, void (*on_click)(Widget *, int), void (*on_double_click)(Widget *), void (*on_right_click)(Widget *))
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	data->on_click = on_click;
	data->on_double_click = on_double_click;
	data->on_right_click = on_right_click;
}

void widget_itemslot_set_drag_callbacks(
    Widget *slot, void (*on_drag_start)(Widget *), void (*on_drop)(Widget *, int, int))
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	data->on_drag_start = on_drag_start;
	data->on_drop = on_drop;
}

void widget_itemslot_set_tooltip(Widget *slot, const char *text)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT || !text) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	strncpy(data->tooltip_text, text, sizeof(data->tooltip_text) - 1);
	data->tooltip_text[sizeof(data->tooltip_text) - 1] = '\0';
}

void widget_itemslot_set_fkey(Widget *slot, int fkey)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return;
	}

	data->fkey_slot = fkey;
	widget_mark_dirty(slot);
}

int widget_itemslot_get_item(Widget *slot)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return -1;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return -1;
	}

	return data->item_index;
}

int widget_itemslot_is_empty(Widget *slot)
{
	ItemSlotData *data;

	if (!slot || slot->type != WIDGET_TYPE_ITEMSLOT) {
		return 1;
	}

	data = (ItemSlotData *)slot->user_data;
	if (!data) {
		return 1;
	}

	return data->item_index < 0;
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void itemslot_render(Widget *self)
{
	ItemSlotData *data;
	int screen_x, screen_y;
	unsigned short border_color;

	if (!self) {
		return;
	}

	data = (ItemSlotData *)self->user_data;
	if (!data) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Determine border color based on state
	if (data->highlighted) {
		border_color = data->highlight_color;
	} else if (data->selected) {
		border_color = data->select_color;
	} else {
		border_color = data->border_color;
	}

	// Draw slot background
	render_rect(screen_x + 1, screen_y + 1, screen_x + self->width - 1, screen_y + self->height - 1, data->bg_color);

	// Draw slot border
	render_line(screen_x, screen_y, screen_x + self->width, screen_y, border_color);
	render_line(screen_x, screen_y + self->height, screen_x + self->width, screen_y + self->height, border_color);
	render_line(screen_x, screen_y, screen_x, screen_y + self->height, border_color);
	render_line(screen_x + self->width, screen_y, screen_x + self->width, screen_y + self->height, border_color);

	// Draw item sprite if present
	if (data->item_index >= 0 && data->item_sprite > 0) {
		int sprite_x = screen_x + self->width / 2;
		int sprite_y = screen_y + self->height / 2;

		// Render sprite centered
		render_sprite(data->item_sprite, sprite_x, sprite_y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);

		// Draw stack count if > 1
		if (data->item_count > 1) {
			char count_text[16];
			snprintf(count_text, sizeof(count_text), "%d", data->item_count);
			render_text(screen_x + self->width - 3, screen_y + self->height - 10, IRGB(31, 31, 31),
			    RENDER_TEXT_RIGHT | RENDER_TEXT_SMALL, count_text);
		}
	}

	// Draw price if set (vendor display)
	if (data->item_price > 0) {
		char price_text[32];
		if (data->item_price > 99) {
			snprintf(price_text, sizeof(price_text), "%d.%02dG", data->item_price / 100, data->item_price % 100);
		} else {
			snprintf(price_text, sizeof(price_text), "%ds", data->item_price);
		}
		render_text(screen_x + self->width / 2, screen_y + self->height + 2, IRGB(25, 25, 28),
		    RENDER_ALIGN_CENTER | RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, price_text);
	}

	// Draw F-key indicator
	if (data->fkey_slot >= 0 && data->fkey_slot < 4) {
		const char *fkey_labels[] = {"F1", "F2", "F3", "F4"};
		render_text(screen_x + self->width / 2, screen_y - 12, IRGB(25, 25, 28),
		    RENDER_ALIGN_CENTER | RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, fkey_labels[data->fkey_slot]);
	}

	// TODO: Draw tooltip if hovering and tooltip text is set
	// This will be implemented with the Tooltip widget
}

static int itemslot_on_mouse_down(Widget *self, int x, int y, int button)
{
	ItemSlotData *data;
	unsigned int now;

	if (!self) {
		return 0;
	}

	data = (ItemSlotData *)self->user_data;
	if (!data) {
		return 0;
	}

	now = SDL_GetTicks();

	if (button == MOUSE_BUTTON_LEFT) {
		// Check for double-click
		if (last_click_slot_id == self->id && (now - last_click_time) < DOUBLE_CLICK_TIME) {
			// Double-click
			if (data->on_double_click) {
				data->on_double_click(self);
			}
			last_click_slot_id = -1; // Reset to prevent triple-click
		} else {
			// Single click
			last_click_slot_id = self->id;
			last_click_time = now;

			// Start drag if allowed and has item
			if (data->allow_drag && data->item_index >= 0) {
				// Prepare drag data
				int *drag_data = xmalloc(sizeof(int), MEM_GUI);
				if (drag_data) {
					*drag_data = data->item_index;
					widget_manager_start_item_drag(self, drag_data, data->drag_source_type);

					// Call drag start callback
					if (data->on_drag_start) {
						data->on_drag_start(self);
					}
				}
			}
		}

		return 1;
	} else if (button == MOUSE_BUTTON_RIGHT) {
		// Right-click
		if (data->on_right_click) {
			data->on_right_click(self);
		}
		return 1;
	}

	return 0;
}

static int itemslot_on_mouse_up(Widget *self, int x, int y, int button)
{
	ItemSlotData *data;
	int drag_type;
	int *drag_data;

	if (!self || button != MOUSE_BUTTON_LEFT) {
		return 0;
	}

	data = (ItemSlotData *)self->user_data;
	if (!data) {
		return 0;
	}

	// Check if item is being dragged
	if (widget_manager_is_item_dragging()) {
		drag_data = (int *)widget_manager_get_drag_data(&drag_type);

		if (drag_data && data->allow_drop) {
			// Item was dropped onto this slot
			int source_index = *drag_data;

			// Call drop callback
			if (data->on_drop) {
				data->on_drop(self, drag_type, source_index);
			}

			// Stop drag
			xfree(widget_manager_stop_item_drag(self));

			return 1;
		}
	} else {
		// Regular click (not dragging)
		if (data->on_click) {
			data->on_click(self, button);
		}
		return 1;
	}

	return 0;
}

static int itemslot_on_mouse_move(Widget *self, int x, int y)
{
	ItemSlotData *data;

	if (!self) {
		return 0;
	}

	data = (ItemSlotData *)self->user_data;
	if (!data) {
		return 0;
	}

	// Highlight if dragging and this is a valid drop target
	if (widget_manager_is_item_dragging() && data->allow_drop) {
		widget_itemslot_set_highlighted(self, 1);
	} else {
		widget_itemslot_set_highlighted(self, 0);
	}

	return 0;
}

static void itemslot_on_destroy(Widget *self)
{
	// User data will be freed automatically
}
