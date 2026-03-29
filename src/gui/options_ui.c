/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/options_ui.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "sdl/gamepad.h"

#define OPT_WIDTH      340
#define OPT_PAD        8
#define OPT_ROW        18
#define OPT_TITLE_H    16
#define OPT_TAB_H      16
#define OPT_SEP        4
#define OPT_NTABS      6
#define OPT_SLIDER_LBL 90
#define OPT_SLIDER_VAL 28

#define COL_TITLE     IRGB(28, 26, 22)
#define COL_LABEL     IRGB(22, 21, 19)
#define COL_VALUE     IRGB(29, 28, 26)
#define COL_HEADER    IRGB(16, 15, 14)
#define COL_ACTIVE    IRGB(12, 11, 9)
#define COL_INACTIVE  IRGB(5, 5, 4)
#define COL_CHECK     IRGB(20, 22, 14)
#define COL_SLIDER_BG IRGB(6, 6, 5)
#define COL_SLIDER_FG IRGB(22, 20, 16)

static int opt_open;
static int opt_tab;
static int opt_scroll;

static int opt_px, opt_py, opt_pw, opt_ph;
static int opt_lx, opt_rx, opt_content_w;
static int opt_tab_bar_y, opt_content_y;
static int opt_visible_rows;

static int in_rect(int mx, int my, int x, int y, int w, int h)
{
	return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void opt_compute_layout(void)
{
	int map_top = doty(DOT_MTL) + 10;
	int map_bot = doty(DOT_HOTBAR) - 10;
	int map_lx = dotx(DOT_MTL);
	int map_rx = dotx(DOT_MBR);
	int avail_h = map_bot - map_top;
	int avail_w = map_rx - map_lx;

	opt_pw = OPT_WIDTH;
	if (opt_pw > avail_w - 20) {
		opt_pw = avail_w - 20;
	}
	opt_ph = avail_h;
	opt_px = map_lx + (avail_w - opt_pw) / 2;
	opt_py = map_top;

	opt_lx = opt_px + OPT_PAD;
	opt_rx = opt_px + opt_pw - OPT_PAD;
	opt_content_w = opt_pw - OPT_PAD * 2;

	opt_tab_bar_y = opt_py + OPT_TITLE_H + OPT_PAD;
	opt_content_y = opt_tab_bar_y + OPT_TAB_H + OPT_SEP;

	int content_h = opt_ph - (opt_content_y - opt_py);
	opt_visible_rows = content_h / OPT_ROW;
	if (opt_visible_rows < 1) {
		opt_visible_rows = 1;
	}
}

static int opt_row_y(int row)
{
	if (row < opt_scroll || row >= opt_scroll + opt_visible_rows) {
		return -1;
	}
	return opt_content_y + (row - opt_scroll) * OPT_ROW;
}

extern SDL_Window *sdlwnd;

static int opt_tab_total(void)
{
	switch (opt_tab) {
	case 0:
		return 6;
	case 1:
		return 4;
	case 2:
		return 5;
	case 3:
		return 11;
	case 4:
		return 10;
	case 5:
		return 20;
	default:
		return 0;
	}
}

static void draw_checkbox(int x, int y, int checked, const char *label)
{
	render_rect_alpha(x, y + 1, x + 12, y + 13, COL_INACTIVE, 220);
	if (checked) {
		render_rect_alpha(x + 2, y + 3, x + 10, y + 11, COL_CHECK, 240);
	}
	render_text(x + 16, y, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, label);
}

static void draw_slider(int x, int y, int w, int value, int min_val, int max_val, const char *label)
{
	char buf[16];
	int tx = x + OPT_SLIDER_LBL;
	int tw = w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;

	render_text(x, y, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, label);
	if (tw < 10) {
		tw = 10;
	}
	render_rect_alpha(tx, y + 4, tx + tw, y + 12, COL_SLIDER_BG, 220);
	if (max_val > min_val) {
		int filled = (value - min_val) * tw / (max_val - min_val);
		if (filled > 0) {
			render_rect_alpha(tx, y + 4, tx + filled, y + 12, COL_SLIDER_FG, 220);
		}
	}
	snprintf(buf, sizeof(buf), "%d", value);
	render_text(tx + tw + 4, y, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, buf);
}

static int draw_tab(int x, int y, int w, int h, const char *label, int active)
{
	unsigned short bg = (unsigned short)(active ? COL_ACTIVE : COL_INACTIVE);
	unsigned short text_col = (unsigned short)(active ? COL_VALUE : COL_LABEL);

	if (active) {
		render_rect_alpha(x, y, x + w, y + h, bg, 220);
	} else {
		render_rect_alpha(x, y, x + w, y + h, bg, 160);
	}
	render_text(x + w / 2, y + 2, text_col, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, label);
	return 0;
}

static void draw_section_header(int x, int y, int w, const char *label)
{
	render_rect_alpha(x, y + 7, x + w, y + 8, COL_HEADER, 150);
	render_text(x, y, COL_HEADER, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, label);
}

static void opt_display_audio(void)
{
	int ry;

	ry = opt_row_y(0);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Volume");
	}

	ry = opt_row_y(1);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, sound_volume, 0, 128, "Master");
	}

	ry = opt_row_y(2);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, sound_volume_sfx, 0, 128, "Sound Effects");
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, sound_volume_ambient, 0, 128, "Ambient");
	}

	ry = opt_row_y(4);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, sound_volume_ui, 0, 128, "Interface");
	}

	ry = opt_row_y(5);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_SOUND) != 0, "Sound Enabled");
	}
}

