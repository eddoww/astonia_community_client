/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Context Menu
 *
 * Displays the context menu on right click and handles mouse clicks
 * in the menu.
 *
 */

#include <math.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "modder/modder.h"

static int c_on = 0, c_x, c_y, d_y, ori_x, ori_y;
static size_t csel = -1ull, isel = -1ull, msel = -1ull;
static int lcmd_override = CMD_NONE;
static int hotbar_targeting; /* 1 = targeting was started from hotbar click, not key-hold */

#define MAXLINE    20
#define MAXLEN     120
#define MENUWIDTH  100
#define MENUHEIGHT (8 * 10 + 8)

struct menu {
	int linecnt;
	char line[MAXLINE][MAXLEN];
	int cmd[MAXLINE];
	int opt1[MAXLINE], opt2[MAXLINE];
};
struct menu menu;

static void update_ori(void)
{
	int x, y;
	const int mapdx_int = (int)MAPDX;

	if (msel != MAXMN) {
		x = (int)(msel % MAPDX) + ori_x - originx;
		y = (int)(msel / MAPDX) + ori_y - originy;
		if (x < 0 || x >= mapdx_int || y < 0 || y >= mapdx_int) {
			msel = MAXMN;
		} else {
			msel = (size_t)((unsigned int)x + (unsigned int)y * MAPDX);
		}
	}

	if (isel != MAXMN) {
		x = (int)(isel % MAPDX) + ori_x - originx;
		y = (int)(isel / MAPDX) + ori_y - originy;
		if (x < 0 || x >= mapdx_int || y < 0 || y >= mapdx_int) {
			isel = MAXMN;
		} else {
			isel = (size_t)((unsigned int)x + (unsigned int)y * MAPDX);
		}
	}

	if (csel != MAXMN) {
		x = (int)(csel % MAPDX) + ori_x - originx;
		y = (int)(csel / MAPDX) + ori_y - originy;
		if (x < 0 || x >= mapdx_int || y < 0 || y >= mapdx_int) {
			csel = MAXMN;
		} else {
			csel = (size_t)((unsigned int)x + (unsigned int)y * MAPDX);
		}
	}

	ori_x = originx;
	ori_y = originy;
}

