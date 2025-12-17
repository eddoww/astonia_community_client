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

#include "astonia.h"
#include "client/client.h"
#include "game/game.h"
#include "gui/gui.h"

// External declarations from gui module (not exported in headers)
extern int mousex, mousey;

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

// Get character stat value
static int l_get_value(lua_State *L)
{
	int type = (int)luaL_checkinteger(L, 1); // 0 = base, 1 = current
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

	// Return nil if player slot is empty
	if (p->csprite == 0) {
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

// --- Registration ---

static const luaL_Reg client_funcs[] = {
    // Logging
    {"note", l_note},
    {"warn", l_warn},
    {"addline", l_addline},

    // Rendering
    {"render_text", l_render_text},
    {"render_rect", l_render_rect},
    {"render_line", l_render_line},
    {"render_pixel", l_render_pixel},
    {"render_sprite", l_render_sprite},
    {"render_text_length", l_render_text_length},

    // Colors
    {"rgb", l_rgb},

    // Game data
    {"get_hp", l_get_hp},
    {"get_mana", l_get_mana},
    {"get_rage", l_get_rage},
    {"get_endurance", l_get_endurance},
    {"get_lifeshield", l_get_lifeshield},
    {"get_experience", l_get_experience},
    {"get_gold", l_get_gold},
    {"get_tick", l_get_tick},
    {"get_username", l_get_username},
    {"get_origin", l_get_origin},
    {"get_mouse", l_get_mouse},
    {"get_value", l_get_value},
    {"get_item", l_get_item},
    {"get_item_flags", l_get_item_flags},
    {"get_map_tile", l_get_map_tile},
    {"get_player", l_get_player},

    // GUI helpers
    {"dotx", l_dotx},
    {"doty", l_doty},
    {"butx", l_butx},
    {"buty", l_buty},

    // Utilities
    {"exp2level", l_exp2level},
    {"level2exp", l_level2exp},

    // Commands
    {"cmd_text", l_cmd_text},

    {NULL, NULL}
};

void lua_api_register(lua_State *L)
{
	// Create the 'client' table for API functions
	lua_newtable(L);
	luaL_setfuncs(L, client_funcs, 0);
	lua_setglobal(L, "client");

	// Create constants table
	lua_newtable(L);

	// Stat value indices
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
	lua_pushinteger(L, TICKS);
	lua_setfield(L, -2, "TICKS");

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
