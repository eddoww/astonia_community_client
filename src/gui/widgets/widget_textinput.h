/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * TextInput Widget - Single-line text input field
 */

#ifndef WIDGET_TEXTINPUT_H
#define WIDGET_TEXTINPUT_H

#include "../widget.h"

// Text input callback types
typedef void (*TextInputCallback)(Widget *input, const char *text, void *param);
typedef void (*TextInputChangeCallback)(Widget *input, void *param);

// TextInput-specific data
typedef struct {
	char *text; // Text buffer (dynamically allocated)
	int text_capacity; // Allocated size of text buffer
	int text_length; // Current text length
	int cursor_pos; // Cursor position (0 to text_length)
	int scroll_offset; // Horizontal scroll offset for long text

	// Selection (for future implementation)
	int selection_start;
	int selection_end;

	// Visual
	unsigned short bg_color;
	unsigned short border_color;
	unsigned short text_color;
	unsigned short cursor_color;
	unsigned short selection_color;
	int show_cursor; // Cursor visibility (blinks)
	unsigned int cursor_blink_time; // Last cursor blink time

	// Behavior
	int max_length; // Maximum text length (0 = unlimited)
	int password_mode; // Show asterisks instead of text
	int multiline; // Allow newlines (not implemented yet)
	int readonly; // Prevent editing

	// Callbacks
	TextInputCallback on_submit; // Called when Enter is pressed
	TextInputChangeCallback on_change; // Called when text changes
	void *callback_param;

	// Placeholder
	char placeholder[128]; // Placeholder text when empty
	unsigned short placeholder_color;
} TextInputData;

/**
 * Create a text input widget
 *
 * @param x X position
 * @param y Y position
 * @param width Width
 * @param height Height
 * @return TextInput widget
 */
DLL_EXPORT Widget *widget_textinput_create(int x, int y, int width, int height);

/**
 * Set text content
 *
 * @param input TextInput widget
 * @param text Text to set
 */
DLL_EXPORT void widget_textinput_set_text(Widget *input, const char *text);

/**
 * Get text content
 *
 * @param input TextInput widget
 * @return Text content (do not modify or free)
 */
DLL_EXPORT const char *widget_textinput_get_text(Widget *input);

/**
 * Clear text
 *
 * @param input TextInput widget
 */
DLL_EXPORT void widget_textinput_clear(Widget *input);

/**
 * Set cursor position
 *
 * @param input TextInput widget
 * @param pos Cursor position (0 to text length)
 */
DLL_EXPORT void widget_textinput_set_cursor(Widget *input, int pos);

/**
 * Set maximum text length
 *
 * @param input TextInput widget
 * @param max_length Maximum length (0 = unlimited)
 */
DLL_EXPORT void widget_textinput_set_max_length(Widget *input, int max_length);

/**
 * Set password mode
 *
 * @param input TextInput widget
 * @param password 1 to show asterisks, 0 for normal
 */
DLL_EXPORT void widget_textinput_set_password_mode(Widget *input, int password);

/**
 * Set readonly mode
 *
 * @param input TextInput widget
 * @param readonly 1 to prevent editing, 0 for normal
 */
DLL_EXPORT void widget_textinput_set_readonly(Widget *input, int readonly);

/**
 * Set placeholder text
 *
 * @param input TextInput widget
 * @param placeholder Placeholder text
 */
DLL_EXPORT void widget_textinput_set_placeholder(Widget *input, const char *placeholder);

/**
 * Set colors
 *
 * @param input TextInput widget
 * @param bg Background color
 * @param border Border color
 * @param text Text color
 */
DLL_EXPORT void widget_textinput_set_colors(
    Widget *input, unsigned short bg, unsigned short border, unsigned short text);

/**
 * Set submit callback (called when Enter is pressed)
 *
 * @param input TextInput widget
 * @param callback Callback function
 * @param param User parameter
 */
DLL_EXPORT void widget_textinput_set_submit_callback(Widget *input, TextInputCallback callback, void *param);

/**
 * Set change callback (called when text changes)
 *
 * @param input TextInput widget
 * @param callback Callback function
 * @param param User parameter
 */
DLL_EXPORT void widget_textinput_set_change_callback(Widget *input, TextInputChangeCallback callback, void *param);

#endif // WIDGET_TEXTINPUT_H
