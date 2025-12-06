/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Container Widget - Layout container for other widgets
 */

#ifndef WIDGET_CONTAINER_H
#define WIDGET_CONTAINER_H

#include "../widget.h"

// Layout modes for container
typedef enum {
	LAYOUT_NONE = 0, // Manual positioning (default)
	LAYOUT_VERTICAL, // Stack children vertically
	LAYOUT_HORIZONTAL, // Stack children horizontally
	LAYOUT_GRID // Grid layout with fixed columns
} LayoutMode;

// Container-specific data
typedef struct {
	LayoutMode layout_mode;
	int padding; // Padding around children
	int spacing; // Spacing between children
	int grid_columns; // For LAYOUT_GRID mode

	// Scrolling support
	int scrollable;
	int scroll_offset_x;
	int scroll_offset_y;
	int content_width;
	int content_height;

	// Background
	unsigned short bg_color;
	int draw_background;
} ContainerData;

/**
 * Create a container widget
 *
 * @param x X position
 * @param y Y position
 * @param width Width
 * @param height Height
 * @return Container widget
 */
DLL_EXPORT Widget *widget_container_create(int x, int y, int width, int height);

/**
 * Set container layout mode
 *
 * @param container Container widget
 * @param mode Layout mode
 */
DLL_EXPORT void widget_container_set_layout(Widget *container, LayoutMode mode);

/**
 * Set container padding and spacing
 *
 * @param container Container widget
 * @param padding Padding around children
 * @param spacing Spacing between children
 */
DLL_EXPORT void widget_container_set_spacing(Widget *container, int padding, int spacing);

/**
 * Set grid columns (for LAYOUT_GRID mode)
 *
 * @param container Container widget
 * @param columns Number of columns
 */
DLL_EXPORT void widget_container_set_grid_columns(Widget *container, int columns);

/**
 * Enable/disable scrolling
 *
 * @param container Container widget
 * @param scrollable 1 to enable, 0 to disable
 */
DLL_EXPORT void widget_container_set_scrollable(Widget *container, int scrollable);

/**
 * Set background color
 *
 * @param container Container widget
 * @param color Background color
 * @param draw 1 to draw background, 0 to skip
 */
DLL_EXPORT void widget_container_set_background(Widget *container, unsigned short color, int draw);

/**
 * Update container layout
 * Recalculates child positions based on layout mode
 *
 * @param container Container widget
 */
void widget_container_update_layout(Widget *container);

#endif // WIDGET_CONTAINER_H
