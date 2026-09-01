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
#include "gui/panels.h"
#include "gui/input_bind.h"
#include "gui/spellbook_ui.h"
#include "gui/ui_draw.h"
#include "client/client.h"
#include "game/game.h"

/* ── State ───────────────────────────────────────────────────────────── */

static int sb_dragging = -1; /* action slot being dragged (-1 = none) */

/* ── Layout ──────────────────────────────────────────────────────────── */

#define SB_CELL FDX

/* how many castable spells the window has to hold - init_dots() sizes the
 * panel from this, and display() re-runs the layout when it changes */
int spellbook_slot_count(void)
{
	int avail = 0;

	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (has_action_skill(i)) {
			avail++;
		}
	}
	return avail;
}

/* top-left of the spell grid; the window frame around it is panel chrome */
static void sb_layout(int *ox, int *oy, int *cols_out, int *rows_out)
{
	int avail = spellbook_slot_count();
	int cols, rows;

	if (avail < 1) {
		avail = 1;
	}
	cols = avail < SPB_COLS ? avail : SPB_COLS;
	rows = (avail + SPB_COLS - 1) / SPB_COLS;

	*ox = dotx(DOT_SPB);
	*oy = doty(DOT_SPB);
	if (cols_out) {
		*cols_out = cols;
	}
	if (rows_out) {
		*rows_out = rows;
	}
}

static int sb_hit(int mx, int my)
{
	int ox, oy;

	if (!panel_content_shown(PANEL_SPELLBOOK)) {
		return -1;
	}
	sb_layout(&ox, &oy, NULL, NULL);

	int col = 0;
	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (!has_action_skill(i)) {
			continue;
		}

		int row = col / SPB_COLS;
		int c = col % SPB_COLS;
		int cx = ox + c * SB_CELL + SB_CELL / 2;
		int cy = oy + row * SB_CELL + SB_CELL / 2;
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
	RenderFX fx;
	int ox, oy, cols, rows;
	int hover_slot;
	int col = 0;

	if (!panel_content_shown(PANEL_SPELLBOOK)) {
		return;
	}
	sb_layout(&ox, &oy, &cols, &rows);
	(void)cols;
	(void)rows;

	if (!spellbook_slot_count()) {
		render_text(ox + 4, oy + 6, UI_TEXT_MUTED, UI_FONT_BODY, "No spells learned yet.");
		return;
	}

	hover_slot = sb_hit(mousex, mousey);

	for (int i = 0; i < MAXACTIONSLOT; i++) {
		if (!has_action_skill(i)) {
			continue;
		}

		int row = col / SPB_COLS;
		int c = col % SPB_COLS;
		int cx = ox + c * SB_CELL + SB_CELL / 2;
		int cy = oy + row * SB_CELL + SB_CELL / 2;
		col++;

		render_sprite(opt_sprite(SPR_ITPAD), cx, cy, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		if (i == hover_slot) {
			render_sprite(opt_sprite(SPR_ITSEL), cx, cy, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		}

		/* spell icon (sprites 800 + action_slot) */
		bzero(&fx, sizeof(fx));
		fx.sprite = (unsigned int)(SPELL_ICON_SPRITE_BASE + i);
		fx.scale = 80;
		fx.sat = 14;
		fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = (i == hover_slot) ? RENDERFX_BRIGHT : RENDERFX_NORMAL_LIGHT;
		render_sprite_fx(&fx, cx, cy);
	}

	/* tooltip for hovered spell */
	if (hover_slot >= 0) {
		const char *name = get_action_text(hover_slot);
		if (name) {
			int tw = render_text_length(RENDER_TEXT_SMALL, name);
			int tx1 = mousex - tw / 2 - UI_PAD_TIGHT;
			int tx2 = mousex + tw / 2 + UI_PAD_TIGHT;
			int ty1 = mousey - 24 - UI_PAD_TIGHT + 1;
			int ty2 = mousey - 24 + 10 + UI_PAD_TIGHT - 1;
			render_rounded_rect_filled_alpha(tx1, ty1, tx2, ty2, UI_R_CHIP, UI_BG_BASE, UI_A_TOOLTIP);
			render_rounded_rect_alpha(tx1, ty1, tx2, ty2, UI_R_CHIP, UI_BORDER, UI_A_BORDER_HOV);
			render_text(mousex, mousey - 24, UI_TEXT, UI_FONT_CENTER, name);
		}
	}

	/* draw spell on cursor if dragging */
	if (sb_dragging >= 0) {
		bzero(&fx, sizeof(fx));
		fx.sprite = (unsigned int)(SPELL_ICON_SPRITE_BASE + sb_dragging);
		fx.scale = 80;
		fx.sat = 14;
		fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = RENDERFX_BRIGHT;
		render_sprite_fx(&fx, mousex, mousey);
	}
}

/* ── Toggle / state ──────────────────────────────────────────────────── */

int spellbook_over(int mx, int my)
{
	int x1, y1, x2, y2;

	if (!panel_shown(PANEL_SPELLBOOK) || !panel_frame_rect(PANEL_SPELLBOOK, &x1, &y1, &x2, &y2)) {
		return 0;
	}
	return mx >= x1 && mx <= x2 && my >= y1 && my <= y2;
}

int spellbook_is_open(void)
{
	return panel_content_shown(PANEL_SPELLBOOK);
}

void spellbook_toggle(void)
{
	panel_toggle(PANEL_SPELLBOOK);
	if (!panel_visible(PANEL_SPELLBOOK)) {
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
	if (!panel_content_shown(PANEL_SPELLBOOK)) {
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

	/* consume clicks on the window body (the chrome handles its own) */
	if (spellbook_over(mx, my)) {
		return 1;
	}

	return 0;
}

int spellbook_rclick(int mx, int my)
{
	(void)mx;
	(void)my;
	if (!panel_content_shown(PANEL_SPELLBOOK)) {
		return 0;
	}

	/* right-click cancels drag */
	if (sb_dragging >= 0) {
		sb_dragging = -1;
		return 1;
	}
	return 0;
}
