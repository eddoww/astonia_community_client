/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/options_ui.h"
#include "gui/panels.h"
#include "gui/ui_draw.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "sdl/sdl_gpu.h"
#include "sdl/font_manager.h"
#include "sdl/gamepad.h"
#include "modder/modder.h"
#include "amod/amod_options.h"

#define OPT_WIDTH      360
#define OPT_TITLE_H    16
#define OPT_TAB_H      16
#define OPT_SEP        4
#define OPT_NTABS      7
#define OPT_SLIDER_LBL 90
#define OPT_SLIDER_VAL 28

#define COL_SLIDER_FG IRGB(22, 20, 16)
#define COL_KNOB      IRGB(30, 28, 24)

static int opt_open;
static int opt_tab;
static int opt_scroll;

extern int ui_scale_pct; /* sdl_core.c - UI layer scale, percent */

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

	opt_lx = opt_px + UI_PAD;
	opt_rx = opt_px + opt_pw - UI_PAD;
	opt_content_w = opt_pw - UI_PAD * 2;

	opt_tab_bar_y = opt_py + OPT_TITLE_H + UI_PAD;
	opt_content_y = opt_tab_bar_y + OPT_TAB_H + OPT_SEP;

	int content_h = opt_ph - (opt_content_y - opt_py);
	opt_visible_rows = content_h / UI_ROW_H;
	if (opt_visible_rows < 1) {
		opt_visible_rows = 1;
	}
}

static int opt_row_y(int row)
{
	if (row < opt_scroll || row >= opt_scroll + opt_visible_rows) {
		return -1;
	}
	return opt_content_y + (row - opt_scroll) * UI_ROW_H;
}

extern SDL_Window *sdlwnd;

/* Native row counts per tab; mod rows tagged for the tab are appended below
 * them (see amod_option_tab). The Gameplay tab always shows its native Combat
 * rows, mod or not. */
#define OPT_AUDIO_NATIVE 6
/* rows 0..9 classic, 10..15 window sizes, 16..19+MAX_PANEL panels
 * (16 header, 17 lock, 18 minimize direction, 19.. per-panel toggles, then
 * the reset row) */
#define OPT_UI_PANEL_ROW0   19
#define OPT_UI_NATIVE       (OPT_UI_PANEL_ROW0 + MAX_PANEL + 1)
#define OPT_GAMEPLAY_NATIVE 2

#define OPT_MAX_MOD_ROWS 64

/* Collect the mod option indices shown in the given AMOD_TAB_* tab. */
static int opt_mod_rows(int amod_tab, int *map)
{
	int n = amod_options_count();
	int cnt = 0;

	for (int i = 0; i < n && cnt < OPT_MAX_MOD_ROWS; i++) {
		if (amod_option_tab(i) == amod_tab) {
			map[cnt++] = i;
		}
	}
	return cnt;
}

static int opt_mod_row_count(int amod_tab)
{
	int map[OPT_MAX_MOD_ROWS];
	return opt_mod_rows(amod_tab, map);
}

static int opt_tab_total(void)
{
	switch (opt_tab) {
	case 0:
		return OPT_AUDIO_NATIVE + opt_mod_row_count(AMOD_TAB_AUDIO);
	case 1:
		return 4;
	case 2:
		return 11;
	case 3:
		return OPT_UI_NATIVE + opt_mod_row_count(AMOD_TAB_UI);
	case 4:
		return OPT_GAMEPLAY_NATIVE + opt_mod_row_count(AMOD_TAB_GAMEPLAY);
	case 5:
		return 10;
	case 6:
		return 20;
	default:
		return 0;
	}
}

/* Soft highlight behind the row under the mouse cursor */
static int draw_row_hover(int y)
{
	if (in_rect(mousex, mousey, opt_lx - 4, y - 1, opt_content_w + 8, UI_ROW_H)) {
		ui_row_hover(opt_lx - 4, y - 1, opt_rx + 4, UI_ROW_H - 1);
		return 1;
	}
	return 0;
}

