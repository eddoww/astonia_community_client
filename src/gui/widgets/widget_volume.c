/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Volume Widget Implementation
 */

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_volume.h"
#include "widget_container.h"
#include "widget_slider.h"
#include "widget_button.h"
#include "widget_label.h"
#include "../../game/game.h"
#include "../../sdl/sdl.h"

// External sound volume from sound.c
extern int sound_volume;

// Volume widget singleton
static Widget *g_volume_widget = NULL;
static VolumeData *g_volume_data = NULL;
static int volume_initialized = 0;

// Settings file name
#define VOLUME_SETTINGS_FILE "volume_settings.txt"

// Forward declarations
static void on_sound_slider_change(Widget *slider, float value, void *param);
static void on_mute_button_click(Widget *button, void *param);

// Helper to get settings file path
static char *get_volume_settings_path(void)
{
	char *base_path;
	char *full_path;
	size_t path_len;

	// Use SDL_GetPrefPath for cross-platform config directory
	base_path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
	if (!base_path) {
		base_path = SDL_strdup("./");
	}

	path_len = strlen(base_path) + strlen(VOLUME_SETTINGS_FILE) + 1;
	full_path = SDL_malloc(path_len);
	if (!full_path) {
		SDL_free(base_path);
		return NULL;
	}

	snprintf(full_path, path_len, "%s%s", base_path, VOLUME_SETTINGS_FILE);
	SDL_free(base_path);

	return full_path;
}

// Update the mute button text
static void update_mute_button_text(void)
{
	if (!g_volume_data || !g_volume_data->mute_button) {
		return;
	}

	if (g_volume_data->muted) {
		widget_button_set_text(g_volume_data->mute_button, "Unmute");
	} else {
		widget_button_set_text(g_volume_data->mute_button, "Mute");
	}
}

// Update the volume value label (0-100%)
static void update_volume_value_label(void)
{
	char text[16];
	int percent;

	if (!g_volume_data || !g_volume_data->volume_value) {
		return;
	}

	// Convert 0-128 to 0-100%
	if (g_volume_data->muted) {
		percent = 0;
	} else {
		percent = (g_volume_data->sound_volume * 100 + 64) / 128; // Round to nearest
	}

	snprintf(text, sizeof(text), "%d%%", percent);
	widget_label_set_text(g_volume_data->volume_value, text);
}

Widget *widget_volume_create(int x, int y)
{
	Widget *widget;

	// Create container widget
	widget = widget_container_create(x, y, 180, 112);
	if (!widget) {
		return NULL;
	}

	// Setup container layout - vertical stacking like demo widget
	widget_container_set_layout(widget, LAYOUT_VERTICAL);
	widget_container_set_spacing(widget, 8, 6); // padding=8, spacing=6
	widget_container_set_background(widget, IRGB(5, 5, 7), 1);

	// Enable window chrome
	widget_set_window_chrome(widget, 1, 1, 0, 1, 1);
	widget_set_title(widget, "Volume");
	widget_set_name(widget, "volume_control");

	// Allocate volume-specific data (stored globally, not in widget)
	g_volume_data = xmalloc(sizeof(VolumeData), MEM_GUI);
	if (!g_volume_data) {
		widget_destroy(widget);
		return NULL;
	}
	bzero(g_volume_data, sizeof(VolumeData));

	// Initialize data
	g_volume_data->sound_volume = sound_volume;
	g_volume_data->muted = 0;
	g_volume_data->pre_mute_volume = sound_volume;

	// Create sound label
	g_volume_data->sound_label = widget_label_create(0, 0, 164, 16, "Sound Volume");
	if (g_volume_data->sound_label) {
		widget_label_set_color(g_volume_data->sound_label, IRGB(25, 25, 28));
		widget_label_set_alignment(g_volume_data->sound_label, LABEL_ALIGN_CENTER);
		widget_add_child(widget, g_volume_data->sound_label);
	}

	// Create sound slider
	g_volume_data->sound_slider = widget_slider_create(0, 0, 164, 18, SLIDER_HORIZONTAL);
	if (g_volume_data->sound_slider) {
		widget_slider_set_max(g_volume_data->sound_slider, 128.0f);
		widget_slider_set_value(g_volume_data->sound_slider, (float)sound_volume);
		widget_slider_set_callback(g_volume_data->sound_slider, on_sound_slider_change, NULL);
		widget_add_child(widget, g_volume_data->sound_slider);
	}

	// Create volume value label (shows percentage)
	g_volume_data->volume_value = widget_label_create(0, 0, 164, 16, "100%");
	if (g_volume_data->volume_value) {
		widget_label_set_color(g_volume_data->volume_value, IRGB(25, 25, 28));
		widget_label_set_alignment(g_volume_data->volume_value, LABEL_ALIGN_CENTER);
		widget_add_child(widget, g_volume_data->volume_value);
		update_volume_value_label(); // Set initial value
	}

	/* TODO: Music volume slider (commented out for future implementation)
	// Create music label
	g_volume_data->music_label = widget_label_create(0, 0, 164, 16, "Music Volume");
	if (g_volume_data->music_label) {
	    widget_label_set_color(g_volume_data->music_label, IRGB(25, 25, 28));
	    widget_label_set_alignment(g_volume_data->music_label, LABEL_ALIGN_CENTER);
	    widget_add_child(widget, g_volume_data->music_label);
	}

	// Create music slider
	g_volume_data->music_slider = widget_slider_create(0, 0, 164, 18, SLIDER_HORIZONTAL);
	if (g_volume_data->music_slider) {
	    widget_slider_set_max(g_volume_data->music_slider, 128.0f);
	    widget_slider_set_value(g_volume_data->music_slider, 64.0f);
	    widget_slider_set_callback(g_volume_data->music_slider, on_music_slider_change, NULL);
	    widget_add_child(widget, g_volume_data->music_slider);
	}
	*/

	// Create mute button
	g_volume_data->mute_button = widget_button_create(0, 0, 164, 24, "Mute");
	if (g_volume_data->mute_button) {
		widget_button_set_callback(g_volume_data->mute_button, on_mute_button_click, NULL);
		widget_add_child(widget, g_volume_data->mute_button);
	}

	// Update layout now that all children are added
	widget_container_update_layout(widget);

	return widget;
}

