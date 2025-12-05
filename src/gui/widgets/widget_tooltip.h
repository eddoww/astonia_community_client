/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Tooltip Widget - Hover information display
 */

#ifndef WIDGET_TOOLTIP_H
#define WIDGET_TOOLTIP_H

#include "../widget.h"

// Tooltip display modes
typedef enum {
	TOOLTIP_FOLLOW_MOUSE = 0, // Follow mouse cursor
	TOOLTIP_ANCHOR_WIDGET // Anchored to target widget
} TooltipMode;

// Tooltip-specific data
typedef struct {
	char *text; // Tooltip text (dynamically allocated)
	int text_capacity; // Allocated size of text buffer

	// Display settings
	TooltipMode mode; // How tooltip is positioned
	Widget *target_widget; // Target widget if anchored
	int offset_x, offset_y; // Offset from mouse/widget
	int max_width; // Maximum width before wrapping

	// Timing
	unsigned int show_delay; // Delay before showing (milliseconds)
	unsigned int show_timer; // Timer for delayed show
	int pending_show; // Waiting to show after delay

	// Visual
	unsigned short bg_color;
	unsigned short border_color;
	unsigned short text_color;
	int padding; // Padding around text
} TooltipData;

/**
 * Create a tooltip widget
 *
 * Tooltips are typically created hidden and shown on demand
 *
 * @param x Initial X position
 * @param y Initial Y position
 * @return Tooltip widget
 */
DLL_EXPORT Widget *widget_tooltip_create(int x, int y);

/**
 * Set tooltip text
 *
 * @param tooltip Tooltip widget
 * @param text Text to display (supports newlines for multi-line)
 */
DLL_EXPORT void widget_tooltip_set_text(Widget *tooltip, const char *text);

/**
 * Set tooltip display delay
 *
 * @param tooltip Tooltip widget
 * @param delay_ms Delay in milliseconds before showing (default 500)
 */
DLL_EXPORT void widget_tooltip_set_delay(Widget *tooltip, unsigned int delay_ms);

/**
 * Set tooltip maximum width
 *
 * Text will wrap if it exceeds this width
 *
 * @param tooltip Tooltip widget
 * @param max_width Maximum width in pixels
 */
DLL_EXPORT void widget_tooltip_set_max_width(Widget *tooltip, int max_width);

/**
 * Show tooltip at mouse position after delay
 *
 * @param tooltip Tooltip widget
 * @param mouse_x Current mouse X position
 * @param mouse_y Current mouse Y position
 */
DLL_EXPORT void widget_tooltip_show_at_mouse(Widget *tooltip, int mouse_x, int mouse_y);

/**
 * Show tooltip anchored to a widget after delay
 *
 * @param tooltip Tooltip widget
 * @param target Target widget to anchor to
 * @param offset_x Horizontal offset from widget
 * @param offset_y Vertical offset from widget
 */
DLL_EXPORT void widget_tooltip_show_at_widget(Widget *tooltip, Widget *target, int offset_x, int offset_y);

/**
 * Update tooltip position (if following mouse)
 *
 * @param tooltip Tooltip widget
 * @param mouse_x Current mouse X position
 * @param mouse_y Current mouse Y position
 */
DLL_EXPORT void widget_tooltip_update_position(Widget *tooltip, int mouse_x, int mouse_y);

/**
 * Hide tooltip immediately
 *
 * @param tooltip Tooltip widget
 */
DLL_EXPORT void widget_tooltip_hide(Widget *tooltip);

/**
 * Cancel pending tooltip show
 *
 * Useful when mouse moves away before delay expires
 *
 * @param tooltip Tooltip widget
 */
DLL_EXPORT void widget_tooltip_cancel(Widget *tooltip);

/**
 * Set tooltip colors
 *
 * @param tooltip Tooltip widget
 * @param bg Background color
 * @param border Border color
 * @param text Text color
 */
DLL_EXPORT void widget_tooltip_set_colors(
    Widget *tooltip, unsigned short bg, unsigned short border, unsigned short text);

#endif // WIDGET_TOOLTIP_H
