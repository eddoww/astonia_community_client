/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Lua API
 *
 * Exposes client functions and data to Lua scripts.
 */

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "astonia.h"
#include "client/client.h"
#include "game/game.h"
#include "gui/gui.h"

// External declarations from gui module (not exported in headers)
extern int mousex, mousey;
extern int plrmn;
extern map_index_t itmsel, chrsel, mapsel;

// External declarations for skills
extern struct skill *game_skill;
extern char **game_skilldesc;

// External declarations for quests
extern struct quest quest[];
extern struct questlog *game_questlog;
extern int *game_questcount;

// External declarations for mil_rank function
int mil_rank(int exp);

// Forward declaration for API registration function (called from lua_core.c)
void lua_api_register(lua_State *L);

// --- Logging functions ---

static int l_note(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	note("[Lua] %s", msg);
	return 0;
}

static int l_warn(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	warn("[Lua] %s", msg);
	return 0;
}

static int l_addline(lua_State *L)
{
	const char *msg = luaL_checkstring(L, 1);
	addline("%s", msg);
	return 0;
}

// --- Rendering functions ---

static int l_render_text(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	unsigned short color = (unsigned short)luaL_checkinteger(L, 3);
	int flags = (int)luaL_optinteger(L, 4, 0);
	const char *text = luaL_checkstring(L, 5);

	int result = render_text(x, y, color, flags, text);
	lua_pushinteger(L, result);
	return 1;
}

static int l_render_rect(lua_State *L)
{
	int sx = (int)luaL_checkinteger(L, 1);
	int sy = (int)luaL_checkinteger(L, 2);
	int ex = (int)luaL_checkinteger(L, 3);
	int ey = (int)luaL_checkinteger(L, 4);
	unsigned short color = (unsigned short)luaL_checkinteger(L, 5);

	render_rect(sx, sy, ex, ey, color);
	return 0;
}

static int l_render_line(lua_State *L)
{
	int fx = (int)luaL_checkinteger(L, 1);
	int fy = (int)luaL_checkinteger(L, 2);
	int tx = (int)luaL_checkinteger(L, 3);
	int ty = (int)luaL_checkinteger(L, 4);
	unsigned short color = (unsigned short)luaL_checkinteger(L, 5);

	// Skip rendering if any coordinate is negative (prevents crashes)
	if (fx < 0 || fy < 0 || tx < 0 || ty < 0) {
		return 0;
	}

	render_line(fx, fy, tx, ty, color);
	return 0;
}

static int l_render_pixel(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	unsigned short color = (unsigned short)luaL_checkinteger(L, 3);

	// Skip rendering if coordinates are negative (prevents crashes)
	if (x < 0 || y < 0) {
		return 0;
	}

	render_pixel(x, y, color);
	return 0;
}

static int l_render_sprite(lua_State *L)
{
	unsigned int sprite = (unsigned int)luaL_checkinteger(L, 1);
	int x = (int)luaL_checkinteger(L, 2);
	int y = (int)luaL_checkinteger(L, 3);
	char light = (char)luaL_optinteger(L, 4, 0);
	char align = (char)luaL_optinteger(L, 5, 0);

	// Skip invalid sprite 0 (causes texture cache issues)
	if (sprite == 0) {
		return 0;
	}

	render_sprite(sprite, x, y, light, align);
	return 0;
}

static int l_render_text_length(lua_State *L)
{
	int flags = (int)luaL_optinteger(L, 1, 0);
	const char *text = luaL_checkstring(L, 2);

	int length = render_text_length(flags, text);
	lua_pushinteger(L, length);
	return 1;
}

// --- Color functions ---

static int l_rgb(lua_State *L)
{
	int r = (int)luaL_checkinteger(L, 1);
	int g = (int)luaL_checkinteger(L, 2);
	int b = (int)luaL_checkinteger(L, 3);

	// Clamp to 5-bit values (0-31)
	if (r < 0) {
		r = 0;
	}
	if (r > 31) {
		r = 31;
	}
	if (g < 0) {
		g = 0;
	}
	if (g > 31) {
		g = 31;
	}
	if (b < 0) {
		b = 0;
	}
	if (b > 31) {
		b = 31;
	}

	unsigned short color = (unsigned short)IRGB(r, g, b);
	lua_pushinteger(L, color);
	return 1;
}

