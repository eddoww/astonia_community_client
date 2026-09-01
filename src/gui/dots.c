/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Dots and Buttons
 *
 * Dots are used to position GUI elements. Positioning can be changed by
 * changing init_dots() or individual elements of the dots and/or button
 * array.
 */

#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/panels.h"
#include "gui/ui_tokens.h"
#include "game/game.h"
#include "client/client.h"

extern int __textdisplay_sy;

static DOT dot_storage[MAX_DOT];
DOT *dot = dot_storage;
static BUT but_storage[MAX_BUT];
BUT *but = but_storage;

// dot and but helpers
static void set_dot(int didx, int x, int y, int flags);
static void set_but(int bidx, int x, int y, int hitrad, int flags);

static void set_dot(int didx, int x, int y, int flags)
{
	assert(didx >= 0 && didx < MAX_DOT && "set_dot: ill didx");

	dot[didx].flags = flags;
	dot[didx].x = x;
	dot[didx].y = y;
}

DLL_EXPORT int dotx(int didx)
{
	return dot[didx].x;
}

DLL_EXPORT int doty(int didx)
{
	if (dot[didx].flags & DOTF_TOPOFF) {
		return dot[didx].y + gui_topoff;
	}
	return dot[didx].y;
}

static void set_but(int bidx, int x, int y, int hitrad, int flags)
{
	assert(bidx >= 0 && bidx < MAX_BUT && "set_but: ill bidx");

	but[bidx].flags = flags;

	but[bidx].x = x;
	but[bidx].y = y;

	but[bidx].sqhitrad = hitrad * hitrad;
}

DLL_EXPORT int butx(int bidx)
{
	return but[bidx].x;
}

DLL_EXPORT int buty(int bidx)
{
	if (but[bidx].flags & BUTF_TOPOFF) {
		return but[bidx].y + gui_topoff;
	}
	return but[bidx].y;
}

#define stop (game_options & GO_SMALLTOP)
#define sbot (game_options & GO_SMALLBOT)

void dots_update(void)
{
	/* anchor the tutorial popup above the bottom bar, not the map bottom -
	 * with the fullscreen world view DOT_MBR is the bottom of the screen */
	int base = min(doty(DOT_MBR), doty(DOT_BOT) + 4);

	set_dot(DOT_TUT, (XRES - 410) / 2, base - 122 - (context_action_enabled() ? 30 : 0), 0);
}

/* ── Equipment paper doll ────────────────────────────────────────────────
 *
 * The worn-equipment slots used to be a 12-wide strip inside the top bar.
 * They are now a Diablo-style paper doll: the centre column is the body
 * axis (head, torso, belt, legs, feet) top to bottom, the flanking columns
 * carry the paired gear, with the two hands and the two rings on matching
 * rows. Entries are weatab indices (see weaname[] in gui_core.c);
 * WEA_NONE leaves the cell empty. */
#define WEA_NONE (-1)

static const int wea_doll[WEA_ROWS][WEA_COLS] = {
    /*  left        centre        right   */
    {4, 5, 6}, /* neck    | head   | cloak  */
    {9, 7, WEA_NONE}, /* arms    | body   |        */
    {1, 8, 2}, /* r.hand  | belt   | l.hand */
    {3, 10, 0}, /* l.ring  | legs   | r.ring */
    {WEA_NONE, 11, WEA_NONE}, /*         | feet   |        */
};

/* where slot i sits in the doll, or 0 when it is not placed */
static int wea_doll_cell(int slot, int *col, int *row)
{
	for (int r = 0; r < WEA_ROWS; r++) {
		for (int c = 0; c < WEA_COLS; c++) {
			if (wea_doll[r][c] == slot) {
				*col = c;
				*row = r;
				return 1;
			}
		}
	}
	return 0;
}

int wea_slot_pos(int slot, int *x, int *y)
{
	int col, row;

	if (!wea_doll_cell(slot, &col, &row)) {
		return 0;
	}
	*x = dot[DOT_WEA].x + col * FDX;
	*y = dot[DOT_WEA].y + row * FDX;
	return 1;
}