static void draw_checkbox(int x, int y, int checked, const char *label)
{
	int hov = draw_row_hover(y);

	/* box */
	render_rounded_rect_filled_alpha(x, y + 1, x + 13, y + 14, 2, UI_BG_SUNKEN, 230);
	render_rounded_rect_alpha(x, y + 1, x + 13, y + 14, 2, hov ? UI_ACCENT : UI_BORDER, hov ? 220 : 160);
	if (checked) {
		/* check mark: two strokes, drawn twice for weight */
		render_line_alpha(x + 3, y + 7, x + 6, y + 10, UI_ACCENT, 255);
		render_line_alpha(x + 3, y + 8, x + 6, y + 11, UI_ACCENT, 255);
		render_line_alpha(x + 6, y + 10, x + 10, y + 4, UI_ACCENT, 255);
		render_line_alpha(x + 6, y + 11, x + 10, y + 5, UI_ACCENT, 255);
	}
	render_text(x + 18, y, hov ? UI_TEXT : UI_TEXT_LABEL, UI_FONT_BODY, label);
}

static void draw_slider(int x, int y, int w, int value, int min_val, int max_val, const char *label)
{
	char buf[16];
	int tx = x + OPT_SLIDER_LBL;
	int tw = w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int hov = draw_row_hover(y);
	int filled = 0;

	render_text(x, y, hov ? UI_TEXT : UI_TEXT_LABEL, UI_FONT_BODY, label);
	if (tw < 10) {
		tw = 10;
	}
	if (max_val > min_val) {
		filled = (value - min_val) * tw / (max_val - min_val);
		if (filled < 0) {
			filled = 0;
		}
		if (filled > tw) {
			filled = tw;
		}
	}
	/* track + filled part */
	render_rounded_rect_filled_alpha(tx, y + 5, tx + tw, y + 11, 3, UI_BG_SUNKEN, 230);
	if (filled > 0) {
		render_rounded_rect_filled_alpha(tx, y + 5, tx + filled, y + 11, 3, COL_SLIDER_FG, 230);
	}
	render_rounded_rect_alpha(tx, y + 5, tx + tw, y + 11, 3, UI_BORDER, 120);
	/* knob */
	render_circle_filled_alpha(tx + filled, y + 8, 5, hov ? UI_ACCENT : COL_KNOB, 255);
	render_circle_filled_alpha(tx + filled, y + 8, 2, UI_BG_SUNKEN, 200);
	snprintf(buf, sizeof(buf), "%d", value);
	render_text(tx + tw + OPT_SLIDER_VAL - render_text_length(RENDER_TEXT_SMALL, buf), y, UI_TEXT, UI_FONT_BODY, buf);
}

static int draw_tab(int x, int y, int w, int h, const char *label, int active)
{
	int hov = in_rect(mousex, mousey, x, y, w, h);
	unsigned short text_col = (unsigned short)(active ? UI_TEXT : (hov ? UI_TEXT : UI_TEXT_LABEL));

	if (active) {
		render_gradient_rect_v(x + 1, y, x + w - 1, y + h, UI_BG_ROW_ACTIVE, UI_BG_RAISED, 240);
		render_rect_alpha(x + 1, y + h - 2, x + w - 1, y + h, UI_ACCENT, 255);
	} else {
		render_rect_alpha(x + 1, y, x + w - 1, y + h, hov ? UI_BG_ROW_HOVER : UI_BG_INACTIVE, hov ? 200 : 140);
	}
	render_text(x + w / 2, y + 3, text_col, UI_FONT_CENTER, label);
	return 0;
}

static void draw_section_header(int x, int y, int w, const char *label)
{
	ui_section_header(x, y, w, label);
}

/* Thin scrollbar on the right edge when the tab has more rows than fit */
static void draw_scrollbar(int total)
{
	int track_y = opt_content_y;
	int track_h = opt_visible_rows * UI_ROW_H;
	int sx = opt_rx + 6;
	int thumb_h, thumb_y;

	if (total <= opt_visible_rows || track_h <= 0) {
		return;
	}
	render_rounded_rect_filled_alpha(sx, track_y, sx + 4, track_y + track_h, 2, UI_BG_SUNKEN, 200);
	thumb_h = track_h * opt_visible_rows / total;
	if (thumb_h < 12) {
		thumb_h = 12;
	}
	thumb_y = track_y + (track_h - thumb_h) * opt_scroll / (total - opt_visible_rows);
	render_rounded_rect_filled_alpha(sx, thumb_y, sx + 4, thumb_y + thumb_h, 2, UI_ACCENT, 220);
}

