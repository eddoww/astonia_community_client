/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * GUI panel system - see panels.h for the overview.
 *
 * Layout model: init_dots() always computes the pristine default layout and
 * publishes each panel's content rectangle, then panels_apply_offsets()
 * shifts each panel's dots, buttons and content rect by the panel's stored
 * (dx,dy). Dragging updates the stored offset and shifts the live geometry by
 * the same delta, so a later init_dots() (resolution change, small-bottom
 * toggle, grid resize) reproduces the moved layout instead of losing it.
 *
 * Frames are derived, never stored: the chrome rectangle of a panel is its
 * content rect grown by the padding its frame kind asks for, so the window
 * follows whatever the panel's content geometry turns out to be.
 */

#include <stdint.h>
#include <stddef.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/panels.h"
#include "gui/gesture.h"
#include "gui/input_bind.h"
#include "gui/ui_draw.h"
#include "gui/ui_tokens.h"
#include "client/client.h"
#include "game/game.h"
#include "lib/cjson/cJSON.h"

DLL_EXPORT int gui_overlay_visible = 1;

typedef struct {
	int beg, end; /* inclusive button id range */
} ButRange;

typedef struct {
	const char *id;
	const char *name;
	const int *dots;
	int ndots;
	const ButRange *buts;
	int nbuts;
	int frame; /* PANEL_FRAME_* */
	int resizable;
	int default_visible; /* summoned windows start closed */
	int visible;
	int collapsed;
	int locked; /* position/size lock (per panel)   */
	int dx, dy; /* the player's offset from the default layout (persisted) */
	int cdx, cdy; /* transient clamp shift on top of it, this layout pass only */
	int cx1, cy1, cx2, cy2; /* content rect, published by init_dots() */
} Panel;

/* dot lists - the first dot is the clamp reference kept on screen */
static const int skills_dots[] = {DOT_SKL, DOT_SK2};
static const int container_dots[] = {DOT_CN1, DOT_CN2, DOT_CON, DOT_CSC};
static const int chat_dots[] = {DOT_TXT, DOT_TX2};
static const int inv_dots[] = {DOT_INV, DOT_IN1, DOT_IN2, DOT_GLD, DOT_JNK};
static const int speed_dots[] = {DOT_MOD};
static const int buffs_dots[] = {DOT_SSP};
static const int hotbar_dots[] = {DOT_HOTBAR};
static const int equipment_dots[] = {DOT_WEA};
static const int spellbook_dots[] = {DOT_SPB};
static const int status_dots[] = {DOT_STAT};
static const int sysmenu_dots[] = {DOT_MENU};
static const int clock_dots[] = {DOT_CLK};
static const int help_dots[] = {DOT_HLP, DOT_HL2};

static const ButRange skills_buts[] = {{BUT_SKL_BEG, BUT_SKL_END}, {BUT_SCL_UP, BUT_SCL_DW}};
static const ButRange container_buts[] = {{BUT_CON_BEG, BUT_CON_END}, {BUT_CSC_UP, BUT_CSC_DW}};
static const ButRange chat_buts[] = {{0, -1}};
static const ButRange inv_buts[] = {{BUT_INV_BEG, BUT_INV_END}, {BUT_SCR_UP, BUT_SCR_DW}, {BUT_GLD, BUT_JNK}};
static const ButRange speed_buts[] = {{BUT_MOD_WALK0, BUT_MOD_WALK2}};
static const ButRange buffs_buts[] = {{0, -1}};
static const ButRange hotbar_buts[] = {{BUT_HOTBAR_BEG, BUT_HOTBAR_END}};
static const ButRange equipment_buts[] = {{BUT_WEA_BEG, BUT_WEA_END}, {BUT_WEA_LCK, BUT_WEA_LCK}};
static const ButRange spellbook_buts[] = {{0, -1}}; /* cells are hit-tested by spellbook_ui.c */
static const ButRange status_buts[] = {{BUT_EXPBAR, BUT_EXPBAR}, {BUT_MILBAR, BUT_MILBAR}};
static const ButRange sysmenu_buts[] = {{BUT_EXIT, BUT_HELP}, {BUT_QUEST, BUT_QUEST}};
static const ButRange clock_buts[] = {{0, -1}};
static const int minimap_dots[] = {DOT_MMAP};
static const ButRange minimap_buts[] = {{0, -1}};
static const ButRange help_buts[] = {{0, -1}}; /* page controls are rect-hit in gui_buttons.c */

