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

	// equipment, inventory, container. center of first displayed item.
	/* equipment row is centered between the top bar's left ornament and
	 * the right control cluster (== 180 on the classic 800px bar) */
	set_dot(DOT_WEA, (XRES - 480) / 2 + 20, 20, !stop ? 0 : DOTF_TOPOFF);
	// inventory cluster is anchored to the bottom-right corner (the classic
	// grid lives in art columns 645-795 of the XRES0-wide bottom bar, which
	// is drawn right-anchored too); the container keeps the classic 4-wide
	// count regardless of the inventory grid setting
	__condy = !sbot ? 4 : 3;
	__invdy = inv_grid_rows() ? inv_grid_rows() : __condy;

	// last row's bottom edge stays flush with the classic bar bottom; extra
	// rows and columns grow up/left from there. At 4 cols and auto rows this
	// reproduces the classic positions exactly (XRES-140, BOT+27).
	set_dot(DOT_IN2, XRES - 5, doty(DOT_BO2) - 2, 0);
	set_dot(DOT_INV, XRES - 20 - (inv_grid_cols() - 1) * FDX, doty(DOT_IN2) - 1 - __invdy * FDX + FDX / 2, 0);
	set_dot(DOT_IN1, dotx(DOT_INV) - 15, doty(DOT_INV) - 25, 0);
	set_dot(DOT_CON, 20, doty(DOT_BOT) + 27, 0);

	// scroll bars (the right rail hugs the inventory grid's left edge:
	// XRES-165 for the classic grid, further left for denser grids)
	set_dot(DOT_SCL, 160 + 5, 0, 0);
	set_dot(DOT_SCR, dotx(DOT_IN1) - 10, 0, 0);
	set_dot(DOT_SCU, 0, doty(DOT_BOT) + 15, 0);
	if (!sbot) {
		set_dot(DOT_SCD, 0, doty(DOT_BOT) + 160, 0);
	} else {
		set_dot(DOT_SCD, 0, doty(DOT_BOT) + 120, 0);
	}

	// self spell bars (bless, potion, rage, ...)
	if (!sbot) {
		set_dot(DOT_SSP, dotx(DOT_BOT) + 179, doty(DOT_BOT) + 68, 0);
	} else {
		set_dot(DOT_SSP, dotx(DOT_BOT) + 179, doty(DOT_BOT) + 52, 0);
	}

	// chat text
	set_dot(DOT_TXT, 230, doty(DOT_BOT) + 8, 0);
	if (!sbot) {
		set_dot(DOT_TX2, 624, doty(DOT_BOT) + 158, 0);
		__textdisplay_sy = 150;
	} else {
		set_dot(DOT_TX2, 624, doty(DOT_BOT) + 118, 0);
		__textdisplay_sy = 110;
	}

	// skill list
	set_dot(DOT_SKL, 8, doty(DOT_BOT) + 12, 0);
	set_dot(DOT_SK2, 156, doty(DOT_BO2) - 2, 0);
	if (!sbot) {
		__skldy = 16;
	} else {
		__skldy = 12;
	}

	// gold
	set_dot(DOT_GLD, 195, doty(DOT_BO2) - 22, 0);

	// trashcan (right-anchored, next to the inventory)
	set_dot(DOT_JNK, XRES - 190, doty(DOT_BO2) - 22, 0);

	// speed options: stealth/normal/fast
	set_dot(DOT_MOD, 181, doty(DOT_BOT) + 24, 0);

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
	// note("map: %dx%d, center: %d,%d, origin: %d,%d,
	// (%d,%d)",x,y,dotx(DOT_MCT),doty(DOT_MCT),dotx(DOT_MTL),doty(DOT_MTL),dotx(DOT_MBR),doty(DOT_MBR));

	// help and quest window
	set_dot(DOT_HLP, 0, !stop ? 40 : 0, 0);
	set_dot(DOT_HL2, 222, (!stop ? 40 : 0) + 394, 0);

	// teleporter window
	set_dot(DOT_TEL, (XRES - 520) / 2, (doty(DOT_MBR) - doty(DOT_MTL) - 320 - (!stop ? 0 : 40)) / 2 + doty(DOT_MTL), 0);

	// look at window
	set_dot(DOT_LOK, 150, 50, 0);

	// color picker window
	set_dot(DOT_COL, 340, 210, 0);

	// action bar (kept for BUT_ACT_* hit testing, but no longer rendered)
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

	// note to self: do not use dotx(),doty() here because the moving top bar logic is built into the
	// button flags as well
	for (i = 0; i < 12; i++) {
		set_but(BUT_WEA_BEG + i, dot[DOT_WEA].x + i * FDX, dot[DOT_WEA].y + 0, 40, !stop ? 0 : BUTF_TOPOFF);
	}
	{
		int cols = inv_grid_cols();
		/* the classic grid sits inside the bar art, so its generous radius-40
		 * hit circles only overreach onto chrome; denser grids border the
		 * open map and use the hotbar's pad-sized radius instead so slots
		 * don't grab hovers from map tiles above the grid */
		int hitrad = inv_grid_is_classic() ? 40 : 23;

		for (x = 0; x < cols; x++) {
			for (y = 0; y < __invdy; y++) {
				set_but(BUT_INV_BEG + x + y * cols, dot[DOT_INV].x + x * FDX, dot[DOT_INV].y + y * FDX, hitrad, 0);
			}
		}
		/* disable hit testing on the unused part of the button range */
		for (i = cols * __invdy; i <= BUT_INV_END - BUT_INV_BEG; i++) {
			set_but(BUT_INV_BEG + i, 0, 0, 0, BUTF_NOHIT);
		}
	}
	for (x = 0; x < 4; x++) {
		for (y = 0; y < 4; y++) {
			set_but(BUT_CON_BEG + x + y * 4, dot[DOT_CON].x + x * FDX, dot[DOT_CON].y + y * FDX, 40, 0);
		}
	}
	for (i = 0; i < 16; i++) {
		set_but(BUT_SKL_BEG + i, dot[DOT_SKL].x, dot[DOT_SKL].y + i * LINEHEIGHT, 10, 0);
	}
	for (i = 0; i < LEGACY_ACTIONBAR_SLOTS; i++) {
		set_but(BUT_ACT_BEG + i, dot[DOT_ACT].x + i * 40, dot[DOT_ACT].y, 18, 0);
	}

	/* gear lock sits flush against the right-anchored Menu cluster, as it
	 * did on the classic 800px bar - not floating after the equipment row */
	set_but(BUT_WEA_LCK, XRES - XRES0 + 648, dot[DOT_WEA].y + 4, 18, !stop ? 0 : BUTF_TOPOFF);
	set_but(BUT_ACT_LCK, dot[DOT_ACT].x - 40, dot[DOT_ACT].y, 18, 0);
	set_but(BUT_ACT_OPN, dot[DOT_ACT].x + LEGACY_ACTIONBAR_SLOTS * 40, dot[DOT_ACT].y, 18, 0);

	set_but(BUT_SCL_UP, dot[DOT_SCL].x + 0, dot[DOT_SCU].y + 0, 30, 0);
	set_but(BUT_SCL_TR, dot[DOT_SCL].x + 0, dot[DOT_SCU].y + 10, 40, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCL_DW, dot[DOT_SCL].x + 0, dot[DOT_SCD].y + 0, 30, 0);

	set_but(BUT_SCR_UP, dot[DOT_SCR].x + 0, dot[DOT_SCU].y + 0, 30, 0);
	set_but(BUT_SCR_TR, dot[DOT_SCR].x + 0, dot[DOT_SCU].y + 10, 40, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCR_DW, dot[DOT_SCR].x + 0, dot[DOT_SCD].y + 0, 30, 0);

	if (!stop) {
		set_but(BUT_GLD, dot[DOT_GLD].x + 0, dot[DOT_GLD].y + 10, 30, BUTF_CAPTURE);
	} else {
		set_but(BUT_GLD, dot[DOT_GLD].x + 0, dot[DOT_GLD].y + 10, 15, BUTF_CAPTURE);
	}

	set_but(BUT_JNK, dot[DOT_JNK].x + 0, dot[DOT_JNK].y + 0, 30, 0);

	set_but(BUT_MOD_WALK0, dot[DOT_MOD].x + 1 * 14, dot[DOT_MOD].y + 0 * 30, 30, 0);
	set_but(BUT_MOD_WALK1, dot[DOT_MOD].x + 0 * 14, dot[DOT_MOD].y + 0 * 30, 30, 0);
	set_but(BUT_MOD_WALK2, dot[DOT_MOD].x + 2 * 14, dot[DOT_MOD].y + 0 * 30, 30, 0);
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
			 * band between rows were dead. NOT the inventory's 40: unlike
			 * the inventory grid, the hotbar borders the open map, and a
			 * 40-radius circle hovered slots from 24px above the pads. */
			set_but(BUT_HOTBAR_BEG + i, bx, by, 23, 0);
		}
		/* disable hit testing on inactive slots */
		for (i = total_active; i < HOTBAR_MAX_SLOTS; i++) {
			set_but(BUT_HOTBAR_BEG + i, 0, 0, 0, BUTF_NOHIT);
		}
	}

	// panel drag handles (small hit radius, mouse-capture drag)
	set_but(
	    BUT_DRAG_SKILLS, (dot[DOT_SKL].x + dot[DOT_SK2].x) / 2, dot[DOT_SKL].y - 6, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_DRAG_CHAT, (dot[DOT_TXT].x + dot[DOT_TX2].x) / 2, dot[DOT_TXT].y - 6, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_DRAG_INV, (dot[DOT_IN1].x + dot[DOT_IN2].x) / 2, dot[DOT_IN1].y - 6, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_DRAG_GOLD, dot[DOT_GLD].x, dot[DOT_GLD].y - 6, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_DRAG_SPEED, dot[DOT_MOD].x + 14, dot[DOT_MOD].y - 16, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_DRAG_BUFFS, dot[DOT_SSP].x + 15, dot[DOT_SSP].y - 6, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
	if (hotbar_rows() > 0) {
		set_but(BUT_DRAG_HOTBAR, dot[DOT_HOTBAR].x - FDX / 2 - 8, dot[DOT_HOTBAR].y, 12, BUTF_CAPTURE | BUTF_MOVEEXEC);
	} else {
		set_but(BUT_DRAG_HOTBAR, 0, 0, 0, BUTF_NOHIT);
	}
	/* equipment drag handle sits left of the first worn slot; it needs the
	 * same TOPOFF flag as the slots so it slides with the small top bar */
	set_but(BUT_DRAG_EQUIPMENT, dot[DOT_WEA].x - FDX / 2 - 8, dot[DOT_WEA].y, 12,
	    BUTF_CAPTURE | BUTF_MOVEEXEC | (!stop ? 0 : BUTF_TOPOFF));

	// shift every panel by its stored drag offset - must stay the last
	// step so it sees the complete default layout
	panels_apply_offsets();
}