/* Draw one mod-provided option at the given row y. */
static void draw_mod_option_row(int ry, int idx)
{
	struct amod_option o;

	if (!amod_option_get(idx, &o)) {
		return;
	}
	o.label[sizeof(o.label) - 1] = 0;
	switch (o.type) {
	case AMOD_OPT_TOGGLE:
		draw_checkbox(opt_lx, ry, o.value != 0, o.label);
		break;
	case AMOD_OPT_SLIDER:
		draw_slider(opt_lx, ry, opt_content_w, o.value, o.min_val, o.max_val, o.label);
		break;
	default:
		draw_section_header(opt_lx, ry, opt_content_w, o.label);
		break;
	}
}

/* Handle a click that already hit the given mod option's row. */
static int click_mod_option_row(int mx, int idx)
{
	struct amod_option o;
	int tx = opt_lx + OPT_SLIDER_LBL;
	int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int val;

	if (!amod_option_get(idx, &o)) {
		return 0;
	}
	if (o.type == AMOD_OPT_TOGGLE) {
		amod_option_set(idx, !o.value);
		return 1;
	}
	if (o.type == AMOD_OPT_SLIDER && o.max_val > o.min_val) {
		if (tw < 10) {
			tw = 10;
		}
		val = o.min_val + (mx - tx) * (o.max_val - o.min_val) / tw;
		if (val < o.min_val) {
			val = o.min_val;
		}
		if (val > o.max_val) {
			val = o.max_val;
		}
		amod_option_set(idx, val);
		return 1;
	}
	return 0;
}

/* Mod rows tagged for this tab, appended after the native rows. */
static void draw_mod_tab_rows(int amod_tab, int first_row)
{
	int map[OPT_MAX_MOD_ROWS];
	int cnt = opt_mod_rows(amod_tab, map);

	for (int i = 0; i < cnt; i++) {
		int ry = opt_row_y(first_row + i);
		if (ry >= 0) {
			draw_mod_option_row(ry, map[i]);
		}
	}
}

static int click_mod_tab_rows(int amod_tab, int first_row, int mx, int my)
{
	int map[OPT_MAX_MOD_ROWS];
	int cnt = opt_mod_rows(amod_tab, map);

	for (int i = 0; i < cnt; i++) {
		int ry = opt_row_y(first_row + i);
		if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
			return click_mod_option_row(mx, map[i]);
		}
	}
	return 0;
}

/* The user-facing volume sliders show 0-100; the sound_volume_* variables
 * stay in the historical 0-128 range (sound.c divides by 128, the #volume
 * chat command clamps to it). */
static int vol_to_pct(int vol)
{
	return (vol * 100 + 64) / 128;
}

static int pct_from_click(int mx)
{
	int tx = opt_lx + OPT_SLIDER_LBL;
	int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int pct;

	if (tw < 10) {
		tw = 10;
	}
	pct = (mx - tx) * 100 / tw;
	if (pct < 0) {
		pct = 0;
	}
	if (pct > 100) {
		pct = 100;
	}
	return pct;
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
		draw_slider(opt_lx, ry, opt_content_w, vol_to_pct(sound_volume), 0, 100, "Master");
	}

	ry = opt_row_y(2);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, vol_to_pct(sound_volume_sfx), 0, 100, "Sound Effects");
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, vol_to_pct(sound_volume_ambient), 0, 100, "Ambient");
	}

	ry = opt_row_y(4);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, vol_to_pct(sound_volume_ui), 0, 100, "Interface");
	}

	ry = opt_row_y(5);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_SOUND) != 0, "Sound Enabled");
	}

	draw_mod_tab_rows(AMOD_TAB_AUDIO, OPT_AUDIO_NATIVE);
}