#define PANEL_ENTRY(idstr, namestr, d, b, fr, rs, vis)                                                                 \
	{idstr, namestr, d, ARRAYSIZE(d), b, ARRAYSIZE(b), fr, rs, vis, vis, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

static Panel panels[MAX_PANEL] = {
    [PANEL_SKILLS] = PANEL_ENTRY("skills", "Skills", skills_dots, skills_buts, PANEL_FRAME_WINDOW, 1, 1),
    [PANEL_CHAT] = PANEL_ENTRY("chat", "Chat", chat_dots, chat_buts, PANEL_FRAME_HUD, 0, 1),
    [PANEL_INVENTORY] = PANEL_ENTRY("inventory", "Inventory", inv_dots, inv_buts, PANEL_FRAME_WINDOW, 1, 1),
    [PANEL_SPEED] = PANEL_ENTRY("speed", "Speed", speed_dots, speed_buts, PANEL_FRAME_HUD, 0, 1),
    [PANEL_BUFFS] = PANEL_ENTRY("buffs", "Effects", buffs_dots, buffs_buts, PANEL_FRAME_HUD, 0, 1),
    [PANEL_HOTBAR] = PANEL_ENTRY("hotbar", "Hotbar", hotbar_dots, hotbar_buts, PANEL_FRAME_NONE, 0, 1),
    [PANEL_EQUIPMENT] = PANEL_ENTRY("equipment", "Equipment", equipment_dots, equipment_buts, PANEL_FRAME_WINDOW, 0, 1),
    [PANEL_SPELLBOOK] = PANEL_ENTRY("spellbook", "Spells", spellbook_dots, spellbook_buts, PANEL_FRAME_WINDOW, 0, 0),
    [PANEL_STATUS] = PANEL_ENTRY("status", "Progress", status_dots, status_buts, PANEL_FRAME_HUD, 0, 1),
    [PANEL_SYSMENU] = PANEL_ENTRY("sysmenu", "System Menu", sysmenu_dots, sysmenu_buts, PANEL_FRAME_HUD, 0, 1),
    [PANEL_CLOCK] = PANEL_ENTRY("clock", "Classic Clock", clock_dots, clock_buts, PANEL_FRAME_HUD, 0, 0),
    [PANEL_HELP] = PANEL_ENTRY("help", "Help", help_dots, help_buts, PANEL_FRAME_WINDOW, 0, 0),
    [PANEL_MINIMAP] = PANEL_ENTRY("minimap", "Minimap", minimap_dots, minimap_buts, PANEL_FRAME_NONE, 0, 1),
    [PANEL_CONTAINER] = PANEL_ENTRY("container", "Container", container_dots, container_buts, PANEL_FRAME_WINDOW, 1, 1),
};

_Static_assert(MAX_PANEL <= PANEL_BUT_SLOTS, "every panel needs a slot in each per-panel button bank");

static int drag_dirty; /* a drag/resize moved something since the last save */

/* Options > "Lock GUI Layout": freezes every panel in place at once */
static int layout_locked;

/* the container window was closed by hand; cleared when the next shop/grave
 * opens so the window comes back on its own */
static int con_dismissed;

/* Options > UI "Minimize Upward" - see panel_set_collapsed() */
static int collapse_up;

/* an external chat window (the mod's tabbed chat) has taken over: the classic
 * chat panel stays hidden except while the classic input line itself is live,
 * so a stray "/say" flow is never typed blind */
static int chat_external;

DLL_EXPORT int panel_visible(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return 0;
	}
	return panels[p].visible;
}

DLL_EXPORT void panel_set_visible(int p, int on)
{
	if (p < 0 || p >= MAX_PANEL) {
		return;
	}
	panels[p].visible = on ? 1 : 0;
}

DLL_EXPORT void panel_toggle(int p)
{
	panel_set_visible(p, !panel_visible(p));
}

DLL_EXPORT int panel_shown(int p)
{
	if (!gui_overlay_visible) {
		return 0;
	}
	if (p == PANEL_CHAT) {
		return 0; /* the classic chat is gone - the tabbed chat window owns chat */
	}
	/* the help / quest-log window is summoned by its buttons and keys, not
	 * by the visibility toggle - it exists exactly while one is open */
	if (p == PANEL_HELP) {
		return display_help || display_quest;
	}
	/* the container window exists exactly while a shop / grave / depot is
	 * open (and was not closed by hand for this container) - the Options
	 * toggle has no say in it */
	if (p == PANEL_CONTAINER) {
		return con_cnt && !con_dismissed;
	}
	if (panel_visible(p)) {
		return 1;
	}

	return 0;
}

DLL_EXPORT void panel_chat_external(int on)
{
	chat_external = on ? 1 : 0;
}

DLL_EXPORT int panel_chat_is_external(void)
{
	return chat_external;
}

/* The close button on a merchant/grave view hides the container window for
 * this container only - the next shop opens it again. */
void panel_dismiss_container(void)
{
	con_dismissed = 1;
}

void panel_container_opened(void)
{
	con_dismissed = 0;
}

DLL_EXPORT int panel_collapsed(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return 0;
	}
	return panels[p].collapsed;
}

static void panel_shift(int p, int dx, int dy);
static void panel_clamp(int p, int *dx, int *dy);
static void panel_adopt_clamp(int p);

/* Minimize / restore. In "upward" mode the window collapses towards its
 * bottom edge: the title bar drops to where the bottom was and comes back up
 * on restore, so a window parked above the hotbar stays parked there. The
 * shift is folded into the stored offset, so a layout saved while collapsed
 * reloads in the same place and still restores upward. */
DLL_EXPORT void panel_set_collapsed(int p, int on)
{
	int x1, y1, x2, y2, full_h = 0;

	if (p < 0 || p >= MAX_PANEL) {
		return;
	}
	on = on ? 1 : 0;
	if (panels[p].collapsed == on) {
		return;
	}
	if (collapse_up && panels[p].frame == PANEL_FRAME_WINDOW) {
		int was = panels[p].collapsed;

		panels[p].collapsed = 0;
		if (panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
			full_h = y2 - y1;
		}
		panels[p].collapsed = was;
	}
	panels[p].collapsed = on;
	if (full_h > UI_WIN_TITLE_H) {
		int dx = 0, dy = full_h - UI_WIN_TITLE_H;

		if (!on) {
			dy = -dy;
		}
		panel_adopt_clamp(p);
		panel_clamp(p, &dx, &dy);
		panels[p].dx += dx;
		panels[p].dy += dy;
		panel_shift(p, dx, dy);
	}
}

DLL_EXPORT int panel_collapse_upward(void)
{
	return collapse_up;
}

DLL_EXPORT void panels_set_collapse_upward(int on)
{
	collapse_up = on ? 1 : 0;
}

DLL_EXPORT int panels_layout_locked(void)
{
	return layout_locked;
}

DLL_EXPORT void panels_set_layout_locked(int on)
{
	layout_locked = on ? 1 : 0;
}

DLL_EXPORT int panel_locked(int p)
{
	if (layout_locked) {
		return 1;
	}
	if (p < 0 || p >= MAX_PANEL) {
		return 0;
	}
	return panels[p].locked;
}

DLL_EXPORT void panel_set_locked(int p, int on)
{
	if (p < 0 || p >= MAX_PANEL) {
		return;
	}
	panels[p].locked = on ? 1 : 0;
}

/* the padlock glyph flips the panel's OWN flag (the global Options lock is
 * a separate layer on top) */
DLL_EXPORT void panel_toggle_locked(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return;
	}
	panels[p].locked = !panels[p].locked;
}

DLL_EXPORT int panel_content_shown(int p)
{
	return panel_shown(p) && !panel_collapsed(p);
}

DLL_EXPORT int panel_dx(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return 0;
	}
	return panels[p].dx;
}