static void makemenu(void)
{
	unsigned int co;
	char *name = "someone";

	update_ori();

	if (csel != MAXMN) {
		co = map[(int)csel].cn;
		if (co > 0 && co < MAXCHARS) {
			if (player[co].name[0]) {
				name = player[co].name;
			}
		} else {
			csel = MAXMN;
		}
	}

	menu.linecnt = 0;

#if 0
	if (csel!=MAPDX*MAPDY/2) {
	    sprintf(menu.line[menu.linecnt],"Walk");
	    menu.cmd[menu.linecnt]=CMD_MAP_MOVE;
	    menu.opt1[menu.linecnt]=originx-MAPDX/2+msel%MAPDX;
	    menu.opt2[menu.linecnt]=originy-MAPDY/2+msel/MAPDX;
	    menu.linecnt++;
	}
#endif

	if (isel != MAXMN) {
		if (map[(int)isel].flags & CMF_TAKE) {
			sprintf(menu.line[menu.linecnt], "Take Item");
			menu.cmd[menu.linecnt] = CMD_ITM_TAKE;
			menu.opt1[menu.linecnt] = originx - (int)(MAPDX / 2U) + (int)(isel % MAPDX);
			menu.opt2[menu.linecnt] = originy - (int)(MAPDY / 2U) + (int)(isel / MAPDX);
			menu.linecnt++;
		} else if (map[isel].flags & CMF_USE) {
			if (csprite) {
				sprintf(menu.line[menu.linecnt], "Use Item with");
			} else {
				sprintf(menu.line[menu.linecnt], "Use Item");
			}
			menu.cmd[menu.linecnt] = CMD_ITM_USE;
			menu.opt1[menu.linecnt] = originx - (int)(MAPDX / 2U) + (int)(isel % MAPDX);
			menu.opt2[menu.linecnt] = originy - (int)(MAPDY / 2U) + (int)(isel / MAPDX);
			menu.linecnt++;
		}
	}
	if (csprite && !map[msel].isprite && !map[msel].csprite) {
		sprintf(menu.line[menu.linecnt], "Drop");
		menu.cmd[menu.linecnt] = CMD_MAP_DROP;
		menu.opt1[menu.linecnt] = originx - (int)(MAPDX / 2U) + (int)(msel % MAPDX);
		menu.opt2[menu.linecnt] = originy - (int)(MAPDY / 2U) + (int)(msel / MAPDX);
		menu.linecnt++;
	}

	if (csel != MAXMN) {
		if (csprite && csel != MAPDX * MAPDY / 2U) {
			sprintf(menu.line[menu.linecnt], "Give to %s", name);
			menu.cmd[menu.linecnt] = CMD_CHR_GIVE;
			menu.opt1[menu.linecnt] = (int)map[csel].cn;
			menu.opt2[menu.linecnt] = 0;
			menu.linecnt++;
		}
		if (csel != MAPDX * MAPDY / 2U) {
			sprintf(menu.line[menu.linecnt], "Attack %s", name);
			menu.cmd[menu.linecnt] = CMD_CHR_ATTACK;
			menu.opt1[menu.linecnt] = (int)map[csel].cn;
			;
			menu.opt2[menu.linecnt] = 0;
			menu.linecnt++;
		}

		if (csel == MAPDX * MAPDY / 2U) {
			if (value[0][sv_val(V_FLASH)]) {
				sprintf(menu.line[menu.linecnt], "Cast Flash");
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = 0;
				menu.opt2[menu.linecnt] = CL_FLASH;
				menu.linecnt++;
			}

			if (value[0][sv_val(V_FREEZE)]) {
				sprintf(menu.line[menu.linecnt], "Cast Freeze");
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = 0;
				menu.opt2[menu.linecnt] = CL_FREEZE;
				menu.linecnt++;
			}

			if (sv_ver == 30 && value[0][V3_PULSE]) {
				sprintf(menu.line[menu.linecnt], "Cast Pulse");
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = 0;
				menu.opt2[menu.linecnt] = CL_PULSE;
				menu.linecnt++;
			}

			if (value[0][sv_val(V_WARCRY)]) {
				sprintf(menu.line[menu.linecnt], "Warcry");
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = 0;
				menu.opt2[menu.linecnt] = CL_WARCRY;
				menu.linecnt++;
			}

			if (value[0][sv_val(V_MAGICSHIELD)]) {
				sprintf(menu.line[menu.linecnt], "Cast Magic Shield");
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = 0;
				menu.opt2[menu.linecnt] = CL_MAGICSHIELD;
				menu.linecnt++;
			}

			if (value[0][sv_val(V_HEAL)]) {
				sprintf(menu.line[menu.linecnt], "Cast Heal");
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = (int)map[csel].cn;
				;
				menu.opt2[menu.linecnt] = CL_HEAL;
				menu.linecnt++;
			}

			if (value[0][sv_val(V_BLESS)]) {
				sprintf(menu.line[menu.linecnt], "Cast Bless");
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = (int)map[csel].cn;
				;
				menu.opt2[menu.linecnt] = CL_BLESS;
				menu.linecnt++;
			}
		} else {
			if (value[0][sv_val(V_FIREBALL)]) {
				sprintf(menu.line[menu.linecnt], "Fireball %s", name);
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = (int)map[csel].cn;
				menu.opt2[menu.linecnt] = CL_FIREBALL;
				menu.linecnt++;
			}

			if (value[0][sv_val(V_FLASH)]) {
				sprintf(menu.line[menu.linecnt], "L'ball %s", name);
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = (int)map[csel].cn;
				menu.opt2[menu.linecnt] = CL_BALL;
				menu.linecnt++;
			}

			if (value[0][sv_val(V_HEAL)]) {
				sprintf(menu.line[menu.linecnt], "Heal %s", name);
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = (int)map[csel].cn;
				;
				menu.opt2[menu.linecnt] = CL_HEAL;
				menu.linecnt++;
			}

			if (sv_ver == 30 && value[0][V3_BLESS]) {
				sprintf(menu.line[menu.linecnt], "Bless %s", name);
				menu.cmd[menu.linecnt] = CMD_CHR_CAST_K;
				menu.opt1[menu.linecnt] = (int)map[csel].cn;
				;
				menu.opt2[menu.linecnt] = CL_BLESS;
				menu.linecnt++;
			}
		}
	}

	sprintf(menu.line[menu.linecnt], "Examine ground");
	menu.cmd[menu.linecnt] = CMD_MAP_LOOK;
	menu.opt1[menu.linecnt] = originx - (int)(MAPDX / 2U) + (int)(msel % MAPDX);
	menu.opt2[menu.linecnt] = originy - (int)(MAPDY / 2U) + (int)(msel / MAPDX);
	menu.linecnt++;
	if (isel != MAXMN) {
		sprintf(menu.line[menu.linecnt], "Examine item");
		menu.cmd[menu.linecnt] = CMD_ITM_LOOK;
		menu.opt1[menu.linecnt] = originx - (int)(MAPDX / 2U) + (int)(isel % MAPDX);
		menu.opt2[menu.linecnt] = originy - (int)(MAPDY / 2U) + (int)(isel / MAPDX);
		menu.linecnt++;
	}
	if (csel != MAXMN) {
		sprintf(menu.line[menu.linecnt], "Inspect %s", name);
		menu.cmd[menu.linecnt] = CMD_CHR_LOOK;
		menu.opt1[menu.linecnt] = (int)map[csel].cn;
		;
		menu.opt2[menu.linecnt] = 0;
		menu.linecnt++;
	}
}

