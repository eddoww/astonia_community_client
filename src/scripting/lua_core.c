/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Lua Scripting Core
 *
 * Manages LuaJIT VM lifecycle, script loading, and sandboxed execution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "astonia.h"
#include "scripting/lua_interface.h"

// Forward declarations for API registration
void lua_api_register(lua_State *L);

// Global Lua state
static lua_State *L = NULL;

// Mods directory (at game root level, not in bin/)
static const char *MODS_DIR = "mods";

// Version string for loaded mods
static char lua_version_str[256] = "LuaJIT Scripting";

// Track loaded scripts for hot-reload
#define MAX_SCRIPTS 128
static struct {
	char path[512];
	time_t mtime;
} loaded_scripts[MAX_SCRIPTS];
static int loaded_script_count = 0;

// Track loaded mod names
#define MAX_MODS 32
static char loaded_mod_names[MAX_MODS][64];
static int loaded_mod_count = 0;

// Sandbox configuration - functions to remove from global environment
static const char *unsafe_functions[] = {
    "dofile",
    "loadfile",
    "load",
    "loadstring",
    NULL
};

// Unsafe packages to remove
static const char *unsafe_packages[] = {
    "io",
    "os",
    "debug",
    "package",
    NULL
};

// Safe os functions to keep (time-related only)
static void setup_safe_os(lua_State *L)
{
	// Create a new limited os table
	lua_newtable(L);

	// Get the original os table
	lua_getglobal(L, "os");
	if (lua_istable(L, -1)) {
		// Copy only safe functions
		lua_getfield(L, -1, "time");
		lua_setfield(L, -3, "time");

		lua_getfield(L, -1, "date");
		lua_setfield(L, -3, "date");

		lua_getfield(L, -1, "difftime");
		lua_setfield(L, -3, "difftime");

		lua_getfield(L, -1, "clock");
		lua_setfield(L, -3, "clock");
	}
	lua_pop(L, 1); // pop original os

	// Set the new limited os table
	lua_setglobal(L, "os");
}

// Apply sandboxing to the Lua environment
static void apply_sandbox(lua_State *L)
{
	// Remove unsafe global functions
	for (int i = 0; unsafe_functions[i] != NULL; i++) {
		lua_pushnil(L);
		lua_setglobal(L, unsafe_functions[i]);
	}

	// Remove unsafe packages (except os which we handle specially)
	for (int i = 0; unsafe_packages[i] != NULL; i++) {
		if (strcmp(unsafe_packages[i], "os") != 0) {
			lua_pushnil(L);
			lua_setglobal(L, unsafe_packages[i]);
		}
	}

	// Setup restricted os table
	setup_safe_os(L);

	// Remove rawset/rawget that could bypass metatables
	lua_pushnil(L);
	lua_setglobal(L, "rawset");
	lua_pushnil(L);
	lua_setglobal(L, "rawget");
	lua_pushnil(L);
	lua_setglobal(L, "rawequal");
	lua_pushnil(L);
	lua_setglobal(L, "rawlen");

	note("Lua sandbox applied");
}

// Get modification time of a file
static time_t get_file_mtime(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0) {
		return st.st_mtime;
	}
	return 0;
}

// Check if a file has .lua extension
static int is_lua_file(const char *filename)
{
	size_t len = strlen(filename);
	if (len > 4 && strcmp(filename + len - 4, ".lua") == 0) {
		return 1;
	}
	return 0;
}

// Load a single Lua script
static int load_script(const char *path)
{
	int result = luaL_dofile(L, path);
	if (result != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		fail("Lua error loading %s: %s", path, error ? error : "unknown error");
		lua_pop(L, 1);
		return 0;
	}

	// Track the script for hot-reload
	if (loaded_script_count < MAX_SCRIPTS) {
		strncpy(loaded_scripts[loaded_script_count].path, path, sizeof(loaded_scripts[0].path) - 1);
		loaded_scripts[loaded_script_count].path[sizeof(loaded_scripts[0].path) - 1] = '\0';
		loaded_scripts[loaded_script_count].mtime = get_file_mtime(path);
		loaded_script_count++;
	}

	note("Loaded Lua script: %s", path);
	return 1;
}