DLL_EXPORT int panel_dy(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return 0;
	}
	return panels[p].dy;
}

const char *panel_id(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return "";
	}
	return panels[p].id;
}

const char *panel_name(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return "";
	}
	return panels[p].name;
}

/* Live window titles: the container window is named after the shop / grave
 * it shows, the inventory counts its used slots so a glance at the title bar
 * (even a minimized one) says how much room is left. */
const char *panel_title(int p)
{
	if (p == PANEL_CONTAINER && con_cnt) {
		return con_name;
	}
	if (p == PANEL_INVENTORY) {
		static char buf[48];
		int used = 0, total = _inventorysize - INVENTORY_EQUIP_SLOTS;

		for (int i = INVENTORY_EQUIP_SLOTS; i < _inventorysize && i < MAX_INVENTORYSIZE; i++) {
			if (item[i]) {
				used++;
			}
		}
		if (total > 0) {
			snprintf(buf, sizeof(buf), "Inventory %d/%d", used, total);
			return buf;
		}
	}
	if (p == PANEL_HELP && display_quest) {
		return "Quest Log";
	}
	return panel_name(p);
}

int panel_frame_kind(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return PANEL_FRAME_NONE;
	}
	return panels[p].frame;
}

int panel_resizable(int p)
{
	if (p < 0 || p >= MAX_PANEL) {
		return 0;
	}
	return panels[p].resizable;
}

void panel_set_content_rect(int p, int x1, int y1, int x2, int y2)
{
	if (p < 0 || p >= MAX_PANEL) {
		return;
	}
	panels[p].cx1 = x1;
	panels[p].cy1 = y1;
	panels[p].cx2 = x2;
	panels[p].cy2 = y2;
}

int panel_content_rect(int p, int *x1, int *y1, int *x2, int *y2)
{
	if (p < 0 || p >= MAX_PANEL || panels[p].cx2 <= panels[p].cx1) {
		return 0;
	}
	*x1 = panels[p].cx1;
	*y1 = panels[p].cy1;
	*x2 = panels[p].cx2;
	*y2 = panels[p].cy2;
	return 1;
}

/* mod-facing: how many panels exist, and the on-screen footprint of a shown
 * one - the mod's windows snap against these */
DLL_EXPORT int panel_count(void)
{
	return MAX_PANEL;
}

static int panel_bounds_rect(int p, int *x1, int *y1, int *x2, int *y2);

DLL_EXPORT int panel_snap_rect(int p, int *x1, int *y1, int *x2, int *y2)
{
	if (p < 0 || p >= MAX_PANEL || !panel_shown(p)) {
		return 0;
	}
	return panel_bounds_rect(p, x1, y1, x2, y2);
}

int panel_frame_rect(int p, int *x1, int *y1, int *x2, int *y2)
{
	int a, b, c, d;

	if (panel_frame_kind(p) == PANEL_FRAME_NONE || !panel_content_rect(p, &a, &b, &c, &d)) {
		return 0;
	}
	*x1 = a - UI_WIN_PAD;
	*x2 = c + UI_WIN_PAD;
	*y2 = d + UI_WIN_PAD;
	if (panels[p].frame == PANEL_FRAME_WINDOW) {
		*y1 = b - UI_WIN_TITLE_H;
		if (panel_collapsed(p)) {
			*y2 = *y1 + UI_WIN_TITLE_H;
		}
	} else {
		*y1 = b - HUD_GRIP_H;
	}
	return 1;
}

/* ── chrome button geometry ─────────────────────────────────────────────
 *
 * Derived from the frame rect so it always tracks the panel. init_dots()
 * mirrors these into but[] (via panels_place_chrome_buttons) so the shared
 * capture/click machinery can drive them. */

static int chrome_close_pos(int p, int *cx, int *cy)
{
	int x1, y1, x2, y2;

	if (panel_frame_kind(p) != PANEL_FRAME_WINDOW || !panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
		return 0;
	}
	*cx = x2 - UI_WIN_PAD - UI_WIN_GLYPH / 2;
	*cy = y1 + UI_WIN_TITLE_H / 2;
	return 1;
}

static int chrome_min_pos(int p, int *cx, int *cy)
{
	if (!chrome_close_pos(p, cx, cy)) {
		return 0;
	}
	*cx -= UI_WIN_GLYPH + 3;
	return 1;
}

static int chrome_lock_pos(int p, int *cx, int *cy)
{
	int x1, y1, x2, y2;

	if (panel_frame_kind(p) == PANEL_FRAME_HUD) {
		/* HUD plates: the padlock sits at the grab strip's right end */
		if (!panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
			return 0;
		}
		*cx = x2 - 9;
		*cy = y1 + HUD_GRIP_H / 2 + 2;
		return 1;
	}
	if (!chrome_min_pos(p, cx, cy)) {
		return 0;
	}
	*cx -= UI_WIN_GLYPH + 3;
	return 1;
}

static int chrome_grip_pos(int p, int *cx, int *cy)
{
	int x1, y1, x2, y2;

	if (!panel_resizable(p) || panel_collapsed(p) || panel_locked(p) || !panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
		return 0;
	}
	*cx = x2 - UI_WIN_GRIP / 2 - 1;
	*cy = y2 - UI_WIN_GRIP / 2 - 1;
	return 1;
}

/* Title bar / grab strip: the whole width minus the glyph buttons. */
static int chrome_grab_rect(int p, int *x1, int *y1, int *x2, int *y2)
{
	int fx1, fy1, fx2, fy2;

	if (!panel_frame_rect(p, &fx1, &fy1, &fx2, &fy2)) {
		return 0;
	}
	*x1 = fx1;
	*y1 = fy1;
	*x2 = fx2;
	if (panels[p].frame == PANEL_FRAME_WINDOW) {
		int cx, cy;

		*y2 = fy1 + UI_WIN_TITLE_H;
		if (chrome_lock_pos(p, &cx, &cy)) {
			*x2 = cx - UI_WIN_GLYPH / 2 - 2;
		}
	} else {
		*x2 = fx2 - 18; /* keep the strip's padlock corner clickable */
		*y2 = fy1 + HUD_GRIP_H;
	}
	return 1;
}

