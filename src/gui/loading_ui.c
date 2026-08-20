/*
 * Startup loading screen. See loading_ui.h.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/loading_ui.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "modder/modder.h"

#define LS_PENDING 0
#define LS_ACTIVE  1
#define LS_DONE    2

#define COL_BAR_BG IRGB(4, 4, 4)
#define COL_BAR_FG IRGB(28, 22, 10)
#define COL_BORDER IRGB(18, 16, 12)
#define COL_DONE   IRGB(22, 26, 14)
#define COL_DIM    IRGB(14, 14, 13)
#define COL_BRIGHT IRGB(30, 29, 26)

static const char *step_labels[LS_COUNT] = {
    "Loading graphics",
    "Loading sounds",
    "Starting mods",
    "Connecting to server",
    "Logging in",
    "Loading world",
};

static int step_state[LS_COUNT];
static int cur_step = -1;
static int prog_done, prog_total;
static char detail[96];
static int finished;
static Uint64 started_at, step_started_at;

void loading_step(int step)
{
	int i;

	if (step < 0 || step >= LS_COUNT) {
		return;
	}
	if (!started_at) {
		started_at = SDL_GetTicks();
	}
	for (i = 0; i < step; i++) {
		step_state[i] = LS_DONE;
	}
	if (step_state[step] != LS_ACTIVE) {
		step_started_at = SDL_GetTicks();
	}
	step_state[step] = LS_ACTIVE;
	cur_step = step;
	prog_done = prog_total = 0;
	detail[0] = 0;
}

void loading_progress(int done, int total)
{
	prog_done = done;
	prog_total = total;
}

void loading_detail(const char *text)
{
	if (!text) {
		detail[0] = 0;
		return;
	}
	snprintf(detail, sizeof(detail), "%s", text);
}

void loading_finish(void)
{
	int i;

	for (i = 0; i < LS_COUNT; i++) {
		step_state[i] = LS_DONE;
	}
	if (!finished) {
		note("startup complete after %u ms", (unsigned)(SDL_GetTicks() - started_at));
	}
	finished = 1;
}

int loading_active(void)
{
	return !finished;
}

static void draw_check(int x, int y, unsigned short col)
{
	render_line_alpha(x, y + 4, x + 3, y + 7, col, 255);
	render_line_alpha(x, y + 5, x + 3, y + 8, col, 255);
	render_line_alpha(x + 3, y + 7, x + 8, y + 1, col, 255);
	render_line_alpha(x + 3, y + 8, x + 8, y + 2, col, 255);
}

void loading_display(void)
{
	int cx = XRES / 2;
	int i, y, pct = 0;
	int bw = 240, bh = 8, bx = cx - bw / 2;
	int list_w = 200, lx = cx - list_w / 2;
	Uint64 now = SDL_GetTicks();
	char buf[128];

	/* whole window black, logo on top */
	render_rect(0, 0, XRES, YRES, blackcolor);
	render_sprite(
	    60, cx, 64, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER); /* centre-aligned; 30 clipped the top of the logo */

	/* step list */
	y = YRES / 2 - (LS_COUNT * 16) / 2 - 10;
	for (i = 0; i < LS_COUNT; i++, y += 16) {
		unsigned short col = (step_state[i] == LS_DONE)     ? COL_DONE
		                     : (step_state[i] == LS_ACTIVE) ? COL_BRIGHT
		                                                    : COL_DIM;
		render_rounded_rect_alpha(lx, y + 2, lx + 11, y + 13, 2, col, 200);
		if (step_state[i] == LS_DONE) {
			draw_check(lx + 1, y + 2, COL_DONE);
		} else if (step_state[i] == LS_ACTIVE) {
			/* pulsing dot */
			int r = 2 + (int)((now / 120) % 3);
			render_circle_filled_alpha(lx + 6, y + 8, r, COL_BAR_FG, 230);
		}
		render_text(lx + 18, y, col, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, step_labels[i]);
		if (step_state[i] == LS_ACTIVE && i == LS_CONNECT) {
			unsigned secs = (unsigned)((now - step_started_at) / 1000);
			if (secs >= 3) {
				snprintf(buf, sizeof(buf), "%us", secs);
				render_text(lx + list_w, y, COL_DIM, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, buf);
			}
		}
	}

	/* detail line + progress bar under the list */
	y += 6;
	if (detail[0]) {
		render_text(cx, y, COL_DIM, RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, detail);
	}
	y += 16;
	render_rounded_rect_filled_alpha(bx, y, bx + bw, y + bh, 3, COL_BAR_BG, 230);
	if (prog_total > 0) {
		pct = prog_done * 100 / prog_total;
		if (pct > 100) {
			pct = 100;
		}
		render_rounded_rect_filled_alpha(bx, y, bx + bw * pct / 100, y + bh, 3, COL_BAR_FG, 240);
	} else {
		/* indeterminate sweep */
		int sw = bw / 4, sx = bx + (int)((now / 8) % (Uint64)(bw - sw));
		render_rounded_rect_filled_alpha(sx, y, sx + sw, y + bh, 3, COL_BAR_FG, 240);
	}
	render_rounded_rect_alpha(bx, y, bx + bw, y + bh, 3, COL_BORDER, 200);
	if (prog_total > 0) {
		snprintf(buf, sizeof(buf), "%d%%", pct);
		render_text(cx, y + bh + 6, textcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, buf);
	}

	/* long connect: point at help */
	if (cur_step == LS_CONNECT && now - step_started_at > 15000) {
		render_text_fmt(cx, y + bh + 26, textcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
		    "Still connecting... please check %s for help.", game_url);
	}
}

void loading_present(void)
{
	if (!sdl_is_shown()) {
		return;
	}
	sdl_loop();
	loading_display();
	sdl_render();
}
