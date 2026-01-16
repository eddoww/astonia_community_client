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

extern int __textdisplay_sy;
extern int __textdisplay_sx;

// Independent UI scaling factors (1.0 = 100%, range 0.5 to 1.5)
// Default both to 0.75 (75%) for more game area - can be adjusted via saved options
DLL_EXPORT float ui_top_scale = 0.75f;
DLL_EXPORT float ui_bot_scale = 0.75f;

// Get the scaled height of the top UI panel
int ui_get_top_height(void)
{
	int base_height = (game_options & GO_SMALLTOP) ? UI_TOP_HEIGHT_SMALL : UI_TOP_HEIGHT_NORMAL;
	return (int)((float)base_height * ui_top_scale);
}

// Get the scaled height of the bottom UI panel
int ui_get_bot_height(void)
{
	int base_height = (game_options & GO_SMALLBOT) ? UI_BOT_HEIGHT_SMALL : UI_BOT_HEIGHT_NORMAL;
	return (int)((float)base_height * ui_bot_scale);
}

// Scale a value for bottom UI (relative offsets within the panel)
int ui_scale_bot(int value)
{
	return (int)((float)value * ui_bot_scale);
}

// Scale a value for top UI
int ui_scale_top(int value)
{
	return (int)((float)value * ui_top_scale);
}

// Get X offset to center bottom UI panel on screen
int ui_bot_x_offset(void)
{
	int scaled_width = (int)(800.0f * ui_bot_scale);
	return (XRES - scaled_width) / 2;
}

// Get X offset to center top UI panel on screen
int ui_top_x_offset(void)
{
	int scaled_width = (int)(800.0f * ui_top_scale);
	return (XRES - scaled_width) / 2;
}

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
	set_dot(DOT_TUT, (XRES - 410) / 2, doty(DOT_MBR) - 100 - (context_action_enabled() ? 30 : 0), 0);
}

