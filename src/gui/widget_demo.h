/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Widget System Demo/Test Integration - Header
 */

#ifndef WIDGET_DEMO_H
#define WIDGET_DEMO_H

/**
 * Initialize the widget demo
 * Creates test widgets positioned in the top-right area
 */
void widget_demo_init(void);

/**
 * Cleanup widget demo
 */
void widget_demo_cleanup(void);

/**
 * Toggle widget demo visibility (F11 key)
 */
void widget_demo_toggle(void);

/**
 * Update widget demo (call every frame in display())
 * @param dt Delta time in milliseconds
 */
void widget_demo_update(int dt);

/**
 * Render widget demo (call in display() after other GUI elements)
 */
void widget_demo_render(void);

/**
 * Handle mouse button events
 * @param x Mouse X position
 * @param y Mouse Y position
 * @param button Mouse button (SDL_BUTTON_LEFT, etc.)
 * @param down 1 for button down, 0 for button up
 * @return 1 if event was handled by widgets, 0 otherwise
 */
int widget_demo_handle_mouse_button(int x, int y, int button, int down);

/**
 * Handle mouse motion
 * @param x Mouse X position
 * @param y Mouse Y position
 */
void widget_demo_handle_mouse_motion(int x, int y);

/**
 * Handle keyboard events
 * @param key SDL key code
 * @param down 1 for key down, 0 for key up
 * @return 1 if event was handled by widgets, 0 otherwise
 */
int widget_demo_handle_key(int key, int down);

/**
 * Handle text input events (for text input widgets)
 * @param text UTF-8 text input
 */
void widget_demo_handle_text_input(const char *text);

/**
 * Check if widget demo is enabled
 * @return 1 if enabled, 0 otherwise
 */
int widget_demo_is_enabled(void);

#endif // WIDGET_DEMO_H