int context_open(int mx, int my)
{
	if (!(game_options & GO_CONTEXT)) {
		return 0;
	}

	csel = get_near_char(mx, my, 1);
	isel = get_near_item(mx, my, CMF_USE | CMF_TAKE, 1);
	msel = get_near_ground(mx, my);
	ori_x = originx;
	ori_y = originy;

	makemenu();
	if (menu.linecnt == 1) {
		cmd_look_map(ori_x - (int)(MAPDX / 2U) + (int)(msel % MAPDX), ori_y - (int)(MAPDY / 2U) + (int)(msel / MAPDX));
		return 1;
	}

	c_on = 1;
	c_x = mx;
	c_y = my - 10;
	d_y = 8;

	if (c_x < dotx(DOT_MTL) + 10) {
		c_x = dotx(DOT_MTL) + 10;
	}
	if (c_x > dotx(DOT_MBR) - MENUWIDTH - 10) {
		c_x = dotx(DOT_MBR) - MENUWIDTH - 10;
	}
	if (c_y < doty(DOT_MTL) + 10) {
		c_y = doty(DOT_MTL) + 10;
	}
	if (c_y > doty(DOT_MBR) - MENUHEIGHT - 10) {
		c_y = doty(DOT_MBR) - MENUHEIGHT - 10;
	}

	return 1;
}

map_index_t context_getnm(void)
{
	if (!(game_options & GO_CONTEXT)) {
		return MAXMN;
	}
	update_ori();

	if (c_on) {
		return msel;
	} else {
		return MAXMN;
	}
}

void context_stop(void)
{
	c_on = 0;
}

void context_display(int mx __attribute__((unused)), int my __attribute__((unused)))
{
	int x, y, n;

	if ((game_options & GO_CONTEXT) && c_on) {
		makemenu();

		d_y = menu.linecnt * 10 + 8;

		render_shaded_rect(c_x, c_y, c_x + MENUWIDTH, c_y + d_y, 0x0000, 95);
		x = c_x + 4;
		y = c_y + 4;

		for (n = 0; n < menu.linecnt; n++) {
			if (mousex > c_x && mousex < c_x + MENUWIDTH && mousey >= c_y + n * 10 + 4 && mousey < c_y + n * 10 + 14) {
				render_text(x, y, whitecolor, RENDER_TEXT_LEFT, menu.line[n]);
			} else {
				render_text(x, y, graycolor, RENDER_TEXT_LEFT, menu.line[n]);
			}
			y += 10;
		}
	}
}

