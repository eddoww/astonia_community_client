/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/keybind_settings_ui.h"
#include "gui/ui_draw.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"

#define KS_WIDTH 340
#define KS_PAD   UI_PAD
#define KS_ROW   UI_ROW_H_DENSE
#define KS_SEP   4

#define COL_REMOVE  IRGB(28, 14, 12)
#define COL_CAPTURE IRGB(28, 26, 16)
#define COL_WARN    IRGB(28, 12, 10)

static int ks_open;
static int ks_scroll;
static int ks_capture_idx;
static char ks_warn[80];
static uint32_t ks_warn_time;
static char ks_info[48];
static uint32_t ks_info_time;

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

	int content_h = ks_ph - UI_TITLE_H - UI_PAD_TIGHT - KS_PAD - KS_SEP - KS_ROW - 2;
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

	ui_panel(ks_px, ks_py, ks_px + ks_pw, ks_py + ks_ph);

	int cx = ks_px + ks_pw / 2;
	int lx = ks_px + KS_PAD;
	int rx = ks_px + ks_pw - KS_PAD;

	ui_titlebar(ks_px, ks_py, ks_px + ks_pw, "Keybindings");
	ui_button(ks_px + ks_pw - KS_PAD - 12, ks_py + 2, 12, 12, "X",
	    in_rect(mousex, mousey, ks_px + ks_pw - KS_PAD - 12, ks_py + 2, 12, 12) ? UI_BTN_HOVER : UI_BTN_REST);

	/* preset buttons double as "reset to defaults" - hover highlight and a
	 * confirmation line give them the button feedback players asked for */
	int btn_y = ks_py + UI_TITLE_H + UI_PAD_TIGHT;
	int hov_m = in_rect(mousex, mousey, lx, btn_y, 105, KS_ROW);
	int hov_l = in_rect(mousex, mousey, lx + 110, btn_y, 105, KS_ROW);
	if (hov_m) {
		ui_row_hover(lx - 2, btn_y - 2, lx + 105, KS_ROW);
	}
	if (hov_l) {
		ui_row_hover(lx + 108, btn_y - 2, lx + 215, KS_ROW);
	}
	render_text(lx, btn_y, hov_m ? UI_TEXT : UI_TEXT_LABEL, UI_FONT_BODY, "[Modern Defaults]");
	render_text(lx + 110, btn_y, hov_l ? UI_TEXT : UI_TEXT_LABEL, UI_FONT_BODY, "[Legacy Defaults]");
	if (ks_info[0] && tick - ks_info_time < (uint32_t)(TICKS * 3)) {
		render_text(rx, btn_y, COL_CAPTURE, UI_FONT_RIGHT, ks_info);
	}
	render_rect_alpha(lx, btn_y + KS_ROW + 2, rx, btn_y + KS_ROW + 3, UI_BORDER, UI_A_RULE);

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
				ui_section_header(lx, ry, rx - lx, input_category_name(b->category));
			}
			row++;
			last_cat = (int)b->category;
		}

		if (row >= ks_scroll && row < ks_scroll + ks_visible_rows) {
			int ry = content_y + (row - ks_scroll) * KS_ROW;
			int binding_idx = i;

			if (ks_capture_idx == binding_idx) {
				render_rect_alpha(lx, ry - 1, rx, ry + KS_ROW - 1, COL_CAPTURE, 40);
				render_text(lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, b->display_name);
				render_text(lx + 110, ry, COL_CAPTURE, UI_FONT_BODY, "Press a key...");
			} else {
				int hov = in_rect(mousex, mousey, lx, ry, ks_pw - KS_PAD * 2, KS_ROW);
				if (hov) {
					ui_row_hover(lx - 2, ry - 1, rx + 2, KS_ROW - 1);
				}
				render_text(lx, ry, hov ? UI_TEXT : UI_TEXT_LABEL, UI_FONT_BODY, b->display_name);
				const char *kstr = input_key_to_string(b->key, b->modifiers);
				render_text(rx - 90, ry, UI_TEXT, UI_FONT_BODY, kstr);
				render_text(rx - 20, ry, hov ? UI_TEXT : UI_TEXT_LABEL, UI_FONT_BODY, "[R]");
				if (b->key != b->default_key || b->modifiers != b->default_modifiers) {
					render_text(rx - 38, ry, COL_REMOVE, UI_FONT_BODY, "[X]");
				}
			}
		}
		row++;
	}

	if (ks_warn[0] && tick - ks_warn_time < (uint32_t)(TICKS * 3)) {
		render_text(cx, ks_py + ks_ph + 2, COL_WARN, UI_FONT_CENTER, ks_warn);
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

	if (in_rect(mx, my, ks_px + ks_pw - KS_PAD - 12, ks_py + 2, 12, 12)) {
		keybind_settings_close();
		return 1;
	}

	int lx = ks_px + KS_PAD;
	int rx = ks_px + ks_pw - KS_PAD;
	/* must mirror the layout in keybind_settings_display exactly: the rows
	 * start below the Modern/Legacy button row */
	int btn_y = ks_py + UI_TITLE_H + UI_PAD_TIGHT;
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

	if (in_rect(mx, my, lx, btn_y, 105, KS_ROW)) {
		input_load_modern_defaults();
		save_options();
		snprintf(ks_info, sizeof(ks_info), "Modern defaults applied");
		ks_info_time = tick;
		return 1;
	}
	if (in_rect(mx, my, lx + 110, btn_y, 105, KS_ROW)) {
		input_load_legacy_defaults();
		save_options();
		snprintf(ks_info, sizeof(ks_info), "Legacy defaults applied");
		ks_info_time = tick;
		return 1;
	}

	return 1;
}