// Load all Lua files from a single mod directory
static int load_mod_scripts(const char *mod_path, const char *mod_name)
{
	DIR *dir;
	struct dirent *entry;
	int count = 0;

	dir = opendir(mod_path);
	if (!dir) {
		return 0;
	}

	// First, look for and load init.lua if it exists
	char init_path[512];
	snprintf(init_path, sizeof(init_path), "%s/init.lua", mod_path);
	FILE *init_file = fopen(init_path, "r");
	if (init_file) {
		fclose(init_file);
		if (load_script(init_path)) {
			count++;
		}
	}

	// Load all other .lua files (except init.lua)
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_REG && is_lua_file(entry->d_name)) {
			// Skip init.lua, we already loaded it
			if (strcmp(entry->d_name, "init.lua") == 0) {
				continue;
			}

			char full_path[512];
			snprintf(full_path, sizeof(full_path), "%s/%s", mod_path, entry->d_name);

			if (load_script(full_path)) {
				count++;
			}
		}
	}

	closedir(dir);

	// Track the mod name
	if (count > 0 && loaded_mod_count < MAX_MODS) {
		strncpy(loaded_mod_names[loaded_mod_count], mod_name, sizeof(loaded_mod_names[0]) - 1);
		loaded_mod_names[loaded_mod_count][sizeof(loaded_mod_names[0]) - 1] = '\0';
		loaded_mod_count++;
	}

	return count;
}

// Load all mods from the mods directory (mods/MODNAME/*.lua)
static int load_all_mods(void)
{
	char mods_path[512];
	DIR *dir;
	struct dirent *entry;
	int total_scripts = 0;
	int mod_count = 0;

	// Reset tracking
	loaded_script_count = 0;
	loaded_mod_count = 0;

	// Try to find mods directory - first at game root level
	snprintf(mods_path, sizeof(mods_path), "%s", MODS_DIR);
	dir = opendir(mods_path);

	if (!dir) {
		// Try relative to bin/ (when running from bin/)
		snprintf(mods_path, sizeof(mods_path), "../%s", MODS_DIR);
		dir = opendir(mods_path);
	}

	if (!dir) {
		note("Mods directory '%s' not found, no Lua mods will be loaded", MODS_DIR);
		return 0;
	}

	// Iterate through subdirectories (each is a mod)
	while ((entry = readdir(dir)) != NULL) {
		// Skip . and ..
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		// Check if it's a directory
		if (entry->d_type == DT_DIR) {
			char mod_path[512];
			snprintf(mod_path, sizeof(mod_path), "%s/%s", mods_path, entry->d_name);

			int scripts_loaded = load_mod_scripts(mod_path, entry->d_name);
			if (scripts_loaded > 0) {
				note("Loaded mod '%s' (%d scripts)", entry->d_name, scripts_loaded);
				total_scripts += scripts_loaded;
				mod_count++;
			}
		}
	}

	closedir(dir);

	// Update version string with loaded mod names
	if (loaded_mod_count > 0) {
		char mod_list[200] = "";
		for (int i = 0; i < loaded_mod_count && i < 5; i++) {
			if (i > 0) {
				strncat(mod_list, ", ", sizeof(mod_list) - strlen(mod_list) - 1);
			}
			strncat(mod_list, loaded_mod_names[i], sizeof(mod_list) - strlen(mod_list) - 1);
		}
		if (loaded_mod_count > 5) {
			strncat(mod_list, "...", sizeof(mod_list) - strlen(mod_list) - 1);
		}
		snprintf(lua_version_str, sizeof(lua_version_str), "LuaJIT Mods: %s", mod_list);
	} else {
		snprintf(lua_version_str, sizeof(lua_version_str), "LuaJIT (no mods loaded)");
	}

	note("Loaded %d mods with %d total scripts", mod_count, total_scripts);
	return total_scripts;
}

// Call a Lua event handler function if it exists
static int call_lua_handler(const char *name)
{
	if (!L) {
		return 0;
	}

	lua_getglobal(L, name);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		warn("Lua error in %s: %s", name, error ? error : "unknown");
		lua_pop(L, 1);
		return 0;
	}

	return 1;
}

// Call a Lua event handler with integer arguments, returning an integer result
static int call_lua_handler_int(const char *name, int nargs, ...)
{
	if (!L) {
		return 0;
	}

	lua_getglobal(L, name);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	// Push arguments
	va_list args;
	va_start(args, nargs);
	for (int i = 0; i < nargs; i++) {
		lua_pushinteger(L, va_arg(args, int));
	}
	va_end(args);

	if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		warn("Lua error in %s: %s", name, error ? error : "unknown");
		lua_pop(L, 1);
		return 0;
	}

	int result = 0;
	if (lua_isnumber(L, -1)) {
		result = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);

	return result;
}