int context_click(int mx, int my)
{
	int n;

	if ((game_options & GO_CONTEXT) && c_on) {
		c_on = 0;

		if (mx > c_x && mx < c_x + MENUWIDTH && my >= c_y && my < c_y + menu.linecnt * 10 + 8) {
			n = (my - c_y - 4) / 10;
			if (n < 0) {
				n = 0;
			}
			if (n >= menu.linecnt) {
				n = menu.linecnt - 1;
			}
			switch (menu.cmd[n]) {
			case CMD_MAP_MOVE:
				cmd_move(menu.opt1[n], menu.opt2[n]);
				break;
			case CMD_MAP_DROP:
				cmd_drop(menu.opt1[n], menu.opt2[n]);
				break;
			case CMD_MAP_LOOK:
				cmd_look_map(menu.opt1[n], menu.opt2[n]);
				break;
			case CMD_ITM_TAKE:
				cmd_take(menu.opt1[n], menu.opt2[n]);
				break;
			case CMD_ITM_USE:
				cmd_use(menu.opt1[n], menu.opt2[n]);
				break;
			case CMD_ITM_LOOK:
				cmd_look_item(menu.opt1[n], menu.opt2[n]);
				break;
			case CMD_CHR_ATTACK:
				cmd_kill((unsigned int)menu.opt1[n]);
				break;
			case CMD_CHR_GIVE:
				cmd_give((unsigned int)menu.opt1[n]);
				break;
			case CMD_CHR_LOOK:
				cmd_look_char((unsigned int)menu.opt1[n]);
				break;
			case CMD_CHR_CAST_K:
				cmd_some_spell(menu.opt2[n], 0, 0, (unsigned int)menu.opt1[n]);
				break;
			}
			return 1;
		}
	}
	return 0;
}

static int keymode = 0;

// block the next key coming UP unless any key has been pressed DOWN first
// this is to avoid registering the last letter typed before pressing enter
// as an action
static int keyupblock = 0;

int context_key(int key)
{
	if (!(game_options & GO_ACTION)) {
		return 0;
	}

	keyupblock = 0;

	if (key == '#' || key == CMD_UP || key == CMD_DOWN || key == 9 || key == '/') {
		keymode = 1;
	} else if (key == CMD_RETURN) {
		if (keymode == 1) {
			keymode = 0;
			keyupblock = 1;
			return 0;
		}
		keymode = 1;
		return 1;
	}
	if (keymode) {
		return 0;
	}

	return 1;
}

void context_key_reset(void)
{
	lcmd_override = CMD_NONE;
	hotbar_targeting = 0;
}

int context_key_click(void)
{
	if (lcmd_override == CMD_NONE) {
		return CMD_NONE;
	}

	if (hotbar_targeting) {
		/* hotbar-initiated targeting: execute the actual command (lcmd was
		 * set by context_key_set_cmd), then clear the override */
		hotbar_targeting = 0;
		lcmd_override = CMD_NONE;
		return CMD_NONE; /* fall through to exec_cmd(lcmd) in caller */
	}

	/* key-hold targeting: return CMD_MAP_MOVE so player walks to target,
	 * actual spell execution happens on keyup */
	return CMD_MAP_MOVE;
}

/* ── Self-cast dispatch table ──────────────────────────────────────────
 *
 * Maps ACTION_* indices to CL_* spell IDs for self/map-cast (the 100+
 * row). Used by both execute_action and execute_action_normal to avoid
 * duplicating the same switch block.  Returns 1 if handled, 0 if not.
 */
static int try_self_cast(int action_slot)
{
	static const struct {
		int action;
		int cl_spell;
	} self_spells[] = {
	    {ACTION_FLASH, CL_FLASH},
	    {ACTION_FREEZE, CL_FREEZE},
	    {ACTION_SHIELD, CL_MAGICSHIELD},
	    {ACTION_BLESS, CL_BLESS},
	    {ACTION_HEAL, CL_HEAL},
	    {ACTION_WARCRY, CL_WARCRY},
	    {ACTION_PULSE, CL_PULSE},
	    {ACTION_FIRERING, CL_FIREBALL},
	};

	if (action_slot == 100 + ACTION_MAP) {
		minimap_toggle();
		return 1;
	}

	for (int i = 0; i < (int)(sizeof(self_spells) / sizeof(self_spells[0])); i++) {
		if (action_slot == 100 + self_spells[i].action) {
			cmd_some_spell(self_spells[i].cl_spell, 0, 0, map[plrmn].cn);
			return 1;
		}
	}
	return 0;
}

