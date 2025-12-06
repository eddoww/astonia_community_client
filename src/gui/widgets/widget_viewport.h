/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Viewport Widget - Game Map Rendering Viewport
 *
 * This widget encapsulates the game map rendering, allowing the map
 * to be treated as a widget within the GUI system. It handles clipping,
 * coordinate transformation, and event routing to the game layer.
 */

#ifndef WIDGET_VIEWPORT_H
#define WIDGET_VIEWPORT_H

#include "../widget.h"

// Viewport-specific data
typedef struct {
	// Map offset adjustments (for future scrolling/panning)
	int map_offset_x;
	int map_offset_y;

	// Saved global state (restored after rendering)
	int saved_mapaddx;
	int saved_mapaddy;

	// Rendering state
	int render_enabled; // 1 to render game, 0 to skip (for debugging)

	// Position tracking (for detecting moves during drag)
	int last_screen_x;
	int last_screen_y;
} ViewportData;

/**
 * Create a viewport widget for game map rendering
 *
 * @param x X position
 * @param y Y position
 * @param width Width of viewport
 * @param height Height of viewport
 * @return Viewport widget
 */
DLL_EXPORT Widget *widget_viewport_create(int x, int y, int width, int height);

/**
 * Enable/disable viewport rendering
 *
 * @param viewport Viewport widget
 * @param enabled 1 to render game, 0 to skip
 */
DLL_EXPORT void widget_viewport_set_render_enabled(Widget *viewport, int enabled);

/**
 * Set map offset (for scrolling/panning)
 *
 * @param viewport Viewport widget
 * @param offset_x X offset
 * @param offset_y Y offset
 */
DLL_EXPORT void widget_viewport_set_offset(Widget *viewport, int offset_x, int offset_y);

/**
 * Get the main viewport widget (singleton pattern for primary game view)
 *
 * @return Main viewport widget, or NULL if not created
 */
DLL_EXPORT Widget *widget_viewport_get_main(void);

/**
 * Initialize the main viewport widget
 * Creates the viewport at DOT_MTL/DOT_MBR coordinates
 *
 * @return 1 on success, 0 on failure
 */
DLL_EXPORT int widget_viewport_init(void);

/**
 * Check if viewport widget system is handling game rendering
 * When true, display() should NOT render game directly
 *
 * @return 1 if viewport handles rendering, 0 if legacy rendering
 */
DLL_EXPORT int widget_viewport_is_active(void);

/**
 * Get the viewport bounds in screen coordinates
 * Used by stom() for bounds checking
 *
 * @param x1 Output: left edge
 * @param y1 Output: top edge
 * @param x2 Output: right edge
 * @param y2 Output: bottom edge
 * @return 1 if viewport is active and bounds are valid, 0 otherwise
 */
DLL_EXPORT int widget_viewport_get_bounds(int *x1, int *y1, int *x2, int *y2);

#endif // WIDGET_VIEWPORT_H