// --- Game data accessors ---

static int l_get_hp(lua_State *L)
{
	lua_pushinteger(L, hp);
	return 1;
}

static int l_get_mana(lua_State *L)
{
	lua_pushinteger(L, mana);
	return 1;
}

static int l_get_rage(lua_State *L)
{
	lua_pushinteger(L, rage);
	return 1;
}

static int l_get_endurance(lua_State *L)
{
	lua_pushinteger(L, endurance);
	return 1;
}

static int l_get_lifeshield(lua_State *L)
{
	lua_pushinteger(L, lifeshield);
	return 1;
}

static int l_get_experience(lua_State *L)
{
	lua_pushinteger(L, experience);
	return 1;
}

static int l_get_gold(lua_State *L)
{
	lua_pushinteger(L, gold);
	return 1;
}

static int l_get_tick(lua_State *L)
{
	lua_pushinteger(L, tick);
	return 1;
}

static int l_get_username(lua_State *L)
{
	lua_pushstring(L, username);
	return 1;
}

static int l_get_origin(lua_State *L)
{
	lua_pushinteger(L, originx);
	lua_pushinteger(L, originy);
	return 2;
}

static int l_get_mouse(lua_State *L)
{
	lua_pushinteger(L, mousex);
	lua_pushinteger(L, mousey);
	return 2;
}

// Screen to map coordinate conversion
static int l_stom(lua_State *L)
{
	int scrx = (int)luaL_checkinteger(L, 1);
	int scry = (int)luaL_checkinteger(L, 2);
	int mapx, mapy;

	if (stom(scrx, scry, &mapx, &mapy)) {
		lua_pushinteger(L, mapx);
		lua_pushinteger(L, mapy);
		return 2;
	}

	// Outside map area - return nil
	lua_pushnil(L);
	lua_pushnil(L);
	return 2;
}

// Map to screen coordinate conversion
static int l_mtos(lua_State *L)
{
	unsigned int mapx = (unsigned int)luaL_checkinteger(L, 1);
	unsigned int mapy = (unsigned int)luaL_checkinteger(L, 2);
	int scrx, scry;

	mtos(mapx, mapy, &scrx, &scry);
	lua_pushinteger(L, scrx);
	lua_pushinteger(L, scry);
	return 2;
}

// Get world coordinates (origin + local map position)
// This is what you see when right-clicking in-game
static int l_get_world_pos(lua_State *L)
{
	int mapx = (int)luaL_checkinteger(L, 1);
	int mapy = (int)luaL_checkinteger(L, 2);

	int worldx = originx - (int)(MAPDX / 2) + mapx;
	int worldy = originy - (int)(MAPDY / 2) + mapy;

	lua_pushinteger(L, worldx);
	lua_pushinteger(L, worldy);
	return 2;
}

// Get player map number (local position in map array)
static int l_get_plrmn(lua_State *L)
{
	lua_pushinteger(L, plrmn);
	return 1;
}

// Get player world position
static int l_get_player_world_pos(lua_State *L)
{
	int mapx = (int)((unsigned int)plrmn % MAPDX);
	int mapy = (int)((unsigned int)plrmn / MAPDX);

	int worldx = originx - (int)(MAPDX / 2) + mapx;
	int worldy = originy - (int)(MAPDY / 2) + mapy;

	lua_pushinteger(L, worldx);
	lua_pushinteger(L, worldy);
	return 2;
}

// --- Selection info ---

// Get currently selected item map index
static int l_get_itmsel(lua_State *L)
{
	if (itmsel == MAXMN) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, (int)itmsel);
	return 1;
}

// Get currently selected character map index
static int l_get_chrsel(lua_State *L)
{
	if (chrsel == MAXMN) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, (int)chrsel);
	return 1;
}

// Get currently selected map tile
static int l_get_mapsel(lua_State *L)
{
	if (mapsel == MAXMN) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, (int)mapsel);
	return 1;
}

