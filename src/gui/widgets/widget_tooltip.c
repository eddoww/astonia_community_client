/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Tooltip Widget Implementation
 */

#include <string.h>
#include <SDL2/SDL.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_tooltip.h"
#include "../../game/game.h"

// Forward declarations
static void tooltip_render(Widget *self);
static void tooltip_update(Widget *self, int dt);
static void tooltip_on_destroy(Widget *self);

// Helper functions
static void tooltip_calculate_size(Widget *tooltip);
static void tooltip_position_at_mouse(Widget *tooltip, int mouse_x, int mouse_y);
static void tooltip_position_at_widget(Widget *tooltip, Widget *target, int offset_x, int offset_y);

// Default settings
#define DEFAULT_SHOW_DELAY 500 // milliseconds
#define DEFAULT_MAX_WIDTH  300 // pixels
#define DEFAULT_PADDING    5 // pixels

Widget *widget_tooltip_create(int x, int y)
{
	Widget *widget;
	TooltipData *data;

	// Create base widget (initially small, will auto-size)
	widget = widget_create(WIDGET_TYPE_TOOLTIP, x, y, 100, 50);
	if (!widget) {
		return NULL;
	}

	// Allocate tooltip-specific data
	data = xmalloc(sizeof(TooltipData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(TooltipData));

	// Allocate text buffer
	data->text_capacity = 256;
	data->text = xmalloc(data->text_capacity, MEM_GUI);
	if (!data->text) {
		xfree(data);
		widget_destroy(widget);
		return NULL;
	}

	data->text[0] = '\0';

	// Initialize settings
	data->mode = TOOLTIP_FOLLOW_MOUSE;
	data->target_widget = NULL;
	data->offset_x = 10;
	data->offset_y = 10;
	data->max_width = DEFAULT_MAX_WIDTH;
	data->show_delay = DEFAULT_SHOW_DELAY;
	data->show_timer = 0;
	data->pending_show = 0;

	// Visual settings (dark medieval theme)
	data->bg_color = IRGB(4, 4, 6); // Very dark background
	data->border_color = IRGB(20, 20, 25); // Lighter border
	data->text_color = IRGB(28, 28, 31); // Light text
	data->padding = DEFAULT_PADDING;

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(TooltipData);

	// Set virtual functions
	widget->render = tooltip_render;
	widget->update = tooltip_update;
	widget->on_destroy = tooltip_on_destroy;

	// Tooltips start hidden
	widget->visible = 0;

	// Tooltips don't accept input
	widget->enabled = 0;

	// Tooltips should be on top
	widget->z_order = 10000;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "tooltip_%d", widget->id);

	return widget;
}

void widget_tooltip_set_text(Widget *tooltip, const char *text)
{
	TooltipData *data;
	int len;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP || !text) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
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

	// Recalculate size based on new text
	tooltip_calculate_size(tooltip);

	widget_mark_dirty(tooltip);
}

void widget_tooltip_set_delay(Widget *tooltip, unsigned int delay_ms)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	data->show_delay = delay_ms;
}

void widget_tooltip_set_max_width(Widget *tooltip, int max_width)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	data->max_width = max_width;

	// Recalculate size
	tooltip_calculate_size(tooltip);
}

void widget_tooltip_show_at_mouse(Widget *tooltip, int mouse_x, int mouse_y)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	// Set mode
	data->mode = TOOLTIP_FOLLOW_MOUSE;
	data->target_widget = NULL;

	// Position tooltip
	tooltip_position_at_mouse(tooltip, mouse_x, mouse_y);

	// Start show timer
	data->pending_show = 1;
	data->show_timer = SDL_GetTicks();
}

void widget_tooltip_show_at_widget(Widget *tooltip, Widget *target, int offset_x, int offset_y)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP || !target) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	// Set mode
	data->mode = TOOLTIP_ANCHOR_WIDGET;
	data->target_widget = target;
	data->offset_x = offset_x;
	data->offset_y = offset_y;

	// Position tooltip
	tooltip_position_at_widget(tooltip, target, offset_x, offset_y);

	// Start show timer
	data->pending_show = 1;
	data->show_timer = SDL_GetTicks();
}

void widget_tooltip_update_position(Widget *tooltip, int mouse_x, int mouse_y)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	// Only update if following mouse
	if (data->mode == TOOLTIP_FOLLOW_MOUSE) {
		tooltip_position_at_mouse(tooltip, mouse_x, mouse_y);
	}
}

void widget_tooltip_hide(Widget *tooltip)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	widget_set_visible(tooltip, 0);
	data->pending_show = 0;
}

void widget_tooltip_cancel(Widget *tooltip)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	data->pending_show = 0;
}

