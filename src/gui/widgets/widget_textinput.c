/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * TextInput Widget Implementation
 */

#include <string.h>
#include <ctype.h>
#include <SDL2/SDL.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_textinput.h"
#include "../../game/game.h"

// Forward declarations
static void textinput_render(Widget *self);
static int textinput_on_key_down(Widget *self, int key);
static int textinput_on_text_input(Widget *self, int character);
static void textinput_on_focus_gain(Widget *self);
static void textinput_on_focus_lost(Widget *self);
static void textinput_update(Widget *self, int dt);
static void textinput_on_destroy(Widget *self);

// Cursor blink rate
#define CURSOR_BLINK_RATE 500 // milliseconds

Widget *widget_textinput_create(int x, int y, int width, int height)
{
	Widget *widget;
	TextInputData *data;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_TEXTINPUT, x, y, width, height);
	if (!widget) {
		return NULL;
	}

	// Allocate textinput-specific data
	data = xmalloc(sizeof(TextInputData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(TextInputData));

	// Allocate text buffer
	data->text_capacity = 256;
	data->text = xmalloc(data->text_capacity, MEM_GUI);
	if (!data->text) {
		xfree(data);
		widget_destroy(widget);
		return NULL;
	}

	data->text[0] = '\0';
	data->text_length = 0;
	data->cursor_pos = 0;
	data->scroll_offset = 0;
	data->selection_start = -1;
	data->selection_end = -1;

	// Visual settings
	data->bg_color = IRGB(5, 5, 7);
	data->border_color = IRGB(12, 12, 14);
	data->text_color = IRGB(25, 25, 28);
	data->cursor_color = IRGB(31, 31, 31);
	data->selection_color = IRGB(15, 15, 20);
	data->show_cursor = 0;
	data->cursor_blink_time = SDL_GetTicks();

	// Behavior
	data->max_length = 0;
	data->password_mode = 0;
	data->multiline = 0;
	data->readonly = 0;

	// Callbacks
	data->on_submit = NULL;
	data->on_change = NULL;
	data->callback_param = NULL;

	// Placeholder
	data->placeholder[0] = '\0';
	data->placeholder_color = IRGB(15, 15, 17);

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(TextInputData);

	// Set virtual functions
	widget->render = textinput_render;
	widget->on_key_down = textinput_on_key_down;
	widget->on_text_input = textinput_on_text_input;
	widget->on_focus_gain = textinput_on_focus_gain;
	widget->on_focus_lost = textinput_on_focus_lost;
	widget->update = textinput_update;
	widget->on_destroy = textinput_on_destroy;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "textinput_%d", widget->id);

	return widget;
}

void widget_textinput_set_text(Widget *input, const char *text)
{
	TextInputData *data;
	int len;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT || !text) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	len = strlen(text);

	// Reallocate if needed
	if (len + 1 > data->text_capacity) {
		int new_capacity = len + 1;
		char *new_text = xrealloc(data->text, new_capacity, MEM_GUI);
		if (!new_text) {
			return;
		}
		data->text = new_text;
		data->text_capacity = new_capacity;
	}

	strcpy(data->text, text);
	data->text_length = len;
	data->cursor_pos = len;

	// Call change callback
	if (data->on_change) {
		data->on_change(input, data->callback_param);
	}

	widget_mark_dirty(input);
}

const char *widget_textinput_get_text(Widget *input)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return "";
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return "";
	}

	return data->text;
}

void widget_textinput_clear(Widget *input)
{
	widget_textinput_set_text(input, "");
}

void widget_textinput_set_cursor(Widget *input, int pos)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	if (pos < 0) {
		pos = 0;
	}
	if (pos > data->text_length) {
		pos = data->text_length;
	}

	data->cursor_pos = pos;
	widget_mark_dirty(input);
}

void widget_textinput_set_max_length(Widget *input, int max_length)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	data->max_length = max_length;
}

void widget_textinput_set_password_mode(Widget *input, int password)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	data->password_mode = password;
	widget_mark_dirty(input);
}

