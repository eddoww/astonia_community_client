/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Label Widget - Static text display
 */

#ifndef WIDGET_LABEL_H
#define WIDGET_LABEL_H

#include "../widget.h"

// Text alignment
typedef enum {
	LABEL_ALIGN_LEFT = RENDER_TEXT_LEFT,
	LABEL_ALIGN_CENTER = RENDER_TEXT_CENTER,
	LABEL_ALIGN_RIGHT = RENDER_TEXT_RIGHT
} LabelAlignment;

// Label-specific data
typedef struct {
	char *text; // Text content (dynamically allocated)
	int text_capacity; // Allocated size of text buffer
	unsigned short color; // Text color
	int alignment; // Text alignment (LABEL_ALIGN_*)
	int word_wrap; // Enable word wrapping
	int small_font; // Use small font
} LabelData;

/**
 * Create a label widget
 *
 * @param x X position
 * @param y Y position
 * @param width Width (for word wrapping)
 * @param height Height
 * @param text Initial text
 * @return Label widget
 */
DLL_EXPORT Widget *widget_label_create(int x, int y, int width, int height, const char *text);

/**
 * Set label text
 *
 * @param label Label widget
 * @param text New text
 */
DLL_EXPORT void widget_label_set_text(Widget *label, const char *text);

/**
 * Set label color
 *
 * @param label Label widget
 * @param color Text color
 */
DLL_EXPORT void widget_label_set_color(Widget *label, unsigned short color);

/**
 * Set label alignment
 *
 * @param label Label widget
 * @param alignment Text alignment (LABEL_ALIGN_*)
 */
DLL_EXPORT void widget_label_set_alignment(Widget *label, LabelAlignment alignment);

/**
 * Enable/disable word wrapping
 *
 * @param label Label widget
 * @param wrap 1 to enable, 0 to disable
 */
DLL_EXPORT void widget_label_set_word_wrap(Widget *label, int wrap);

/**
 * Set font size
 *
 * @param label Label widget
 * @param small 1 for small font, 0 for regular
 */
DLL_EXPORT void widget_label_set_small_font(Widget *label, int small);

#endif // WIDGET_LABEL_H
