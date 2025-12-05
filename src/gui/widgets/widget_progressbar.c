/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * ProgressBar Widget Implementation
 */

#include <string.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_progressbar.h"
#include "../../game/game.h"

// Forward declarations
static void progressbar_render(Widget *self);
static void progressbar_on_destroy(Widget *self);

Widget *widget_progressbar_create(int x, int y, int width, int height, ProgressBarOrientation orientation)
{
	Widget *widget;
	ProgressBarData *data;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_PROGRESSBAR, x, y, width, height);
	if (!widget) {
		return NULL;
	}

	// Allocate progressbar-specific data
	data = xmalloc(sizeof(ProgressBarData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(ProgressBarData));

	// Initialize data
	data->value = 0.0f;
	data->max = 100.0f;
	data->orientation = orientation;
	data->fill_color = IRGB(15, 25, 15); // Green fill
	data->bg_color = IRGB(5, 5, 7); // Dark background
	data->border_color = IRGB(12, 12, 14); // Gray border
	data->show_border = 1;
	data->show_text = 0;
	data->text[0] = '\0';
	data->text_color = IRGB(25, 25, 28);

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(ProgressBarData);

	// Set virtual functions
	widget->render = progressbar_render;
	widget->on_destroy = progressbar_on_destroy;

	// Progress bars don't accept input by default
	widget->enabled = 0;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "progressbar_%d", widget->id);

	return widget;
}

void widget_progressbar_set_value(Widget *bar, float value)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data) {
		return;
	}

	// Clamp value
	if (value < 0.0f) {
		value = 0.0f;
	}
	if (value > data->max) {
		value = data->max;
	}

	if (data->value != value) {
		data->value = value;
		widget_mark_dirty(bar);
	}
}

void widget_progressbar_set_max(Widget *bar, float max)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data) {
		return;
	}

	if (max < 0.0f) {
		max = 0.0f;
	}

	if (data->max != max) {
		data->max = max;

		// Clamp current value to new max
		if (data->value > data->max) {
			data->value = data->max;
		}

		widget_mark_dirty(bar);
	}
}

void widget_progressbar_set_range(Widget *bar, float value, float max)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data) {
		return;
	}

	// Set max first
	if (max < 0.0f) {
		max = 0.0f;
	}
	data->max = max;

	// Then set value (will be clamped)
	if (value < 0.0f) {
		value = 0.0f;
	}
	if (value > data->max) {
		value = data->max;
	}
	data->value = value;

	widget_mark_dirty(bar);
}

void widget_progressbar_set_fill_color(Widget *bar, unsigned short color)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data) {
		return;
	}

	data->fill_color = color;
	widget_mark_dirty(bar);
}

void widget_progressbar_set_bg_color(Widget *bar, unsigned short color)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data) {
		return;
	}

	data->bg_color = color;
	widget_mark_dirty(bar);
}

void widget_progressbar_set_border(Widget *bar, unsigned short color, int show)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data) {
		return;
	}

	data->border_color = color;
	data->show_border = show;
	widget_mark_dirty(bar);
}

void widget_progressbar_set_text(Widget *bar, const char *text, unsigned short color)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data) {
		return;
	}

	if (text) {
		strncpy(data->text, text, sizeof(data->text) - 1);
		data->text[sizeof(data->text) - 1] = '\0';
		data->show_text = 1;
		data->text_color = color;
	} else {
		data->text[0] = '\0';
		data->show_text = 0;
	}

	widget_mark_dirty(bar);
}

float widget_progressbar_get_percentage(Widget *bar)
{
	ProgressBarData *data;

	if (!bar || bar->type != WIDGET_TYPE_PROGRESSBAR) {
		return 0.0f;
	}

	data = (ProgressBarData *)bar->user_data;
	if (!data || data->max == 0.0f) {
		return 0.0f;
	}

	return data->value / data->max;
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void progressbar_render(Widget *self)
{
	ProgressBarData *data;
	int screen_x, screen_y;
	float percentage;
	int fill_width, fill_height;

	if (!self) {
		return;
	}

	data = (ProgressBarData *)self->user_data;
	if (!data) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Calculate percentage
	percentage = (data->max > 0.0f) ? (data->value / data->max) : 0.0f;
	if (percentage < 0.0f) {
		percentage = 0.0f;
	}
	if (percentage > 1.0f) {
		percentage = 1.0f;
	}

	// Calculate fill dimensions
	if (data->orientation == PROGRESSBAR_HORIZONTAL) {
		fill_width = (int)(self->width * percentage);
		fill_height = self->height;
	} else {
		fill_width = self->width;
		fill_height = (int)(self->height * percentage);
	}

	// Draw background
	render_rect(screen_x, screen_y, screen_x + self->width, screen_y + self->height, data->bg_color);

	// Draw fill
	if (fill_width > 0 && fill_height > 0) {
		if (data->orientation == PROGRESSBAR_HORIZONTAL) {
			render_rect(screen_x, screen_y, screen_x + fill_width, screen_y + fill_height, data->fill_color);
		} else {
			// Vertical fills from bottom
			int fill_y = screen_y + self->height - fill_height;
			render_rect(screen_x, fill_y, screen_x + fill_width, screen_y + self->height, data->fill_color);
		}
	}

	// Draw border
	if (data->show_border) {
		render_line(screen_x, screen_y, screen_x + self->width, screen_y, data->border_color);
		render_line(
		    screen_x, screen_y + self->height, screen_x + self->width, screen_y + self->height, data->border_color);
		render_line(screen_x, screen_y, screen_x, screen_y + self->height, data->border_color);
		render_line(
		    screen_x + self->width, screen_y, screen_x + self->width, screen_y + self->height, data->border_color);
	}

	// Draw text overlay
	if (data->show_text && data->text[0]) {
		int text_x = screen_x + self->width / 2;
		int text_y = screen_y + self->height / 2 - 4;
		render_text(text_x, text_y, data->text_color, RENDER_ALIGN_CENTER | RENDER_TEXT_SMALL, data->text);
	}
}

static void progressbar_on_destroy(Widget *self)
{
	// User data will be freed automatically
}