// Call a Lua event handler with string argument, returning an integer result
static int call_lua_handler_str(const char *name, const char *str_arg)
{
	if (!L) {
		return 0;
	}

	lua_getglobal(L, name);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	lua_pushstring(L, str_arg);

	if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		warn("Lua error in %s: %s", name, error ? error : "unknown");
		lua_pop(L, 1);
		return 0;
	}

	int result = 0;
	if (lua_isnumber(L, -1)) {
		result = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);

	return result;
}

bool lua_scripting_init(void)
{
	note("Initializing Lua scripting subsystem...");

	// Create new Lua state with standard libraries
	L = luaL_newstate();
	if (!L) {
		fail("Failed to create Lua state");
		return false;
	}

	// Open standard libraries (we'll sandbox them next)
	luaL_openlibs(L);

	// Apply sandboxing
	apply_sandbox(L);

	// Register client API functions
	lua_api_register(L);

	// Load all mods
	load_all_mods();

	// Call initialization handler
	call_lua_handler("on_init");

	note("Lua scripting initialized");
	return true;
}

void lua_scripting_exit(void)
{
	if (!L) {
		return;
	}

	note("Shutting down Lua scripting...");

	// Call exit handler
	call_lua_handler("on_exit");

	// Close Lua state
	lua_close(L);
	L = NULL;

	loaded_script_count = 0;
	note("Lua scripting shutdown complete");
}

// Clear all known callback globals before reload
static void clear_callback_globals(void)
{
	const char *callbacks[] = {
	    "on_init",       "on_exit",        "on_gamestart",   "on_tick",         "on_frame",
	    "on_mouse_move", "on_mouse_click", "on_keydown",     "on_keyup",        "on_client_cmd",
	    "on_areachange", "on_before_reload", "on_after_reload", NULL};

	for (int i = 0; callbacks[i] != NULL; i++) {
		lua_pushnil(L);
		lua_setglobal(L, callbacks[i]);
	}
}

bool lua_scripting_reload(void)
{
	if (!L) {
		return lua_scripting_init();
	}

	note("Reloading Lua scripts...");

	// Call pre-reload handler
	call_lua_handler("on_before_reload");

	// Clear all mod state and callbacks
	lua_pushnil(L);
	lua_setglobal(L, "MOD");
	clear_callback_globals();

	// Re-register API (in case it was modified)
	lua_api_register(L);

	// Reload all mods
	load_all_mods();

	// Call initialization handler (scripts were freshly loaded)
	call_lua_handler("on_init");

	// Call post-reload handler
	call_lua_handler("on_after_reload");

	addline("Lua scripts reloaded");
	return true;
}

void lua_scripting_gamestart(void)
{
	call_lua_handler("on_gamestart");
}

void lua_scripting_tick(void)
{
	call_lua_handler("on_tick");
}

void lua_scripting_frame(void)
{
	call_lua_handler("on_frame");
}

void lua_scripting_mouse_move(int x, int y)
{
	call_lua_handler_int("on_mouse_move", 2, x, y);
}

int lua_scripting_mouse_click(int x, int y, int what)
{
	return call_lua_handler_int("on_mouse_click", 3, x, y, what);
}

int lua_scripting_keydown(int key)
{
	return call_lua_handler_int("on_keydown", 1, key);
}

int lua_scripting_keyup(int key)
{
	return call_lua_handler_int("on_keyup", 1, key);
}

int lua_scripting_client_cmd(const char *buf)
{
	if (!L) {
		// Lua not initialized
		return 0;
	}

	// Special command to reload scripts (allow trailing whitespace)
	if (strncmp(buf, "#lua_reload", 11) == 0 &&
	    (buf[11] == '\0' || buf[11] == ' ' || buf[11] == '\t')) {
		lua_scripting_reload();
		return 1;
	}

	return call_lua_handler_str("on_client_cmd", buf);
}

void lua_scripting_areachange(void)
{
	call_lua_handler("on_areachange");
}

const char *lua_scripting_version(void)
{
	return lua_version_str;
}

// Check for hot-reload (can be called periodically)
void lua_scripting_check_reload(void)
{
	if (!L) {
		return;
	}

	for (int i = 0; i < loaded_script_count; i++) {
		time_t current_mtime = get_file_mtime(loaded_scripts[i].path);
		if (current_mtime > loaded_scripts[i].mtime) {
			note("Detected change in %s, reloading...", loaded_scripts[i].path);
			lua_scripting_reload();
			return;
		}
	}
}
