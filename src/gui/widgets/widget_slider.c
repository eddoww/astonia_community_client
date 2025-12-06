/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Slider Widget Implementation
 */

#include <string.h>
#include <stdio.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_slider.h"
#include "../../game/game.h"

// Forward declarations
static void slider_render(Widget *self);
static int slider_on_mouse_down(Widget *self, int x, int y, int button);
static int slider_on_mouse_up(Widget *self, int x, int y, int button);
static int slider_on_mouse_move(Widget *self, int x, int y);
static void slider_on_mouse_enter(Widget *self);
static void slider_on_mouse_leave(Widget *self);
static void slider_on_destroy(Widget *self);

// Helper to calculate handle position from value
static int get_handle_position(Widget *slider, SliderData *data)
{
	float percentage = (data->max > 0.0f) ? (data->value / data->max) : 0.0f;
	if (percentage < 0.0f) {
		percentage = 0.0f;
	}
	if (percentage > 1.0f) {
		percentage = 1.0f;
	}

	if (data->orientation == SLIDER_HORIZONTAL) {
		int track_width = slider->width - data->handle_size;
		return (int)(percentage * track_width);
	} else {
		int track_height = slider->height - data->handle_size;
		// Vertical slider: 0 at bottom, max at top
		return (int)((1.0f - percentage) * track_height);
	}
}

// Helper to calculate value from position
static float get_value_from_position(Widget *slider, SliderData *data, int pos)
{
	float percentage;

	if (data->orientation == SLIDER_HORIZONTAL) {
		int track_width = slider->width - data->handle_size;
		if (track_width <= 0) {
			return 0.0f;
		}
		percentage = (float)(pos - data->handle_size / 2) / (float)track_width;
	} else {
		int track_height = slider->height - data->handle_size;
		if (track_height <= 0) {
			return 0.0f;
		}
		// Vertical slider: 0 at bottom, max at top
		percentage = 1.0f - (float)(pos - data->handle_size / 2) / (float)track_height;
	}

	// Clamp percentage
	if (percentage < 0.0f) {
		percentage = 0.0f;
	}
	if (percentage > 1.0f) {
		percentage = 1.0f;
	}

	float value = percentage * data->max;

	// Apply step if set
	if (data->step > 0.0f) {
		value = ((int)(value / data->step + 0.5f)) * data->step;
	}

	return value;
}

Widget *widget_slider_create(int x, int y, int width, int height, SliderOrientation orientation)
{
	Widget *widget;
	SliderData *data;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_SLIDER, x, y, width, height);
	if (!widget) {
		return NULL;
	}

	// Allocate slider-specific data
	data = xmalloc(sizeof(SliderData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(SliderData));

	// Initialize data
	data->value = 0.0f;
	data->max = 100.0f;
	data->step = 0.0f; // Continuous by default
	data->orientation = orientation;

	// Dark medieval theme colors
	data->track_color = IRGB(5, 5, 7); // Dark background
	data->fill_color = IRGB(12, 18, 12); // Dark green fill
	data->handle_color = IRGB(15, 15, 18); // Gray handle
	data->handle_hover_color = IRGB(20, 20, 24);
	data->border_color = IRGB(12, 12, 14); // Gray border
	data->show_border = 1;
	data->handle_size = (orientation == SLIDER_HORIZONTAL) ? 12 : 12;

	data->dragging = 0;
	data->hover = 0;
	data->on_change = NULL;
	data->callback_param = NULL;

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(SliderData);

	// Set virtual functions
	widget->render = slider_render;
	widget->on_mouse_down = slider_on_mouse_down;
	widget->on_mouse_up = slider_on_mouse_up;
	widget->on_mouse_move = slider_on_mouse_move;
	widget->on_mouse_enter = slider_on_mouse_enter;
	widget->on_mouse_leave = slider_on_mouse_leave;
	widget->on_destroy = slider_on_destroy;

	// Sliders accept input
	widget->enabled = 1;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "slider_%d", widget->id);

	return widget;
}

