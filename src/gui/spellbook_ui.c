/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Spellbook UI — toggleable panel that shows available spells as
 * draggable icons. Left-click a spell to "pick it up", then click
 * a hotbar slot to assign it.
 */

#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/spellbook_ui.h"
#include "client/client.h"
#include "game/game.h"

/* ── State ───────────────────────────────────────────────────────────── */

static int sb_open; /* 1 = panel is visible */
static int sb_dragging = -1; /* action slot being dragged (-1 = none) */

/* ── Layout constants ────────────────────────────────────────────────── */

#define SB_COLS 7
#define SB_CELL 40 /* same as FDX */
#define SB_PAD  4

/* compute the top-left corner and dimensions of the spellbook panel */
static void sb_layout(int *ox, int *oy, int *cols_out, int *rows_out)
{
	int count = hotbar_visible_slots();
	int hotbar_cx = (butx(BUT_HOTBAR_BEG) + butx(BUT_HOTBAR_BEG + count - 1)) / 2;

	/* count available spells */
	int avail = 0;
	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (has_action_skill(i)) {
			avail++;
		}
	}
	if (avail < 1) {
		avail = 1;
	}

	int cols = avail < SB_COLS ? avail : SB_COLS;
	int rows = (avail + SB_COLS - 1) / SB_COLS;
	if (rows < 1) {
		rows = 1;
	}

	int panel_w = cols * SB_CELL + SB_PAD * 2;
	*ox = hotbar_cx - panel_w / 2;
	*oy = buty(BUT_HOTBAR_BEG) - SB_PAD * 2 - rows * SB_CELL;

	if (cols_out) {
		*cols_out = cols;
	}
	if (rows_out) {
		*rows_out = rows;
	}
}

/* hit-test: returns the action slot index under (mx, my), or -1 */
static int sb_hit(int mx, int my)
{
	if (!sb_open) {
		return -1;
	}

	int ox, oy;
	sb_layout(&ox, &oy, NULL, NULL);

	int col = 0;
	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (!has_action_skill(i)) {
			continue;
		}

		int row = col / SB_COLS;
		int c = col % SB_COLS;
		int cx = ox + SB_PAD + c * SB_CELL + SB_CELL / 2;
		int cy = oy + SB_PAD + row * SB_CELL + SB_CELL / 2;
		col++;

		int dx = mx - cx;
		int dy = my - cy;
		if (dx >= -SB_CELL / 2 && dx < SB_CELL / 2 && dy >= -SB_CELL / 2 && dy < SB_CELL / 2) {
			return i;
		}
	}
	return -1;
}

/* ── Rendering ───────────────────────────────────────────────────────── */

void spellbook_display(void)
{
	if (!sb_open) {
		return;
	}

	RenderFX fx;
	int ox, oy, cols, rows;
	sb_layout(&ox, &oy, &cols, &rows);

	/* count check */
	int avail = 0;
	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (has_action_skill(i)) {
			avail++;
		}
	}
	if (!avail) {
		return;
	}

	int panel_w = cols * SB_CELL + SB_PAD * 2;
	int panel_h = rows * SB_CELL + SB_PAD * 2;

	/* background */
	render_shaded_rect(ox, oy, ox + panel_w, oy + panel_h, 0, 160);

	/* each available spell */
	int col = 0;
	int hover_slot = sb_hit(mousex, mousey);

	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (!has_action_skill(i)) {
			continue;
		}

		int row = col / SB_COLS;
		int c = col % SB_COLS;
		int cx = ox + SB_PAD + c * SB_CELL + SB_CELL / 2;
		int cy = oy + SB_PAD + row * SB_CELL + SB_CELL / 2;
		col++;

		/* slot background */
		render_sprite(opt_sprite(SPR_ITPAD), cx, cy, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);

		/* hover highlight */
		if (i == hover_slot) {
			render_sprite(opt_sprite(SPR_ITSEL), cx, cy, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		}

		/* spell icon (sprites 800 + action_slot) */
		bzero(&fx, sizeof(fx));
		fx.sprite = (unsigned int)(800 + i);
		fx.scale = 80;
		fx.sat = 14;
		fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = (i == hover_slot) ? RENDERFX_BRIGHT : RENDERFX_NORMAL_LIGHT;
		render_sprite_fx(&fx, cx, cy);
	}

	/* tooltip for hovered spell */
	if (hover_slot >= 0) {
		const char *name = get_action_text(hover_slot);
		if (name) {
			render_text(mousex, mousey - 24, whitecolor, RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, name);
		}
	}

	/* draw spell on cursor if dragging */
	if (sb_dragging >= 0) {
		bzero(&fx, sizeof(fx));
		fx.sprite = (unsigned int)(800 + sb_dragging);
		fx.scale = 80;
		fx.sat = 14;
		fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = RENDERFX_BRIGHT;
		render_sprite_fx(&fx, mousex, mousey);
	}
}

/* ── Toggle / state ──────────────────────────────────────────────────── */

int spellbook_is_open(void)
{
	return sb_open;
}

void spellbook_toggle(void)
{
	sb_open = !sb_open;
	if (!sb_open) {
		sb_dragging = -1;
	}
}

int spellbook_is_dragging(void)
{
	return sb_dragging >= 0;
}

int spellbook_dragging_slot(void)
{
	return sb_dragging;
}

void spellbook_cancel_drag(void)
{
	sb_dragging = -1;
}

/* ── Click handling ──────────────────────────────────────────────────── */

int spellbook_click(int mx, int my)
{
	if (!sb_open) {
		return 0;
	}

	/* click inside spellbook — pick up spell */
	int hit = sb_hit(mx, my);
	if (hit >= 0) {
		sb_dragging = hit;
		return 1;
	}

	/* click outside while dragging — cancel drag */
	if (sb_dragging >= 0) {
		sb_dragging = -1;
		return 1;
	}

	return 0;
}

int spellbook_rclick(int mx, int my)
{
	(void)mx;
	(void)my;
	if (!sb_open) {
		return 0;
	}

	/* right-click cancels drag */
	if (sb_dragging >= 0) {
		sb_dragging = -1;
		return 1;
	}
	return 0;
}