static int opt_click_audio(int mx, int my)
{
	int ry;
	int tx = opt_lx + OPT_SLIDER_LBL;
	int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int val;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 0 + (mx - tx) * (128 - 0) / tw;
		if (val < 0) {
			val = 0;
		}
		if (val > 128) {
			val = 128;
		}
		sound_volume = val;
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 0 + (mx - tx) * (128 - 0) / tw;
		if (val < 0) {
			val = 0;
		}
		if (val > 128) {
			val = 128;
		}
		sound_volume_sfx = val;
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 0 + (mx - tx) * (128 - 0) / tw;
		if (val < 0) {
			val = 0;
		}
		if (val > 128) {
			val = 128;
		}
		sound_volume_ambient = val;
		save_options();
		return 1;
	}

	ry = opt_row_y(4);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 0 + (mx - tx) * (128 - 0) / tw;
		if (val < 0) {
			val = 0;
		}
		if (val > 128) {
			val = 128;
		}
		sound_volume_ui = val;
		save_options();
		return 1;
	}

	ry = opt_row_y(5);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_SOUND;
		save_options();
		return 1;
	}

	return 0;
}

static int opt_video_mode(void)
{
	int is_fullscreen = (SDL_GetWindowFlags(sdlwnd) & SDL_WINDOW_FULLSCREEN) != 0;
	int is_exclusive = (game_options & GO_FULL) != 0;

	if (is_fullscreen && is_exclusive) {
		return 2;
	}
	if (is_fullscreen) {
		return 1;
	}
	return 0;
}

static void opt_display_video(void)
{
	int ry;

	ry = opt_row_y(0);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Display");
	}

	ry = opt_row_y(1);
	if (ry >= 0) {
		const char *modes[] = {"Windowed", "Borderless", "Exclusive"};
		int mode = opt_video_mode();
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Window Mode:");
		render_text(opt_lx + 100, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, modes[mode]);
	}

	ry = opt_row_y(2);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, sdl_vsync != 0, "VSync");
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, frames_per_second, 24, 244, "FPS Limit");
	}
}

