/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * ProgressBar Widget - Visual progress/status bar
 */

#ifndef WIDGET_PROGRESSBAR_H
#define WIDGET_PROGRESSBAR_H

#include "../widget.h"

// Progress bar orientation
typedef enum { PROGRESSBAR_HORIZONTAL = 0, PROGRESSBAR_VERTICAL } ProgressBarOrientation;

// ProgressBar-specific data
typedef struct {
	float value; // Current value (0.0 to max)
	float max; // Maximum value
	ProgressBarOrientation orientation;

	// Visual
	unsigned short fill_color; // Fill color
	unsigned short bg_color; // Background color
	unsigned short border_color; // Border color
	int show_border; // Draw border
	int show_text; // Show text overlay
	char text[64]; // Text overlay (optional)
	unsigned short text_color; // Text color
} ProgressBarData;

/**
 * Create a progress bar widget
 *
 * @param x X position
 * @param y Y position
 * @param width Width
 * @param height Height
 * @param orientation Horizontal or vertical
 * @return ProgressBar widget
 */
DLL_EXPORT Widget *widget_progressbar_create(int x, int y, int width, int height, ProgressBarOrientation orientation);

/**
 * Set progress value
 *
 * @param bar ProgressBar widget
 * @param value Current value (will be clamped to 0-max)
 */
DLL_EXPORT void widget_progressbar_set_value(Widget *bar, float value);

/**
 * Set maximum value
 *
 * @param bar ProgressBar widget
 * @param max Maximum value
 */
DLL_EXPORT void widget_progressbar_set_max(Widget *bar, float max);

/**
 * Set value and max together
 *
 * @param bar ProgressBar widget
 * @param value Current value
 * @param max Maximum value
 */
DLL_EXPORT void widget_progressbar_set_range(Widget *bar, float value, float max);

/**
 * Set fill color
 *
 * @param bar ProgressBar widget
 * @param color Fill color
 */
DLL_EXPORT void widget_progressbar_set_fill_color(Widget *bar, unsigned short color);

/**
 * Set background color
 *
 * @param bar ProgressBar widget
 * @param color Background color
 */
DLL_EXPORT void widget_progressbar_set_bg_color(Widget *bar, unsigned short color);

/**
 * Set border color and visibility
 *
 * @param bar ProgressBar widget
 * @param color Border color
 * @param show 1 to show border, 0 to hide
 */
DLL_EXPORT void widget_progressbar_set_border(Widget *bar, unsigned short color, int show);

/**
 * Set text overlay
 *
 * @param bar ProgressBar widget
 * @param text Text to display (NULL to hide)
 * @param color Text color
 */
DLL_EXPORT void widget_progressbar_set_text(Widget *bar, const char *text, unsigned short color);

/**
 * Get current percentage (0.0 to 1.0)
 *
 * @param bar ProgressBar widget
 * @return Percentage as float
 */
DLL_EXPORT float widget_progressbar_get_percentage(Widget *bar);

#endif // WIDGET_PROGRESSBAR_H