void widget_volume_set_sound(Widget *volume, int value)
{
	if (!g_volume_data) {
		return;
	}

	// Clamp value
	if (value < 0) {
		value = 0;
	}
	if (value > 128) {
		value = 128;
	}

	g_volume_data->sound_volume = value;
	sound_volume = value;

	// Update slider if not muted
	if (!g_volume_data->muted && g_volume_data->sound_slider) {
		widget_slider_set_value(g_volume_data->sound_slider, (float)value);
	}
}

int widget_volume_get_sound(Widget *volume)
{
	if (!g_volume_data) {
		return 0;
	}

	return g_volume_data->sound_volume;
}

void widget_volume_set_muted(Widget *volume, int muted)
{
	if (!g_volume_data) {
		return;
	}

	if (muted && !g_volume_data->muted) {
		// Muting
		g_volume_data->pre_mute_volume = g_volume_data->sound_volume;
		g_volume_data->muted = 1;
		g_volume_data->sound_volume = 0;
		sound_volume = 0;
		if (g_volume_data->sound_slider) {
			widget_slider_set_value(g_volume_data->sound_slider, 0.0f);
		}
	} else if (!muted && g_volume_data->muted) {
		// Unmuting
		g_volume_data->muted = 0;
		g_volume_data->sound_volume = g_volume_data->pre_mute_volume;
		sound_volume = g_volume_data->pre_mute_volume;
		if (g_volume_data->sound_slider) {
			widget_slider_set_value(g_volume_data->sound_slider, (float)g_volume_data->pre_mute_volume);
		}
	}

	update_mute_button_text();
	update_volume_value_label();
}

int widget_volume_is_muted(Widget *volume)
{
	if (!g_volume_data) {
		return 0;
	}

	return g_volume_data->muted;
}

void widget_volume_toggle_mute(Widget *volume)
{
	if (!g_volume_data) {
		return;
	}

	widget_volume_set_muted(volume, !g_volume_data->muted);
}

void widget_volume_save_settings(void)
{
	FILE *fp;
	char *path;

	if (!g_volume_data) {
		return;
	}

	path = get_volume_settings_path();
	if (!path) {
		warn("widget_volume_save_settings: failed to get settings path");
		return;
	}

	fp = fopen(path, "w");
	if (!fp) {
		warn("widget_volume_save_settings: failed to open %s for writing", path);
		SDL_free(path);
		return;
	}

	// Save settings in simple format
	fprintf(
	    fp, "sound_volume=%d\n", g_volume_data->muted ? g_volume_data->pre_mute_volume : g_volume_data->sound_volume);
	fprintf(fp, "muted=%d\n", g_volume_data->muted);
	/* TODO: Music volume
	fprintf(fp, "music_volume=%d\n", g_volume_data->music_volume);
	*/

	fclose(fp);
	note("Volume settings saved to %s", path);
	SDL_free(path);
}