void panels_place_chrome_buttons(void (*place)(int bidx, int x, int y, int hitrad, int flags))
{
	int p, cx, cy, x1, y1, x2, y2;

	for (p = 0; p < PANEL_BUT_SLOTS; p++) {
		place(BUT_DRAG_BEG + p, 0, 0, 0, BUTF_NOHIT);
		place(BUT_PCLOSE_BEG + p, 0, 0, 0, BUTF_NOHIT);
		place(BUT_PMIN_BEG + p, 0, 0, 0, BUTF_NOHIT);
		place(BUT_PSIZE_BEG + p, 0, 0, 0, BUTF_NOHIT);
		place(BUT_PLOCK_BEG + p, 0, 0, 0, BUTF_NOHIT);
	}
	place(BUT_PANEL_BODY, 0, 0, 0, BUTF_NOHIT);

	for (p = 0; p < MAX_PANEL; p++) {
		if (chrome_grab_rect(p, &x1, &y1, &x2, &y2)) {
			/* the hit radius is irrelevant - panels_frame_button() does
			 * the real rectangular hit test - but the button must be at
			 * the bar's centre so the cursor lands there on release */
			place(BUT_DRAG_BEG + p, (x1 + x2) / 2, (y1 + y2) / 2, 0, BUTF_CAPTURE | BUTF_MOVEEXEC | BUTF_NOHIT);
		}
		if (chrome_close_pos(p, &cx, &cy)) {
			place(BUT_PCLOSE_BEG + p, cx, cy, 0, BUTF_NOHIT);
		}
		if (chrome_min_pos(p, &cx, &cy)) {
			place(BUT_PMIN_BEG + p, cx, cy, 0, BUTF_NOHIT);
		}
		if (chrome_grip_pos(p, &cx, &cy)) {
			place(BUT_PSIZE_BEG + p, cx, cy, 0, BUTF_CAPTURE | BUTF_MOVEEXEC | BUTF_NOHIT);
		}
		if (chrome_lock_pos(p, &cx, &cy)) {
			place(BUT_PLOCK_BEG + p, cx, cy, 0, BUTF_NOHIT);
		}
	}
}

/* ── movement ───────────────────────────────────────────────────────────── */

static void panel_shift(int p, int dx, int dy)
{
	Panel *pan = &panels[p];

	if (!dx && !dy) {
		return;
	}
	for (int i = 0; i < pan->ndots; i++) {
		dot[pan->dots[i]].x += dx;
		dot[pan->dots[i]].y += dy;
	}
	for (int i = 0; i < pan->nbuts; i++) {
		for (int b = pan->buts[i].beg; b <= pan->buts[i].end; b++) {
			but[b].x += dx;
			but[b].y += dy;
		}
	}
	but[BUT_DRAG_BEG + p].x += dx;
	but[BUT_DRAG_BEG + p].y += dy;
	but[BUT_PCLOSE_BEG + p].x += dx;
	but[BUT_PCLOSE_BEG + p].y += dy;
	but[BUT_PMIN_BEG + p].x += dx;
	but[BUT_PMIN_BEG + p].y += dy;
	but[BUT_PSIZE_BEG + p].x += dx;
	but[BUT_PSIZE_BEG + p].y += dy;
	but[BUT_PLOCK_BEG + p].x += dx;
	but[BUT_PLOCK_BEG + p].y += dy;
	pan->cx1 += dx;
	pan->cx2 += dx;
	pan->cy1 += dy;
	pan->cy2 += dy;
}

/* the panel's on-screen footprint: the chrome frame for framed panels, the
 * content rect for frameless ones (the hotbar publishes one that includes
 * its grab tab) */
static int panel_bounds_rect(int p, int *x1, int *y1, int *x2, int *y2)
{
	if (panel_frame_rect(p, x1, y1, x2, y2)) {
		return 1;
	}
	return panel_content_rect(p, x1, y1, x2, y2);
}

/* keep the WHOLE footprint on the canvas: a panel can never be dragged (or
 * left stranded by a resolution / UI-scale change) even partly off screen.
 * Dragging the hotbar below the bottom edge used to lose it for good. */
static void panel_clamp(int p, int *dx, int *dy)
{
	const Panel *pan = &panels[p];
	int x1, y1, x2, y2;

	if (!panel_bounds_rect(p, &x1, &y1, &x2, &y2)) {
		/* no rect published: fall back to the reference dot */
		x1 = x2 = dot[pan->dots[0]].x;
		y1 = y2 = dot[pan->dots[0]].y;
	}

	/* left/top win for a panel larger than the screen - the drag handle and
	 * title bar live there */
	gesture_clamp_delta(x1, y1, x2, y2, UIXRES, UIYRES, dx, dy);
}

/* Magnetize a dragged panel's edges to the screen edges and to the other
 * shown panels' footprints, so hand-built layouts line up without pixel
 * hunting. Adjusts the delta by at most SNAP_DIST per axis. */
#define SNAP_DIST 8

/* the grab handle drawn next to a frameless panel (the hotbar): a bar that
 * reaches PANEL_HANDLE_L px left of its anchor and PANEL_HANDLE_R px right of
 * it - up to, never over, the first slot - and the whole bar takes the press,
 * not only the small circle under its middle */
#define PANEL_HANDLE_L 20
#define PANEL_HANDLE_R 7
#define PANEL_HANDLE_H 5

static void panel_snap(int p, int *dx, int *dy)
{
	int x1, y1, x2, y2;
	int bestx = SNAP_DIST + 1, besty = SNAP_DIST + 1;
	int adjx = 0, adjy = 0;

	if (!panel_bounds_rect(p, &x1, &y1, &x2, &y2)) {
		return;
	}
	x1 += *dx;
	x2 += *dx;
	y1 += *dy;
	y2 += *dy;

	gesture_snap_axis(x1, x2, 0, UIXRES, SNAP_DIST, &bestx, &adjx);
	gesture_snap_axis(y1, y2, 0, UIYRES, SNAP_DIST, &besty, &adjy);
	for (int q = 0; q < MAX_PANEL; q++) {
		int qx1, qy1, qx2, qy2;

		if (q == p || !panel_shown(q) || !panel_bounds_rect(q, &qx1, &qy1, &qx2, &qy2)) {
			continue;
		}
		gesture_snap_axis(x1, x2, qx1, qx2, SNAP_DIST, &bestx, &adjx);
		gesture_snap_axis(y1, y2, qy1, qy2, SNAP_DIST, &besty, &adjy);
	}
	*dx += adjx;
	*dy += adjy;
}