static int opt_click_video(int mx, int my)
{
	int ry;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		int mode = (opt_video_mode() + 1) % 3;
		switch (mode) {
		case 0:
			SDL_SetWindowFullscreen(sdlwnd, false);
			game_options &= ~GO_FULL;
			break;
		case 1:
			SDL_SetWindowFullscreenMode(sdlwnd, NULL);
			SDL_SetWindowFullscreen(sdlwnd, true);
			game_options &= ~GO_FULL;
			break;
		case 2:
			game_options |= GO_FULL;
			SDL_SetWindowFullscreen(sdlwnd, true);
			break;
		}
		SDL_SyncWindow(sdlwnd);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		sdl_set_vsync(!sdl_vsync);
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		int tx = opt_lx + OPT_SLIDER_LBL;
		int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
		int val = 24 + (mx - tx) * (244 - 24) / tw;
		if (val < 24) {
			val = 24;
		}
		if (val > 244) {
			val = 244;
		}
		frames_per_second = val;
		save_options();
		return 1;
	}

	return 0;
}

static void opt_display_display(void)
{
	int ry;

	ry = opt_row_y(0);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Graphics");
	}

	ry = opt_row_y(1);
	if (ry >= 0) {
		const char *bnames[] = {"Normal", "Bright", "Brighter"};
		int bv = 0;
		if ((game_options & GO_LIGHTER) && (game_options & GO_LIGHTER2)) {
			bv = 2;
		} else if (game_options & GO_LIGHTER) {
			bv = 1;
		}
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Brightness:");
		render_text(opt_lx + 100, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, bnames[bv]);
	}

	ry = opt_row_y(2);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_LOWLIGHT) != 0, "Simplified Lighting");
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_LARGE) != 0, "Large Font");
	}

	ry = opt_row_y(4);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_DARK) != 0, "Dark Theme");
	}
}

static int opt_click_display(int mx, int my)
{
	int ry;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		int bv = 0;
		if ((game_options & GO_LIGHTER) && (game_options & GO_LIGHTER2)) {
			bv = 2;
		} else if (game_options & GO_LIGHTER) {
			bv = 1;
		}
		bv = (bv + 1) % 3;
		game_options &= ~(GO_LIGHTER | GO_LIGHTER2);
		if (bv == 1) {
			game_options |= GO_LIGHTER;
		} else if (bv == 2) {
			game_options |= GO_LIGHTER | GO_LIGHTER2;
		}
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_LOWLIGHT;
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_LARGE;
		save_options();
		return 1;
	}

	ry = opt_row_y(4);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_DARK;
		save_options();
		return 1;
	}

	return 0;
}

static void opt_display_ui(void)
{
	int ry;

	ry = opt_row_y(0);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Interface");
	}

	ry = opt_row_y(1);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_BIGBAR) != 0, "Big Health Bar");
	}

	ry = opt_row_y(2);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_SMALLTOP) != 0, "Small Top Window");
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_SMALLBOT) != 0, "Small Bottom Window");
	}

	ry = opt_row_y(4);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, !(game_options & GO_NOMAP), "Show Minimap");
	}

	ry = opt_row_y(5);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Hotbar");
	}

	ry = opt_row_y(6);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, hotbar_rows(), 1, 3, "Hotbar Rows");
	}

	ry = opt_row_y(7);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, hotbar_visible_slots(), 1, 15, "Visible Slots");
	}

	ry = opt_row_y(8);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, hotbar_show_hotkeys(), "Show Hotkey Labels");
	}

	ry = opt_row_y(9);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, hotbar_show_names(), "Show Slot Names");
	}

	ry = opt_row_y(10);
	if (ry >= 0) {
		const char *cast_names[] = {"Normal", "Quick Cast", "Quick Cast w/ Indicator"};
		int cm = hotbar_cast_mode();
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Cast Mode:");
		render_text(opt_lx + 100, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, cast_names[cm]);
	}
}

