/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Button Widget - Clickable button with text and/or icon
 */

#ifndef WIDGET_BUTTON_H
#define WIDGET_BUTTON_H

#include "../widget.h"

// Button states
typedef enum { BUTTON_STATE_NORMAL = 0, BUTTON_STATE_HOVER, BUTTON_STATE_PRESSED, BUTTON_STATE_DISABLED } ButtonState;

// Button callback function
typedef void (*ButtonCallback)(Widget *button, void *user_param);

// Button-specific data
typedef struct {
	char text[128];
	int sprite; // Icon sprite (-1 for no icon)
	ButtonState state;

	// Colors for each state
	unsigned short normal_color;
	unsigned short hover_color;
	unsigned short pressed_color;
	unsigned short disabled_color;
	unsigned short text_color;

	// Callback
	ButtonCallback on_click;
	void *callback_param;
} ButtonData;

/**
 * Create a button widget
 *
 * @param x X position
 * @param y Y position
 * @param width Width
 * @param height Height
 * @param text Button text
 * @return Button widget
 */
DLL_EXPORT Widget *widget_button_create(int x, int y, int width, int height, const char *text);

/**
 * Set button text
 *
 * @param button Button widget
 * @param text New text
 */
DLL_EXPORT void widget_button_set_text(Widget *button, const char *text);

/**
 * Set button icon sprite
 *
 * @param button Button widget
 * @param sprite Sprite ID (-1 for no icon)
 */
DLL_EXPORT void widget_button_set_sprite(Widget *button, int sprite);

/**
 * Set button click callback
 *
 * @param button Button widget
 * @param callback Function to call when button is clicked
 * @param param User parameter passed to callback
 */
DLL_EXPORT void widget_button_set_callback(Widget *button, ButtonCallback callback, void *param);

/**
 * Set button colors
 *
 * @param button Button widget
 * @param normal Normal state color
 * @param hover Hover state color
 * @param pressed Pressed state color
 * @param disabled Disabled state color
 */
DLL_EXPORT void widget_button_set_colors(
    Widget *button, unsigned short normal, unsigned short hover, unsigned short pressed, unsigned short disabled);

/**
 * Set button text color
 *
 * @param button Button widget
 * @param color Text color
 */
DLL_EXPORT void widget_button_set_text_color(Widget *button, unsigned short color);

#endif // WIDGET_BUTTON_H
