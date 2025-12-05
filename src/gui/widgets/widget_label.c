/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Label Widget Implementation
 */

#include <string.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_label.h"
#include "../../game/game.h"

// Forward declarations
static void label_render(Widget *self);
static void label_on_destroy(Widget *self);

Widget *widget_label_create(int x, int y, int width, int height, const char *text)
{
	Widget *widget;
	LabelData *data;
	int text_len;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_LABEL, x, y, width, height);
	if (!widget) {
		return NULL;
	}

	// Allocate label-specific data
	data = xmalloc(sizeof(LabelData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(LabelData));

	// Initialize data
	text_len = text ? strlen(text) + 1 : 128;
	if (text_len < 128) {
		text_len = 128;
	}

	data->text = xmalloc(text_len, MEM_GUI);
	if (!data->text) {
		xfree(data);
		widget_destroy(widget);
		return NULL;
	}

	data->text_capacity = text_len;
	if (text) {
		strncpy(data->text, text, text_len - 1);
		data->text[text_len - 1] = '\0';
	} else {
		data->text[0] = '\0';
	}

	data->color = IRGB(25, 25, 28); // Light gray text
	data->alignment = LABEL_ALIGN_LEFT;
	data->word_wrap = 0;
	data->small_font = 0;

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(LabelData);

	// Set virtual functions
	widget->render = label_render;
	widget->on_destroy = label_on_destroy;

	// Labels don't accept input by default
	widget->enabled = 0;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "label_%d", widget->id);

	return widget;
}

void widget_label_set_text(Widget *label, const char *text)
{
	LabelData *data;
	int text_len;

	if (!label || label->type != WIDGET_TYPE_LABEL || !text) {
		return;
	}

	data = (LabelData *)label->user_data;
	if (!data) {
		return;
	}

	text_len = strlen(text) + 1;

	// Reallocate if needed
	if (text_len > data->text_capacity) {
		char *new_text = xrealloc(data->text, text_len, MEM_GUI);
		if (!new_text) {
			fail("widget_label_set_text: failed to reallocate text buffer");
			return;
		}
		data->text = new_text;
		data->text_capacity = text_len;
	}

	strcpy(data->text, text);
	widget_mark_dirty(label);
}

void widget_label_set_color(Widget *label, unsigned short color)
{
	LabelData *data;

	if (!label || label->type != WIDGET_TYPE_LABEL) {
		return;
	}

	data = (LabelData *)label->user_data;
	if (!data) {
		return;
	}

	data->color = color;
	widget_mark_dirty(label);
}

void widget_label_set_alignment(Widget *label, LabelAlignment alignment)
{
	LabelData *data;

	if (!label || label->type != WIDGET_TYPE_LABEL) {
		return;
	}

	data = (LabelData *)label->user_data;
	if (!data) {
		return;
	}

	data->alignment = alignment;
	widget_mark_dirty(label);
}

void widget_label_set_word_wrap(Widget *label, int wrap)
{
	LabelData *data;

	if (!label || label->type != WIDGET_TYPE_LABEL) {
		return;
	}

	data = (LabelData *)label->user_data;
	if (!data) {
		return;
	}

	data->word_wrap = wrap;
	widget_mark_dirty(label);
}

void widget_label_set_small_font(Widget *label, int small)
{
	LabelData *data;

	if (!label || label->type != WIDGET_TYPE_LABEL) {
		return;
	}

	data = (LabelData *)label->user_data;
	if (!data) {
		return;
	}

	data->small_font = small;
	widget_mark_dirty(label);
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void label_render(Widget *self)
{
	LabelData *data;
	int screen_x, screen_y;
	int flags;

	if (!self) {
		return;
	}

	data = (LabelData *)self->user_data;
	if (!data || !data->text[0]) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Build render flags
	flags = data->alignment;
	if (data->small_font) {
		flags |= RENDER_TEXT_SMALL;
	}

	// Render text
	if (data->word_wrap) {
		// Word-wrapped text
		render_text_break(screen_x, screen_y, screen_x + self->width, data->color, flags, data->text);
	} else {
		// Single line text
		// Adjust X position based on alignment
		int text_x = screen_x;
		if (data->alignment == LABEL_ALIGN_CENTER) {
			text_x = screen_x + self->width / 2;
		} else if (data->alignment == LABEL_ALIGN_RIGHT) {
			text_x = screen_x + self->width;
		}

		render_text(text_x, screen_y, data->color, flags, data->text);
	}
}

static void label_on_destroy(Widget *self)
{
	LabelData *data;

	if (!self) {
		return;
	}

	data = (LabelData *)self->user_data;
	if (data && data->text) {
		xfree(data->text);
		data->text = NULL;
	}
}