void widget_tooltip_set_colors(Widget *tooltip, unsigned short bg, unsigned short border, unsigned short text)
{
	TooltipData *data;

	if (!tooltip || tooltip->type != WIDGET_TYPE_TOOLTIP) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	data->bg_color = bg;
	data->border_color = border;
	data->text_color = text;
	widget_mark_dirty(tooltip);
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void tooltip_render(Widget *self)
{
	TooltipData *data;
	int screen_x, screen_y;
	char *line_start;
	char *line_end;
	int text_y;
	char line_buffer[512];

	if (!self) {
		return;
	}

	data = (TooltipData *)self->user_data;
	if (!data || !data->text[0]) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Draw background
	render_rect(screen_x + 1, screen_y + 1, screen_x + self->width - 1, screen_y + self->height - 1, data->bg_color);

	// Draw border
	render_line(screen_x, screen_y, screen_x + self->width, screen_y, data->border_color);
	render_line(screen_x, screen_y + self->height, screen_x + self->width, screen_y + self->height, data->border_color);
	render_line(screen_x, screen_y, screen_x, screen_y + self->height, data->border_color);
	render_line(screen_x + self->width, screen_y, screen_x + self->width, screen_y + self->height, data->border_color);

	// Draw text (multi-line support)
	text_y = screen_y + data->padding;
	line_start = data->text;

	while (*line_start) {
		// Find end of line (newline or end of string)
		line_end = line_start;
		while (*line_end && *line_end != '\n') {
			line_end++;
		}

		// Copy line to buffer
		int line_len = line_end - line_start;
		if (line_len > 0) {
			if (line_len >= sizeof(line_buffer)) {
				line_len = sizeof(line_buffer) - 1;
			}
			strncpy(line_buffer, line_start, line_len);
			line_buffer[line_len] = '\0';

			// Render line
			render_text(
			    screen_x + data->padding, text_y, data->text_color, RENDER_TEXT_LEFT | RENDER_TEXT_SMALL, line_buffer);

			text_y += 10; // Line height for small text
		}

		// Move to next line
		if (*line_end == '\n') {
			line_start = line_end + 1;
		} else {
			break;
		}
	}
}

static void tooltip_update(Widget *self, int dt)
{
	TooltipData *data;
	unsigned int now;

	if (!self) {
		return;
	}

	data = (TooltipData *)self->user_data;
	if (!data) {
		return;
	}

	// Handle delayed show
	if (data->pending_show && !self->visible) {
		now = SDL_GetTicks();
		if (now - data->show_timer >= data->show_delay) {
			// Show tooltip
			widget_set_visible(self, 1);
			widget_bring_to_front(self);
			data->pending_show = 0;
		}
	}

	// Update position if anchored to widget
	if (self->visible && data->mode == TOOLTIP_ANCHOR_WIDGET && data->target_widget) {
		tooltip_position_at_widget(self, data->target_widget, data->offset_x, data->offset_y);
	}
}

static void tooltip_on_destroy(Widget *self)
{
	TooltipData *data;

	if (!self) {
		return;
	}

	data = (TooltipData *)self->user_data;
	if (data && data->text) {
		xfree(data->text);
		data->text = NULL;
	}
}

// =============================================================================
// Helper Functions
// =============================================================================

static void tooltip_calculate_size(Widget *tooltip)
{
	TooltipData *data;
	char *line_start;
	char *line_end;
	int max_line_width;
	int line_count;
	char line_buffer[512];
	int line_width;

	if (!tooltip) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data || !data->text[0]) {
		// Empty tooltip
		widget_set_size(tooltip, data->padding * 2 + 50, data->padding * 2 + 10);
		return;
	}

	// Calculate width and height based on text
	max_line_width = 0;
	line_count = 0;
	line_start = data->text;

	while (*line_start) {
		// Find end of line
		line_end = line_start;
		while (*line_end && *line_end != '\n') {
			line_end++;
		}

		// Get line width
		int line_len = line_end - line_start;
		if (line_len > 0) {
			if (line_len >= sizeof(line_buffer)) {
				line_len = sizeof(line_buffer) - 1;
			}
			strncpy(line_buffer, line_start, line_len);
			line_buffer[line_len] = '\0';

			line_width = render_text_length(RENDER_TEXT_SMALL, line_buffer);

			if (line_width > max_line_width) {
				max_line_width = line_width;
			}

			line_count++;
		}

		// Move to next line
		if (*line_end == '\n') {
			line_start = line_end + 1;
		} else {
			break;
		}
	}

	// Clamp width to max_width
	if (max_line_width > data->max_width) {
		max_line_width = data->max_width;
	}

	// Set size
	int width = max_line_width + data->padding * 2;
	int height = line_count * 10 + data->padding * 2; // 10 pixels per line for small text

	widget_set_size(tooltip, width, height);
}

static void tooltip_position_at_mouse(Widget *tooltip, int mouse_x, int mouse_y)
{
	TooltipData *data;
	int new_x, new_y;

	if (!tooltip) {
		return;
	}

	data = (TooltipData *)tooltip->user_data;
	if (!data) {
		return;
	}

	// Position tooltip offset from mouse
	new_x = mouse_x + data->offset_x;
	new_y = mouse_y + data->offset_y;

	// TODO: Clamp to screen bounds (need screen dimensions from widget manager)
	// For now, just set position
	widget_set_position(tooltip, new_x, new_y);
}

static void tooltip_position_at_widget(Widget *tooltip, Widget *target, int offset_x, int offset_y)
{
	int target_screen_x, target_screen_y;
	int new_x, new_y;

	if (!tooltip || !target) {
		return;
	}

	// Get target screen position
	widget_get_screen_position(target, &target_screen_x, &target_screen_y);

	// Position tooltip below and to the right of target
	new_x = target_screen_x + offset_x;
	new_y = target_screen_y + target->height + offset_y;

	// TODO: Clamp to screen bounds
	widget_set_position(tooltip, new_x, new_y);
}
