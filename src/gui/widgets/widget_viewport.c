/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Viewport Widget Implementation
 *
 * Renders the game map within a clipped widget area. This allows the game
 * world to be treated as a first-class widget, supporting positioning,
 * z-ordering, and future features like multiple viewports or picture-in-picture.
 */

#include <stdio.h>
#include <string.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_viewport.h"
#include "../../game/game.h"
#include "../gui.h"

// Singleton for the main game viewport
static Widget *main_viewport = NULL;
static int viewport_active = 0;

// Forward declarations
static void viewport_render(Widget *self);
static void viewport_update(Widget *self, int dt);
static int viewport_on_mouse_down(Widget *self, int x, int y, int button);
static int viewport_on_mouse_up(Widget *self, int x, int y, int button);
static int viewport_on_mouse_move(Widget *self, int x, int y);
static void viewport_on_destroy(Widget *self);
static void viewport_on_resize(Widget *self, int old_width, int old_height);
static void viewport_update_stom_offsets(Widget *self);

Widget *widget_viewport_create(int x, int y, int width, int height)
{
	Widget *widget;
	ViewportData *data;

	// Create base widget
	widget = widget_create(WIDGET_TYPE_VIEWPORT, x, y, width, height);
	if (!widget) {
		return NULL;
	}

	// Allocate viewport-specific data
	data = xmalloc(sizeof(ViewportData), MEM_GUI);
	if (!data) {
		widget_destroy(widget);
		return NULL;
	}

	bzero(data, sizeof(ViewportData));

	// Initialize data
	data->map_offset_x = 0;
	data->map_offset_y = 0;
	data->saved_mapaddx = 0;
	data->saved_mapaddy = 0;
	data->render_enabled = 1;

	// Set user data
	widget->user_data = data;
	widget->user_data_size = sizeof(ViewportData);

	// Set virtual functions
	widget->render = viewport_render;
	widget->update = viewport_update;
	widget->on_mouse_down = viewport_on_mouse_down;
	widget->on_mouse_up = viewport_on_mouse_up;
	widget->on_mouse_move = viewport_on_mouse_move;
	widget->on_destroy = viewport_on_destroy;
	widget->on_resize = viewport_on_resize;

	// Viewport doesn't have children or layout
	widget->cap_has_layout = 0;
	widget->cap_has_children = 0;
	widget->cap_scrollable = 0;

	// CRITICAL: Mouse events pass through to game layer
	// This allows clicks on the game map to reach the game's mouse handler
	widget->cap_pass_through_mouse = 1;

	// Low z-order so other widgets render on top
	widget->z_order = -1000;

	// Set name
	snprintf(widget->name, sizeof(widget->name), "viewport_%d", widget->id);

	return widget;
}

void widget_viewport_set_render_enabled(Widget *viewport, int enabled)
{
	ViewportData *data;

	if (!viewport || viewport->type != WIDGET_TYPE_VIEWPORT) {
		return;
	}

	data = (ViewportData *)viewport->user_data;
	if (data) {
		data->render_enabled = enabled;
	}
}

void widget_viewport_set_offset(Widget *viewport, int offset_x, int offset_y)
{
	ViewportData *data;

	if (!viewport || viewport->type != WIDGET_TYPE_VIEWPORT) {
		return;
	}

	data = (ViewportData *)viewport->user_data;
	if (data) {
		data->map_offset_x = offset_x;
		data->map_offset_y = offset_y;
		widget_mark_dirty(viewport);
	}
}

Widget *widget_viewport_get_main(void)
{
	return main_viewport;
}

int widget_viewport_init(void)
{
	int x, y, width, height;
	Widget *root;

	printf("[VIEWPORT INIT] Starting viewport initialization...\n");

	// Already initialized?
	if (main_viewport) {
		printf("[VIEWPORT INIT] Already initialized, skipping\n");
		return 1;
	}

	// Get map bounds from DOT system
	x = dotx(DOT_MTL);
	y = doty(DOT_MTL);
	width = dotx(DOT_MBR) - x;
	height = doty(DOT_MBR) - y;

	printf("[VIEWPORT INIT] Creating viewport at (%d, %d) size %dx%d\n", x, y, width, height);

	// Create the viewport
	main_viewport = widget_viewport_create(x, y, width, height);
	if (!main_viewport) {
		printf("[VIEWPORT INIT] Failed to create viewport!\n");
		return 0;
	}

	// Set a meaningful name for state persistence
	widget_set_name(main_viewport, "main_viewport");

	// Enable window chrome for dragging/resizing
	// DEBUG: Enable titlebar to visually confirm viewport widget is being used
	main_viewport->has_titlebar = 1; // Show titlebar for debugging
	main_viewport->draggable = 1; // Allow moving the viewport
	main_viewport->resizable = 1; // Allow resizing
	snprintf(main_viewport->title, sizeof(main_viewport->title), "Game View");

	// Add to root widget
	root = widget_manager_get_root();
	if (root) {
		widget_add_child(root, main_viewport);
	}

	// Initialize coordinate system offsets
	viewport_update_stom_offsets(main_viewport);

	// Mark viewport system as active
	viewport_active = 1;

	printf("[VIEWPORT INIT] Viewport initialized successfully! viewport_active=%d visible=%d\n", viewport_active,
	    main_viewport->visible);

	return 1;
}