/* try to execute a targeted action at whatever is under the cursor.
 * Returns 1 if a valid target was found and the action executed. */
static int try_targeted_action(int action_slot, size_t csel, size_t isel, size_t msel)
{
	switch (action_slot) {
	case ACTION_ATTACK:
		if (csel != MAXMN) {
			cmd_kill(map[csel].cn);
			return 1;
		}
		break;
	case ACTION_FIREBALL:
		if (csel != MAXMN) {
			cmd_some_spell(CL_FIREBALL, 0, 0, map[csel].cn);
			return 1;
		}
		break;
	case ACTION_LBALL:
		if (csel != MAXMN) {
			cmd_some_spell(CL_BALL, 0, 0, map[csel].cn);
			return 1;
		}
		break;
	case ACTION_BLESS:
		if (csel != MAXMN) {
			cmd_some_spell(CL_BLESS, 0, 0, map[csel].cn);
			return 1;
		}
		break;
	case ACTION_HEAL:
		if (csel != MAXMN) {
			cmd_some_spell(CL_HEAL, 0, 0, map[csel].cn);
			return 1;
		}
		break;
	case 100 + ACTION_FIREBALL:
		if (msel != MAXMN) {
			cmd_some_spell(CL_FIREBALL, originx - (int)(MAPDX / 2U) + (int)(msel % MAPDX),
			    originy - (int)(MAPDY / 2U) + (int)(msel / MAPDX), 0);
			return 1;
		}
		break;
	case 100 + ACTION_LBALL:
		if (msel != MAXMN) {
			cmd_some_spell(CL_BALL, originx - (int)(MAPDX / 2U) + (int)(msel % MAPDX),
			    originy - (int)(MAPDY / 2U) + (int)(msel / MAPDX), 0);
			return 1;
		}
		break;
	case ACTION_TAKEGIVE:
		if (csprite) {
			if (csel != MAXMN) {
				cmd_give(map[csel].cn);
				return 1;
			}
			if (isel != MAXMN && (map[isel].flags & CMF_USE)) {
				cmd_use(originx - (int)(MAPDX / 2U) + (int)(isel % MAPDX),
				    originy - (int)(MAPDY / 2U) + (int)(isel / MAPDX));
				return 1;
			}
			if (msel != MAXMN) {
				cmd_drop(originx - (int)(MAPDX / 2U) + (int)(msel % MAPDX),
				    originy - (int)(MAPDY / 2U) + (int)(msel / MAPDX));
				return 1;
			}
		} else if (isel != MAXMN) {
			if (map[(int)isel].flags & CMF_TAKE) {
				cmd_take(originx - (int)(MAPDX / 2U) + (int)(isel % MAPDX),
				    originy - (int)(MAPDY / 2U) + (int)(isel / MAPDX));
				return 1;
			}
			if (map[isel].flags & CMF_USE) {
				cmd_use(originx - (int)(MAPDX / 2U) + (int)(isel % MAPDX),
				    originy - (int)(MAPDY / 2U) + (int)(isel / MAPDX));
				return 1;
			}
		}
		break;
	case ACTION_LOOK:
		if (isel != MAXMN) {
			cmd_look_item(
			    originx - (int)(MAPDX / 2U) + (int)(isel % MAPDX), originy - (int)(MAPDY / 2U) + (int)(isel / MAPDX));
			return 1;
		}
		if (csel != MAXMN) {
			cmd_look_char(map[csel].cn);
			return 1;
		}
		if (msel != MAXMN) {
			cmd_look_map(
			    originx - (int)(MAPDX / 2U) + (int)(msel % MAPDX), originy - (int)(MAPDY / 2U) + (int)(msel / MAPDX));
			return 1;
		}
		break;
	}
	return 0;
}

