/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * GUI panel system - see panels.h for the overview.
 *
 * Layout model: init_dots() always computes the pristine default layout,
 * then panels_apply_offsets() shifts each panel's dots and buttons by the
 * panel's stored (dx,dy). Dragging updates the stored offset and shifts the
 * live dots/buttons by the same delta, so a later init_dots() (resolution
 * change, small-bottom toggle, hotbar resize) reproduces the moved layout
 * instead of losing it.
 */

#include <stdint.h>
#include <stddef.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/panels.h"
#include "gui/ui_tokens.h"
#include "client/client.h"
#include "game/game.h"
#include "lib/cjson/cJSON.h"

DLL_EXPORT int gui_overlay_visible = 1;

static int fullscreen_world = 1;

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
	int visible;
	int dx, dy;
} Panel;

/* dot lists - the first dot is the clamp reference kept on screen */
static const int skills_dots[] = {DOT_SKL, DOT_SK2, DOT_CON};
static const int chat_dots[] = {DOT_TXT, DOT_TX2};
static const int inv_dots[] = {DOT_INV, DOT_IN1, DOT_IN2};
static const int gold_dots[] = {DOT_GLD, DOT_JNK};
static const int speed_dots[] = {DOT_MOD};
static const int buffs_dots[] = {DOT_SSP};
static const int hotbar_dots[] = {DOT_HOTBAR};
static const int equipment_dots[] = {DOT_WEA};

static const ButRange skills_buts[] = {{BUT_SKL_BEG, BUT_SKL_END}, {BUT_CON_BEG, BUT_CON_END}, {BUT_SCL_UP, BUT_SCL_DW},
    {BUT_DRAG_SKILLS, BUT_DRAG_SKILLS}};
static const ButRange chat_buts[] = {{BUT_DRAG_CHAT, BUT_DRAG_CHAT}};
static const ButRange inv_buts[] = {{BUT_INV_BEG, BUT_INV_END}, {BUT_SCR_UP, BUT_SCR_DW}, {BUT_DRAG_INV, BUT_DRAG_INV}};
static const ButRange gold_buts[] = {{BUT_GLD, BUT_JNK}, {BUT_DRAG_GOLD, BUT_DRAG_GOLD}};
static const ButRange speed_buts[] = {{BUT_MOD_WALK0, BUT_MOD_WALK2}, {BUT_DRAG_SPEED, BUT_DRAG_SPEED}};
static const ButRange buffs_buts[] = {{BUT_DRAG_BUFFS, BUT_DRAG_BUFFS}};
static const ButRange hotbar_buts[] = {{BUT_HOTBAR_BEG, BUT_HOTBAR_END}, {BUT_DRAG_HOTBAR, BUT_DRAG_HOTBAR}};
static const ButRange equipment_buts[] = {
    {BUT_WEA_BEG, BUT_WEA_END}, {BUT_WEA_LCK, BUT_WEA_LCK}, {BUT_DRAG_EQUIPMENT, BUT_DRAG_EQUIPMENT}};

#define PANEL_ENTRY(idstr, namestr, d, b) {idstr, namestr, d, ARRAYSIZE(d), b, ARRAYSIZE(b), 1, 0, 0}

static Panel panels[MAX_PANEL] = {
    [PANEL_SKILLS] = PANEL_ENTRY("skills", "Skills Panel", skills_dots, skills_buts),
    [PANEL_CHAT] = PANEL_ENTRY("chat", "Chat Panel", chat_dots, chat_buts),
    [PANEL_INVENTORY] = PANEL_ENTRY("inventory", "Inventory Panel", inv_dots, inv_buts),
    [PANEL_GOLD] = PANEL_ENTRY("gold", "Gold & Trash Panel", gold_dots, gold_buts),
    [PANEL_SPEED] = PANEL_ENTRY("speed", "Speed Panel", speed_dots, speed_buts),
    [PANEL_BUFFS] = PANEL_ENTRY("buffs", "Buffs Panel", buffs_dots, buffs_buts),
    [PANEL_HOTBAR] = PANEL_ENTRY("hotbar", "Hotbar", hotbar_dots, hotbar_buts),
    [PANEL_EQUIPMENT] = PANEL_ENTRY("equipment", "Equipment Panel", equipment_dots, equipment_buts),
};

static int drag_dirty; /* a drag moved something since the last save */

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
	if (panel_visible(p)) {
		return 1;
	}
	/* auto-show: an open container needs the skills panel area, an active
	 * chat line needs the chat panel (you must see what you type) */
	if (p == PANEL_SKILLS && con_cnt) {
		return 1;
	}
	if (p == PANEL_CHAT && cmd_is_active()) {
		return 1;
	}
	return 0;
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

static void panel_shift(int p, int dx, int dy)
{
	const Panel *pan = &panels[p];

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
	if (x > XRES) {
		*dx -= x - XRES;
	}
	if (y < 0) {
		*dy -= y;
	}
	if (y > YRES) {
		*dy -= y - YRES;
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

void panels_drag_finished(void)
{
	if (drag_dirty) {
		drag_dirty = 0;
		save_options();
	}
}

int panel_owns_button(int b)
{
	for (int p = 0; p < MAX_PANEL; p++) {
		for (int i = 0; i < panels[p].nbuts; i++) {
			if (b >= panels[p].buts[i].beg && b <= panels[p].buts[i].end) {
				return p;
			}
		}
	}
	return -1;
}

void panels_display_handles(void)
{
	const int proximity = 30;
	const int handle_w = 20, handle_h = 5;

	for (int p = 0; p < MAX_PANEL; p++) {
		int b = BUT_DRAG_BEG + p;
		int bx, by, dx, dy;

		if (!panel_shown(p) || (but[b].flags & BUTF_NOHIT)) {
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
		panels[p].visible = 1;
		panels[p].dx = 0;
		panels[p].dy = 0;
	}
}

DLL_EXPORT int panels_fullscreen_world(void)
{
	return fullscreen_world;
}

void panels_set_fullscreen_world(int on)
{
	fullscreen_world = on ? 1 : 0;
}

void panels_save_json(struct cJSON *root)
{
	cJSON *jp = cJSON_CreateObject();

	if (!jp) {
		return;
	}
	cJSON_AddBoolToObject(jp, "fullscreen_world", fullscreen_world);
	for (int p = 0; p < MAX_PANEL; p++) {
		cJSON *e = cJSON_CreateObject();

		if (!e) {
			continue;
		}
		cJSON_AddBoolToObject(e, "on", panels[p].visible);
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
	v = cJSON_GetObjectItem(jp, "fullscreen_world");
	if (v && cJSON_IsBool(v)) {
		fullscreen_world = cJSON_IsTrue(v) ? 1 : 0;
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