void init_dots(void)
{
	int i, x, y, xc, yc;

	// top left, bottom right of screen
	set_dot(DOT_TL, 0, 0, 0);
	set_dot(DOT_BR, XRES, YRES, 0);

	// top and bottom window
	set_dot(DOT_TOP, 0, 0, !stop ? 0 : DOTF_TOPOFF);
	if (!sbot) {
		set_dot(DOT_BOT, 0, YRES - 170, 0);
	} else {
		set_dot(DOT_BOT, 0, YRES - 130, 0);
	}
	set_dot(DOT_BO2, XRES, YRES, 0);

	/* every floating panel keeps this much clearance from the canvas edge */
	const int edge = 6 + UI_WIN_PAD;

	__condy = !sbot ? 4 : 3;
	__invdy = inv_grid_rows() ? inv_grid_rows() : __condy;
	__skldy = skl_grid_rows_effective();

	/* ── Inventory window (bottom right) ─────────────────────────────
	 * DOT_IN1/DOT_IN2 are the window's content rectangle: scrollbar rail,
	 * item grid and the purse/trashcan footer. DOT_INV stays the centre of
	 * the first grid cell. */
	{
		int cols = inv_grid_cols();
		int grid_w = cols * FDX;
		int grid_h = __invdy * FDX;
		int content_w = INV_RAIL_W + INV_RAIL_GAP + grid_w;
		int content_h = grid_h + INV_FOOT_H;

		set_dot(DOT_IN2, XRES - edge, YRES - edge, 0);
		set_dot(DOT_IN1, dotx(DOT_IN2) - content_w, doty(DOT_IN2) - content_h, 0);
		set_dot(DOT_INV, dotx(DOT_IN1) + INV_RAIL_W + INV_RAIL_GAP + FDX / 2, doty(DOT_IN1) + FDX / 2, 0);

		/* purse left, trashcan right, both on the footer row */
		y = doty(DOT_IN1) + grid_h + INV_FOOT_H / 2;
		set_dot(DOT_GLD, dotx(DOT_IN1) + INV_RAIL_W + INV_RAIL_GAP + 16, y, 0);
		set_dot(DOT_JNK, dotx(DOT_IN2) - 16, y, 0);
	}

	/* ── Skills window (bottom left) ─────────────────────────────────
	 * Holds either the skill list or, while a container is open, the 4x4
	 * shop/grave grid. It is sized for whichever it is currently showing,
	 * so a short skill list does not leave a container-sized void; display()
	 * re-runs init_dots() when a container opens or closes. */
	{
		int body_w = con_cnt ? CONDX * FDX : SKLWIDTH + 8;
		int content_h = con_cnt ? CONDY * FDX + 4 : __skldy * LINEHEIGHT + 6;

		set_dot(DOT_SKL, 8 + 4, YRES - edge - content_h + 8, 0);
		set_dot(DOT_SK2, 8 + body_w, YRES - edge, 0);
		set_dot(DOT_CON, 8 + FDX / 2 + 2, YRES - edge - content_h + FDX / 2 + 2, 0);
		panel_set_content_rect(PANEL_SKILLS, 8, YRES - edge - content_h, 8 + body_w + SKL_RAIL_W, YRES - edge);
	}

	/* scroll rails: the skills rail hugs the right edge of its window, the
	 * inventory rail the left edge of its own */
	set_dot(DOT_SCL, dotx(DOT_SK2) + SKL_RAIL_W / 2, doty(DOT_SKL), 0);
	set_dot(DOT_SCR, dotx(DOT_IN1) + INV_RAIL_W / 2, doty(DOT_IN1), 0);
	set_dot(DOT_SCU, 0, doty(DOT_IN1) + 8, 0);
	set_dot(DOT_SCD, 0, doty(DOT_IN1) + __invdy * FDX - 8, 0);

	/* ── Chat (bottom centre) ────────────────────────────────────────── */
	__textdisplay_sy = !sbot ? 150 : 110;
	set_dot(DOT_TXT, 230, YRES - edge - __textdisplay_sy, 0);
	set_dot(DOT_TX2, 624, YRES - edge, 0);
	panel_set_content_rect(PANEL_CHAT, dotx(DOT_TXT), doty(DOT_TXT), dotx(DOT_TX2), doty(DOT_TX2));

	/* ── Speed selector, stacked above the skills window ─────────────── */
	{
		int w = SPEED_SEG_W * 3 + SPEED_SEG_GAP * 2;
		int y2, y1;

		y2 = (YRES - edge - (doty(DOT_SK2) - doty(DOT_SKL)) - 8) - UI_WIN_TITLE_H - UI_WIN_PAD * 2 - 4;
		y1 = y2 - SPEED_SEG_H;
		set_dot(DOT_MOD, 8, y1, 0);
		panel_set_content_rect(PANEL_SPEED, 8, y1, 8 + w, y2);
	}

	/* ── Buff chips, stacked above the speed selector ────────────────── */
	{
		int w = BUFF_CHIP * BUFF_COUNT + BUFF_GAP * (BUFF_COUNT - 1);
		int h = BUFF_CHIP + BUFF_LABEL_H;
		int y1 = doty(DOT_MOD) - HUD_GRIP_H - UI_WIN_PAD - h - 4;

		set_dot(DOT_SSP, 8, y1, 0);
		panel_set_content_rect(PANEL_BUFFS, 8, y1, 8 + w, y1 + h);
	}

	/* ── Equipment paper doll (upper right, clear of the minimap) ─────── */
	{
		int content_w = WEA_COLS * FDX;
		int content_h = WEA_ROWS * FDX + WEA_FOOT_H;
		int x1 = XRES - edge - content_w;
		int y1 = 150;

		set_dot(DOT_WEA, x1 + FDX / 2, y1 + FDX / 2, 0);
		panel_set_content_rect(PANEL_EQUIPMENT, x1, y1, x1 + content_w, y1 + content_h);
	}

	// map top left, bottom right, center
	if (panels_fullscreen_world()) {
		// fullscreen world view: the map spans the whole canvas, the GUI
		// overlays it, and the player character sits at the true center
		set_dot(DOT_MTL, 0, 0, 0);
		set_dot(DOT_MBR, XRES, YRES, 0);
		set_dot(DOT_MCT, XRES / 2, YRES / 2, 0);
	} else {
		set_dot(DOT_MTL, 0, 40, !stop ? 0 : DOTF_TOPOFF);
		set_dot(DOT_MBR, XRES, min(doty(DOT_MTL) + 450 - (!stop ? 0 : 40), doty(DOT_BOT) + 4), 0);
		x = dotx(DOT_MBR) - dotx(DOT_MTL);
		y = doty(DOT_MBR) - doty(DOT_MTL) + (!stop ? 0 : 40);
		xc = x / 2;
		if (y < 430) {
			yc = y / 2 + 20;
		} else if (y < 450) {
			yc = y / 2 + 20 - y + 430;
		} else {
			yc = y / 2;
		}
		set_dot(DOT_MCT, dotx(DOT_MTL) + xc, doty(DOT_MTL) - (!stop ? 0 : 40) + yc, 0);
	}

	// help and quest window
	set_dot(DOT_HLP, 0, !stop ? 40 : 0, 0);
	set_dot(DOT_HL2, 222, (!stop ? 40 : 0) + 394, 0);

	// teleporter window
	set_dot(DOT_TEL, (XRES - 520) / 2, (doty(DOT_MBR) - doty(DOT_MTL) - 320 - (!stop ? 0 : 40)) / 2 + doty(DOT_MTL), 0);

	// look at window
	set_dot(DOT_LOK, 150, 50, 0);

	// color picker window
	set_dot(DOT_COL, 340, 210, 0);

	// action bar - no longer rendered; DOT_ACT only anchors the overhead
	// text (display_otext) and the BUT_ACT_* boxes below, which are dead
	set_dot(
	    DOT_ACT, XRES - LEGACY_ACTIONBAR_SLOTS * 40 - (XRES - LEGACY_ACTIONBAR_SLOTS * 40) / 2, doty(DOT_BOT) - 12, 0);

	// hotbar — centered above the bottom panel
	// DOT_HOTBAR marks the BOTTOM row (row 0). Additional rows stack upward.
	// The slot-name labels draw inside the lower part of each slot, so
	// toggling "Show Slot Names" must not move the bar (it used to jump
	// up by 10 pixels).
	{
		int shown_rows = hotbar_rows() > 0 ? hotbar_rows() : 1; /* 0 rows: keep a sane anchor */
		int row_offset = (shown_rows - 1) * (FDX + 2); /* extra rows above */
		set_dot(DOT_HOTBAR, (XRES - hotbar_visible_slots() * FDX) / 2, doty(DOT_BOT) - 15 - row_offset, 0);
	}

	// tutor window
	dots_update();

	set_but(BUT_MAP, XRES / 2, YRES / 2, 0, BUTF_NOHIT);

	/* worn equipment: paper-doll cells, empty cells stay unhittable */
	for (i = 0; i < 12; i++) {
		if (wea_slot_pos(i, &x, &y)) {
			set_but(BUT_WEA_BEG + i, x, y, 20, 0);
		} else {
			set_but(BUT_WEA_BEG + i, 0, 0, 0, BUTF_NOHIT);
		}
	}
	{
		int cols = inv_grid_cols();

		for (x = 0; x < cols; x++) {
			for (y = 0; y < __invdy; y++) {
				/* hitrad 20: half the cell. The grid borders the open world
				 * on every side now, so the classic radius-40 circles would
				 * steal hovers from map tiles well outside the window. */
				set_but(BUT_INV_BEG + x + y * cols, dot[DOT_INV].x + x * FDX, dot[DOT_INV].y + y * FDX, 20, 0);
			}
		}
		/* disable hit testing on the unused part of the button range */
		for (i = cols * __invdy; i <= BUT_INV_END - BUT_INV_BEG; i++) {
			set_but(BUT_INV_BEG + i, 0, 0, 0, BUTF_NOHIT);
		}
	}
	for (x = 0; x < CONDX; x++) {
		for (y = 0; y < CONDY; y++) {
			set_but(BUT_CON_BEG + x + y * CONDX, dot[DOT_CON].x + x * FDX, dot[DOT_CON].y + y * FDX, 20, 0);
		}
	}
	for (i = CONDX * CONDY; i <= BUT_CON_END - BUT_CON_BEG; i++) {
		set_but(BUT_CON_BEG + i, 0, 0, 0, BUTF_NOHIT);
	}
	for (i = 0; i <= BUT_SKL_END - BUT_SKL_BEG; i++) {
		if (i < __skldy) {
			set_but(BUT_SKL_BEG + i, dot[DOT_SKL].x, dot[DOT_SKL].y + i * LINEHEIGHT, 10, 0);
		} else {
			set_but(BUT_SKL_BEG + i, 0, 0, 0, BUTF_NOHIT);
		}
	}
	/* Legacy action bar: display_action() is empty, so nothing draws here -
	 * and an invisible control must not be clickable. The positions stay for
	 * reference; the hit boxes are dead. */
	for (i = 0; i < LEGACY_ACTIONBAR_SLOTS; i++) {
		set_but(BUT_ACT_BEG + i, dot[DOT_ACT].x + i * 40, dot[DOT_ACT].y, 18, BUTF_NOHIT);
	}

	/* gear lock: bottom-left of the equipment window's footer */
	{
		int cx1, cy1, cx2, cy2;

		if (panel_content_rect(PANEL_EQUIPMENT, &cx1, &cy1, &cx2, &cy2)) {
			set_but(BUT_WEA_LCK, cx1 + 10, cy2 - WEA_FOOT_H / 2, 18, 0);
		}
	}
	/* the action bar's padlock and open chevron are just as invisible as its
	 * slots - see above */
	set_but(BUT_ACT_LCK, dot[DOT_ACT].x - 40, dot[DOT_ACT].y, 18, BUTF_NOHIT);
	set_but(BUT_ACT_OPN, dot[DOT_ACT].x + LEGACY_ACTIONBAR_SLOTS * 40, dot[DOT_ACT].y, 18, BUTF_NOHIT);

	/* skills rail spans the skill list, inventory rail spans its grid */
	set_but(BUT_SCL_UP, dot[DOT_SCL].x, doty(DOT_SKL) - 2, 8, 0);
	set_but(BUT_SCL_TR, dot[DOT_SCL].x, doty(DOT_SKL) + 8, 10, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCL_DW, dot[DOT_SCL].x, doty(DOT_SK2) - 8, 8, 0);

	set_but(BUT_SCR_UP, dot[DOT_SCR].x, doty(DOT_IN1) + 8, 8, 0);
	set_but(BUT_SCR_TR, dot[DOT_SCR].x, doty(DOT_IN1) + 18, 10, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCR_DW, dot[DOT_SCR].x, doty(DOT_IN1) + __invdy * FDX - 8, 8, 0);

	set_but(BUT_GLD, dot[DOT_GLD].x, dot[DOT_GLD].y, 14, BUTF_CAPTURE);
	set_but(BUT_JNK, dot[DOT_JNK].x, dot[DOT_JNK].y, 14, 0);

	/* speed selector: stealth | normal | fast, slowest first */
	{
		static const int seg_but[3] = {BUT_MOD_WALK2, BUT_MOD_WALK0, BUT_MOD_WALK1};

		for (i = 0; i < 3; i++) {
			set_but(seg_but[i], dot[DOT_MOD].x + i * (SPEED_SEG_W + SPEED_SEG_GAP) + SPEED_SEG_W / 2,
			    dot[DOT_MOD].y + SPEED_SEG_H / 2, 20, 0);
		}
	}

	set_but(BUT_HELP_DRAG, (dotx(DOT_HLP) + dotx(DOT_HL2)) / 2, doty(DOT_HLP) + 6, 0, BUTF_CAPTURE | BUTF_MOVEEXEC);

	{
		int cols = hotbar_visible_slots();
		int rows = hotbar_rows();
		int total_active = cols * rows;
		for (i = 0; i < total_active; i++) {
			int row = i / cols;
			int col = i % cols;
			int bx = dot[DOT_HOTBAR].x + col * FDX;
			int by = dot[DOT_HOTBAR].y + row * (FDX + 2);
			/* hitrad 23: just past the pad's half-diagonal (22.6), so the
			 * whole 32x32 pad is clickable - at FDX/2 (20) the corners and a
			 * band between rows were dead. */
			set_but(BUT_HOTBAR_BEG + i, bx, by, 23, 0);
		}
		/* disable hit testing on inactive slots */
		for (i = total_active; i < HOTBAR_MAX_SLOTS; i++) {
			set_but(BUT_HOTBAR_BEG + i, 0, 0, 0, BUTF_NOHIT);
		}
	}

	/* the remaining panels publish their content rect from the geometry
	 * above; the inventory rect doubles as DOT_IN1/DOT_IN2 */
	panel_set_content_rect(PANEL_INVENTORY, dotx(DOT_IN1), doty(DOT_IN1), dotx(DOT_IN2), doty(DOT_IN2));
	panel_set_content_rect(PANEL_HOTBAR, 0, 0, 0, 0);

	/* title bars, close/minimize glyphs and resize grips derive from the
	 * content rects, so they are placed last - and before the offsets */
	panels_place_chrome_buttons(set_but);

	// shift every panel by its stored drag offset - must stay the last
	// step so it sees the complete default layout
	panels_apply_offsets();
}