/* resolve cursor targets from the current frame's pre-computed selections. */
static void resolve_cursor_targets(size_t *out_csel, size_t *out_isel, size_t *out_msel)
{
	if (mousex >= dotx(DOT_MTL) && mousey >= doty(DOT_MTL) && mousex < dotx(DOT_MBR) && mousey < doty(DOT_MBR)) {
		*out_csel = chrsel;
		*out_isel = itmsel;
		*out_msel = mapsel;
	} else {
		*out_csel = *out_isel = *out_msel = MAXMN;
	}
}

/* execute an action immediately using whatever is under the cursor.
 * For self-cast spells, executes right away. For targeted spells,
 * uses chrsel/itmsel/mapsel from the current frame.
 * Returns 1 if something was executed, 0 if it needs a target (sets lcmd_override). */
int context_execute_action(int action_slot)
{
	if (!(game_options & GO_ACTION)) {
		return 0;
	}

	if (try_self_cast(action_slot)) {
		return 1;
	}

	size_t csel, isel, msel;
	resolve_cursor_targets(&csel, &isel, &msel);

	if (try_targeted_action(action_slot, csel, isel, msel)) {
		return 1;
	}

	/* no valid target under cursor — fall back to setting lcmd_override
	 * so the cursor changes and the next click on a valid target executes */
	context_activate_action(action_slot);
	hotbar_targeting = 1;
	return 0;
}

/* normal cast: self-cast spells execute immediately, targeted spells
 * always enter targeting mode (cursor indicator) regardless of what's
 * under the cursor. The player must click to confirm the target. */
int context_execute_action_normal(int action_slot)
{
	if (!(game_options & GO_ACTION)) {
		return 0;
	}

	if (try_self_cast(action_slot)) {
		return 1;
	}

	/* targeted spells: enter targeting mode, don't execute yet */
	context_activate_action(action_slot);
	hotbar_targeting = 1;
	return 0;
}

/* activate an action by slot index (0-13 = combat/target, 100+ = self/map cast) */
void context_activate_action(int action_slot)
{
	if (!(game_options & GO_ACTION)) {
		return;
	}
	if (keymode) {
		return;
	}

	switch (action_slot) {
	case ACTION_ATTACK:
		lcmd_override = CMD_CHR_ATTACK;
		break;
	case ACTION_FIREBALL:
		lcmd_override = CMD_CHR_CAST_L;
		break;
	case ACTION_LBALL:
		lcmd_override = CMD_CHR_CAST_R;
		break;
	case ACTION_BLESS:
	case ACTION_HEAL:
		lcmd_override = CMD_CHR_CAST_K;
		break;
	case ACTION_TAKEGIVE:
		lcmd_override = CMD_ITM_USE;
		break;
	case ACTION_LOOK:
		lcmd_override = CMD_ITM_LOOK;
		break;
	case 100 + ACTION_FIREBALL:
		lcmd_override = CMD_MAP_CAST_L;
		break;
	case 100 + ACTION_LBALL:
		lcmd_override = CMD_MAP_CAST_R;
		break;
	case 100 + ACTION_FLASH:
	case 100 + ACTION_FREEZE:
	case 100 + ACTION_SHIELD:
	case 100 + ACTION_BLESS:
	case 100 + ACTION_HEAL:
	case 100 + ACTION_WARCRY:
	case 100 + ACTION_PULSE:
	case 100 + ACTION_FIRERING:
		lcmd_override = CMD_SLF_CAST_K;
		break;
	}
}

void context_keydown(SDL_Keycode key)
{
	// ignore key-down while over action bar
	if (actsel != -1) {
		return;
	}
	context_activate_action(action_key2slot(key));
}

