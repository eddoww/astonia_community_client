/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Button Widget Implementation
 */

#include <string.h>
#include <stdio.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_button.h"
#include "../../game/game.h"

// Forward declarations
static void button_render(Widget *self);
static int button_on_mouse_down(Widget *self, int x, int y, int button);
static int button_on_mouse_up(Widget *self, int x, int y, int button);
static int button_on_mouse_move(Widget *self, int x, int y);
static void button_on_destroy(Widget *self);

Widget *widget_button_create(int x, int y, int width, int height, const char *text)
{
	Widget *widget;
	ButtonData *data;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_BUTTON, x, y, width, height);
	if (!widget) {
		return NULL;
	}

	// Allocate button-specific data
	data = xmalloc(sizeof(ButtonData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(ButtonData));

	// Initialize data
	if (text) {
		strncpy(data->text, text, sizeof(data->text) - 1);
		data->text[sizeof(data->text) - 1] = '\0';
	} else {
		data->text[0] = '\0';
	}

	data->sprite = -1; // No icon by default
	data->state = BUTTON_STATE_NORMAL;

	// Default colors (dark medieval theme)
	data->normal_color = IRGB(10, 10, 12); // Dark gray
	data->hover_color = IRGB(13, 13, 15); // Slightly lighter
	data->pressed_color = IRGB(8, 8, 10); // Darker when pressed
	data->disabled_color = IRGB(6, 6, 8); // Very dark when disabled
	data->text_color = IRGB(25, 25, 28); // Light gray text

	data->on_click = NULL;
	data->callback_param = NULL;

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(ButtonData);

	// Set virtual functions
	widget->render = button_render;
	widget->on_mouse_down = button_on_mouse_down;
	widget->on_mouse_up = button_on_mouse_up;
	widget->on_mouse_move = button_on_mouse_move;
	widget->on_destroy = button_on_destroy;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "button_%d", widget->id);

	return widget;
}

void widget_button_set_text(Widget *button, const char *text)
{
	ButtonData *data;

	if (!button || button->type != WIDGET_TYPE_BUTTON || !text) {
		return;
	}

	data = (ButtonData *)button->user_data;
	if (!data) {
		return;
	}

	strncpy(data->text, text, sizeof(data->text) - 1);
	data->text[sizeof(data->text) - 1] = '\0';
	widget_mark_dirty(button);
}

void widget_button_set_sprite(Widget *button, int sprite)
{
	ButtonData *data;

	if (!button || button->type != WIDGET_TYPE_BUTTON) {
		return;
	}

	data = (ButtonData *)button->user_data;
	if (!data) {
		return;
	}

	data->sprite = sprite;
	widget_mark_dirty(button);
}

void widget_button_set_callback(Widget *button, ButtonCallback callback, void *param)
{
	ButtonData *data;

	if (!button || button->type != WIDGET_TYPE_BUTTON) {
		return;
	}

	data = (ButtonData *)button->user_data;
	if (!data) {
		return;
	}

	data->on_click = callback;
	data->callback_param = param;
}

void widget_button_set_colors(
    Widget *button, unsigned short normal, unsigned short hover, unsigned short pressed, unsigned short disabled)
{
	ButtonData *data;

	if (!button || button->type != WIDGET_TYPE_BUTTON) {
		return;
	}

	data = (ButtonData *)button->user_data;
	if (!data) {
		return;
	}

	data->normal_color = normal;
	data->hover_color = hover;
	data->pressed_color = pressed;
	data->disabled_color = disabled;
	widget_mark_dirty(button);
}