static int opt_click_audio(int mx, int my)
{
	int ry;
	int *target = NULL;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		target = &sound_volume;
	}
	ry = opt_row_y(2);
	if (!target && ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		target = &sound_volume_sfx;
	}
	ry = opt_row_y(3);
	if (!target && ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		target = &sound_volume_ambient;
	}
	ry = opt_row_y(4);
	if (!target && ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		target = &sound_volume_ui;
	}
	if (target) {
		*target = pct_from_click(mx) * 128 / 100;
		sound_refresh_gains(); /* running loops (rain, music) pick it up now, not at their next restart */
		save_options();
		return 1;
	}

	ry = opt_row_y(5);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_SOUND;
		if (!(game_options & GO_SOUND)) {
			sound_stop_all(); /* silence running loops too - play paths only gate new sounds */
		}
		game_options_record_override(GO_SOUND);
		save_options();
		return 1;
	}

	return click_mod_tab_rows(AMOD_TAB_AUDIO, OPT_AUDIO_NATIVE, mx, my);
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

/* Switch the window mode (0 windowed, 1 borderless, 2 exclusive) and re-derive
 * the canvas from the new window size - without this, going borderless kept
 * the old windowed resolution. Shared by the Options click and the startup
 * restore of a saved mode. */
void options_apply_window_mode(int mode)
{
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
	default:
		return;
	}
	SDL_SyncWindow(sdlwnd);
	sdl_recompute_canvas();
	init_dots();
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
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "Window Mode:");
		render_text(opt_lx + 100, ry, UI_TEXT, UI_FONT_BODY, modes[mode]);
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
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		options_apply_window_mode((opt_video_mode() + 1) % 3);
		saved_window_mode = opt_video_mode();
		game_options_record_override(GO_FULL);
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		sdl_set_vsync(!sdl_vsync);
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
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
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "Brightness:");
		render_text(opt_lx + 100, ry, UI_TEXT, UI_FONT_BODY, bnames[bv]);
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

	ry = opt_row_y(5);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_TTF) != 0,
		    fm_available() ? "TTF Text (experimental)" : "TTF Text (unavailable)");
	}

	/* Font + Text Size belong to the TTF path (the bitmap fonts have their
	 * fixed rasters and the Large Font toggle above), so while TTF Text is
	 * off they show their values muted with a hint instead of hiding */
	ry = opt_row_y(6);
	if (ry >= 0) {
		const char *fname = fm_current_font_name();
		int hov = draw_row_hover(ry);

		render_text(opt_lx, ry, hov ? UI_TEXT : UI_TEXT_LABEL, UI_FONT_BODY, "Font:");
		if (!fname) {
			render_text(opt_lx + 100, ry, UI_TEXT_MUTED, UI_FONT_BODY, "(no fonts in res/fonts)");
		} else if (game_options & GO_TTF) {
			render_text(opt_lx + 100, ry, UI_TEXT, UI_FONT_BODY, fname);
		} else {
			char buf[96];

			snprintf(buf, sizeof(buf), "%s (needs TTF Text)", fname);
			render_text(opt_lx + 100, ry, UI_TEXT_MUTED, UI_FONT_BODY, buf);
		}
	}

	ry = opt_row_y(7);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, fm_text_scale(), FM_SCALE_MIN, FM_SCALE_MAX,
		    (game_options & GO_TTF) ? "Text Size %" : "Text Size % (TTF)");
	}

	ry = opt_row_y(8);
	if (ry >= 0) {
		const char *label;

		if (use_gpu_rendering) {
			label = "GPU Renderer";
		} else if (game_options & GO_GPU) {
			label = "GPU Renderer (unavailable - using fallback)";
		} else {
			label = "GPU Renderer (needs restart)";
		}
		draw_checkbox(opt_lx, ry, (game_options & GO_GPU) != 0, label);
	}

	ry = opt_row_y(9);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, (game_options & GO_SHADERFX) != 0,
		    use_gpu_rendering ? "Batched Sprite Effects (needs restart)"
		                      : "Batched Sprite Effects (needs GPU Renderer)");
	}

	ry = opt_row_y(10);
	if (ry >= 0) {
		/* the glow pipeline only exists under the GPU renderer; say so
		 * rather than showing a checkbox that does nothing */
		draw_checkbox(opt_lx, ry, (game_options & GO_NOFANCYFX) == 0,
		    use_gpu_rendering ? "Glowing Spell Effects" : "Glowing Spell Effects (needs GPU Renderer)");
	}
}