void init_dots(void)
{
	int i, x, y, xc, yc;
	int bot_height = ui_get_bot_height();
	int top_height = ui_get_top_height();
	int bot_xoff = ui_bot_x_offset(); // X offset to center bottom UI
	int top_xoff = ui_top_x_offset(); // X offset to center top UI

	// top left, bottom right of screen
	set_dot(DOT_TL, 0, 0, 0);
	set_dot(DOT_BR, XRES, YRES, 0);

	// top and bottom window - use scaled heights, centered horizontally
	set_dot(DOT_TOP, top_xoff, 0, !stop ? 0 : DOTF_TOPOFF);
	set_dot(DOT_BOT, bot_xoff, YRES - bot_height, 0);
	set_dot(DOT_BO2, bot_xoff + ui_scale_bot(800), YRES, 0);

	// equipment, inventory, container. center of first displayed item.
	set_dot(DOT_WEA, top_xoff + ui_scale_top(180), ui_scale_top(20), !stop ? 0 : DOTF_TOPOFF);
	set_dot(DOT_INV, bot_xoff + ui_scale_bot(660), doty(DOT_BOT) + ui_scale_bot(27), 0);
	set_dot(DOT_CON, bot_xoff + ui_scale_bot(20), doty(DOT_BOT) + ui_scale_bot(27), 0);

	// inventory top left and bottom right
	set_dot(DOT_IN1, bot_xoff + ui_scale_bot(645), doty(DOT_BOT) + ui_scale_bot(2), 0);
	set_dot(DOT_IN2, bot_xoff + ui_scale_bot(795), doty(DOT_BO2) - ui_scale_bot(2), 0);
	// Keep same number of rows - scaled items fit in scaled space
	if (!sbot) {
		__invdy = 4;
	} else {
		__invdy = 3;
	}

	// scroll bars - X positions scaled for bottom panel
	set_dot(DOT_SCL, bot_xoff + ui_scale_bot(160 + 5), 0, 0);
	set_dot(DOT_SCR, bot_xoff + ui_scale_bot(640 - 5), 0, 0);
	set_dot(DOT_SCU, 0, doty(DOT_BOT) + ui_scale_bot(15), 0);
	if (!sbot) {
		set_dot(DOT_SCD, 0, doty(DOT_BOT) + ui_scale_bot(160), 0);
	} else {
		set_dot(DOT_SCD, 0, doty(DOT_BOT) + ui_scale_bot(120), 0);
	}

	// self spell bars (bless, potion, rage, ...)
	if (!sbot) {
		set_dot(DOT_SSP, bot_xoff + ui_scale_bot(179), doty(DOT_BOT) + ui_scale_bot(68), 0);
	} else {
		set_dot(DOT_SSP, bot_xoff + ui_scale_bot(179), doty(DOT_BOT) + ui_scale_bot(52), 0);
	}

	// chat text
	set_dot(DOT_TXT, bot_xoff + ui_scale_bot(230), doty(DOT_BOT) + ui_scale_bot(8), 0);
	__textdisplay_sx = ui_scale_bot(396); // scaled text width for line wrapping
	if (!sbot) {
		set_dot(DOT_TX2, bot_xoff + ui_scale_bot(624), doty(DOT_BOT) + ui_scale_bot(158), 0);
		__textdisplay_sy = ui_scale_bot(150);
	} else {
		set_dot(DOT_TX2, bot_xoff + ui_scale_bot(624), doty(DOT_BOT) + ui_scale_bot(118), 0);
		__textdisplay_sy = ui_scale_bot(110);
	}

	// skill list
	set_dot(DOT_SKL, bot_xoff + ui_scale_bot(8), doty(DOT_BOT) + ui_scale_bot(12), 0);
	set_dot(DOT_SK2, bot_xoff + ui_scale_bot(156), doty(DOT_BO2) - ui_scale_bot(2), 0);
	// Keep same number of rows - scaled text fits in scaled space
	if (!sbot) {
		__skldy = 16;
	} else {
		__skldy = 12;
	}

	// gold
	set_dot(DOT_GLD, bot_xoff + ui_scale_bot(195), doty(DOT_BO2) - ui_scale_bot(22), 0);

	// trashcan
	set_dot(DOT_JNK, bot_xoff + ui_scale_bot(610), doty(DOT_BO2) - ui_scale_bot(22), 0);

	// speed options: stealth/normal/fast
	set_dot(DOT_MOD, bot_xoff + ui_scale_bot(181), doty(DOT_BOT) + ui_scale_bot(24), 0);

	// map top left, bottom right, center - expand to fill available space
	// Map starts below the top UI (scaled height) and extends to the bottom UI
	set_dot(DOT_MTL, 0, top_height, !stop ? 0 : DOTF_TOPOFF);
	// Map bottom extends to the top of the bottom UI panel (with small overlap)
	set_dot(DOT_MBR, XRES, doty(DOT_BOT) + 4, 0);
	x = dotx(DOT_MBR) - dotx(DOT_MTL);
	y = doty(DOT_MBR) - doty(DOT_MTL) + (!stop ? 0 : top_height);
	xc = x / 2;
	yc = y / 2;
	set_dot(DOT_MCT, dotx(DOT_MTL) + xc, doty(DOT_MTL) - (!stop ? 0 : top_height) + yc, 0);
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

	// action bar (positioned above the bottom panel)
	set_dot(DOT_ACT, XRES - MAXACTIONSLOT * 40 - (XRES - MAXACTIONSLOT * 40) / 2, doty(DOT_BOT) - ui_scale_bot(12), 0);

	// tutor window
	dots_update();

	set_but(BUT_MAP, XRES / 2, YRES / 2, 0, BUTF_NOHIT);

	// note to self: do not use dotx(),doty() here because the moving top bar logic is built into the
	// button flags as well
	int scaled_fdx = ui_scale_bot(FDX);
	int scaled_hitrad = ui_scale_bot(40);
	int scaled_lineheight = ui_scale_bot(LINEHEIGHT);

	for (i = 0; i < 12; i++) {
		set_but(BUT_WEA_BEG + i, dot[DOT_WEA].x + i * ui_scale_top(FDX), dot[DOT_WEA].y + 0, ui_scale_top(40),
		    !stop ? 0 : BUTF_TOPOFF);
	}
	for (x = 0; x < 4; x++) {
		for (y = 0; y < __invdy; y++) {
			set_but(BUT_INV_BEG + x + y * 4, dot[DOT_INV].x + x * scaled_fdx, dot[DOT_INV].y + y * scaled_fdx,
			    scaled_hitrad, 0);
		}
	}
	for (x = 0; x < 4; x++) {
		for (y = 0; y < 4; y++) {
			set_but(BUT_CON_BEG + x + y * 4, dot[DOT_CON].x + x * scaled_fdx, dot[DOT_CON].y + y * scaled_fdx,
			    scaled_hitrad, 0);
		}
	}
	for (i = 0; i < __skldy; i++) {
		set_but(BUT_SKL_BEG + i, dot[DOT_SKL].x, dot[DOT_SKL].y + i * scaled_lineheight, ui_scale_bot(10), 0);
	}
	for (i = 0; i < MAXACTIONSLOT; i++) {
		set_but(BUT_ACT_BEG + i, dot[DOT_ACT].x + i * 40, dot[DOT_ACT].y, 18, 0);
	}

	set_but(BUT_WEA_LCK, dot[DOT_WEA].x + 12 * ui_scale_top(FDX) - ui_scale_top(12), dot[DOT_WEA].y + ui_scale_top(4),
	    ui_scale_top(18), !stop ? 0 : BUTF_TOPOFF);
	set_but(BUT_ACT_LCK, dot[DOT_ACT].x - 40, dot[DOT_ACT].y, 18, 0);
	set_but(BUT_ACT_OPN, dot[DOT_ACT].x + MAXACTIONSLOT * 40, dot[DOT_ACT].y, 18, 0);

	set_but(BUT_SCL_UP, dot[DOT_SCL].x + 0, dot[DOT_SCU].y + 0, ui_scale_bot(30), 0);
	set_but(
	    BUT_SCL_TR, dot[DOT_SCL].x + 0, dot[DOT_SCU].y + ui_scale_bot(10), scaled_hitrad, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCL_DW, dot[DOT_SCL].x + 0, dot[DOT_SCD].y + 0, ui_scale_bot(30), 0);

	set_but(BUT_SCR_UP, dot[DOT_SCR].x + 0, dot[DOT_SCU].y + 0, ui_scale_bot(30), 0);
	set_but(
	    BUT_SCR_TR, dot[DOT_SCR].x + 0, dot[DOT_SCU].y + ui_scale_bot(10), scaled_hitrad, BUTF_CAPTURE | BUTF_MOVEEXEC);
	set_but(BUT_SCR_DW, dot[DOT_SCR].x + 0, dot[DOT_SCD].y + 0, ui_scale_bot(30), 0);

	if (!stop) {
		set_but(BUT_GLD, dot[DOT_GLD].x + 0, dot[DOT_GLD].y + ui_scale_bot(10), ui_scale_bot(30), BUTF_CAPTURE);
	} else {
		set_but(BUT_GLD, dot[DOT_GLD].x + 0, dot[DOT_GLD].y + ui_scale_bot(10), ui_scale_bot(15), BUTF_CAPTURE);
	}

	set_but(BUT_JNK, dot[DOT_JNK].x + 0, dot[DOT_JNK].y + 0, ui_scale_bot(30), 0);

	set_but(BUT_MOD_WALK0, dot[DOT_MOD].x + ui_scale_bot(1 * 14), dot[DOT_MOD].y, ui_scale_bot(30), 0);
	set_but(BUT_MOD_WALK1, dot[DOT_MOD].x + ui_scale_bot(0 * 14), dot[DOT_MOD].y, ui_scale_bot(30), 0);
	set_but(BUT_MOD_WALK2, dot[DOT_MOD].x + ui_scale_bot(2 * 14), dot[DOT_MOD].y, ui_scale_bot(30), 0);
	set_but(BUT_HELP_DRAG, (dotx(DOT_HLP) + dotx(DOT_HL2)) / 2, doty(DOT_HLP) + 6, 0, BUTF_CAPTURE | BUTF_MOVEEXEC);
}