void widget_textinput_set_readonly(Widget *input, int readonly)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	data->readonly = readonly;
}

void widget_textinput_set_placeholder(Widget *input, const char *placeholder)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT || !placeholder) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	strncpy(data->placeholder, placeholder, sizeof(data->placeholder) - 1);
	data->placeholder[sizeof(data->placeholder) - 1] = '\0';
	widget_mark_dirty(input);
}

void widget_textinput_set_colors(Widget *input, unsigned short bg, unsigned short border, unsigned short text)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	data->bg_color = bg;
	data->border_color = border;
	data->text_color = text;
	widget_mark_dirty(input);
}

void widget_textinput_set_submit_callback(Widget *input, TextInputCallback callback, void *param)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	data->on_submit = callback;
	data->callback_param = param;
}

void widget_textinput_set_change_callback(Widget *input, TextInputChangeCallback callback, void *param)
{
	TextInputData *data;

	if (!input || input->type != WIDGET_TYPE_TEXTINPUT) {
		return;
	}

	data = (TextInputData *)input->user_data;
	if (!data) {
		return;
	}

	data->on_change = callback;
	data->callback_param = param;
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void textinput_render(Widget *self)
{
	TextInputData *data;
	int screen_x, screen_y;
	const char *display_text;
	char password_text[256];
	int text_x, text_y;

	if (!self) {
		return;
	}

	data = (TextInputData *)self->user_data;
	if (!data) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Draw background
	render_rect(screen_x + 1, screen_y + 1, screen_x + self->width - 1, screen_y + self->height - 1, data->bg_color);

	// Draw border (highlighted if focused)
	unsigned short border_color = self->focused ? IRGB(20, 20, 25) : data->border_color;
	render_line(screen_x, screen_y, screen_x + self->width, screen_y, border_color);
	render_line(screen_x, screen_y + self->height, screen_x + self->width, screen_y + self->height, border_color);
	render_line(screen_x, screen_y, screen_x, screen_y + self->height, border_color);
	render_line(screen_x + self->width, screen_y, screen_x + self->width, screen_y + self->height, border_color);

	// Prepare display text
	if (data->password_mode && data->text_length > 0) {
		// Show asterisks for password
		int i;
		for (i = 0; i < data->text_length && i < 255; i++) {
			password_text[i] = '*';
		}
		password_text[i] = '\0';
		display_text = password_text;
	} else if (data->text_length > 0) {
		display_text = data->text;
	} else if (data->placeholder[0] && !self->focused) {
		display_text = data->placeholder;
	} else {
		display_text = "";
	}

	// Draw text
	text_x = screen_x + 5;
	text_y = screen_y + self->height / 2 - 4;

	if (display_text[0]) {
		unsigned short color = (display_text == data->placeholder) ? data->placeholder_color : data->text_color;
		render_text(text_x, text_y, color, RENDER_TEXT_LEFT | RENDER_TEXT_SMALL, display_text);
	}

	// Draw cursor if focused
	if (self->focused && data->show_cursor && !data->readonly) {
		// Calculate cursor X position
		char temp[256];
		int cursor_x;

		if (data->cursor_pos > 0) {
			strncpy(temp, display_text, data->cursor_pos);
			temp[data->cursor_pos] = '\0';
			cursor_x = text_x + render_text_length(RENDER_TEXT_SMALL, temp);
		} else {
			cursor_x = text_x;
		}

		// Draw cursor line
		render_line(cursor_x, screen_y + 3, cursor_x, screen_y + self->height - 3, data->cursor_color);
	}
}

static int textinput_on_key_down(Widget *self, int key)
{
	TextInputData *data;

	if (!self) {
		return 0;
	}

	data = (TextInputData *)self->user_data;
	if (!data || data->readonly) {
		return 0;
	}

	switch (key) {
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		// Submit
		if (data->on_submit) {
			data->on_submit(self, data->text, data->callback_param);
		}
		return 1;

	case SDLK_BACKSPACE:
		// Delete character before cursor
		if (data->cursor_pos > 0) {
			memmove(&data->text[data->cursor_pos - 1], &data->text[data->cursor_pos],
			    data->text_length - data->cursor_pos + 1);
			data->cursor_pos--;
			data->text_length--;

			if (data->on_change) {
				data->on_change(self, data->callback_param);
			}

			widget_mark_dirty(self);
		}
		return 1;

	case SDLK_DELETE:
		// Delete character at cursor
		if (data->cursor_pos < data->text_length) {
			memmove(
			    &data->text[data->cursor_pos], &data->text[data->cursor_pos + 1], data->text_length - data->cursor_pos);
			data->text_length--;

			if (data->on_change) {
				data->on_change(self, data->callback_param);
			}

			widget_mark_dirty(self);
		}
		return 1;

	case SDLK_LEFT:
		// Move cursor left
		if (data->cursor_pos > 0) {
			data->cursor_pos--;
			widget_mark_dirty(self);
		}
		return 1;

	case SDLK_RIGHT:
		// Move cursor right
		if (data->cursor_pos < data->text_length) {
			data->cursor_pos++;
			widget_mark_dirty(self);
		}
		return 1;

	case SDLK_HOME:
		// Move to start
		data->cursor_pos = 0;
		widget_mark_dirty(self);
		return 1;

	case SDLK_END:
		// Move to end
		data->cursor_pos = data->text_length;
		widget_mark_dirty(self);
		return 1;

	default:
		break;
	}

	return 0;
}

static int textinput_on_text_input(Widget *self, int character)
{
	TextInputData *data;

	if (!self) {
		return 0;
	}

	data = (TextInputData *)self->user_data;
	if (!data || data->readonly) {
		return 0;
	}

	// Check if printable
	if (character < 32 || character > 126) {
		return 0;
	}

	// Check max length
	if (data->max_length > 0 && data->text_length >= data->max_length) {
		return 1; // Handled but rejected
	}

	// Ensure capacity
	if (data->text_length + 2 > data->text_capacity) {
		int new_capacity = data->text_capacity * 2;
		char *new_text = xrealloc(data->text, new_capacity, MEM_GUI);
		if (!new_text) {
			return 0;
		}
		data->text = new_text;
		data->text_capacity = new_capacity;
	}

	// Insert character at cursor
	memmove(&data->text[data->cursor_pos + 1], &data->text[data->cursor_pos], data->text_length - data->cursor_pos + 1);
	data->text[data->cursor_pos] = (char)character;
	data->cursor_pos++;
	data->text_length++;

	// Call change callback
	if (data->on_change) {
		data->on_change(self, data->callback_param);
	}

	widget_mark_dirty(self);

	return 1;
}

static void textinput_on_focus_gain(Widget *self)
{
	TextInputData *data;

	if (!self) {
		return;
	}

	data = (TextInputData *)self->user_data;
	if (!data) {
		return;
	}

	// Show cursor
	data->show_cursor = 1;
	data->cursor_blink_time = SDL_GetTicks();
	widget_mark_dirty(self);
}

static void textinput_on_focus_lost(Widget *self)
{
	TextInputData *data;

	if (!self) {
		return;
	}

	data = (TextInputData *)self->user_data;
	if (!data) {
		return;
	}

	// Hide cursor
	data->show_cursor = 0;
	widget_mark_dirty(self);
}

static void textinput_update(Widget *self, int dt)
{
	TextInputData *data;
	unsigned int now;

	if (!self || !self->focused) {
		return;
	}

	data = (TextInputData *)self->user_data;
	if (!data) {
		return;
	}

	// Blink cursor
	now = SDL_GetTicks();
	if (now - data->cursor_blink_time > CURSOR_BLINK_RATE) {
		data->show_cursor = !data->show_cursor;
		data->cursor_blink_time = now;
		widget_mark_dirty(self);
	}
}

static void textinput_on_destroy(Widget *self)
{
	TextInputData *data;

	if (!self) {
		return;
	}

	data = (TextInputData *)self->user_data;
	if (data && data->text) {
		xfree(data->text);
		data->text = NULL;
	}
}