static int opt_click_display(int mx, int my)
{
	int ry;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
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
		game_options_record_override(GO_LIGHTER | GO_LIGHTER2);
		/* brightness is baked into sprites at texture build time - drop the
		 * cache or the change is invisible until textures happen to evict */
		sdl_texture_flush_sprites();
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_LOWLIGHT;
		game_options_record_override(GO_LOWLIGHT);
		sdl_texture_flush_sprites(); /* baked at texture build time, like brightness */
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_LARGE;
		game_options_record_override(GO_LARGE);
		save_options();
		return 1;
	}

	ry = opt_row_y(4);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_DARK;
		game_options_record_override(GO_DARK);
		save_options();
		return 1;
	}

	ry = opt_row_y(5);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* persisted via the extra-options file (like GO_NOLAG) - the
		 * launcher does not know this bit. The text texture cache needs no
		 * flush: the TTF cache generation is part of the cache key. */
		game_options ^= GO_TTF;
		fm_set_enabled((game_options & GO_TTF) != 0);
		save_options();
		return 1;
	}

	ry = opt_row_y(6);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* cycle through the faces found in res/fonts. Persisted via the
		 * extra-options file like the TTF toggle; the reload bumps the TTF
		 * cache generation, so cached text textures are simply rebuilt. */
		int n = fm_font_count();

		if (n > 0) {
			int idx = (fm_current_font_index() + 1) % n;

			fm_select_font(fm_font_name(idx));
			save_options();
		}
		return 1;
	}

	ry = opt_row_y(7);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* text size: snap to FM_SCALE_STEP, applied live the same way */
		int tx = opt_lx + OPT_SLIDER_LBL;
		int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
		int val;

		if (tw < 10) {
			tw = 10;
		}
		val = FM_SCALE_MIN + (mx - tx) * (FM_SCALE_MAX - FM_SCALE_MIN) / tw;
		val = ((val + FM_SCALE_STEP / 2) / FM_SCALE_STEP) * FM_SCALE_STEP;
		if (val < FM_SCALE_MIN) {
			val = FM_SCALE_MIN;
		}
		if (val > FM_SCALE_MAX) {
			val = FM_SCALE_MAX;
		}
		fm_set_text_scale(val);
		save_options();
		return 1;
	}

	ry = opt_row_y(8);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* persisted via the extra-options file, like the TTF toggle. The
		 * renderer cannot be swapped at runtime - the choice takes effect on
		 * the next start (and silently falls back to SDL_Renderer when the
		 * GPU path is not usable on this system). */
		game_options ^= GO_GPU;
		save_options();
		return 1;
	}

	ry = opt_row_y(9);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* the sprite batch is built during sdl_init, so like the renderer
		 * itself this takes effect on the next start */
		game_options ^= GO_SHADERFX;
		save_options();
		return 1;
	}

	ry = opt_row_y(10);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* checked per draw, so unlike the two above this takes effect on
		 * the very next frame */
		game_options ^= GO_NOFANCYFX;
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
		draw_slider(opt_lx, ry, opt_content_w, ui_scale_pct, 50, 200, "UI Scale %");
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, !(game_options & GO_NOMAP), "Show Minimap");
	}

	ry = opt_row_y(4);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, !(game_options & GO_NOLAG), "Show Lag Warning");
	}

	ry = opt_row_y(5);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Hotbar");
	}

	ry = opt_row_y(6);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, hotbar_rows(), 0, 3, "Hotbar Rows");
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
		draw_section_header(opt_lx, ry, opt_content_w, "Window Sizes");
	}

	ry = opt_row_y(11);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, inv_grid_cols(), INV_GRID_MIN_COLS, INV_GRID_MAX_COLS, "Items Per Row");
	}

	ry = opt_row_y(12);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, inv_grid_rows(), 0, INV_GRID_MAX_ROWS, "Visible Rows (0 = Auto)");
	}

	ry = opt_row_y(13);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, skl_grid_rows(), 0, SKL_GRID_MAX_ROWS, "Skill Rows (0 = Auto)");
	}

	ry = opt_row_y(14);
	if (ry >= 0) {
		draw_slider(
		    opt_lx, ry, opt_content_w, con_grid_cols(), CON_GRID_MIN_COLS, CON_GRID_MAX_COLS, "Merchant Items Per Row");
	}

	ry = opt_row_y(15);
	if (ry >= 0) {
		draw_slider(opt_lx, ry, opt_content_w, con_grid_rows(), 0, CON_GRID_MAX_ROWS, "Merchant Rows (0 = Auto)");
	}

	ry = opt_row_y(16);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Panels & World");
	}

	ry = opt_row_y(17);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, panels_layout_locked(), "Lock GUI Layout (freeze all panels)");
	}

	ry = opt_row_y(18);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, panel_collapse_upward(), "Minimize Windows Upward (title bar drops to the bottom)");
	}

	for (int p = 0; p < MAX_PANEL; p++) {
		char label[48];

		ry = opt_row_y(OPT_UI_PANEL_ROW0 + p);
		if (ry >= 0) {
			if (p == PANEL_CHAT || p == PANEL_CONTAINER) {
				continue; /* no classic chat; the container window is summoned */
			}
			snprintf(label, sizeof(label), "Show %s", panel_name(p));
			draw_checkbox(opt_lx, ry, panel_visible(p), label);
		}
	}

	ry = opt_row_y(OPT_UI_PANEL_ROW0 + MAX_PANEL);
	if (ry >= 0) {
		draw_checkbox(opt_lx, ry, 0, "Reset Panel Layout");
	}

	draw_mod_tab_rows(AMOD_TAB_UI, OPT_UI_NATIVE);
}

