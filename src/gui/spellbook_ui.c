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

static int sb_dragging = -1; /* action slot being carried on the cursor (-1 = none) */

/* the press that picked the spell up: a press-drag-release that travels
 * past SB_DRAG_DEAD and ends on nothing drops the spell again, while a
 * plain click (no travel) leaves it on the cursor for a second click */
static int sb_pressed;
static int sb_press_x, sb_press_y;
#define SB_DRAG_DEAD 4

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
}

/* The carried spell rides the cursor everywhere - drawn outside the
 * spellbook's clip, late in display(), so it does not vanish the moment the
 * pointer leaves the window on its way to the hotbar. */
void spellbook_display_carry(void)
{
	RenderFX fx;

	if (sb_dragging < 0) {
		return;
	}
	bzero(&fx, sizeof(fx));
	fx.sprite = (unsigned int)(SPELL_ICON_SPRITE_BASE + sb_dragging);
	fx.scale = 80;
	fx.sat = 14;
	fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = RENDERFX_BRIGHT;
	render_sprite_fx(&fx, mousex, mousey);
}

/* ── Toggle / state ──────────────────────────────────────────────────── */

int spellbook_over(int mx, int my)
{
	int x1, y1, x2, y2;

	/* CONTENT rect, not the frame: the title bar and glyphs must reach the
	 * panel chrome hit-test, or the window can never be dragged or closed */
	if (!panel_content_shown(PANEL_SPELLBOOK) || !panel_content_rect(PANEL_SPELLBOOK, &x1, &y1, &x2, &y2)) {
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
	sb_pressed = 0;
}

/* ── Click handling ──────────────────────────────────────────────────── */

int spellbook_mousedown(int mx, int my)
{
	int hit;

	sb_pressed = 0;
	if (!panel_content_shown(PANEL_SPELLBOOK)) {
		return 0;
	}
	hit = sb_hit(mx, my);
	if (hit < 0) {
		return 0;
	}
	/* picked up on the press: from here the icon follows the pointer, and
	 * the release decides whether this was a drag or a click */
	sb_dragging = hit;
	sb_pressed = 1;
	sb_press_x = mx;
	sb_press_y = my;
	return 1;
}

int spellbook_click(int mx, int my)
{
	int hit, moved;

	/* the hotbar takes its release first (hotbar_click assigns the carried
	 * spell); what arrives here is a release on anything else */
	moved = sb_pressed && (abs(mx - sb_press_x) >= SB_DRAG_DEAD || abs(my - sb_press_y) >= SB_DRAG_DEAD);
	sb_pressed = 0;

	if (!panel_content_shown(PANEL_SPELLBOOK)) {
		if (sb_dragging >= 0) {
			sb_dragging = -1; /* the window closed under a carried spell */
			return 1;
		}
		return 0;
	}

	hit = sb_hit(mx, my);
	if (hit >= 0) {
		/* a click (or a drag that came back) on a cell: carry that spell */
		sb_dragging = hit;
		return 1;
	}

	if (sb_dragging >= 0) {
		/* a drag released on nothing drops the spell; a click anywhere while
		 * carrying one puts it down too */
		(void)moved;
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
		sb_pressed = 0;
		return 1;
	}
	return 0;
}
