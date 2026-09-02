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

void dots_update(void)
{
	/* anchor the tutorial popup above the bottom bar, not the map bottom -
	 * with the fullscreen world view DOT_MBR is the bottom of the screen */
	int base = min(UIYRES, doty(DOT_BOT) + 4);

	set_dot(DOT_TUT, (UIXRES - 410) / 2, base - 122 - (context_action_enabled() ? 30 : 0), 0);
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
	int i, x, y;

	// top left, bottom right of screen
	set_dot(DOT_TL, 0, 0, 0);
	set_dot(DOT_BR, UIXRES, UIYRES, 0);

	// top and bottom window
	set_dot(DOT_TOP, 0, 0, 0); /* legacy anchor - the top bar is gone */
	set_dot(DOT_BOT, 0, UIYRES - 170, 0);
	set_dot(DOT_BO2, UIXRES, UIYRES, 0);

	/* every floating panel keeps this much clearance from the canvas edge */
	const int edge = 6 + UI_WIN_PAD;

	int auto_rows = 4;

	__condy = con_grid_rows() ? con_grid_rows() : auto_rows;
	__invdy = inv_grid_rows() ? inv_grid_rows() : auto_rows;
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

		set_dot(DOT_IN2, UIXRES - edge, UIYRES - edge, 0);
		set_dot(DOT_IN1, dotx(DOT_IN2) - content_w, doty(DOT_IN2) - content_h, 0);
		set_dot(DOT_INV, dotx(DOT_IN1) + INV_RAIL_W + INV_RAIL_GAP + FDX / 2, doty(DOT_IN1) + FDX / 2, 0);

		/* purse left, trashcan right, both on the footer row */
		y = doty(DOT_IN1) + grid_h + INV_FOOT_H / 2;
		set_dot(DOT_GLD, dotx(DOT_IN1) + INV_RAIL_W + INV_RAIL_GAP + 16, y, 0);
		set_dot(DOT_JNK, dotx(DOT_IN2) - 16, y, 0);
	}

	/* ── Skills window (bottom left) ───────────────────────────────── */
	int skl_list_h = __skldy * LINEHEIGHT + 6;
	{
		int body_w = SKLWIDTH + 8;
		int content_h = skl_list_h;

		set_dot(DOT_SKL, 8 + 4, UIYRES - edge - content_h + 8, 0);
		set_dot(DOT_SK2, 8 + body_w, UIYRES - edge, 0);
		panel_set_content_rect(PANEL_SKILLS, 8, UIYRES - edge - content_h, 8 + body_w + SKL_RAIL_W, UIYRES - edge);
	}

	/* ── Container window (shop / grave / depot grid), its own window to
	 *    the left of the inventory so skills and merchants can be open at
	 *    the same time; display() re-runs init_dots() when one opens ──── */
	{
		int grid_w = CONDX * FDX;
		int grid_h = CONDY * FDX;
		int content_w = grid_w + 4 + SKL_RAIL_W;
		int content_h = grid_h + 4;
		/* above the hotbar's default row(s) - the hotbar is laid out further
		 * down in this function, so its top edge is derived here the same way */
		int hb_rows = hotbar_rows() > 0 ? hotbar_rows() : 1;
		int hb_top = (UIYRES - 170) - 15 - (hb_rows - 1) * (FDX + 2) - FDX / 2;
		int x2 = dotx(DOT_IN1) - UI_WIN_PAD * 2 - 10;
		int x1 = x2 - content_w;
		int y2 = hb_top - UI_WIN_PAD * 2 - 4;
		int y1 = y2 - content_h;

		set_dot(DOT_CN1, x1, y1, 0);
		set_dot(DOT_CN2, x2, y2, 0);
		set_dot(DOT_CON, x1 + 2 + FDX / 2, y1 + 2 + FDX / 2, 0);
		set_dot(DOT_CSC, x2 - SKL_RAIL_W / 2, y1, 0);
		panel_set_content_rect(PANEL_CONTAINER, x1, y1, x2, y2);
	}

	/* scroll rails: the skills rail hugs the right edge of its window, the
	 * inventory rail the left edge of its own */
	set_dot(DOT_SCL, dotx(DOT_SK2) + SKL_RAIL_W / 2, doty(DOT_SKL), 0);
	set_dot(DOT_SCR, dotx(DOT_IN1) + INV_RAIL_W / 2, doty(DOT_IN1), 0);
	set_dot(DOT_SCU, 0, doty(DOT_IN1) + 8, 0);
	set_dot(DOT_SCD, 0, doty(DOT_IN1) + __invdy * FDX - 8, 0);

	/* ── Chat (bottom centre) ────────────────────────────────────────── */
	__textdisplay_sy = 150;
	set_dot(DOT_TXT, 230, UIYRES - edge - __textdisplay_sy, 0);
	set_dot(DOT_TX2, 624, UIYRES - edge, 0);
	/* no PANEL_CHAT content rect: the classic chat panel is gone (the
	 * tabbed chat window owns chat); the text dots stay for the loading
	 * screens' line display */

	/* ── Speed selector, stacked above the skills window ─────────────── */
	{
		int w = SPEED_SEG_W * 3 + SPEED_SEG_GAP * 2;
		/* stacked above the skills window's TALLER possible shape (skill
		 * list or merchant grid) so neither opening a shop nor closing one
		 * ever bumps these two around - the anchor is a constant of the
		 * current settings, not of what the window happens to show */
		/* anchored to the DEFAULT window heights, not the live ones -
		 * resizing the skills list or a merchant grid must never bump
		 * these two around */
		int skl_max_h = max(SKL_GRID_DEF_ROWS * LINEHEIGHT + 6, 4 * FDX + 4);
		int y2 = UIYRES - edge - skl_max_h - UI_WIN_TITLE_H - UI_WIN_PAD * 2 - 4;
		int y1 = y2 - SPEED_SEG_H;

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
		int content_w = WEA_COLS * FDX + WEA_BONUS_GAP + WEA_BONUS_W;
		int content_h = WEA_ROWS * FDX + WEA_FOOT_H;
		int x1 = UIXRES - edge - content_w;
		int y1 = 150;

		set_dot(DOT_WEA, x1 + FDX / 2, y1 + FDX / 2, 0);
		panel_set_content_rect(PANEL_EQUIPMENT, x1, y1, x1 + content_w, y1 + content_h);
	}

	/* ── Status panel: level + military as two long WoW-style bars at the
	 *    very bottom of the screen, centered ──────────────────────────── */
	{
		int w = UIXRES * STAT_W_NUM / STAT_W_DEN;
		int x1, y1;

		if (w < STAT_MIN_W) {
			w = STAT_MIN_W;
		}
		x1 = (UIXRES - w) / 2;
		y1 = UIYRES - 2 * STAT_ROW_H - 2;
		set_dot(DOT_STAT, x1, y1, 0);
		panel_set_content_rect(PANEL_STATUS, x1, y1, x1 + w, y1 + 2 * STAT_ROW_H);
		set_but(BUT_EXPBAR, x1 + w / 2, y1 + STAT_BAR_H / 2, 0, BUTF_NOHIT); /* rect-hit */
		set_but(BUT_MILBAR, x1 + w / 2, y1 + STAT_ROW_H + STAT_BAR_H / 2, 0, BUTF_NOHIT);
	}

	/* ── System menu strip: Menu | Help | Quests, top-right of the world,
	 *    left of the minimap's column ─────────────────────────────────── */
	{
		int w = 3 * SYSM_BTN_W + 2 * SYSM_GAP;
		int x1 = UIXRES - 150 - w;

		set_dot(DOT_MENU, x1, 8, 0);
		panel_set_content_rect(PANEL_SYSMENU, x1, 8, x1 + w, 8 + SYSM_BTN_H);
		set_but(BUT_EXIT, x1 + SYSM_BTN_W / 2, 8 + SYSM_BTN_H / 2, 14, 0);
		set_but(BUT_HELP, x1 + SYSM_BTN_W + SYSM_GAP + SYSM_BTN_W / 2, 8 + SYSM_BTN_H / 2, 14, 0);
		set_but(BUT_QUEST, x1 + 2 * (SYSM_BTN_W + SYSM_GAP) + SYSM_BTN_W / 2, 8 + SYSM_BTN_H / 2, 14, 0);
	}

	/* ── Classic flip clock, under the system menu (hidden by default) ── */
	{
		int x1 = UIXRES - 150 - CLK_W;

		set_dot(DOT_CLK, x1, 8 + SYSM_BTN_H + HUD_GRIP_H + UI_WIN_PAD + 8, 0);
		panel_set_content_rect(PANEL_CLOCK, dotx(DOT_CLK), doty(DOT_CLK), dotx(DOT_CLK) + CLK_W, doty(DOT_CLK) + CLK_H);
	}

	/* ── Spellbook window, centred over the hotbar ───────────────────── */
	{
		int avail = spellbook_slot_count();
		int cols = avail < SPB_COLS ? avail : SPB_COLS;
		int rows = (avail + SPB_COLS - 1) / SPB_COLS;
		int cx = (UIXRES - hotbar_visible_slots() * FDX) / 2 + hotbar_visible_slots() * FDX / 2;
		int x1, y1;

		if (cols < 1) {
			cols = 1;
		}
		if (rows < 1) {
			rows = 1;
		}
		x1 = cx - cols * FDX / 2;
		y1 = doty(DOT_BOT) - 40 - rows * FDX;
		if (y1 < 60) {
			y1 = 60;
		}
		set_dot(DOT_SPB, x1, y1, 0);
		panel_set_content_rect(PANEL_SPELLBOOK, x1, y1, x1 + cols * FDX, y1 + rows * FDX);
	}

	// map: the world is always fullscreen - it spans the whole CANVAS (not
	// the UI layer: the world never scales with the UI), the GUI overlays
	// it, and the player character sits at the true center
	/* the world fills the window, so in UI space the "map area" is the whole
	 * canvas - mods and overlays treat these as the screen box. The world
	 * renderer itself works in native dims and uses XRES/YRES directly. */
	set_dot(DOT_MTL, 0, 0, 0);
	set_dot(DOT_MBR, UIXRES, UIYRES, 0);
	set_dot(DOT_MCT, UIXRES / 2, UIYRES / 2, 0);

	/* ── Minimap (top right): no frame, the footprint IS the map - the
	 *    small circle's box or the big square, whichever is showing. Both
	 *    hang off the same right-aligned default, so flipping the size
	 *    keeps the right edge in place. ─────────────────────────────── */
	{
		int d = minimap_footprint();

		set_dot(DOT_MMAP, UIXRES - d - edge, 46, 0);
		panel_set_content_rect(PANEL_MINIMAP, dotx(DOT_MMAP), doty(DOT_MMAP), dotx(DOT_MMAP) + d, doty(DOT_MMAP) + d);
	}

	/* help and quest window: clear of the speed / buff plates that sit in
	 * the bottom-left corner, so its navigation bar is never buried */
	set_dot(DOT_HLP, 164, 60, 0);
	set_dot(DOT_HL2, 164 + 222, 60 + 394, 0);
	panel_set_content_rect(PANEL_HELP, dotx(DOT_HLP), doty(DOT_HLP), dotx(DOT_HL2), doty(DOT_HL2));

	// teleporter window
	set_dot(DOT_TEL, (UIXRES - 520) / 2, (UIYRES - 320) / 2, 0);

	/* look-at window: a real panel now (draggable, minimizable, remembered);
	 * the dot is the content's top-left, the title bar sits above it */
	set_dot(DOT_LOK, 150, 50 + UI_WIN_TITLE_H, 0);
	panel_set_content_rect(
	    PANEL_LOOK, dotx(DOT_LOK), doty(DOT_LOK), dotx(DOT_LOK) + LOOK_W, doty(DOT_LOK) + LOOK_H - UI_WIN_TITLE_H);

	// color picker window
	set_dot(DOT_COL, 340, 210, 0);

	// action bar - no longer rendered; DOT_ACT only anchors the overhead
	// text (display_otext) and the BUT_ACT_* boxes below, which are dead
	set_dot(DOT_ACT, UIXRES - LEGACY_ACTIONBAR_SLOTS * 40 - (UIXRES - LEGACY_ACTIONBAR_SLOTS * 40) / 2,
	    doty(DOT_BOT) - 12, 0);

	// hotbar — centered above the bottom panel
	// DOT_HOTBAR marks the BOTTOM row (row 0). Additional rows stack upward.
	// The slot-name labels draw inside the lower part of each slot, so
	// toggling "Show Slot Names" must not move the bar (it used to jump
	// up by 10 pixels).
	{
		int shown_rows = hotbar_rows() > 0 ? hotbar_rows() : 1; /* 0 rows: keep a sane anchor */
		int row_offset = (shown_rows - 1) * (FDX + 2); /* extra rows above */
		set_dot(DOT_HOTBAR, (UIXRES - hotbar_visible_slots() * FDX) / 2, doty(DOT_BOT) - 15 - row_offset, 0);
	}

	// tutor window
	dots_update();

	set_but(BUT_MAP, UIXRES / 2, UIYRES / 2, 0, BUTF_NOHIT);

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
	{
		int cols = CONDX;

		for (x = 0; x < cols; x++) {
			for (y = 0; y < CONDY; y++) {
				set_but(BUT_CON_BEG + x + y * cols, dot[DOT_CON].x + x * FDX, dot[DOT_CON].y + y * FDX, 20, 0);
			}
		}
		for (i = cols * CONDY; i <= BUT_CON_END - BUT_CON_BEG; i++) {
			set_but(BUT_CON_BEG + i, 0, 0, 0, BUTF_NOHIT);
		}
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

	/* container rail spans its grid */
	set_but(BUT_CSC_UP, dot[DOT_CSC].x, doty(DOT_CN1) + 8, 8, 0);
	set_but(BUT_CSC_TR, dot[DOT_CSC].x, doty(DOT_CN1) + 18, 10, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_CSC_DW, dot[DOT_CSC].x, doty(DOT_CN2) - 8, 8, 0);

	set_but(BUT_GLD, dot[DOT_GLD].x, dot[DOT_GLD].y, 14, BUTF_CAPTURE);
	set_but(BUT_JNK, dot[DOT_JNK].x, dot[DOT_JNK].y, 14, 0);

	/* speed selector: fast | normal | stealth, left to right */
	{
		static const int seg_but[3] = {BUT_MOD_WALK1, BUT_MOD_WALK0, BUT_MOD_WALK2};

		for (i = 0; i < 3; i++) {
			set_but(seg_but[i], dot[DOT_MOD].x + i * (SPEED_SEG_W + SPEED_SEG_GAP) + SPEED_SEG_W / 2,
			    dot[DOT_MOD].y + SPEED_SEG_H / 2, 20, 0);
		}
	}


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
	/* the hotbar's footprint (for clamping and snapping) covers the slot
	 * grid plus its grab tab on the left - dragging it below the screen
	 * used to strand it beyond recovery */
	{
		int rows = hotbar_rows() > 0 ? hotbar_rows() : 1;

		panel_set_content_rect(PANEL_HOTBAR, dot[DOT_HOTBAR].x - FDX / 2 - 20, dot[DOT_HOTBAR].y - FDX / 2,
		    dot[DOT_HOTBAR].x + hotbar_visible_slots() * FDX - FDX / 2,
		    dot[DOT_HOTBAR].y + (rows - 1) * (FDX + 2) + FDX / 2 + 10);
	}

	/* title bars, close/minimize glyphs and resize grips derive from the
	 * content rects, so they are placed last - and before the offsets */
	panels_place_chrome_buttons(set_but);

	/* the hotbar is frameless (it is an action bar, not a window), so it
	 * keeps the small proximity grab tab left of its first slot, with its
	 * padlock right below the tab */
	if (hotbar_rows() > 0) {
		set_but(BUT_DRAG_HOTBAR, dot[DOT_HOTBAR].x - FDX / 2 - 8, dot[DOT_HOTBAR].y, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
		set_but(BUT_PLOCK_BEG + PANEL_HOTBAR, dot[DOT_HOTBAR].x - FDX / 2 - 8, dot[DOT_HOTBAR].y + 18, 8, 0);
	}

	/* the minimap is grabbed anywhere on its face (panels_frame_button does
	 * the rectangular test), so its drag button only marks the centre; the
	 * padlock sits in the bottom-left corner, clear of the "N" label */
	{
		int x1, y1, x2, y2;

		if (panel_content_rect(PANEL_MINIMAP, &x1, &y1, &x2, &y2)) {
			set_but(BUT_DRAG_BEG + PANEL_MINIMAP, (x1 + x2) / 2, (y1 + y2) / 2, 0,
			    BUTF_CAPTURE | BUTF_MOVEEXEC | BUTF_NOHIT);
			set_but(BUT_PLOCK_BEG + PANEL_MINIMAP, x1 + 7, y2 - 7, 6, 0);
		}
	}

	// shift every panel by its stored drag offset - must stay the last
	// step so it sees the complete default layout
	panels_apply_offsets();

	/* the minimap caches its anchors - refresh them from the moved panel */
	minimap_reanchor();
}