static int opt_click_ui(int mx, int my)
{
	int ry;
	int tx = opt_lx + OPT_SLIDER_LBL;
	int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int val;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_BIGBAR;
		game_options_record_override(GO_BIGBAR);
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* snap to 25% steps; applies live (the world never scales) */
		val = 50 + (mx - tx) * (200 - 50) / tw;
		val = ((val + 12) / 25) * 25;
		if (val < 50) {
			val = 50;
		}
		if (val > 200) {
			val = 200;
		}
		ui_scale_apply(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_NOMAP;
		game_options_record_override(GO_NOMAP);
		save_options();
		return 1;
	}

	ry = opt_row_y(3);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		/* persisted via the extra-options file, not the keybind config
		 * (game_options_record_override only knows the launcher bits) */
		game_options ^= GO_NOLAG;
		save_options();
		return 1;
	}

	ry = opt_row_y(5);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		val = (mx - tx) * 3 / tw; /* 0 = hotbar off */
		if (val < 0) {
			val = 0;
		}
		if (val > 3) {
			val = 3;
		}
		hotbar_set_rows(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(6);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
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

	ry = opt_row_y(7);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		hotbar_set_show_hotkeys(!hotbar_show_hotkeys());
		save_options();
		return 1;
	}

	ry = opt_row_y(8);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		hotbar_set_show_names(!hotbar_show_names());
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(10);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		val = INV_GRID_MIN_COLS + (mx - tx) * (INV_GRID_MAX_COLS - INV_GRID_MIN_COLS) / tw;
		if (val < INV_GRID_MIN_COLS) {
			val = INV_GRID_MIN_COLS;
		}
		if (val > INV_GRID_MAX_COLS) {
			val = INV_GRID_MAX_COLS;
		}
		inv_grid_set_cols(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(11);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		val = (mx - tx) * INV_GRID_MAX_ROWS / tw; /* 0 = auto (classic row count) */
		if (val < 0) {
			val = 0;
		}
		if (val > INV_GRID_MAX_ROWS) {
			val = INV_GRID_MAX_ROWS;
		}
		inv_grid_set_rows(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(12);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		val = (mx - tx) * SKL_GRID_MAX_ROWS / tw; /* 0 = auto */
		if (val < 0) {
			val = 0;
		}
		if (val > SKL_GRID_MAX_ROWS) {
			val = SKL_GRID_MAX_ROWS;
		}
		skl_grid_set_rows(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(13);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		val = CON_GRID_MIN_COLS + (mx - tx) * (CON_GRID_MAX_COLS - CON_GRID_MIN_COLS) / tw;
		if (val < CON_GRID_MIN_COLS) {
			val = CON_GRID_MIN_COLS;
		}
		if (val > CON_GRID_MAX_COLS) {
			val = CON_GRID_MAX_COLS;
		}
		con_grid_set_cols(val);
		init_dots();
		save_options();
		return 1;
	}

	ry = opt_row_y(17);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		panels_set_layout_locked(!panels_layout_locked());
		init_dots(); /* resize grips follow the lock */
		save_options();
		return 1;
	}

	ry = opt_row_y(18);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		panels_set_collapse_upward(!panel_collapse_upward());
		save_options();
		return 1;
	}

	for (int p = 0; p < MAX_PANEL; p++) {
		ry = opt_row_y(OPT_UI_PANEL_ROW0 + p);
		if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
			if (p == PANEL_CHAT || p == PANEL_CONTAINER) {
				return 1; /* no row for these */
			}
			panel_toggle(p);
			save_options();
			return 1;
		}
	}

	ry = opt_row_y(OPT_UI_PANEL_ROW0 + MAX_PANEL);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		panels_reset_layout();
		init_dots();
		save_options();
		return 1;
	}

	return click_mod_tab_rows(AMOD_TAB_UI, OPT_UI_NATIVE, mx, my);
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
		draw_checkbox(opt_lx, ry, (game_options & GO_WHEELSPEED) != 0, "Wheel Changes Walk Speed");
	}
}

