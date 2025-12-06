/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Slider Widget - Draggable slider for value selection
 */

#ifndef WIDGET_SLIDER_H
#define WIDGET_SLIDER_H

#include "../widget.h"

// Slider orientation
typedef enum { SLIDER_HORIZONTAL = 0, SLIDER_VERTICAL } SliderOrientation;

// Slider callback function - called when value changes
typedef void (*SliderCallback)(Widget *slider, float value, void *user_param);

// Slider-specific data
typedef struct {
	float value; // Current value (0.0 to max)
	float max; // Maximum value
	float step; // Step size (0 for continuous)
	SliderOrientation orientation;

	// Visual
	unsigned short track_color; // Track background color
	unsigned short fill_color; // Filled portion color
	unsigned short handle_color; // Handle color
	unsigned short handle_hover_color;
	unsigned short border_color; // Border color
	int show_border;
	int handle_size; // Handle width (horizontal) or height (vertical)

	// State
	int dragging; // Currently dragging handle
	int hover; // Mouse over handle

	// Callback
	SliderCallback on_change;
	void *callback_param;
} SliderData;

/**
 * Create a slider widget
 *
 * @param x X position
 * @param y Y position
 * @param width Width
 * @param height Height
 * @param orientation Horizontal or vertical
 * @return Slider widget
 */
DLL_EXPORT Widget *widget_slider_create(int x, int y, int width, int height, SliderOrientation orientation);

/**
 * Set slider value
 *
 * @param slider Slider widget
 * @param value New value (will be clamped to 0-max)
 */
DLL_EXPORT void widget_slider_set_value(Widget *slider, float value);

/**
 * Get slider value
 *
 * @param slider Slider widget
 * @return Current value
 */
DLL_EXPORT float widget_slider_get_value(Widget *slider);

/**
 * Set maximum value
 *
 * @param slider Slider widget
 * @param max Maximum value
 */
DLL_EXPORT void widget_slider_set_max(Widget *slider, float max);

/**
 * Set step size
 *
 * @param slider Slider widget
 * @param step Step size (0 for continuous)
 */
DLL_EXPORT void widget_slider_set_step(Widget *slider, float step);

/**
 * Set value change callback
 *
 * @param slider Slider widget
 * @param callback Function to call when value changes
 * @param param User parameter passed to callback
 */
DLL_EXPORT void widget_slider_set_callback(Widget *slider, SliderCallback callback, void *param);

/**
 * Set slider colors
 *
 * @param slider Slider widget
 * @param track Track background color
 * @param fill Filled portion color
 * @param handle Handle color
 */
DLL_EXPORT void widget_slider_set_colors(
    Widget *slider, unsigned short track, unsigned short fill, unsigned short handle);

/**
 * Get slider percentage (0.0 to 1.0)
 *
 * @param slider Slider widget
 * @return Percentage as float
 */
DLL_EXPORT float widget_slider_get_percentage(Widget *slider);

#endif // WIDGET_SLIDER_H