int widget_viewport_is_active(void)
{
	static int debug_count = 0;
	int result = viewport_active && main_viewport && main_viewport->visible;
	if (debug_count < 10) {
		printf("[VIEWPORT] is_active: viewport_active=%d main_viewport=%p visible=%d result=%d\n", viewport_active,
		    (void *)main_viewport, main_viewport ? main_viewport->visible : -1, result);
		debug_count++;
	}
	return result;
}

int widget_viewport_get_bounds(int *x1, int *y1, int *x2, int *y2)
{
	int screen_x, screen_y;

	if (!viewport_active || !main_viewport || !main_viewport->visible) {
		return 0;
	}

	widget_get_screen_position(main_viewport, &screen_x, &screen_y);

	if (x1) {
		*x1 = screen_x;
	}
	if (y1) {
		*y1 = screen_y;
	}
	if (x2) {
		*x2 = screen_x + main_viewport->width;
	}
	if (y2) {
		*y2 = screen_y + main_viewport->height;
	}

	return 1;
}

// =============================================================================
// Virtual Functions
// =============================================================================

static void viewport_render(Widget *self)
{
	ViewportData *data;
	int screen_x, screen_y;
	static int render_debug_count = 0;

	if (!self || !self->visible) {
		if (render_debug_count < 5) {
			printf("[VIEWPORT RENDER] early return: self=%p visible=%d\n", (void *)self, self ? self->visible : -1);
			render_debug_count++;
		}
		return;
	}

	data = (ViewportData *)self->user_data;
	if (!data || !data->render_enabled) {
		if (render_debug_count < 5) {
			printf("[VIEWPORT RENDER] early return: data=%p render_enabled=%d\n", (void *)data,
			    data ? data->render_enabled : -1);
			render_debug_count++;
		}
		return;
	}

	if (render_debug_count < 5) {
		printf("[VIEWPORT RENDER] rendering viewport at (%d, %d) size %dx%d\n", self->x, self->y, self->width,
		    self->height);
		render_debug_count++;
	}

	// Get screen position of this widget
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Save current map offsets
	data->saved_mapaddx = mapaddx;
	data->saved_mapaddy = mapaddy;

	// The game rendering uses mapaddx/mapaddy for positioning
	// We need to adjust these based on the viewport position and any custom offset
	// The default mapaddx/mapaddy are relative to (0, 40) which is DOT_MTL default
	// So we adjust for the difference between widget position and expected position
	mapaddx = data->saved_mapaddx + (screen_x - dotx(DOT_MTL)) + data->map_offset_x;
	mapaddy = data->saved_mapaddy + (screen_y - doty(DOT_MTL)) + data->map_offset_y;

	// Set up clipping to the viewport bounds
	render_push_clip();
	render_more_clip(screen_x, screen_y, screen_x + self->width, screen_y + self->height);

	// Render the game world
	display_game();

	// Restore clipping
	render_pop_clip();

	// Restore map offsets
	mapaddx = data->saved_mapaddx;
	mapaddy = data->saved_mapaddy;
}

static void viewport_update(Widget *self, int dt)
{
	ViewportData *data;
	int screen_x, screen_y;

	if (!self || !self->user_data) {
		return;
	}

	data = (ViewportData *)self->user_data;

	// Get current screen position
	widget_get_screen_position(self, &screen_x, &screen_y);

	// Check if position changed (e.g., widget was dragged)
	if (screen_x != data->last_screen_x || screen_y != data->last_screen_y) {
		// Update stom offsets to match new position
		viewport_update_stom_offsets(self);

		// Store new position
		data->last_screen_x = screen_x;
		data->last_screen_y = screen_y;
	}
}

static int viewport_on_mouse_down(Widget *self, int x, int y, int button)
{
	// Don't consume mouse events - let them pass through to the game layer
	// The game's existing mouse handling in gui_input.c handles map clicks
	return 0;
}

static int viewport_on_mouse_up(Widget *self, int x, int y, int button)
{
	// Don't consume mouse events
	return 0;
}

static int viewport_on_mouse_move(Widget *self, int x, int y)
{
	// Don't consume mouse move events
	return 0;
}

static void viewport_on_destroy(Widget *self)
{
	if (self == main_viewport) {
		main_viewport = NULL;
		viewport_active = 0;
		// Reset stom offsets when viewport is destroyed
		stom_off_x = 0;
		stom_off_y = 0;
	}
	// User data will be freed automatically by widget_destroy
}

static void viewport_on_resize(Widget *self, int old_width, int old_height)
{
	// Update coordinate conversion offsets when viewport is resized or moved
	viewport_update_stom_offsets(self);
}

/**
 * Update the screen-to-map coordinate conversion offsets based on viewport position.
 * This ensures mouse clicks on the game map are correctly translated to map coordinates.
 */
static void viewport_update_stom_offsets(Widget *self)
{
	int screen_x, screen_y;

	if (!self) {
		return;
	}

	widget_get_screen_position(self, &screen_x, &screen_y);

	// stom_off_x/y are subtracted from screen coordinates in stom()
	// They need to compensate for the viewport's position relative to DOT_MTL
	stom_off_x = screen_x - dotx(DOT_MTL);
	stom_off_y = screen_y - doty(DOT_MTL);
}