static int opt_click_advanced(int mx, int my)
{
	int ry;
	int tx = opt_lx + OPT_SLIDER_LBL;
	int tw = opt_content_w - OPT_SLIDER_LBL - OPT_SLIDER_VAL;
	int val;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_PREDICT;
		game_options_record_override(GO_PREDICT);
		save_options();
		return 1;
	}

	ry = opt_row_y(2);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_SHORT;
		game_options_record_override(GO_SHORT);
		save_options();
		return 1;
	}

	ry = opt_row_y(4);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
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
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
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
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_MAPSAVE;
		game_options_record_override(GO_MAPSAVE);
		save_options();
		return 1;
	}

	ry = opt_row_y(8);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_CONTEXT;
		game_options_record_override(GO_CONTEXT);
		save_options();
		return 1;
	}

	ry = opt_row_y(9);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		game_options ^= GO_WHEELSPEED;
		game_options_record_override(GO_WHEELSPEED);
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
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, connected ? "Status: Connected" : "Status: Not Connected");
	}

	if (!connected) {
		ry = opt_row_y(3);
		if (ry >= 0) {
			render_text(opt_lx, ry, UI_TEXT_MUTED, UI_FONT_BODY, "Connect a controller to configure.");
		}
		return;
	}

	ry = opt_row_y(3);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "LT (Left Trigger) + Button");
	}
	ry = opt_row_y(4);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "A/B/X/Y:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Hotbar 1-4");
	}
	ry = opt_row_y(5);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "D-Pad:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Hotbar 5-8");
	}

	ry = opt_row_y(7);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "RT (Right Trigger) + Button");
	}
	ry = opt_row_y(8);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "A/B/X/Y:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Hotbar 9-12");
	}
	ry = opt_row_y(9);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "D-Pad:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Hotbar 13-16");
	}

	ry = opt_row_y(11);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Sticks & Buttons");
	}
	ry = opt_row_y(12);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "Left Stick:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Move");
	}
	ry = opt_row_y(13);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "Right Stick:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Cursor");
	}
	ry = opt_row_y(14);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "R3 (R-Click):");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Left Click");
	}
	ry = opt_row_y(15);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "L3 (L-Click):");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Right Click");
	}
	ry = opt_row_y(16);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "Start:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Menu");
	}
	ry = opt_row_y(17);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "Back/Select:");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Cancel All");
	}
	ry = opt_row_y(18);
	if (ry >= 0) {
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "A (alone):");
		render_text(opt_lx + 80, ry, UI_TEXT, UI_FONT_BODY, "Left Click");
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

