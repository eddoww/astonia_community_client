/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/keybind_settings_ui.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"

#define KS_WIDTH   340
#define KS_PAD     8
#define KS_ROW     14
#define KS_BTN_W   16
#define KS_TITLE_H 16
#define KS_SEP     4

#define COL_TITLE   IRGB(28, 26, 22)
#define COL_LABEL   IRGB(22, 21, 19)
#define COL_VALUE   IRGB(29, 28, 26)
#define COL_BUTTON  IRGB(26, 24, 20)
#define COL_REMOVE  IRGB(28, 14, 12)
#define COL_CAPTURE IRGB(28, 26, 16)
#define COL_HEADER  IRGB(16, 15, 14)
#define COL_WARN    IRGB(28, 12, 10)

static int ks_open;
static int ks_scroll;
static int ks_capture_idx;
static char ks_warn[80];
static uint32_t ks_warn_time;

static int ks_px, ks_py, ks_pw, ks_ph;
static int ks_visible_rows;
static int ks_total_rows;

static int in_rect(int mx, int my, int x, int y, int w, int h)
{
	return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void ks_compute_layout(void)
{
	int map_top = doty(DOT_MTL) + 10;
	int map_bot = doty(DOT_HOTBAR) - 10;
	int map_lx = dotx(DOT_MTL);
	int map_rx = dotx(DOT_MBR);
	int avail_h = map_bot - map_top;
	int avail_w = map_rx - map_lx;

	ks_pw = KS_WIDTH;
	if (ks_pw > avail_w - 20) {
		ks_pw = avail_w - 20;
	}
	ks_ph = avail_h;

	ks_px = map_lx + (avail_w - ks_pw) / 2;
	ks_py = map_top;

	int content_h = ks_ph - KS_TITLE_H - KS_PAD * 2 - KS_SEP - KS_ROW - 2;
	ks_visible_rows = content_h / KS_ROW;
	if (ks_visible_rows < 1) {
		ks_visible_rows = 1;
	}
}

static int ks_count_rows(void)
{
	int count = input_binding_count();
	int rows = 0;
	int last_cat = -1;

	for (int i = 0; i < count; i++) {
		InputBinding *b = input_binding_at(i);
		if (!b || !b->rebindable) {
			continue;
		}
		if ((int)(int)b->category != last_cat) {
			rows++;
			last_cat = (int)b->category;
		}
		rows++;
	}
	return rows;
}

void keybind_settings_display(void)
{
	if (!ks_open) {
		return;
	}

	ks_compute_layout();
	ks_total_rows = ks_count_rows();

	int max_scroll = ks_total_rows - ks_visible_rows;
	if (max_scroll < 0) {
		max_scroll = 0;
	}
	if (ks_scroll > max_scroll) {
		ks_scroll = max_scroll;
	}
	if (ks_scroll < 0) {
		ks_scroll = 0;
	}

	render_shaded_rect(ks_px, ks_py, ks_px + ks_pw, ks_py + ks_ph, 0, 200);

	int cx = ks_px + ks_pw / 2;
	int lx = ks_px + KS_PAD;
	int rx = ks_px + ks_pw - KS_PAD;

	render_text(
	    cx, ks_py + KS_PAD, COL_TITLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, "Keybindings");

	int btn_y = ks_py + KS_TITLE_H + KS_PAD;
	render_text(lx, btn_y, COL_BUTTON, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[Modern]");
	render_text(lx + 60, btn_y, COL_BUTTON, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[Legacy]");
	render_rect_alpha(lx, btn_y + KS_ROW + 2, rx, btn_y + KS_ROW + 3, COL_HEADER, 100);

	int content_y = btn_y + KS_ROW + KS_SEP + 2;
	int count = input_binding_count();
	int row = 0;
	int last_cat = -1;

	for (int i = 0; i < count; i++) {
		InputBinding *b = input_binding_at(i);
		if (!b || !b->rebindable) {
			continue;
		}

		if ((int)b->category != last_cat) {
			if (row >= ks_scroll && row < ks_scroll + ks_visible_rows) {
				int ry = content_y + (row - ks_scroll) * KS_ROW;
				if (row > 0) {
					render_rect_alpha(lx, ry - 1, rx, ry, COL_HEADER, 100);
				}
				render_text(
				    lx, ry, COL_HEADER, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, input_category_name(b->category));
			}
			row++;
			last_cat = (int)b->category;
		}

		if (row >= ks_scroll && row < ks_scroll + ks_visible_rows) {
			int ry = content_y + (row - ks_scroll) * KS_ROW;
			int binding_idx = i;

			if (ks_capture_idx == binding_idx) {
				render_rect_alpha(lx, ry - 1, rx, ry + KS_ROW - 1, COL_CAPTURE, 40);
				render_text(lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, b->display_name);
				render_text(lx + 110, ry, COL_CAPTURE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Press a key...");
			} else {
				render_text(lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, b->display_name);
				const char *kstr = input_key_to_string(b->key, b->modifiers);
				render_text(rx - 90, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, kstr);
				render_text(rx - 20, ry, COL_BUTTON, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[R]");
				if (b->key != b->default_key || b->modifiers != b->default_modifiers) {
					render_text(rx - 38, ry, COL_REMOVE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "[X]");
				}
			}
		}
		row++;
	}

	if (ks_warn[0] && tick - ks_warn_time < (uint32_t)(TICKS * 3)) {
		render_text(
		    cx, ks_py + ks_ph + 2, COL_WARN, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, ks_warn);
	}
}

int keybind_settings_is_open(void)
{
	return ks_open;
}

void keybind_settings_toggle(void)
{
	if (ks_open) {
		keybind_settings_close();
	} else {
		ks_open = 1;
		ks_scroll = 0;
		ks_capture_idx = -1;
		ks_warn[0] = '\0';
	}
}

void keybind_settings_close(void)
{
	ks_open = 0;
	ks_capture_idx = -1;
}

int keybind_settings_capturing(void)
{
	return ks_open && ks_capture_idx >= 0;
}

void keybind_settings_accept_key(SDL_Keycode key, Uint8 mods)
{
	InputBinding *b = input_binding_at(ks_capture_idx);
	if (b) {
		InputBinding *conflict = input_find_conflict(key, mods, b->id);
		if (conflict) {
			snprintf(ks_warn, sizeof(ks_warn), "Unbound: %s", conflict->display_name);
			ks_warn_time = tick;
		} else {
			ks_warn[0] = '\0';
		}
		input_rebind(b->id, key, mods);
		save_options();
	}
	ks_capture_idx = -1;
}

void keybind_settings_cancel_capture(void)
{
	ks_capture_idx = -1;
}

int keybind_settings_scroll(int delta)
{
	if (!ks_open) {
		return 0;
	}

	ks_compute_layout();
	ks_total_rows = ks_count_rows();

	int max_scroll = ks_total_rows - ks_visible_rows;
	if (max_scroll < 0) {
		max_scroll = 0;
	}

	ks_scroll += delta;
	if (ks_scroll < 0) {
		ks_scroll = 0;
	}
	if (ks_scroll > max_scroll) {
		ks_scroll = max_scroll;
	}
	return 1;
}

int keybind_settings_click(int mx, int my)
{
	if (!ks_open) {
		return 0;
	}

	ks_compute_layout();

	if (!in_rect(mx, my, ks_px, ks_py, ks_pw, ks_ph)) {
		return 0;
	}

	int lx = ks_px + KS_PAD;
	int rx = ks_px + ks_pw - KS_PAD;
	int content_y = ks_py + KS_TITLE_H + KS_PAD + KS_SEP;

	int count = input_binding_count();
	int row = 0;
	int last_cat = -1;

	for (int i = 0; i < count; i++) {
		InputBinding *b = input_binding_at(i);
		if (!b || !b->rebindable) {
			continue;
		}

		if ((int)b->category != last_cat) {
			row++;
			last_cat = (int)b->category;
		}

		if (row >= ks_scroll && row < ks_scroll + ks_visible_rows) {
			int ry = content_y + (row - ks_scroll) * KS_ROW;

			if (in_rect(mx, my, lx, ry, ks_pw - KS_PAD * 2, KS_ROW)) {
				if (in_rect(mx, my, rx - 20, ry, 20, KS_ROW)) {
					ks_capture_idx = i;
					return 1;
				}
				if ((b->key != b->default_key || b->modifiers != b->default_modifiers) &&
				    in_rect(mx, my, rx - 38, ry, 18, KS_ROW)) {
					input_reset_one(b->id);
					save_options();
					return 1;
				}
			}
		}
		row++;
	}

	int btn_y = ks_py + KS_TITLE_H + KS_PAD;
	if (in_rect(mx, my, lx, btn_y, 55, KS_ROW)) {
		input_load_modern_defaults();
		save_options();
		return 1;
	}
	if (in_rect(mx, my, lx + 60, btn_y, 55, KS_ROW)) {
		input_load_legacy_defaults();
		save_options();
		return 1;
	}

	return 1;
}