void widget_slider_set_value(Widget *slider, float value)
{
	SliderData *data;

	if (!slider || slider->type != WIDGET_TYPE_SLIDER) {
		return;
	}

	data = (SliderData *)slider->user_data;
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

	// Apply step if set
	if (data->step > 0.0f) {
		value = ((int)(value / data->step + 0.5f)) * data->step;
	}

	if (data->value != value) {
		data->value = value;
		widget_mark_dirty(slider);
	}
}

float widget_slider_get_value(Widget *slider)
{
	SliderData *data;

	if (!slider || slider->type != WIDGET_TYPE_SLIDER) {
		return 0.0f;
	}

	data = (SliderData *)slider->user_data;
	if (!data) {
		return 0.0f;
	}

	return data->value;
}

void widget_slider_set_max(Widget *slider, float max)
{
	SliderData *data;

	if (!slider || slider->type != WIDGET_TYPE_SLIDER) {
		return;
	}

	data = (SliderData *)slider->user_data;
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

		widget_mark_dirty(slider);
	}
}

void widget_slider_set_step(Widget *slider, float step)
{
	SliderData *data;

	if (!slider || slider->type != WIDGET_TYPE_SLIDER) {
		return;
	}

	data = (SliderData *)slider->user_data;
	if (!data) {
		return;
	}

	if (step < 0.0f) {
		step = 0.0f;
	}

	data->step = step;
}

void widget_slider_set_callback(Widget *slider, SliderCallback callback, void *param)
{
	SliderData *data;

	if (!slider || slider->type != WIDGET_TYPE_SLIDER) {
		return;
	}

	data = (SliderData *)slider->user_data;
	if (!data) {
		return;
	}

	data->on_change = callback;
	data->callback_param = param;
}

void widget_slider_set_colors(Widget *slider, unsigned short track, unsigned short fill, unsigned short handle)
{
	SliderData *data;

	if (!slider || slider->type != WIDGET_TYPE_SLIDER) {
		return;
	}

	data = (SliderData *)slider->user_data;
	if (!data) {
		return;
	}

	data->track_color = track;
	data->fill_color = fill;
	data->handle_color = handle;
	widget_mark_dirty(slider);
}