// Get current action and target position
static int l_get_action(lua_State *L)
{
	lua_newtable(L);

	lua_pushinteger(L, act);
	lua_setfield(L, -2, "act");

	lua_pushinteger(L, actx);
	lua_setfield(L, -2, "x");

	lua_pushinteger(L, acty);
	lua_setfield(L, -2, "y");

	return 1;
}

// --- Look/Inspect info ---

// Get current look target name
static int l_get_look_name(lua_State *L)
{
	lua_pushstring(L, look_name);
	return 1;
}

// Get current look target description
static int l_get_look_desc(lua_State *L)
{
	lua_pushstring(L, look_desc);
	return 1;
}

// Get look inventory sprite (for equipment display)
static int l_get_lookinv(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= 12) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, lookinv[idx]);
	return 1;
}

// --- Container info ---

// Get container type (0=none, 1=container, 2=shop)
static int l_get_con_type(lua_State *L)
{
	lua_pushinteger(L, con_type);
	return 1;
}

// Get container name
static int l_get_con_name(lua_State *L)
{
	lua_pushstring(L, con_name);
	return 1;
}

// Get container item count
static int l_get_con_cnt(lua_State *L)
{
	lua_pushinteger(L, con_cnt);
	return 1;
}

// Get container item sprite
static int l_get_container(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= CONTAINERSIZE) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, container[idx]);
	return 1;
}

// --- Player state ---

// Get player speed state: 0=normal, 1=fast, 2=stealth
static int l_get_pspeed(lua_State *L)
{
	lua_pushinteger(L, pspeed);
	return 1;
}

// Get military experience
static int l_get_mil_exp(lua_State *L)
{
	lua_pushinteger(L, mil_exp);
	return 1;
}

// Get military rank from experience
static int l_get_mil_rank(lua_State *L)
{
	int exp = (int)luaL_optinteger(L, 1, (lua_Integer)mil_exp);
	lua_pushinteger(L, mil_rank(exp));
	return 1;
}

// --- Skill info ---

// Get skill name
static int l_get_skill_name(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= V_MAX || game_skill == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushstring(L, game_skill[idx].name);
	return 1;
}

// Get skill description
static int l_get_skill_desc(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= V_MAX || game_skilldesc == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushstring(L, game_skilldesc[idx]);
	return 1;
}

// Get skill info (bases, cost, start)
static int l_get_skill_info(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= V_MAX || game_skill == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);

	lua_pushstring(L, game_skill[idx].name);
	lua_setfield(L, -2, "name");

	lua_pushinteger(L, game_skill[idx].base1);
	lua_setfield(L, -2, "base1");

	lua_pushinteger(L, game_skill[idx].base2);
	lua_setfield(L, -2, "base2");

	lua_pushinteger(L, game_skill[idx].base3);
	lua_setfield(L, -2, "base3");

	lua_pushinteger(L, game_skill[idx].cost);
	lua_setfield(L, -2, "cost");

	lua_pushinteger(L, game_skill[idx].start);
	lua_setfield(L, -2, "start");

	return 1;
}

// Get raise cost for a skill
static int l_get_raise_cost(lua_State *L)
{
	int v = (int)luaL_checkinteger(L, 1);
	int n = (int)luaL_checkinteger(L, 2);

	lua_pushinteger(L, raise_cost(v, n));
	return 1;
}

// --- Quest info ---

// Get quest count
static int l_get_quest_count(lua_State *L)
{
	if (game_questcount == NULL) {
		lua_pushinteger(L, 0);
		return 1;
	}
	lua_pushinteger(L, *game_questcount);
	return 1;
}

// Get quest status (done/open flags)
static int l_get_quest_status(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= MAXQUEST) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);

	lua_pushinteger(L, quest[idx].done);
	lua_setfield(L, -2, "done");

	lua_pushinteger(L, quest[idx].flags);
	lua_setfield(L, -2, "flags");

	return 1;
}