/* Gameplay tab: native combat rows first, then options provided by the loaded
 * mod (see amod_option in amod_structs.h) */
static void opt_display_gameplay(void)
{
	int ry;

	ry = opt_row_y(0);
	if (ry >= 0) {
		draw_section_header(opt_lx, ry, opt_content_w, "Combat");
	}

	ry = opt_row_y(1);
	if (ry >= 0) {
		const char *cast_names[] = {
		    "Normal", "Quick Cast", "Quick Cast w/ Indicator", "Smart (quick if target under cursor)"};
		int cm = hotbar_cast_mode();
		render_text(opt_lx, ry, UI_TEXT_LABEL, UI_FONT_BODY, "Cast Mode:");
		render_text(opt_lx + 100, ry, UI_TEXT, UI_FONT_BODY, cast_names[cm]);
	}

	draw_mod_tab_rows(AMOD_TAB_GAMEPLAY, OPT_GAMEPLAY_NATIVE);
}

static int opt_click_gameplay(int mx, int my)
{
	int ry;

	ry = opt_row_y(1);
	if (ry >= 0 && in_rect(mx, my, opt_lx, ry, opt_content_w, UI_ROW_H)) {
		hotbar_set_cast_mode((hotbar_cast_mode() + 1) % 4);
		save_options();
		return 1;
	}

	return click_mod_tab_rows(AMOD_TAB_GAMEPLAY, OPT_GAMEPLAY_NATIVE, mx, my);
}

void options_display(void)
{
	int total, max_scroll, cx, tab_w, i;
	static const char *tab_labels[OPT_NTABS] = {"Audio", "Video", "Display", "UI", "Gameplay", "Advanced", "Gamepad"};

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

	/* panel: vertical gradient, rounded border, darker title strip */
	ui_panel(opt_px, opt_py, opt_px + opt_pw, opt_py + opt_ph);
	render_rect_alpha(opt_px + 1, opt_py + OPT_TITLE_H + UI_PAD - 2, opt_px + opt_pw - 1,
	    opt_py + OPT_TITLE_H + UI_PAD - 1, UI_BORDER, 120);

	cx = opt_px + opt_pw / 2;
	render_text(cx, opt_py + UI_PAD, UI_TEXT_TITLE, UI_FONT_CENTER, "Options");
	ui_button(opt_px + opt_pw - UI_PAD - 12, opt_py + 2, 12, 12, "X",
	    in_rect(mousex, mousey, opt_px + opt_pw - UI_PAD - 12, opt_py + 2, 12, 12) ? UI_BTN_HOVER : UI_BTN_REST);
	draw_scrollbar(total);

	tab_w = opt_pw / OPT_NTABS;
	for (i = 0; i < OPT_NTABS; i++) {
		draw_tab(opt_px + i * tab_w, opt_tab_bar_y, tab_w, OPT_TAB_H, tab_labels[i], opt_tab == i);
	}

	render_rect_alpha(
	    opt_px + 1, opt_tab_bar_y + OPT_TAB_H, opt_px + opt_pw - 1, opt_tab_bar_y + OPT_TAB_H + 1, UI_BORDER, 160);

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
		opt_display_gameplay();
		break;
	case 5:
		opt_display_advanced();
		break;
	case 6:
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

	if (in_rect(mx, my, opt_px + opt_pw - UI_PAD - 12, opt_py + 2, 12, 12)) {
		options_close();
		return 1;
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
		opt_click_gameplay(mx, my);
		break;
	case 5:
		opt_click_advanced(mx, my);
		break;
	case 6:
		opt_click_gamepad(mx, my);
		break;
	default:
		break;
	}

	return 1;
}
