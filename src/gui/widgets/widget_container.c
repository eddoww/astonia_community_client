/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Container Widget Implementation
 */

#include <string.h>
#include <stdio.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_container.h"
#include "../../game/game.h"

// Forward declarations
static void container_render(Widget *self);
static int container_on_mouse_wheel(Widget *self, int x, int y, int delta);
static void container_on_resize(Widget *self, int new_width, int new_height);
static void container_on_destroy(Widget *self);

Widget *widget_container_create(int x, int y, int width, int height)
{
	Widget *widget;
	ContainerData *data;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_CONTAINER, x, y, width, height);
	if (!widget) {
		return NULL;
	}

	// Allocate container-specific data
	data = xmalloc(sizeof(ContainerData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(ContainerData));

	// Initialize data
	data->layout_mode = LAYOUT_NONE;
	data->padding = 5;
	data->spacing = 5;
	data->grid_columns = 4;
	data->scrollable = 0;
	data->scroll_offset_x = 0;
	data->scroll_offset_y = 0;
	data->content_width = width;
	data->content_height = height;
	data->bg_color = IRGB(5, 5, 7); // Dark background
	data->draw_background = 0;

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(ContainerData);

	// Set virtual functions
	widget->render = container_render;
	widget->on_mouse_wheel = container_on_mouse_wheel;
	widget->on_resize = container_on_resize;
	widget->on_destroy = container_on_destroy;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "container_%d", widget->id);

	return widget;
}

void widget_container_set_layout(Widget *container, LayoutMode mode)
{
	ContainerData *data;

	if (!container || container->type != WIDGET_TYPE_CONTAINER) {
		return;
	}

	data = (ContainerData *)container->user_data;
	if (!data) {
		return;
	}

	if (data->layout_mode != mode) {
		data->layout_mode = mode;
		widget_container_update_layout(container);
		widget_mark_dirty(container);
	}
}

void widget_container_set_spacing(Widget *container, int padding, int spacing)
{
	ContainerData *data;

	if (!container || container->type != WIDGET_TYPE_CONTAINER) {
		return;
	}

	data = (ContainerData *)container->user_data;
	if (!data) {
		return;
	}

	data->padding = padding;
	data->spacing = spacing;
	widget_container_update_layout(container);
	widget_mark_dirty(container);
}

void widget_container_set_grid_columns(Widget *container, int columns)
{
	ContainerData *data;

	if (!container || container->type != WIDGET_TYPE_CONTAINER) {
		return;
	}

	data = (ContainerData *)container->user_data;
	if (!data) {
		return;
	}

	data->grid_columns = columns;
	if (data->layout_mode == LAYOUT_GRID) {
		widget_container_update_layout(container);
		widget_mark_dirty(container);
	}
}

void widget_container_set_scrollable(Widget *container, int scrollable)
{
	ContainerData *data;

	if (!container || container->type != WIDGET_TYPE_CONTAINER) {
		return;
	}

	data = (ContainerData *)container->user_data;
	if (!data) {
		return;
	}

	data->scrollable = scrollable;
}

void widget_container_set_background(Widget *container, unsigned short color, int draw)
{
	ContainerData *data;

	if (!container || container->type != WIDGET_TYPE_CONTAINER) {
		return;
	}

	data = (ContainerData *)container->user_data;
	if (!data) {
		return;
	}

	data->bg_color = color;
	data->draw_background = draw;
	widget_mark_dirty(container);
}