// Get quest log info
static int l_get_quest_info(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (game_questlog == NULL || game_questcount == NULL || idx < 0 || idx >= *game_questcount) {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);

	if (game_questlog[idx].name) {
		lua_pushstring(L, game_questlog[idx].name);
		lua_setfield(L, -2, "name");
	}

	lua_pushinteger(L, game_questlog[idx].minlevel);
	lua_setfield(L, -2, "minlevel");

	lua_pushinteger(L, game_questlog[idx].maxlevel);
	lua_setfield(L, -2, "maxlevel");

	if (game_questlog[idx].giver) {
		lua_pushstring(L, game_questlog[idx].giver);
		lua_setfield(L, -2, "giver");
	}

	if (game_questlog[idx].area) {
		lua_pushstring(L, game_questlog[idx].area);
		lua_setfield(L, -2, "area");
	}

	lua_pushinteger(L, game_questlog[idx].exp);
	lua_setfield(L, -2, "exp");

	lua_pushinteger(L, game_questlog[idx].flags);
	lua_setfield(L, -2, "flags");

	return 1;
}

// Get character stat value
static int l_get_value(lua_State *L)
{
	int type = (int)luaL_checkinteger(L, 1); // 0 = modified (with gear/buffs), 1 = base (trained)
	int idx = (int)luaL_checkinteger(L, 2);

	if (type < 0 || type > 1 || idx < 0 || idx >= V_MAX) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, value[type][idx]);
	return 1;
}

// Get inventory item sprite
static int l_get_item(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= INVENTORYSIZE) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, item[idx]);
	return 1;
}

// Get inventory item flags
static int l_get_item_flags(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= INVENTORYSIZE) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, item_flags[idx]);
	return 1;
}

// Get map tile information
static int l_get_map_tile(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);

	if (x < 0 || (unsigned int)x >= MAPDX || y < 0 || (unsigned int)y >= MAPDY) {
		lua_pushnil(L);
		return 1;
	}

	map_index_t mn = (map_index_t)((unsigned int)x + (unsigned int)y * MAPDX);
	struct map *m = &map[mn];

	// Return a table with tile information
	lua_newtable(L);

	lua_pushinteger(L, m->gsprite);
	lua_setfield(L, -2, "gsprite");

	lua_pushinteger(L, m->fsprite);
	lua_setfield(L, -2, "fsprite");

	lua_pushinteger(L, m->isprite);
	lua_setfield(L, -2, "isprite");

	lua_pushinteger(L, m->csprite);
	lua_setfield(L, -2, "csprite");

	lua_pushinteger(L, m->cn);
	lua_setfield(L, -2, "cn");

	lua_pushinteger(L, m->flags);
	lua_setfield(L, -2, "flags");

	lua_pushinteger(L, m->health);
	lua_setfield(L, -2, "health");

	return 1;
}

// Get player information by index
static int l_get_player(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);

	if (idx < 0 || idx >= MAXCHARS) {
		lua_pushnil(L);
		return 1;
	}

	struct player *p = &player[idx];

	// Return nil if player slot is empty (check name like the game does)
	if (p->name[0] == '\0') {
		lua_pushnil(L);
		return 1;
	}

	lua_newtable(L);

	lua_pushstring(L, p->name);
	lua_setfield(L, -2, "name");

	lua_pushinteger(L, p->csprite);
	lua_setfield(L, -2, "sprite");

	lua_pushinteger(L, p->level);
	lua_setfield(L, -2, "level");

	lua_pushinteger(L, p->clan);
	lua_setfield(L, -2, "clan");

	lua_pushinteger(L, p->pk_status);
	lua_setfield(L, -2, "pk_status");

	return 1;
}

// --- GUI helper functions ---

static int l_dotx(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, dotx(idx));
	return 1;
}

static int l_doty(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, doty(idx));
	return 1;
}

static int l_butx(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, butx(idx));
	return 1;
}

static int l_buty(lua_State *L)
{
	int idx = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, buty(idx));
	return 1;
}

// --- Utility functions ---

static int l_exp2level(lua_State *L)
{
	int exp = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, exp2level(exp));
	return 1;
}

static int l_level2exp(lua_State *L)
{
	int level = (int)luaL_checkinteger(L, 1);
	lua_pushinteger(L, level2exp(level));
	return 1;
}

