/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/escape_menu_ui.h"
#include "gui/keybind_settings_ui.h"
#include "gui/options_ui.h"
#include "game/game.h"

#define EM_WIDTH   160
#define EM_BTN_H   20
#define EM_BTN_PAD 6
#define EM_PAD     12

#define COL_BTN_TEXT IRGB(29, 28, 26)
#define COL_BTN_BG   IRGB(6, 6, 5)
#define COL_BTN_HOV  IRGB(12, 11, 9)
#define COL_TITLE    IRGB(28, 26, 22)

static int em_open;
static int em_px, em_py, em_pw, em_ph;

typedef enum { EM_RESUME, EM_OPTIONS, EM_KEYBINDINGS, EM_EXIT, EM_COUNT } EmButton;

static const char *em_labels[EM_COUNT] = {
    "Resume",
    "Options",
    "Keybindings",
    "Exit Game",
};

static void em_compute_layout(void)
{
	em_pw = EM_WIDTH;
	em_ph = EM_PAD + 16 + EM_BTN_PAD + (EM_BTN_H + EM_BTN_PAD) * EM_COUNT + EM_PAD;

	int map_top = doty(DOT_MTL);
	int map_bot = doty(DOT_HOTBAR) - 10;
	int map_lx = dotx(DOT_MTL);
	int map_rx = dotx(DOT_MBR);

	em_px = map_lx + ((map_rx - map_lx) - em_pw) / 2;
	em_py = map_top + ((map_bot - map_top) - em_ph) / 2;
}

static int em_btn_y(int idx)
{
	return em_py + EM_PAD + 16 + EM_BTN_PAD + idx * (EM_BTN_H + EM_BTN_PAD);
}

static int em_btn_hit(int mx, int my, int idx)
{
	int bx = em_px + EM_PAD;
	int by = em_btn_y(idx);
	int bw = em_pw - EM_PAD * 2;
	return mx >= bx && mx < bx + bw && my >= by && my < by + EM_BTN_H;
}

void escape_menu_display(void)
{
	if (!em_open) {
		return;
	}

	em_compute_layout();

	render_shaded_rect(em_px, em_py, em_px + em_pw, em_py + em_ph, 0, 210);

	int cx = em_px + em_pw / 2;
	render_text(cx, em_py + EM_PAD, COL_TITLE, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, "Menu");

	int bx = em_px + EM_PAD;
	int bw = em_pw - EM_PAD * 2;

	for (int i = 0; i < EM_COUNT; i++) {
		int by = em_btn_y(i);
		int hovered = (mousex >= bx && mousex < bx + bw && mousey >= by && mousey < by + EM_BTN_H);
		unsigned short bg = hovered ? COL_BTN_HOV : COL_BTN_BG;
		render_rect_alpha(bx, by, bx + bw, by + EM_BTN_H, bg, 180);
		render_text(
		    cx, by + 4, COL_BTN_TEXT, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, em_labels[i]);
	}
}

int escape_menu_is_open(void)
{
	return em_open;
}

void escape_menu_toggle(void)
{
	if (em_open) {
		escape_menu_close();
	} else {
		em_open = 1;
		keybind_settings_close();
		options_close();
	}
}

void escape_menu_close(void)
{
	em_open = 0;
}

int escape_menu_click(int mx, int my)
{
	if (!em_open) {
		return 0;
	}

	em_compute_layout();

	if (mx < em_px || mx >= em_px + em_pw || my < em_py || my >= em_py + em_ph) {
		return 0;
	}

	if (em_btn_hit(mx, my, EM_RESUME)) {
		escape_menu_close();
		return 1;
	}

	if (em_btn_hit(mx, my, EM_OPTIONS)) {
		escape_menu_close();
		options_open();
		return 1;
	}

	if (em_btn_hit(mx, my, EM_KEYBINDINGS)) {
		escape_menu_close();
		keybind_settings_toggle();
		return 1;
	}

	if (em_btn_hit(mx, my, EM_EXIT)) {
		quit = 1;
		return 1;
	}

	return 1;
}