float widget_slider_get_percentage(Widget *slider)
{
	SliderData *data;

	if (!slider || slider->type != WIDGET_TYPE_SLIDER) {
		return 0.0f;
	}

	data = (SliderData *)slider->user_data;
	if (!data || data->max == 0.0f) {
		return 0.0f;
	}

	return data->value / data->max;
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void slider_render(Widget *self)
{
	SliderData *data;
	int screen_x, screen_y;
	int handle_pos;
	unsigned short handle_color;

	if (!self) {
		return;
	}

	data = (SliderData *)self->user_data;
	if (!data) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Calculate handle position
	handle_pos = get_handle_position(self, data);

	// Draw track background
	render_rect(screen_x, screen_y, screen_x + self->width, screen_y + self->height, data->track_color);

	// Draw filled portion
	if (data->orientation == SLIDER_HORIZONTAL) {
		int fill_width = handle_pos + data->handle_size / 2;
		if (fill_width > 0) {
			render_rect(screen_x, screen_y, screen_x + fill_width, screen_y + self->height, data->fill_color);
		}
	} else {
		int fill_start = handle_pos + data->handle_size;
		if (fill_start < self->height) {
			render_rect(
			    screen_x, screen_y + fill_start, screen_x + self->width, screen_y + self->height, data->fill_color);
		}
	}

	// Draw handle
	handle_color = (data->hover || data->dragging) ? data->handle_hover_color : data->handle_color;
	if (data->orientation == SLIDER_HORIZONTAL) {
		render_rect(screen_x + handle_pos, screen_y, screen_x + handle_pos + data->handle_size, screen_y + self->height,
		    handle_color);
		// Handle border/highlight
		render_line(screen_x + handle_pos, screen_y, screen_x + handle_pos, screen_y + self->height, IRGB(20, 20, 24));
		render_line(screen_x + handle_pos + data->handle_size, screen_y, screen_x + handle_pos + data->handle_size,
		    screen_y + self->height, IRGB(8, 8, 10));
	} else {
		render_rect(screen_x, screen_y + handle_pos, screen_x + self->width, screen_y + handle_pos + data->handle_size,
		    handle_color);
		// Handle border/highlight
		render_line(screen_x, screen_y + handle_pos, screen_x + self->width, screen_y + handle_pos, IRGB(20, 20, 24));
		render_line(screen_x, screen_y + handle_pos + data->handle_size, screen_x + self->width,
		    screen_y + handle_pos + data->handle_size, IRGB(8, 8, 10));
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
}

static int slider_on_mouse_down(Widget *self, int x, int y, int button)
{
	SliderData *data;
	float new_value;

	if (!self || button != MOUSE_BUTTON_LEFT) {
		return 0;
	}

	data = (SliderData *)self->user_data;
	if (!data) {
		return 0;
	}

	// Start dragging
	data->dragging = 1;
	self->pressed = 1;

	// Calculate new value from click position
	if (data->orientation == SLIDER_HORIZONTAL) {
		new_value = get_value_from_position(self, data, x);
	} else {
		new_value = get_value_from_position(self, data, y);
	}

	// Update value if changed
	if (new_value != data->value) {
		data->value = new_value;
		widget_mark_dirty(self);

		// Call callback
		if (data->on_change) {
			data->on_change(self, data->value, data->callback_param);
		}
	}

	return 1;
}

static int slider_on_mouse_up(Widget *self, int x, int y, int button)
{
	SliderData *data;

	if (!self || button != MOUSE_BUTTON_LEFT) {
		return 0;
	}

	data = (SliderData *)self->user_data;
	if (!data) {
		return 0;
	}

	if (data->dragging) {
		data->dragging = 0;
		self->pressed = 0;
		widget_mark_dirty(self);
		return 1;
	}

	return 0;
}

static int slider_on_mouse_move(Widget *self, int x, int y)
{
	SliderData *data;
	float new_value;
	int handle_pos;
	int on_handle;

	if (!self) {
		return 0;
	}

	data = (SliderData *)self->user_data;
	if (!data) {
		return 0;
	}

	// Update dragging
	if (data->dragging) {
		if (data->orientation == SLIDER_HORIZONTAL) {
			new_value = get_value_from_position(self, data, x);
		} else {
			new_value = get_value_from_position(self, data, y);
		}

		// Clamp value
		if (new_value < 0.0f) {
			new_value = 0.0f;
		}
		if (new_value > data->max) {
			new_value = data->max;
		}

		if (new_value != data->value) {
			data->value = new_value;
			widget_mark_dirty(self);

			// Call callback
			if (data->on_change) {
				data->on_change(self, data->value, data->callback_param);
			}
		}
		return 1;
	}

	// Update hover state
	handle_pos = get_handle_position(self, data);
	if (data->orientation == SLIDER_HORIZONTAL) {
		on_handle = (x >= handle_pos && x <= handle_pos + data->handle_size);
	} else {
		on_handle = (y >= handle_pos && y <= handle_pos + data->handle_size);
	}

	if (on_handle != data->hover) {
		data->hover = on_handle;
		widget_mark_dirty(self);
	}

	return 0;
}

static void slider_on_mouse_enter(Widget *self)
{
	// Hover is handled per-handle in on_mouse_move
}

static void slider_on_mouse_leave(Widget *self)
{
	SliderData *data;

	if (!self) {
		return;
	}

	data = (SliderData *)self->user_data;
	if (!data) {
		return;
	}

	if (data->hover) {
		data->hover = 0;
		widget_mark_dirty(self);
	}
}

static void slider_on_destroy(Widget *self)
{
	// User data will be freed automatically
}