// --- Command functions ---

static int l_cmd_text(lua_State *L)
{
	const char *text = luaL_checkstring(L, 1);
	// Create a non-const copy for cmd_text
	char buf[256];
	strncpy(buf, text, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	cmd_text(buf);
	return 0;
}

// --- Clipboard functions ---

// Set clipboard text
static int l_set_clipboard(lua_State *L)
{
	const char *text = luaL_checkstring(L, 1);
	int result = SDL_SetClipboardText(text);
	lua_pushboolean(L, result == 0);
	return 1;
}

// Get clipboard text
static int l_get_clipboard(lua_State *L)
{
	if (!SDL_HasClipboardText()) {
		lua_pushnil(L);
		return 1;
	}
	char *text = SDL_GetClipboardText();
	if (text) {
		lua_pushstring(L, text);
		SDL_free(text);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// --- Registration ---

static const luaL_Reg client_funcs[] = {
    // Logging
    {"note", l_note}, {"warn", l_warn}, {"addline", l_addline},

    // Rendering
    {"render_text", l_render_text}, {"render_rect", l_render_rect}, {"render_line", l_render_line},
    {"render_pixel", l_render_pixel}, {"render_sprite", l_render_sprite}, {"render_text_length", l_render_text_length},

    // Colors
    {"rgb", l_rgb},

    // Game data
    {"get_hp", l_get_hp}, {"get_mana", l_get_mana}, {"get_rage", l_get_rage}, {"get_endurance", l_get_endurance},
    {"get_lifeshield", l_get_lifeshield}, {"get_experience", l_get_experience}, {"get_gold", l_get_gold},
    {"get_tick", l_get_tick}, {"get_username", l_get_username}, {"get_origin", l_get_origin},
    {"get_mouse", l_get_mouse}, {"stom", l_stom}, {"mtos", l_mtos}, {"get_world_pos", l_get_world_pos},
    {"get_plrmn", l_get_plrmn}, {"get_player_world_pos", l_get_player_world_pos}, {"get_value", l_get_value},
    {"get_item", l_get_item}, {"get_item_flags", l_get_item_flags}, {"get_map_tile", l_get_map_tile},
    {"get_player", l_get_player},

    // Selection info
    {"get_itmsel", l_get_itmsel}, {"get_chrsel", l_get_chrsel}, {"get_mapsel", l_get_mapsel},
    {"get_action", l_get_action},

    // Look/Inspect info
    {"get_look_name", l_get_look_name}, {"get_look_desc", l_get_look_desc}, {"get_lookinv", l_get_lookinv},

    // Container info
    {"get_con_type", l_get_con_type}, {"get_con_name", l_get_con_name}, {"get_con_cnt", l_get_con_cnt},
    {"get_container", l_get_container},

    // Player state
    {"get_pspeed", l_get_pspeed}, {"get_mil_exp", l_get_mil_exp}, {"get_mil_rank", l_get_mil_rank},

    // Skill info
    {"get_skill_name", l_get_skill_name}, {"get_skill_desc", l_get_skill_desc}, {"get_skill_info", l_get_skill_info},
    {"get_raise_cost", l_get_raise_cost},

    // Quest info
    {"get_quest_count", l_get_quest_count}, {"get_quest_status", l_get_quest_status},
    {"get_quest_info", l_get_quest_info},

    // GUI helpers
    {"dotx", l_dotx}, {"doty", l_doty}, {"butx", l_butx}, {"buty", l_buty},

    // Utilities
    {"exp2level", l_exp2level}, {"level2exp", l_level2exp},

    // Commands
    {"cmd_text", l_cmd_text},

    // Clipboard
    {"set_clipboard", l_set_clipboard}, {"get_clipboard", l_get_clipboard},

    {NULL, NULL}};

void lua_api_register(lua_State *L)
{
	// Create the 'client' table for API functions
	lua_newtable(L);
	luaL_setfuncs(L, client_funcs, 0);
	lua_setglobal(L, "client");

	// Create constants table
	lua_newtable(L);

	// Primary stat indices
	lua_pushinteger(L, V_HP);
	lua_setfield(L, -2, "V_HP");
	lua_pushinteger(L, V_ENDURANCE);
	lua_setfield(L, -2, "V_ENDURANCE");
	lua_pushinteger(L, V_MANA);
	lua_setfield(L, -2, "V_MANA");
	lua_pushinteger(L, V_WIS);
	lua_setfield(L, -2, "V_WIS");
	lua_pushinteger(L, V_INT);
	lua_setfield(L, -2, "V_INT");
	lua_pushinteger(L, V_AGI);
	lua_setfield(L, -2, "V_AGI");
	lua_pushinteger(L, V_STR);
	lua_setfield(L, -2, "V_STR");

	// Combat stat indices
	lua_pushinteger(L, V_ARMOR);
	lua_setfield(L, -2, "V_ARMOR");
	lua_pushinteger(L, V_WEAPON);
	lua_setfield(L, -2, "V_WEAPON");
	lua_pushinteger(L, V_LIGHT);
	lua_setfield(L, -2, "V_LIGHT");
	lua_pushinteger(L, V_SPEED);
	lua_setfield(L, -2, "V_SPEED");

	// Weapon skills
	lua_pushinteger(L, V_PULSE);
	lua_setfield(L, -2, "V_PULSE");
	lua_pushinteger(L, V_DAGGER);
	lua_setfield(L, -2, "V_DAGGER");
	lua_pushinteger(L, V_HAND);
	lua_setfield(L, -2, "V_HAND");
	lua_pushinteger(L, V_STAFF);
	lua_setfield(L, -2, "V_STAFF");
	lua_pushinteger(L, V_SWORD);
	lua_setfield(L, -2, "V_SWORD");
	lua_pushinteger(L, V_TWOHAND);
	lua_setfield(L, -2, "V_TWOHAND");

	// Combat skills
	lua_pushinteger(L, V_ARMORSKILL);
	lua_setfield(L, -2, "V_ARMORSKILL");
	lua_pushinteger(L, V_ATTACK);
	lua_setfield(L, -2, "V_ATTACK");
	lua_pushinteger(L, V_PARRY);
	lua_setfield(L, -2, "V_PARRY");
	lua_pushinteger(L, V_WARCRY);
	lua_setfield(L, -2, "V_WARCRY");
	lua_pushinteger(L, V_TACTICS);
	lua_setfield(L, -2, "V_TACTICS");
	lua_pushinteger(L, V_SURROUND);
	lua_setfield(L, -2, "V_SURROUND");
	lua_pushinteger(L, V_BODYCONTROL);
	lua_setfield(L, -2, "V_BODYCONTROL");
	lua_pushinteger(L, V_SPEEDSKILL);
	lua_setfield(L, -2, "V_SPEEDSKILL");

	// Utility skills
	lua_pushinteger(L, V_BARTER);
	lua_setfield(L, -2, "V_BARTER");
	lua_pushinteger(L, V_PERCEPT);
	lua_setfield(L, -2, "V_PERCEPT");
	lua_pushinteger(L, V_STEALTH);
	lua_setfield(L, -2, "V_STEALTH");

	// Magic skills
	lua_pushinteger(L, V_BLESS);
	lua_setfield(L, -2, "V_BLESS");
	lua_pushinteger(L, V_HEAL);
	lua_setfield(L, -2, "V_HEAL");
	lua_pushinteger(L, V_FREEZE);
	lua_setfield(L, -2, "V_FREEZE");
	lua_pushinteger(L, V_MAGICSHIELD);
	lua_setfield(L, -2, "V_MAGICSHIELD");
	lua_pushinteger(L, V_FLASH);
	lua_setfield(L, -2, "V_FLASH");
	lua_pushinteger(L, V_FIREBALL);
	lua_setfield(L, -2, "V_FIREBALL");
	lua_pushinteger(L, V_REGENERATE);
	lua_setfield(L, -2, "V_REGENERATE");
	lua_pushinteger(L, V_MEDITATE);
	lua_setfield(L, -2, "V_MEDITATE");
	lua_pushinteger(L, V_IMMUNITY);
	lua_setfield(L, -2, "V_IMMUNITY");

	// Other skills
	lua_pushinteger(L, V_DEMON);
	lua_setfield(L, -2, "V_DEMON");
	lua_pushinteger(L, V_DURATION);
	lua_setfield(L, -2, "V_DURATION");
	lua_pushinteger(L, V_RAGE);
	lua_setfield(L, -2, "V_RAGE");
	lua_pushinteger(L, V_COLD);
	lua_setfield(L, -2, "V_COLD");
	lua_pushinteger(L, V_PROFESSION);
	lua_setfield(L, -2, "V_PROFESSION");

	// DOT indices for UI positioning
	lua_pushinteger(L, DOT_TL);
	lua_setfield(L, -2, "DOT_TL");
	lua_pushinteger(L, DOT_BR);
	lua_setfield(L, -2, "DOT_BR");
	lua_pushinteger(L, DOT_INV);
	lua_setfield(L, -2, "DOT_INV");
	lua_pushinteger(L, DOT_SKL);
	lua_setfield(L, -2, "DOT_SKL");
	lua_pushinteger(L, DOT_TXT);
	lua_setfield(L, -2, "DOT_TXT");
	lua_pushinteger(L, DOT_MCT);
	lua_setfield(L, -2, "DOT_MCT");
	lua_pushinteger(L, DOT_TOP);
	lua_setfield(L, -2, "DOT_TOP");
	lua_pushinteger(L, DOT_BOT);
	lua_setfield(L, -2, "DOT_BOT");

	// Map constants
	lua_pushinteger(L, MAPDX);
	lua_setfield(L, -2, "MAPDX");
	lua_pushinteger(L, MAPDY);
	lua_setfield(L, -2, "MAPDY");
	lua_pushinteger(L, DIST);
	lua_setfield(L, -2, "DIST");

	// Other constants
	lua_pushinteger(L, MAXCHARS);
	lua_setfield(L, -2, "MAXCHARS");
	lua_pushinteger(L, INVENTORYSIZE);
	lua_setfield(L, -2, "INVENTORYSIZE");
	lua_pushinteger(L, CONTAINERSIZE);
	lua_setfield(L, -2, "CONTAINERSIZE");
	lua_pushinteger(L, TICKS);
	lua_setfield(L, -2, "TICKS");
	lua_pushinteger(L, V_MAX);
	lua_setfield(L, -2, "V_MAX");
	lua_pushinteger(L, MAXQUEST);
	lua_setfield(L, -2, "MAXQUEST");
	lua_pushinteger(L, MAXMN);
	lua_setfield(L, -2, "MAXMN");

	// Quest flags
	lua_pushinteger(L, QF_OPEN);
	lua_setfield(L, -2, "QF_OPEN");
	lua_pushinteger(L, QF_DONE);
	lua_setfield(L, -2, "QF_DONE");

	// Speed states (for pspeed) - 0=normal, 1=fast, 2=stealth
	lua_pushinteger(L, 0);
	lua_setfield(L, -2, "SPEED_NORMAL");
	lua_pushinteger(L, 1);
	lua_setfield(L, -2, "SPEED_FAST");
	lua_pushinteger(L, 2);
	lua_setfield(L, -2, "SPEED_STEALTH");

	lua_setglobal(L, "C");

	// Pre-define common colors
	lua_newtable(L);
	lua_pushinteger(L, whitecolor);
	lua_setfield(L, -2, "white");
	lua_pushinteger(L, redcolor);
	lua_setfield(L, -2, "red");
	lua_pushinteger(L, greencolor);
	lua_setfield(L, -2, "green");
	lua_pushinteger(L, bluecolor);
	lua_setfield(L, -2, "blue");
	lua_pushinteger(L, textcolor);
	lua_setfield(L, -2, "text");
	lua_pushinteger(L, healthcolor);
	lua_setfield(L, -2, "health");
	lua_pushinteger(L, manacolor);
	lua_setfield(L, -2, "mana");
	lua_setglobal(L, "colors");

	note("Lua API registered");
}
