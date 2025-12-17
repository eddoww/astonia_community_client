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

// Scripts directory relative to bin/
static const char *SCRIPTS_DIR = "scripts";

// Version string for loaded mods
static char lua_version_str[256] = "LuaJIT Scripting";

// Track loaded scripts for hot-reload
#define MAX_SCRIPTS 64
static struct {
	char path[256];
	time_t mtime;
} loaded_scripts[MAX_SCRIPTS];
static int loaded_script_count = 0;

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

// Load all scripts from the scripts directory
static int load_all_scripts(void)
{
	char scripts_path[512];
	DIR *dir;
	struct dirent *entry;
	int count = 0;

	// Reset loaded scripts tracking
	loaded_script_count = 0;

	// Build path to scripts directory
	snprintf(scripts_path, sizeof(scripts_path), "%s", SCRIPTS_DIR);

	dir = opendir(scripts_path);
	if (!dir) {
		// Try with bin/ prefix
		snprintf(scripts_path, sizeof(scripts_path), "bin/%s", SCRIPTS_DIR);
		dir = opendir(scripts_path);
	}

	if (!dir) {
		note("Scripts directory not found, no Lua mods will be loaded");
		return 0;
	}

	// First, look for and load init.lua if it exists
	char init_path[512];
	snprintf(init_path, sizeof(init_path), "%s/init.lua", scripts_path);
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
			snprintf(full_path, sizeof(full_path), "%s/%s", scripts_path, entry->d_name);

			if (load_script(full_path)) {
				count++;
			}
		}
	}

	closedir(dir);

	note("Loaded %d Lua scripts", count);
	return count;
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

	// Load all scripts
	load_all_scripts();

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

bool lua_scripting_reload(void)
{
	if (!L) {
		return lua_scripting_init();
	}

	note("Reloading Lua scripts...");

	// Call pre-reload handler
	call_lua_handler("on_before_reload");

	// We need to preserve the Lua state but reload scripts
	// First, clear any global mod state
	lua_pushnil(L);
	lua_setglobal(L, "MOD");

	// Re-register API (in case it was modified)
	lua_api_register(L);

	// Reload scripts
	loaded_script_count = 0;
	load_all_scripts();

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
	// Special command to reload scripts
	if (strcmp(buf, "#lua_reload") == 0) {
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