/* Shift every panel by its stored offset, clamped on-canvas. The clamp is a
 * presentation correction for THIS layout pass only - it is remembered in
 * cdx/cdy, never folded into the stored offset. Writing it back used to
 * wreck layouts for good: init_dots() runs several times while a window is
 * still being sized at startup (and on every fullscreen switch), each pass
 * clamped the whole default layout into a canvas that was not final yet,
 * and the accumulated corrections were then saved as the player's own
 * arrangement. A stranded panel still heals visually every pass; the
 * player's intent is only rewritten when they grab the panel themselves
 * (panels_drag_begin). */
void panels_apply_offsets(void)
{
	for (int p = 0; p < MAX_PANEL; p++) {
		int dx = panels[p].dx, dy = panels[p].dy;

		panel_clamp(p, &dx, &dy);
		panels[p].cdx = dx - panels[p].dx;
		panels[p].cdy = dy - panels[p].dy;
		panel_shift(p, dx, dy);
	}
}

/* the live position becomes the stored one: called when the player takes
 * hold of a panel, so a clamp correction turns into intent instead of
 * snapping back at the first motion */
static void panel_adopt_clamp(int p)
{
	panels[p].dx += panels[p].cdx;
	panels[p].dy += panels[p].cdy;
	panels[p].cdx = panels[p].cdy = 0;
}

/* ── drag gesture ───────────────────────────────────────────────────────
 *
 * Absolute pointer positions: the panel's offset is the offset it had at the
 * press plus the pointer's travel since. Nothing accumulates, so a dropped
 * event, a re-laid-out default (init_dots() mid-gesture) or a snap can never
 * make the panel drift or run away - and a cancel puts it back exactly. */
static struct {
	int p; /* panel being moved, -1 = none */
	int grab_x, grab_y; /* pointer at the press         */
	int start_dx, start_dy; /* stored offset at the press   */
	int moved; /* left the click dead zone     */
} drag = {-1, 0, 0, 0, 0, 0};

/* a press that never travels this far is a click on the handle, not a drag
 * (the minimap flips between its two sizes on such a click) */
#define DRAG_DEAD_ZONE 3

/* move panel p so its stored offset becomes (want_dx,want_dy): clamped
 * on-canvas, snapped to the screen edges and the other panels, then clamped
 * once more so a snap can never push anything off screen */
static void panel_move_to_offset(int p, int want_dx, int want_dy, int snap)
{
	int dx = want_dx - panels[p].dx;
	int dy = want_dy - panels[p].dy;

	panel_clamp(p, &dx, &dy);
	if (snap) {
		panel_snap(p, &dx, &dy);
		panel_clamp(p, &dx, &dy);
	}
	if (!dx && !dy) {
		return;
	}
	panels[p].dx += dx;
	panels[p].dy += dy;
	panel_shift(p, dx, dy);
	drag_dirty = 1;
}

void panels_drag_begin(int p, int mx, int my)
{
	drag.p = -1;
	if (p < 0 || p >= MAX_PANEL || panel_locked(p)) {
		return;
	}
	panel_adopt_clamp(p);
	drag.p = p;
	drag.grab_x = mx;
	drag.grab_y = my;
	drag.start_dx = panels[p].dx;
	drag.start_dy = panels[p].dy;
	drag.moved = 0;
}

void panels_drag_update(int p, int mx, int my)
{
	if (p < 0 || p >= MAX_PANEL || drag.p != p) {
		return;
	}
	if (!panel_shown(p) || panel_locked(p)) {
		drag.p = -1; /* the panel went away (or got locked) under the pointer */
		return;
	}
	if (!drag.moved) {
		if (abs(mx - drag.grab_x) < DRAG_DEAD_ZONE && abs(my - drag.grab_y) < DRAG_DEAD_ZONE) {
			return;
		}
		drag.moved = 1;
	}
	panel_move_to_offset(p, drag.start_dx + (mx - drag.grab_x), drag.start_dy + (my - drag.grab_y), 1);
}

int panels_drag_moved(void)
{
	return drag.p != -1 && drag.moved;
}

void panels_drag_cancel(void)
{
	if (drag.p != -1) {
		panel_move_to_offset(drag.p, drag.start_dx, drag.start_dy, 0);
		drag.p = -1;
	}
}

void panels_drag_end(void)
{
	drag.p = -1;
	if (drag_dirty) {
		drag_dirty = 0;
		save_options();
	}
}

/* Shift a panel so its content rect's top-left lands back on (x1,y1). A
 * resize re-derives the whole default layout, and the inventory's default is
 * right-anchored, so without this the window would grow away from the grip. */
void panel_keep_anchor(int p, int x1, int y1)
{
	int cx1, cy1, cx2, cy2, dx, dy;

	if (p < 0 || p >= MAX_PANEL || !panel_content_rect(p, &cx1, &cy1, &cx2, &cy2)) {
		return;
	}
	panel_adopt_clamp(p); /* a resize pins the corner the player sees */
	dx = x1 - cx1;
	dy = y1 - cy1;
	panel_clamp(p, &dx, &dy);
	panels[p].dx += dx;
	panels[p].dy += dy;
	panel_shift(p, dx, dy);
}

/* ── resize gesture ─────────────────────────────────────────────────────
 *
 * The grip follows the pointer 1:1: a virtual grip position - the corner of
 * the content rect at the press plus the pointer's travel since - and the
 * size setting is read straight off where that point sits relative to the
 * grid's top-left corner, which stays fixed for the whole gesture. */