int context_key_set_cmd(void)
{
	if (!(game_options & GO_ACTION)) {
		return 0;
	}
	if (keymode) {
		return 0;
	}
	if (lcmd_override == CMD_NONE) {
		return 0;
	}

	chrsel = itmsel = mapsel = MAXMN;

	switch (lcmd_override) {
	case CMD_CHR_ATTACK:
	case CMD_CHR_CAST_K:
		chrsel = get_near_char(mousex, mousey, 3);
		break;
	case CMD_CHR_CAST_L:
	case CMD_CHR_CAST_R:
		/* for fireball/lball: try character first, fall back to map */
		chrsel = get_near_char(mousex, mousey, 3);
		if (chrsel == MAXMN) {
			mapsel = get_near_ground(mousex, mousey);
			lcmd_override = (lcmd_override == CMD_CHR_CAST_L) ? CMD_MAP_CAST_L : CMD_MAP_CAST_R;
		}
		break;
	case CMD_MAP_CAST_L:
	case CMD_MAP_CAST_R:
		mapsel = get_near_ground(mousex, mousey);
		break;

	case CMD_ITM_LOOK:
	case CMD_CHR_LOOK:
	case CMD_MAP_LOOK: {
		map_index_t tmp = get_near_ex(mousex, mousey, CMF_USE | CMF_TAKE | NEAR_ITEM | NEAR_CHAR, 5);
		if (tmp != MAXMN) {
			if (map[tmp].csprite) {
				chrsel = tmp;
				lcmd_override = CMD_CHR_LOOK;
			} else {
				itmsel = tmp;
				lcmd_override = CMD_ITM_LOOK;
			}
		} else {
			mapsel = get_near_ground(mousex, mousey);
			lcmd_override = CMD_MAP_LOOK;
		}
		break;
	}
	case CMD_ITM_USE:
	case CMD_ITM_USE_WITH:
	case CMD_ITM_TAKE:
	case CMD_CHR_GIVE:
	case CMD_MAP_DROP:
		if (csprite) { // give, use with or drop
			map_index_t tmp = get_near_ex(mousex, mousey, CMF_USE | CMF_TAKE | NEAR_ITEM | NEAR_CHAR | NEAR_NOTSELF, 2);
			if (tmp != MAXMN) {
				if (map[tmp].csprite) {
					chrsel = tmp;
					lcmd_override = CMD_CHR_GIVE;
				} else {
					itmsel = tmp;
					lcmd_override = CMD_ITM_USE_WITH;
				}
			} else {
				mapsel = get_near_ground(mousex, mousey);
				lcmd_override = CMD_MAP_DROP;
			}
		} else { // take or use
			itmsel = get_near_item(mousex, mousey, CMF_TAKE | CMF_USE, 5);
			if (itmsel != MAXMN) {
				if (map[itmsel].flags & CMF_TAKE) {
					lcmd_override = CMD_ITM_TAKE;
				} else if (map[itmsel].flags & CMF_USE) {
					lcmd_override = CMD_ITM_USE;
				} else {
					itmsel = MAXMN;
				}
			}
		}
		break;
	}
	lcmd = lcmd_override;

	return 1;
}

void context_keyup(SDL_Keycode key)
{
	/* don't clear targeting mode if it was initiated from the hotbar —
	 * the player pressed a hotkey, released it, and will click to cast */
	if (!hotbar_targeting) {
		lcmd_override = CMD_NONE;
	}

	if (amod_keyup(key)) {
		return;
	}

	if (!(game_options & GO_ACTION)) {
		return;
	}
	if (keymode) {
		return;
	}
	if (keyupblock) {
		return;
	}
	if (key == '-') {
		return;
	}
	if ((unsigned int)key & 0xffffff00U) {
		return;
	}

	if (actsel != (int)MAXMN && !act_lck) {
		action_set_key(actsel, key);
		return;
	}

	int slot = action_key2slot(key);
	if (try_self_cast(slot)) {
		return;
	}

	size_t csel, isel, msel;
	resolve_cursor_targets(&csel, &isel, &msel);
	try_targeted_action(slot, csel, isel, msel);
}

int context_key_set(int onoff)
{
	int old;
	if (!(game_options & GO_ACTION)) {
		return 1;
	}
	old = keymode;
	keymode = onoff;
	return old;
}

int context_key_isset(void)
{
	if (!(game_options & GO_ACTION)) {
		return 1;
	}
	return keymode;
}

int context_key_enabled(void)
{
	return (game_options & GO_ACTION);
}

void context_action_enable(int onoff)
{
	action_enabled = onoff;
	dots_update();
	save_options();
}

int context_action_enabled(void)
{
	return (game_options & GO_ACTION) && action_enabled;
}
