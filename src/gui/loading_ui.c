/*
 * Startup loading screen. See loading_ui.h.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
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

/* server-side refusal shown on the loading screen (+ optional auto retry) */
static char err_text[256];
static char last_exit_reason[256];
static char notice_text[160];
static Uint64 retry_at; /* SDL ticks when to reconnect, 0 = no automatic retry */
static int retry_secs;

void loading_notice(const char *text)
{
	if (!text) {
		notice_text[0] = 0;
		return;
	}
	if (strcmp(notice_text, text) != 0) {
		note("loading: %s", text);
	}
	snprintf(notice_text, sizeof(notice_text), "%s", text);
}

const char *loading_last_exit_reason(void)
{
	return last_exit_reason;
}

void loading_server_exit(const char *reason)
{
	const char *tag;
	size_t n;

	if (!reason) {
		return;
	}
	snprintf(last_exit_reason, sizeof(last_exit_reason), "%s", reason);
	if ((tag = strstr(last_exit_reason, "[retry="))) {
		n = (size_t)(tag - last_exit_reason);
		while (n > 0 && last_exit_reason[n - 1] == ' ') {
			n--;
		}
		last_exit_reason[n] = 0;
	}
	if (finished) {
		return;
	}
	notice_text[0] = 0;
	snprintf(err_text, sizeof(err_text), "%s", reason);
	retry_at = 0;
	retry_secs = 0;
	tag = strstr(err_text, "[retry=");
	if (tag) {
		retry_secs = atoi(tag + 7);
		/* strip the hint (and the blank before it) from what the player sees */
		n = (size_t)(tag - err_text);
		while (n > 0 && err_text[n - 1] == ' ') {
			n--;
		}
		err_text[n] = 0;
		if (retry_secs > 0) {
			retry_at = SDL_GetTicks() + (Uint64)retry_secs * 1000;
		}
	}
	note("loading: server ended the session: %s%s", err_text, retry_at ? " (will retry automatically)" : "");
	if (retry_at) {
		note("loading: automatic retry in %d s", retry_secs);
	}
}

int loading_retry_due(void)
{
	return err_text[0] && retry_at && SDL_GetTicks() >= retry_at;
}

void loading_retry_begin(void)
{
	note("loading: retrying connection/login now");
	err_text[0] = 0;
	notice_text[0] = 0;
	retry_at = 0;
	retry_secs = 0;
	step_state[LS_LOGIN] = LS_PENDING;
	step_state[LS_WORLD] = LS_PENDING;
	loading_step(LS_CONNECT);
}

/* word-wrap helper for the error text */
static int wrap_lines(const char *text, int max_w, char out[][128], int max_lines)
{
	int lines = 0;
	const char *p = text;
	char cur[128] = "";

	while (*p && lines < max_lines) {
		const char *sp = strchr(p, ' ');
		size_t wl = sp ? (size_t)(sp - p) : strlen(p);
		char word[128], trial[128];

		if (wl >= sizeof(word)) {
			wl = sizeof(word) - 1;
		}
		memcpy(word, p, wl);
		word[wl] = 0;
		p += wl;
		while (*p == ' ') {
			p++;
		}
		if (cur[0]) {
			snprintf(trial, sizeof(trial), "%s %s", cur, word);
		} else {
			snprintf(trial, sizeof(trial), "%s", word);
		}
		if (cur[0] && render_text_length(RENDER_TEXT_SMALL, trial) > max_w) {
			snprintf(out[lines++], 128, "%s", cur);
			snprintf(cur, sizeof(cur), "%s", word);
		} else {
			snprintf(cur, sizeof(cur), "%s", trial);
		}
	}
	if (cur[0] && lines < max_lines) {
		snprintf(out[lines++], 128, "%s", cur);
	}
	return lines;
}

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

	/* non-fatal status: connection attempts failing, connection lost, server silent */
	if (notice_text[0] && !err_text[0]) {
		char lines[4][128];
		int n = wrap_lines(notice_text, bw + 200, lines, 4), ly = y + bh + 26;

		for (i = 0; i < n; i++, ly += 13) {
			render_text(cx, ly, textcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, lines[i]);
		}
	}

	/* the server refused us (e.g. another character of the account still in the world) */
	if (err_text[0]) {
		char lines[6][128];
		int n = wrap_lines(err_text, bw + 200, lines, 6), ly = y + bh + 26;

		for (i = 0; i < n; i++, ly += 13) {
			render_text(cx, ly, redcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, lines[i]);
		}
		if (retry_at) {
			Uint64 left = retry_at > now ? (retry_at - now + 999) / 1000 : 0;
			if (left > 0) {
				snprintf(buf, sizeof(buf), "Retrying automatically in %llu:%02llu", (unsigned long long)(left / 60),
				    (unsigned long long)(left % 60));
			} else {
				snprintf(buf, sizeof(buf), "Retrying...");
			}
			render_text(cx, ly + 6, textcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, buf);
		} else {
			render_text(cx, ly + 6, COL_DIM, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
			    "Close the game and start it again from the launcher.");
		}
		return;
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
