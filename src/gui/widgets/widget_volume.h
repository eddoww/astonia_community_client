/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Volume Widget - Audio volume control with sliders and mute toggle
 */

#ifndef WIDGET_VOLUME_H
#define WIDGET_VOLUME_H

#include "../widget.h"

// Volume-specific data
typedef struct {
	Widget *sound_slider; // Sound effects volume slider
	Widget *sound_label; // Sound label
	Widget *volume_value; // Volume percentage display (0-100%)
	Widget *mute_button; // Mute toggle button
	// Widget *music_slider;    // Music volume slider (TODO: future)
	// Widget *music_label;     // Music label (TODO: future)

	int sound_volume; // Current sound volume (0-128)
	// int music_volume;        // Current music volume (0-128) (TODO: future)
	int muted; // Sound is muted
	int pre_mute_volume; // Volume before muting (for unmute restore)
} VolumeData;

/**
 * Create a volume control widget
 *
 * @param x X position
 * @param y Y position
 * @return Volume widget (container with sliders and controls)
 */
DLL_EXPORT Widget *widget_volume_create(int x, int y);

/**
 * Set sound volume
 *
 * @param volume Volume widget
 * @param value Volume value (0-128)
 */
DLL_EXPORT void widget_volume_set_sound(Widget *volume, int value);

/**
 * Get sound volume
 *
 * @param volume Volume widget
 * @return Current sound volume (0-128)
 */
DLL_EXPORT int widget_volume_get_sound(Widget *volume);

/**
 * Set muted state
 *
 * @param volume Volume widget
 * @param muted 1 to mute, 0 to unmute
 */
DLL_EXPORT void widget_volume_set_muted(Widget *volume, int muted);

/**
 * Get muted state
 *
 * @param volume Volume widget
 * @return 1 if muted, 0 otherwise
 */
DLL_EXPORT int widget_volume_is_muted(Widget *volume);

/**
 * Toggle mute state
 *
 * @param volume Volume widget
 */
DLL_EXPORT void widget_volume_toggle_mute(Widget *volume);

/**
 * Save volume settings to disk
 */
DLL_EXPORT void widget_volume_save_settings(void);

/**
 * Load volume settings from disk
 */
DLL_EXPORT void widget_volume_load_settings(void);

/**
 * Initialize volume widget system
 * Called once during game initialization
 */
DLL_EXPORT void widget_volume_init(void);

/**
 * Cleanup volume widget system
 * Called once during game shutdown
 */
DLL_EXPORT void widget_volume_cleanup(void);

/**
 * Toggle volume widget visibility
 */
DLL_EXPORT void widget_volume_toggle(void);

/**
 * Check if volume widget is visible
 *
 * @return 1 if visible, 0 otherwise
 */
DLL_EXPORT int widget_volume_is_visible(void);

#endif // WIDGET_VOLUME_H