static struct {
	int p; /* panel being resized, -1 = none          */
	int grab_x, grab_y; /* pointer at the press                    */
	int start_x2, start_y2; /* content rect corner at the press        */
	int originx, originy; /* grid top-left at the press              */
	int start_cols, start_rows; /* size settings at the press (for cancel) */
} rsz = {-1, 0, 0, 0, 0, 0, 0, 0, 0};

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static int panel_size_cols(int p)
{
	if (p == PANEL_INVENTORY) {
		return inv_grid_cols();
	}
	if (p == PANEL_CONTAINER) {
		return con_grid_cols();
	}
	return 0;
}

static int panel_size_rows(int p)
{
	if (p == PANEL_INVENTORY) {
		return __invdy;
	}
	if (p == PANEL_CONTAINER) {
		return __condy;
	}
	return __skldy;
}

/* push a size into the panel's settings; 1 when something changed and the
 * layout has to be rebuilt */
static int panel_apply_size(int p, int cols, int rows)
{
	int changed = 0;

	if (p == PANEL_INVENTORY) {
		cols = clampi(cols, INV_GRID_MIN_COLS, INV_GRID_MAX_COLS);
		rows = clampi(rows, INV_GRID_MIN_ROWS, INV_GRID_MAX_ROWS);
		if (cols != inv_grid_cols()) {
			inv_grid_set_cols(cols);
			changed = 1;
		}
		if (rows != __invdy) {
			inv_grid_set_rows(rows);
			changed = 1;
		}
	} else if (p == PANEL_CONTAINER) {
		cols = clampi(cols, CON_GRID_MIN_COLS, CON_GRID_MAX_COLS);
		rows = clampi(rows, CON_GRID_MIN_ROWS, CON_GRID_MAX_ROWS);
		if (cols != con_grid_cols()) {
			con_grid_set_cols(cols);
			changed = 1;
		}
		if (rows != __condy) {
			con_grid_set_rows(rows);
			changed = 1;
		}
	} else if (p == PANEL_SKILLS) {
		rows = clampi(rows, SKL_GRID_MIN_ROWS, SKL_GRID_MAX_ROWS);
		if (rows != __skldy) {
			skl_grid_set_rows(rows);
			changed = 1;
		}
	}
	return changed;
}

/* the layout rebuild that follows a size change: the default layout is
 * re-derived from the settings, then the panel is shifted back so the corner
 * the grip is not dragging stays put */
static void panel_relayout_anchored(int p, int x1, int y1)
{
	init_dots();
	panel_keep_anchor(p, x1, y1);
}

int panels_resize_begin(int p, int mx, int my)
{
	int cx1, cy1, cx2, cy2;

	rsz.p = -1;
	if (p < 0 || p >= MAX_PANEL || !panel_resizable(p) || panel_locked(p) || panel_collapsed(p)) {
		return 0;
	}
	if (!panel_content_rect(p, &cx1, &cy1, &cx2, &cy2)) {
		return 0;
	}
	rsz.p = p;
	rsz.grab_x = mx;
	rsz.grab_y = my;
	rsz.start_x2 = cx2;
	rsz.start_y2 = cy2;
	rsz.originx = cx1 + (p == PANEL_INVENTORY ? INV_RAIL_W + INV_RAIL_GAP : 0);
	rsz.originy = cy1;
	rsz.start_cols = panel_size_cols(p);
	rsz.start_rows = panel_size_rows(p);
	return 1;
}

int panels_resize_update(int p, int mx, int my)
{
	int gripx, gripy, cols = 0, rows = 0, x1, y1, x2, y2;

	if (p < 0 || p >= MAX_PANEL || rsz.p != p) {
		return 0;
	}
	if (!panel_shown(p) || panel_locked(p) || panel_collapsed(p)) {
		rsz.p = -1; /* the panel went away under the pointer */
		return 0;
	}
	gripx = rsz.start_x2 + (mx - rsz.grab_x);
	gripy = rsz.start_y2 + (my - rsz.grab_y);

	if (p == PANEL_INVENTORY) {
		cols = (gripx - rsz.originx + FDX / 2) / FDX;
		rows = (gripy - rsz.originy - INV_FOOT_H + FDX / 2) / FDX;
	} else if (p == PANEL_CONTAINER) {
		cols = (gripx - rsz.originx - SKL_RAIL_W + FDX / 2) / FDX;
		rows = (gripy - rsz.originy + FDX / 2) / FDX;
	} else if (p == PANEL_SKILLS) {
		rows = (gripy - rsz.originy + LINEHEIGHT / 2) / LINEHEIGHT;
	} else {
		return 0;
	}

	if (!panel_content_rect(p, &x1, &y1, &x2, &y2) || !panel_apply_size(p, cols, rows)) {
		return 0;
	}
	drag_dirty = 1;
	panel_relayout_anchored(p, x1, y1);
	return 1;
}

void panels_resize_cancel(void)
{
	int p = rsz.p, x1, y1, x2, y2;

	if (p == -1) {
		return;
	}
	rsz.p = -1;
	if (panel_content_rect(p, &x1, &y1, &x2, &y2) && panel_apply_size(p, rsz.start_cols, rsz.start_rows)) {
		panel_relayout_anchored(p, x1, y1);
	}
}

void panels_resize_end(void)
{
	rsz.p = -1;
	if (drag_dirty) {
		drag_dirty = 0;
		save_options();
	}
}

/* ── button ownership ───────────────────────────────────────────────────── */

static int chrome_button_panel(int b)
{
	if (b >= BUT_DRAG_BEG && b <= BUT_DRAG_END) {
		return b - BUT_DRAG_BEG;
	}
	if (b >= BUT_PCLOSE_BEG && b <= BUT_PCLOSE_END) {
		return b - BUT_PCLOSE_BEG;
	}
	if (b >= BUT_PMIN_BEG && b <= BUT_PMIN_END) {
		return b - BUT_PMIN_BEG;
	}
	if (b >= BUT_PSIZE_BEG && b <= BUT_PSIZE_END) {
		return b - BUT_PSIZE_BEG;
	}
	if (b >= BUT_PLOCK_BEG && b <= BUT_PLOCK_END) {
		return b - BUT_PLOCK_BEG;
	}
	return -1;
}