void widget_button_set_text_color(Widget *button, unsigned short color)
{
	ButtonData *data;

	if (!button || button->type != WIDGET_TYPE_BUTTON) {
		return;
	}

	data = (ButtonData *)button->user_data;
	if (!data) {
		return;
	}

	data->text_color = color;
	widget_mark_dirty(button);
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void button_render(Widget *self)
{
	ButtonData *data;
	int screen_x, screen_y;
	unsigned short bg_color, border_color;

	if (!self) {
		return;
	}

	data = (ButtonData *)self->user_data;
	if (!data) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Select color based on state
	if (!self->enabled) {
		bg_color = data->disabled_color;
		border_color = IRGB(8, 8, 8);
	} else if (data->state == BUTTON_STATE_PRESSED) {
		bg_color = data->pressed_color;
		border_color = IRGB(18, 18, 20);
	} else if (data->state == BUTTON_STATE_HOVER) {
		bg_color = data->hover_color;
		border_color = IRGB(15, 15, 17);
	} else {
		bg_color = data->normal_color;
		border_color = IRGB(12, 12, 14);
	}

	// Draw button background
	render_rect(screen_x + 1, screen_y + 1, screen_x + self->width - 1, screen_y + self->height - 1, bg_color);

	// Draw button border
	render_line(screen_x, screen_y, screen_x + self->width, screen_y, border_color);
	render_line(screen_x, screen_y + self->height, screen_x + self->width, screen_y + self->height, border_color);
	render_line(screen_x, screen_y, screen_x, screen_y + self->height, border_color);
	render_line(screen_x + self->width, screen_y, screen_x + self->width, screen_y + self->height, border_color);

	// Draw icon if set
	if (data->sprite >= 0) {
		int icon_x = screen_x + 5;
		int icon_y = screen_y + (self->height - 32) / 2; // Assuming 32x32 sprites
		render_sprite(data->sprite, icon_x, icon_y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	}

	// Draw text
	if (data->text[0]) {
		int text_x = screen_x + self->width / 2;
		int text_y = screen_y + self->height / 2 - 4; // Center vertically

		// Offset text if icon is present
		if (data->sprite >= 0) {
			text_x = screen_x + 40; // After icon
		}

		render_text(text_x, text_y, data->text_color,
		    (data->sprite >= 0 ? RENDER_TEXT_LEFT : RENDER_ALIGN_CENTER) | RENDER_TEXT_SMALL, data->text);
	}
}

static int button_on_mouse_down(Widget *self, int x, int y, int button)
{
	ButtonData *data;

	if (!self || button != MOUSE_BUTTON_LEFT) {
		return 0;
	}

	data = (ButtonData *)self->user_data;
	if (!data) {
		return 0;
	}

	// Set pressed state
	data->state = BUTTON_STATE_PRESSED;
	widget_mark_dirty(self);

	return 1; // Event handled
}

static int button_on_mouse_up(Widget *self, int x, int y, int button)
{
	ButtonData *data;

	if (!self || button != MOUSE_BUTTON_LEFT) {
		return 0;
	}

	data = (ButtonData *)self->user_data;
	if (!data) {
		return 0;
	}

	// Was button pressed?
	if (data->state == BUTTON_STATE_PRESSED) {
		// Check if mouse is still over button
		if (x >= 0 && x < self->width && y >= 0 && y < self->height) {
			// Call click callback
			if (data->on_click) {
				data->on_click(self, data->callback_param);
			}

			// Set hover state
			data->state = BUTTON_STATE_HOVER;
		} else {
			// Mouse moved outside button
			data->state = BUTTON_STATE_NORMAL;
		}

		widget_mark_dirty(self);
		return 1;
	}

	return 0;
}

static int button_on_mouse_move(Widget *self, int x, int y)
{
	ButtonData *data;

	if (!self) {
		return 0;
	}

	data = (ButtonData *)self->user_data;
	if (!data) {
		return 0;
	}

	// Update hover state (but don't override pressed state)
	if (data->state != BUTTON_STATE_PRESSED) {
		if (self->hover && data->state != BUTTON_STATE_HOVER) {
			data->state = BUTTON_STATE_HOVER;
			widget_mark_dirty(self);
		} else if (!self->hover && data->state == BUTTON_STATE_HOVER) {
			data->state = BUTTON_STATE_NORMAL;
			widget_mark_dirty(self);
		}
	}

	return 0;
}

static void button_on_destroy(Widget *self)
{
	// User data will be freed automatically
}
