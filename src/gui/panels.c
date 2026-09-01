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
	int dx, dy;
	int cx1, cy1, cx2, cy2; /* content rect, published by init_dots() */
} Panel;

/* dot lists - the first dot is the clamp reference kept on screen */
static const int skills_dots[] = {DOT_SKL, DOT_SK2, DOT_CON};
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

static const ButRange skills_buts[] = {
    {BUT_SKL_BEG, BUT_SKL_END}, {BUT_CON_BEG, BUT_CON_END}, {BUT_SCL_UP, BUT_SCL_DW}};
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
static const ButRange help_buts[] = {{0, -1}}; /* page controls are rect-hit in gui_buttons.c */

#define PANEL_ENTRY(idstr, namestr, d, b, fr, rs, vis)                                                                 \
	{idstr, namestr, d, ARRAYSIZE(d), b, ARRAYSIZE(b), fr, rs, vis, vis, 0, 0, 0, 0, 0, 0, 0}

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
};

_Static_assert(MAX_PANEL <= PANEL_BUT_SLOTS, "every panel needs a slot in each per-panel button bank");

static int drag_dirty; /* a drag/resize moved something since the last save */

/* the skills window's container view was closed by hand; cleared when the
 * next shop/grave opens so the window comes back on its own */
static int con_dismissed;

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
	if (p == PANEL_CHAT && chat_external) {
		return cmd_is_active();
	}
	/* the help / quest-log window is summoned by its buttons and keys, not
	 * by the visibility toggle - it exists exactly while one is open */
	if (p == PANEL_HELP) {
		return display_help || display_quest;
	}
	if (panel_visible(p)) {
		return 1;
	}
	/* auto-show: an open container needs the skills panel area, an active
	 * chat line needs the chat panel (you must see what you type) */
	if (p == PANEL_SKILLS && con_cnt && !con_dismissed) {
		return 1;
	}
	if (p == PANEL_CHAT && cmd_is_active()) {
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

/* The close button on a merchant/grave view hides that view without turning
 * the Skills panel off for good - the next shop opens it again. */
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

DLL_EXPORT void panel_set_collapsed(int p, int on)
{
	if (p < 0 || p >= MAX_PANEL) {
		return;
	}
	panels[p].collapsed = on ? 1 : 0;
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

/* The skills window doubles as the shop/grave container view - name it after
 * whatever it is currently showing. */
const char *panel_title(int p)
{
	if (p == PANEL_SKILLS && con_cnt) {
		return con_name;
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

static int chrome_grip_pos(int p, int *cx, int *cy)
{
	int x1, y1, x2, y2;

	if (!panel_resizable(p) || panel_collapsed(p) || !panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
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
		if (chrome_min_pos(p, &cx, &cy)) {
			*x2 = cx - UI_WIN_GLYPH / 2 - 2;
		}
	} else {
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
	pan->cx1 += dx;
	pan->cx2 += dx;
	pan->cy1 += dy;
	pan->cy2 += dy;
}

/* keep the clamp reference dot within the canvas so a panel can never be
 * dragged (or left stranded by a resolution change) fully off screen */
static void panel_clamp(int p, int *dx, int *dy)
{
	const Panel *pan = &panels[p];
	int x = dot[pan->dots[0]].x + *dx;
	int y = dot[pan->dots[0]].y + *dy;

	if (x < 0) {
		*dx -= x;
	}
	if (x > UIXRES) {
		*dx -= x - UIXRES;
	}
	if (y < 0) {
		*dy -= y;
	}
	if (y > UIYRES) {
		*dy -= y - UIYRES;
	}
}

void panels_apply_offsets(void)
{
	for (int p = 0; p < MAX_PANEL; p++) {
		int dx = panels[p].dx, dy = panels[p].dy;

		panel_clamp(p, &dx, &dy);
		panels[p].dx = dx;
		panels[p].dy = dy;
		panel_shift(p, dx, dy);
	}
}

void panels_drag(int p)
{
	int dx, dy;

	if (p < 0 || p >= MAX_PANEL) {
		return;
	}

	dx = mousedx;
	dy = mousedy;
	panel_clamp(p, &dx, &dy);
	panels[p].dx += dx;
	panels[p].dy += dy;
	panel_shift(p, dx, dy);
	if (dx || dy) {
		drag_dirty = 1;
	}

	mousedx = mousedy = 0;
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
	dx = x1 - cx1;
	dy = y1 - cy1;
	panel_clamp(p, &dx, &dy);
	panels[p].dx += dx;
	panels[p].dy += dy;
	panel_shift(p, dx, dy);
}

/* Resize grip.
 *
 * The pointer is captured (and warped back to the canvas centre every frame),
 * so the grip's position is tracked as a virtual point that the raw deltas
 * accumulate into. The size setting is then read straight off where that
 * point sits relative to the grid's fixed top-left corner - the window
 * follows the pointer 1:1 instead of snapping a step per FDX of travel. */
int panels_resize(int p)
{
	static int gripx, gripy; /* virtual grip position          */
	static int originx, originy; /* grid top-left, fixed for the drag */
	static int accp = -1;
	int cols, rows, changed = 0;

	if (p < 0 || p >= MAX_PANEL || !panel_resizable(p)) {
		mousedx = mousedy = 0;
		return 0;
	}
	if (p != accp) {
		int cx1, cy1, cx2, cy2;

		accp = p;
		if (!panel_content_rect(p, &cx1, &cy1, &cx2, &cy2)) {
			mousedx = mousedy = 0;
			return 0;
		}
		gripx = cx2;
		gripy = cy2;
		if (p == PANEL_INVENTORY) {
			originx = cx1 + INV_RAIL_W + INV_RAIL_GAP;
			originy = cy1;
		} else if (p == PANEL_SKILLS && con_cnt) {
			originx = cx1;
			originy = cy1;
		} else {
			originx = cx1;
			originy = cy1;
		}
	}

	gripx += mousedx;
	gripy += mousedy;
	mousedx = mousedy = 0;

	if (p == PANEL_INVENTORY) {
		cols = (gripx - originx + FDX / 2) / FDX;
		rows = (gripy - originy - INV_FOOT_H + FDX / 2) / FDX;
		if (cols != inv_grid_cols()) {
			inv_grid_set_cols(cols);
			changed = 1;
		}
		if (rows != __invdy) {
			inv_grid_set_rows(rows);
			changed = 1;
		}
	} else if (p == PANEL_SKILLS && con_cnt) {
		cols = (gripx - originx - SKL_RAIL_W + FDX / 2) / FDX;
		rows = (gripy - originy + FDX / 2) / FDX;
		if (cols != con_grid_cols()) {
			con_grid_set_cols(cols);
			changed = 1;
		}
		if (rows != __condy) {
			con_grid_set_rows(rows);
			changed = 1;
		}
	} else if (p == PANEL_SKILLS) {
		rows = (gripy - originy + LINEHEIGHT / 2) / LINEHEIGHT;
		if (rows != __skldy) {
			skl_grid_set_rows(rows);
			changed = 1;
		}
	}

	if (changed) {
		drag_dirty = 1;
	}
	return changed;
}

void panels_drag_finished(void)
{
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
		if (chrome_grip_pos(p, &cx, &cy)) {
			int h = UI_WIN_GRIP / 2 + 1;

			if (x >= cx - h && x <= cx + h && y >= cy - h && y <= cy + h) {
				return BUT_PSIZE_BEG + p;
			}
		}
		if (chrome_grab_rect(p, &x1, &y1, &x2, &y2) && x >= x1 && x <= x2 && y >= y1 && y <= y2) {
			return BUT_DRAG_BEG + p;
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
			if (chrome_grip_pos(p, &cx, &cy)) {
				ui_resize_grip(cx, cy, butsel == BUT_PSIZE_BEG + p || capbut == BUT_PSIZE_BEG + p);
			}
		} else {
			int hot = (butsel == BUT_DRAG_BEG + p) || (capbut == BUT_DRAG_BEG + p);
			int mx = (x1 + x2) / 2, my = y1 + HUD_GRIP_H / 2;

			ui_panel_light(x1, y1, x2, y2);
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
	const int handle_w = 20, handle_h = 5;

	for (int p = 0; p < MAX_PANEL; p++) {
		int b = BUT_DRAG_BEG + p;
		int bx, by, dx, dy;

		/* framed panels have real chrome to grab; only the bare ones need
		 * the proximity hint */
		if (panels[p].frame != PANEL_FRAME_NONE || !panel_shown(p) || (but[b].flags & BUTF_NOHIT)) {
			continue;
		}
		bx = butx(b);
		by = buty(b);
		dx = mousex - bx;
		dy = mousey - by;
		if (dx * dx + dy * dy < proximity * proximity) {
			int hot = (butsel == b) || (capbut == b);
			render_shaded_rect(bx - handle_w, by - handle_h, bx + handle_w, by + handle_h,
			    hot ? UI_ACCENT : UI_BORDER_STRONG, hot ? UI_A_BORDER_HOV : UI_A_ROW_HOVER);
		}
	}
}

void panels_reset_layout(void)
{
	for (int p = 0; p < MAX_PANEL; p++) {
		panels[p].visible = panels[p].default_visible;
		panels[p].collapsed = 0;
		panels[p].dx = 0;
		panels[p].dy = 0;
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
	for (int p = 0; p < MAX_PANEL; p++) {
		cJSON *e = cJSON_CreateObject();

		if (!e) {
			continue;
		}
		cJSON_AddBoolToObject(e, "on", panels[p].visible);
		cJSON_AddBoolToObject(e, "min", panels[p].collapsed);
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