int panel_owns_button(int b)
{
	int p = chrome_button_panel(b);

	if (p != -1) {
		return p < MAX_PANEL ? p : -1;
	}
	for (p = 0; p < MAX_PANEL; p++) {
		for (int i = 0; i < panels[p].nbuts; i++) {
			if (b >= panels[p].buts[i].beg && b <= panels[p].buts[i].end) {
				return p;
			}
		}
	}
	return -1;
}

int panel_button_live(int b)
{
	int p = panel_owns_button(b);

	if (p == -1) {
		return 1;
	}
	if (!panel_shown(p)) {
		return 0;
	}
	/* the title bar's own controls stay live while minimized - that is how
	 * you get the window back */
	if (chrome_button_panel(b) != -1) {
		return 1;
	}
	return !panel_collapsed(p);
}

/* ── chrome hit testing ─────────────────────────────────────────────────── */

static int in_glyph(int x, int y, int cx, int cy)
{
	int h = UI_WIN_GLYPH / 2 + 1;

	return x >= cx - h && x <= cx + h && y >= cy - h && y <= cy + h;
}

int panels_frame_button(int x, int y)
{
	for (int p = 0; p < MAX_PANEL; p++) {
		int x1, y1, x2, y2, cx, cy;

		if (!panel_shown(p) || !panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
			continue;
		}
		if (x < x1 || x > x2 || y < y1 || y > y2) {
			continue;
		}
		if (chrome_close_pos(p, &cx, &cy) && in_glyph(x, y, cx, cy)) {
			return BUT_PCLOSE_BEG + p;
		}
		if (chrome_min_pos(p, &cx, &cy) && in_glyph(x, y, cx, cy)) {
			return BUT_PMIN_BEG + p;
		}
		if (chrome_lock_pos(p, &cx, &cy) && in_glyph(x, y, cx, cy)) {
			return BUT_PLOCK_BEG + p;
		}
		if (chrome_grip_pos(p, &cx, &cy)) {
			int h = UI_WIN_GRIP / 2 + 1;

			if (x >= cx - h && x <= cx + h && y >= cy - h && y <= cy + h) {
				return BUT_PSIZE_BEG + p;
			}
		}
		if (chrome_grab_rect(p, &x1, &y1, &x2, &y2) && x >= x1 && x <= x2 && y >= y1 && y <= y2) {
			/* a locked panel keeps no drag handle, but its title bar must
			 * still swallow the click - or it falls through to whatever
			 * sits underneath (skill rows, the world) */
			return panel_locked(p) ? BUT_PANEL_BODY : BUT_DRAG_BEG + p;
		}
	}

	/* the minimap has no chrome at all: its whole footprint is the handle
	 * (press and drag moves it, a plain click flips small <-> big), and a
	 * locked one still swallows the press so the world below is not walked */
	{
		int x1, y1, x2, y2;

		if (panel_shown(PANEL_MINIMAP) && panel_content_rect(PANEL_MINIMAP, &x1, &y1, &x2, &y2) && x >= x1 && x <= x2 &&
		    y >= y1 && y <= y2) {
			int b = BUT_PLOCK_BEG + PANEL_MINIMAP;

			if (!(but[b].flags & BUTF_NOHIT) && in_glyph(x, y, butx(b), buty(b))) {
				return b;
			}
			return panel_locked(PANEL_MINIMAP) ? BUT_PANEL_BODY : BUT_DRAG_BEG + PANEL_MINIMAP;
		}
	}

	/* frameless panels: the grab handle drawn next to the bar is what the
	 * player aims at, so the whole handle takes the press (a locked panel
	 * draws no handle and offers none) */
	for (int p = 0; p < MAX_PANEL; p++) {
		int b = BUT_DRAG_BEG + p;

		if (panels[p].frame != PANEL_FRAME_NONE || !panel_shown(p) || panel_locked(p) || (but[b].flags & BUTF_NOHIT)) {
			continue;
		}
		if (x >= butx(b) - PANEL_HANDLE_L && x <= butx(b) + PANEL_HANDLE_R && y >= buty(b) - PANEL_HANDLE_H &&
		    y <= buty(b) + PANEL_HANDLE_H) {
			return b;
		}
	}
	return -1;
}

int panels_frame_over(int x, int y)
{
	for (int p = 0; p < MAX_PANEL; p++) {
		int x1, y1, x2, y2;

		if (!panel_shown(p) || !panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
			continue;
		}
		if (x >= x1 && x <= x2 && y >= y1 && y <= y2) {
			return 1;
		}
	}
	return 0;
}

/* ── chrome drawing ─────────────────────────────────────────────────────── */

void panels_display_frames(void)
{
	for (int p = 0; p < MAX_PANEL; p++) {
		int x1, y1, x2, y2, cx, cy;
		int collapsed = panel_collapsed(p);

		if (!panel_shown(p) || !panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
			continue;
		}

		if (panels[p].frame == PANEL_FRAME_WINDOW) {
			ui_panel(x1, y1, x2, y2);
			ui_window_titlebar(x1, y1, x2, panel_title(p), collapsed);
			if (chrome_close_pos(p, &cx, &cy)) {
				ui_glyph_button(cx, cy, UI_GLYPH_CLOSE, butsel == BUT_PCLOSE_BEG + p);
			}
			if (chrome_min_pos(p, &cx, &cy)) {
				ui_glyph_button(cx, cy, collapsed ? UI_GLYPH_RESTORE : UI_GLYPH_MINIMIZE, butsel == BUT_PMIN_BEG + p);
			}
			if (chrome_lock_pos(p, &cx, &cy)) {
				ui_glyph_lock(cx, cy, panels[p].locked, butsel == BUT_PLOCK_BEG + p);
			}
			if (chrome_grip_pos(p, &cx, &cy)) {
				ui_resize_grip(cx, cy, butsel == BUT_PSIZE_BEG + p || capbut == BUT_PSIZE_BEG + p);
			}
		} else {
			int hot = (butsel == BUT_DRAG_BEG + p) || (capbut == BUT_DRAG_BEG + p);
			int mx = (x1 + x2) / 2, my = y1 + HUD_GRIP_H / 2;

			ui_panel_light(x1, y1, x2, y2);
			if (panel_locked(p)) {
				hot = 0; /* a locked plate offers no grab affordance */
			}
			{
				int lx, ly;

				if (chrome_lock_pos(p, &lx, &ly) &&
				    (panels[p].locked || (mousex >= x1 && mousex <= x2 && mousey >= y1 && mousey <= y2))) {
					ui_glyph_lock(lx, ly, panels[p].locked, butsel == BUT_PLOCK_BEG + p);
				}
			}
			/* the grab strip is only marked when the pointer is on it -
			 * a permanent handle would clutter a HUD widget */
			if (hot) {
				render_rect_alpha(mx - 12, my - 1, mx + 12, my + 1, UI_ACCENT, 230);
			} else {
				render_rect_alpha(mx - 8, my - 1, mx + 8, my, UI_BORDER_STRONG, UI_A_ROW_HOVER);
			}
		}
	}
}