static int opt_click_ui(int mx, int my)
{
	int ry;
	int tx = opt_lx + OPT_SLIDER_LBL;
	int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int val;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_BIGBAR;
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_SMALLTOP;
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_SMALLBOT;
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(4);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_NOMAP;
		save_options();
		return 1;
	}

	ry = opt_row_y(6);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 1 + (mx - tx) * (3 - 1) / tw;
		if (val < 1) {
			val = 1;
		}
		if (val > 3) {
			val = 3;
		}
		hotbar_set_rows(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(7);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 1 + (mx - tx) * (15 - 1) / tw;
		if (val < 1) {
			val = 1;
		}
		if (val > 15) {
			val = 15;
		}
		hotbar_set_visible_slots(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(8);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		hotbar_set_show_hotkeys(!hotbar_show_hotkeys());
		save_options();
		return 1;
	}

	ry = opt_row_y(9);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		hotbar_set_show_names(!hotbar_show_names());
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(10);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		hotbar_set_cast_mode((hotbar_cast_mode() + 1) % 3);
		save_options();
		return 1;
	}

	return 0;
}

static void opt_display_advanced(void)
{
	int ry;

	ry = opt_row_y(0);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Input");
	}

	ry = opt_row_y(1);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_PREDICT) != 0, "Predictive Input");
	}

	ry = opt_row_y(2);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_SHORT) != 0, "Reduced Input Latency");
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Performance");
	}

	ry = opt_row_y(4);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, sdl_cache_size, 1000, 8000, "Texture Cache");
	}

	ry = opt_row_y(5);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, sdl_multi, 1, 8, "Worker Threads");
	}

	ry = opt_row_y(6);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Data");
	}

	ry = opt_row_y(7);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_MAPSAVE) != 0, "Save Minimap Data");
	}

	ry = opt_row_y(8);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_CONTEXT) != 0, "Context Menu");
	}

	ry = opt_row_y(9);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_WHEELSPEED) != 0, "Scroll Wheel Speed");
	}
}

static int opt_click_advanced(int mx, int my)
{
	int ry;
	int tx = opt_lx + OPT_SLIDER_LBL;
	int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int val;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_PREDICT;
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_SHORT;
		save_options();
		return 1;
	}

	ry = opt_row_y(4);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 1000 + (mx - tx) * (8000 - 1000) / tw;
		if (val < 1000) {
			val = 1000;
		}
		if (val > 8000) {
			val = 8000;
		}
		sdl_cache_size = val;
		save_options();
		return 1;
	}

	ry = opt_row_y(5);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		val = 1 + (mx - tx) * (8 - 1) / tw;
		if (val < 1) {
			val = 1;
		}
		if (val > 8) {
			val = 8;
		}
		sdl_multi = val;
		save_options();
		return 1;
	}

	ry = opt_row_y(7);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_MAPSAVE;
		save_options();
		return 1;
	}

	ry = opt_row_y(8);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_CONTEXT;
		save_options();
		return 1;
	}

	ry = opt_row_y(9);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, OPT_ROW)) {
		game_options ^= GO_WHEELSPEED;
		save_options();
		return 1;
	}

	return 0;
}

static void opt_display_gamepad(void)
{
	int ry;
	int connected = gamepad_is_connected();

	ry = opt_row_y(0);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Controller");
	}
	ry = opt_row_y(1);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED,
		    connected ? "Status: Connected" : "Status: Not Connected");
	}

	if (!connected) {
		ry = opt_row_y(3);
		if (ry >= 0) {
			render_text(
			    opt_lx, ry, COL_HEADER, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Connect a controller to configure.");
		}
		return;
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "LT (Left Trigger) + Button");
	}
	ry = opt_row_y(4);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "A/B/X/Y:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Hotbar 1-4");
	}
	ry = opt_row_y(5);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "D-Pad:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Hotbar 5-8");
	}

	ry = opt_row_y(7);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "RT (Right Trigger) + Button");
	}
	ry = opt_row_y(8);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "A/B/X/Y:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Hotbar 9-12");
	}
	ry = opt_row_y(9);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "D-Pad:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Hotbar 13-16");
	}

	ry = opt_row_y(11);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Sticks & Buttons");
	}
	ry = opt_row_y(12);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Left Stick:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Move");
	}
	ry = opt_row_y(13);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Right Stick:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Cursor");
	}
	ry = opt_row_y(14);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "R3 (R-Click):");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Left Click");
	}
	ry = opt_row_y(15);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "L3 (L-Click):");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Right Click");
	}
	ry = opt_row_y(16);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Start:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Menu");
	}
	ry = opt_row_y(17);
	if (ry >= 0) {
		render_text(opt_lx, ry, COL_LABEL, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Back/Select:");
		render_text(opt_lx + 80, ry, COL_VALUE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, "Cancel All");
	}

	ry = opt_row_y(19);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "LT+RT + Button = Hotbar 17-24");
	}
}