void widget_volume_load_settings(void)
{
	FILE *fp;
	char *path;
	char line[128];
	int loaded_sound_volume = 128;
	int loaded_muted = 0;

	path = get_volume_settings_path();
	if (!path) {
		return;
	}

	fp = fopen(path, "r");
	if (!fp) {
		note("widget_volume_load_settings: no saved settings at %s", path);
		SDL_free(path);
		return;
	}

	// Parse settings file
	while (fgets(line, sizeof(line), fp)) {
		char *equals = strchr(line, '=');
		if (!equals) {
			continue;
		}

		*equals = '\0';
		char *key = line;
		char *value = equals + 1;

		// Remove newline
		char *nl = strchr(value, '\n');
		if (nl) {
			*nl = '\0';
		}

		if (strcmp(key, "sound_volume") == 0) {
			loaded_sound_volume = atoi(value);
			if (loaded_sound_volume < 0) {
				loaded_sound_volume = 0;
			}
			if (loaded_sound_volume > 128) {
				loaded_sound_volume = 128;
			}
		} else if (strcmp(key, "muted") == 0) {
			loaded_muted = atoi(value);
		}
		/* TODO: Music volume
		else if (strcmp(key, "music_volume") == 0) {
		    loaded_music_volume = atoi(value);
		}
		*/
	}

	fclose(fp);
	SDL_free(path);

	// Apply loaded settings to global sound_volume
	sound_volume = loaded_muted ? 0 : loaded_sound_volume;

	// If widget data exists, update it
	if (g_volume_data) {
		g_volume_data->sound_volume = loaded_sound_volume;
		g_volume_data->muted = loaded_muted;
		g_volume_data->pre_mute_volume = loaded_sound_volume;

		if (g_volume_data->sound_slider) {
			widget_slider_set_value(g_volume_data->sound_slider, loaded_muted ? 0.0f : (float)loaded_sound_volume);
		}
		update_mute_button_text();
		update_volume_value_label();
	}

	note("Volume settings loaded: sound=%d, muted=%d", loaded_sound_volume, loaded_muted);
}

void widget_volume_init(void)
{
	Widget *root;

	if (volume_initialized) {
		return;
	}

	// Verify widget manager is initialized
	if (!widget_manager_get()) {
		return;
	}

	// Load settings first (before creating widget)
	widget_volume_load_settings();

	// Create volume widget
	g_volume_widget = widget_volume_create(10, 100);
	if (!g_volume_widget) {
		return;
	}

	// Add to root widget
	root = widget_manager_get_root();
	if (root) {
		widget_add_child(root, g_volume_widget);
	}

	// Apply loaded settings to widget
	if (g_volume_data) {
		// Sync slider with current sound_volume
		if (g_volume_data->sound_slider) {
			widget_slider_set_value(
			    g_volume_data->sound_slider, g_volume_data->muted ? 0.0f : (float)g_volume_data->sound_volume);
		}
		update_mute_button_text();
		update_volume_value_label();
	}

	// Rebuild z-order list to ensure all widgets are included
	widget_manager_rebuild_z_order();

	// Start hidden by default
	widget_set_visible(g_volume_widget, 0);

	volume_initialized = 1;
}

void widget_volume_cleanup(void)
{
	if (!volume_initialized) {
		return;
	}

	// Save settings before cleanup
	widget_volume_save_settings();

	// Note: Don't destroy g_volume_widget here - it's a child of the root widget
	// and gets destroyed automatically by widget_manager_cleanup()
	g_volume_widget = NULL;

	if (g_volume_data) {
		xfree(g_volume_data);
		g_volume_data = NULL;
	}

	volume_initialized = 0;
}

void widget_volume_toggle(void)
{
	if (!volume_initialized) {
		widget_volume_init();
		if (g_volume_widget) {
			widget_set_visible(g_volume_widget, 1);
		}
		return;
	}

	if (g_volume_widget) {
		widget_set_visible(g_volume_widget, !g_volume_widget->visible);
	}
}

int widget_volume_is_visible(void)
{
	if (!volume_initialized || !g_volume_widget) {
		return 0;
	}

	return g_volume_widget->visible;
}

// =============================================================================
// Callbacks
// =============================================================================

static void on_sound_slider_change(Widget *slider, float value, void *param)
{
	int int_value;

	if (!g_volume_data) {
		return;
	}

	int_value = (int)(value + 0.5f);
	if (int_value < 0) {
		int_value = 0;
	}
	if (int_value > 128) {
		int_value = 128;
	}

	g_volume_data->sound_volume = int_value;
	sound_volume = int_value;

	// If user moves slider while muted, unmute
	if (g_volume_data->muted && int_value > 0) {
		g_volume_data->muted = 0;
		g_volume_data->pre_mute_volume = int_value;
		update_mute_button_text();
	}

	// Update percentage display
	update_volume_value_label();
}

static void on_mute_button_click(Widget *button, void *param)
{
	if (!g_volume_data) {
		return;
	}

	widget_volume_set_muted(NULL, !g_volume_data->muted);
}