void panels_display_handles(void)
{
	const int proximity = 30;

	for (int p = 0; p < MAX_PANEL; p++) {
		int b = BUT_DRAG_BEG + p;
		int bx, by, dx, dy;

		/* framed panels have real chrome to grab; only the bare ones need
		 * the proximity hint - and a locked panel offers none. The minimap
		 * is grabbed anywhere on its face, it has no separate handle. */
		if (panels[p].frame != PANEL_FRAME_NONE || p == PANEL_MINIMAP || !panel_shown(p) || panel_locked(p) ||
		    (but[b].flags & BUTF_NOHIT)) {
			continue;
		}
		bx = butx(b);
		by = buty(b);
		dx = mousex - bx;
		dy = mousey - by;
		if (dx * dx + dy * dy < proximity * proximity) {
			int hot = (butsel == b) || (capbut == b);
			render_shaded_rect(bx - PANEL_HANDLE_L, by - PANEL_HANDLE_H, bx + PANEL_HANDLE_R, by + PANEL_HANDLE_H,
			    hot ? UI_ACCENT : UI_BORDER_STRONG, hot ? UI_A_BORDER_HOV : UI_A_ROW_HOVER);
		}
	}

	/* frameless panels keep their padlock next to the grab tab: always
	 * visible while locked, on approach otherwise */
	for (int p = 0; p < MAX_PANEL; p++) {
		int b = BUT_PLOCK_BEG + p;
		int dx, dy;

		if (panels[p].frame != PANEL_FRAME_NONE || !panel_shown(p) || (but[b].flags & BUTF_NOHIT)) {
			continue;
		}
		dx = mousex - butx(b);
		dy = mousey - buty(b);
		if (panels[p].locked || dx * dx + dy * dy < proximity * proximity) {
			ui_glyph_lock(butx(b), buty(b), panels[p].locked, butsel == b);
		}
	}
}

void panels_reset_layout(void)
{
	layout_locked = 0;
	collapse_up = 0;
	for (int p = 0; p < MAX_PANEL; p++) {
		panels[p].visible = panels[p].default_visible;
		panels[p].collapsed = 0;
		panels[p].locked = 0;
		panels[p].dx = 0;
		panels[p].dy = 0;
		panels[p].cdx = 0;
		panels[p].cdy = 0;
	}
}

DLL_EXPORT int panels_fullscreen_world(void)
{
	return 1; /* the world is always fullscreen - owner decision, Sep 2026 */
}

void panels_set_fullscreen_world(int on)
{
	(void)on; /* retired - there is no windowed-map mode anymore */
}

void panels_save_json(struct cJSON *root)
{
	cJSON *jp = cJSON_CreateObject();

	if (!jp) {
		return;
	}
	cJSON_AddBoolToObject(jp, "locked", layout_locked);
	cJSON_AddBoolToObject(jp, "collapse_up", collapse_up);
	for (int p = 0; p < MAX_PANEL; p++) {
		cJSON *e = cJSON_CreateObject();

		if (!e) {
			continue;
		}
		cJSON_AddBoolToObject(e, "on", panels[p].visible);
		cJSON_AddBoolToObject(e, "min", panels[p].collapsed);
		cJSON_AddBoolToObject(e, "lk", panels[p].locked);
		cJSON_AddNumberToObject(e, "dx", panels[p].dx);
		cJSON_AddNumberToObject(e, "dy", panels[p].dy);
		cJSON_AddItemToObject(jp, panels[p].id, e);
	}
	cJSON_AddItemToObject(root, "panels", jp);
}

void panels_load_json(const struct cJSON *root)
{
	const cJSON *jp = cJSON_GetObjectItem(root, "panels");
	const cJSON *v;

	if (!jp || !cJSON_IsObject(jp)) {
		return;
	}
	v = cJSON_GetObjectItem(jp, "locked");
	if (v && cJSON_IsBool(v)) {
		layout_locked = cJSON_IsTrue(v) ? 1 : 0;
	}
	v = cJSON_GetObjectItem(jp, "collapse_up");
	if (v && cJSON_IsBool(v)) {
		collapse_up = cJSON_IsTrue(v) ? 1 : 0;
	}
	for (int p = 0; p < MAX_PANEL; p++) {
		const cJSON *e = cJSON_GetObjectItem(jp, panels[p].id);

		if (!e || !cJSON_IsObject(e)) {
			continue;
		}
		v = cJSON_GetObjectItem(e, "on");
		if (v && cJSON_IsBool(v)) {
			panels[p].visible = cJSON_IsTrue(v) ? 1 : 0;
		}
		v = cJSON_GetObjectItem(e, "min");
		if (v && cJSON_IsBool(v)) {
			panels[p].collapsed = cJSON_IsTrue(v) ? 1 : 0;
		}
		v = cJSON_GetObjectItem(e, "lk");
		if (v && cJSON_IsBool(v)) {
			panels[p].locked = cJSON_IsTrue(v) ? 1 : 0;
		}
		v = cJSON_GetObjectItem(e, "dx");
		if (v && cJSON_IsNumber(v)) {
			panels[p].dx = (int)cJSON_GetNumberValue(v);
		}
		v = cJSON_GetObjectItem(e, "dy");
		if (v && cJSON_IsNumber(v)) {
			panels[p].dy = (int)cJSON_GetNumberValue(v);
		}
	}
}