static int opt_click_gamepad(int mx, int my)
{
	(void)mx;
	(void)my;
	return 0;
}

void options_display(void)
{
	int total, max_scroll, cx, tab_w, i;
	static const char *tab_labels[OPT_NTABS] = {"Audio", "Video", "Display", "UI", "Advanced", "Gamepad"};

	if (!opt_open) {
		return;
	}

	opt_compute_layout();

	total = opt_tab_total();
	max_scroll = total - opt_visible_rows;
	if (max_scroll < 0) {
		max_scroll = 0;
	}
	if (opt_scroll > max_scroll) {
		opt_scroll = max_scroll;
	}
	if (opt_scroll < 0) {
		opt_scroll = 0;
	}

	render_shaded_rect(opt_px, opt_py, opt_px + opt_pw, opt_py + opt_ph, 0, 200);

	cx = opt_px + opt_pw / 2;
	render_text(
	    cx, opt_py + OPT_PAD, COL_TITLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, "Options");

	tab_w = opt_pw / OPT_NTABS;
	for (i = 0; i < OPT_NTABS; i++) {
		draw_tab(opt_px + i * tab_w, opt_tab_bar_y, tab_w, OPT_TAB_H, tab_labels[i], opt_tab == i);
	}

	render_rect_alpha(opt_lx, opt_tab_bar_y + OPT_TAB_H, opt_rx, opt_tab_bar_y + OPT_TAB_H + 1, COL_HEADER, 100);

	switch (opt_tab) {
	case 0:
		opt_display_audio();
		break;
	case 1:
		opt_display_video();
		break;
	case 2:
		opt_display_display();
		break;
	case 3:
		opt_display_ui();
		break;
	case 4:
		opt_display_advanced();
		break;
	case 5:
		opt_display_gamepad();
		break;
	default:
		break;
	}
}

int options_is_open(void)
{
	return opt_open;
}

void options_open(void)
{
	opt_open = 1;
	opt_tab = 0;
	opt_scroll = 0;
}

void options_close(void)
{
	opt_open = 0;
}

int options_scroll(int delta)
{
	int total, max_scroll;

	if (!opt_open) {
		return 0;
	}

	opt_compute_layout();
	total = opt_tab_total();
	max_scroll = total - opt_visible_rows;
	if (max_scroll < 0) {
		max_scroll = 0;
	}

	opt_scroll += delta;
	if (opt_scroll < 0) {
		opt_scroll = 0;
	}
	if (opt_scroll > max_scroll) {
		opt_scroll = max_scroll;
	}

	return 1;
}

int options_click(int mx, int my)
{
	int tab_w, new_tab;

	if (!opt_open) {
		return 0;
	}

	opt_compute_layout();

	if (!in_rect(mx, my, opt_px, opt_py, opt_pw, opt_ph)) {
		return 0;
	}

	if (in_rect(mx, my, opt_px, opt_tab_bar_y, opt_pw, OPT_TAB_H)) {
		tab_w = opt_pw / OPT_NTABS;
		new_tab = (mx - opt_px) / tab_w;
		if (new_tab >= 0 && new_tab < OPT_NTABS) {
			opt_tab = new_tab;
			opt_scroll = 0;
		}
		return 1;
	}

	switch (opt_tab) {
	case 0:
		opt_click_audio(mx, my);
		break;
	case 1:
		opt_click_video(mx, my);
		break;
	case 2:
		opt_click_display(mx, my);
		break;
	case 3:
		opt_click_ui(mx, my);
		break;
	case 4:
		opt_click_advanced(mx, my);
		break;
	case 5:
		opt_click_gamepad(mx, my);
		break;
	default:
		break;
	}

	return 1;
}