void widget_container_update_layout(Widget *container)
{
	ContainerData *data;
	Widget *child;
	int x, y;
	int row, col;

	if (!container || container->type != WIDGET_TYPE_CONTAINER) {
		return;
	}

	data = (ContainerData *)container->user_data;
	if (!data || data->layout_mode == LAYOUT_NONE) {
		return;
	}

	x = data->padding;
	y = data->padding;
	row = 0;
	col = 0;

	// Layout children based on mode
	for (child = container->first_child; child; child = child->next_sibling) {
		if (!child->visible) {
			continue;
		}

		switch (data->layout_mode) {
		case LAYOUT_VERTICAL:
			widget_set_position(child, x, y);
			y += child->height + data->spacing;
			break;

		case LAYOUT_HORIZONTAL:
			widget_set_position(child, x, y);
			x += child->width + data->spacing;
			break;

		case LAYOUT_GRID:
			widget_set_position(child, x, y);
			col++;
			if (col >= data->grid_columns) {
				col = 0;
				row++;
				x = data->padding;
				y += child->height + data->spacing;
			} else {
				x += child->width + data->spacing;
			}
			break;

		default:
			break;
		}
	}

	// Update content size for scrolling
	if (data->layout_mode == LAYOUT_VERTICAL) {
		data->content_width = container->width;
		data->content_height = y + data->padding;
	} else if (data->layout_mode == LAYOUT_HORIZONTAL) {
		data->content_width = x + data->padding;
		data->content_height = container->height;
	} else if (data->layout_mode == LAYOUT_GRID) {
		data->content_width = container->width;
		data->content_height = y + data->padding;
	}
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void container_render(Widget *self)
{
	ContainerData *data;
	int screen_x, screen_y;
	Widget *child;

	if (!self) {
		return;
	}

	data = (ContainerData *)self->user_data;
	if (!data) {
		return;
	}

	// Get screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Render window chrome first (title bar, borders)
	if (self->has_titlebar && !self->minimized) {
		widget_render_chrome(self);
	}

	// Draw background if enabled
	if (data->draw_background) {
		render_rect(screen_x, screen_y, screen_x + self->width, screen_y + self->height, data->bg_color);
	}

	// Set up clipping for children
	render_push_clip();
	render_more_clip(screen_x, screen_y, screen_x + self->width, screen_y + self->height);

	// Apply scroll offset for rendering children
	if (data->scrollable) {
		// Children will render at their positions minus scroll offset
		// This is handled by their screen position calculation
		// For now, just render children normally
		// TODO: Implement scroll offset transform
	}

	// Render children (they'll call their own render functions)
	for (child = self->first_child; child; child = child->next_sibling) {
		if (child->visible) {
			if (child->render) {
				child->render(child);
			}

			// Render child's window chrome if it has it
			if (child->has_titlebar && !child->minimized) {
				widget_render_chrome(child);
			}
		}
	}

	// Restore clipping
	render_pop_clip();

	// Draw scrollbar if scrollable and content exceeds size
	if (data->scrollable && data->content_height > self->height) {
		int scrollbar_x = screen_x + self->width - 10;
		int scrollbar_y = screen_y;
		int scrollbar_height = self->height;

		// Scrollbar background
		render_rect(scrollbar_x, scrollbar_y, scrollbar_x + 10, scrollbar_y + scrollbar_height, IRGB(8, 8, 10));

		// Scrollbar thumb
		int thumb_height = (self->height * scrollbar_height) / data->content_height;
		int thumb_y = scrollbar_y + (data->scroll_offset_y * scrollbar_height) / data->content_height;

		if (thumb_height < 20) {
			thumb_height = 20;
		}

		render_rect(scrollbar_x + 2, thumb_y, scrollbar_x + 8, thumb_y + thumb_height, IRGB(15, 15, 18));
	}
}

static int container_on_mouse_wheel(Widget *self, int x, int y, int delta)
{
	ContainerData *data;

	if (!self) {
		return 0;
	}

	data = (ContainerData *)self->user_data;
	if (!data || !data->scrollable) {
		return 0;
	}

	// Scroll by delta * 20 pixels
	data->scroll_offset_y -= delta * 20;

	// Clamp scroll offset
	int max_scroll = data->content_height - self->height;
	if (max_scroll < 0) {
		max_scroll = 0;
	}

	if (data->scroll_offset_y < 0) {
		data->scroll_offset_y = 0;
	}
	if (data->scroll_offset_y > max_scroll) {
		data->scroll_offset_y = max_scroll;
	}

	widget_mark_dirty(self);

	return 1; // Event handled
}

static void container_on_resize(Widget *self, int new_width, int new_height)
{
	// Recalculate layout when container is resized
	widget_container_update_layout(self);
}

static void container_on_destroy(Widget *self)
{
	// User data will be freed automatically by widget_destroy
	// Nothing special to do here
}
