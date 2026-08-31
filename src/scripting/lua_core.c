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
#include <luajit.h>

#include <SDL3/SDL.h>

#include "astonia.h"
#include "scripting/lua_interface.h"

// Forward declarations for API registration
void lua_api_register(lua_State *L);

// Global Lua state
static lua_State *L = NULL;

// Developer mode (-dev command line flag): enables mtime-based auto-reload
static bool dev_mode = false;

// Mods subdirectory name (within SDL user data path)
static const char *MODS_SUBDIR = "mods";

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

// Current mod path for safe require (set during mod loading)
static char current_mod_path[512] = "";

// Instruction limit for script execution (prevents infinite loops)
// ~10 million instructions is roughly 1-2 seconds of execution
#define LUA_MAX_INSTRUCTIONS 10000000

// Per-state memory ceiling for Lua mods. Exceeding it makes allocations fail,
// which surfaces in the offending script as a "not enough memory" error caught
// by the surrounding pcall - the client itself is unaffected.
#define LUA_MEMORY_LIMIT (64u * 1024u * 1024u) // 64MB

// Bytes currently allocated by the Lua state (maintained by l_limited_alloc)
static size_t lua_mem_used = 0;

// Whether the hard allocator-level cap is active (lua_newstate with a custom
// allocator requires a GC64 LuaJIT build on 64-bit; on older builds we fall
// back to polling the GC count from lua_scripting_tick)
static bool lua_mem_hard_cap = false;

// Memory-capped allocator handed to lua_newstate. LuaJIT guarantees osize==0
// when ptr==NULL, so the accounting below stays exact.
static void *l_limited_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	(void)ud;

	if (nsize == 0) {
		lua_mem_used -= osize;
		free(ptr);
		return NULL;
	}

	if (lua_mem_used - osize + nsize > LUA_MEMORY_LIMIT) {
		return NULL; // over the cap: allocation fails, Lua raises LUA_ERRMEM
	}

	void *nptr = realloc(ptr, nsize);
	if (nptr) {
		lua_mem_used = lua_mem_used - osize + nsize;
	}
	return nptr;
}

static void instruction_limit_hook(lua_State *L, lua_Debug *ar)
{
	(void)ar;
	luaL_error(L, "script exceeded instruction limit (possible infinite loop)");
}

// Depth-counted so nested Lua entry (e.g. client.cmd_text from a callback
// re-entering on_client_cmd) cannot switch the watchdog off for the
// remainder of the outer callback when the inner one returns.
static int instruction_limit_depth = 0;

static void enable_instruction_limit(lua_State *L)
{
	if (++instruction_limit_depth == 1) {
		lua_sethook(L, instruction_limit_hook, LUA_MASKCOUNT, LUA_MAX_INSTRUCTIONS);
	}
}

static void disable_instruction_limit(lua_State *L)
{
	if (instruction_limit_depth > 0 && --instruction_limit_depth == 0) {
		lua_sethook(L, NULL, 0, 0);
	}
}

// Safe require function - loads modules only from the current mod directory
static int l_safe_require(lua_State *L)
{
	const char *modname = luaL_checkstring(L, 1);

	// Security: check for path traversal attempts
	if (strstr(modname, "..") != NULL) {
		return luaL_error(L, "require: path traversal not allowed (..)");
	}

	// Block absolute paths
	if (modname[0] == '/' || modname[0] == '\\') {
		return luaL_error(L, "require: absolute paths not allowed");
	}

	// Block backslashes (Windows path separators)
	if (strchr(modname, '\\') != NULL) {
		return luaL_error(L, "require: backslashes not allowed in module names");
	}

	// Block empty module names
	if (modname[0] == '\0') {
		return luaL_error(L, "require: empty module name");
	}

	// Check if we have a current mod path set
	if (current_mod_path[0] == '\0') {
		return luaL_error(L, "require: no mod context (require can only be used within a mod)");
	}

	// Build the full path
	char full_path[512];
	int written = snprintf(full_path, sizeof(full_path), "%s/%s.lua", current_mod_path, modname);
	if (written >= (int)sizeof(full_path)) {
		return luaL_error(L, "require: path too long");
	}

	// Get or create the _LOADED table
	lua_getglobal(L, "_LOADED");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setglobal(L, "_LOADED");
	}

	// Check if module is already loaded
	lua_getfield(L, -1, full_path);
	if (!lua_isnil(L, -1)) {
		// Already loaded, return cached value
		lua_remove(L, -2); // remove _LOADED table
		return 1;
	}
	lua_pop(L, 1); // pop nil

	// Load the file
	if (luaL_loadfile(L, full_path) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		lua_pop(L, 2); // pop error and _LOADED
		return luaL_error(L, "require '%s': %s", modname, error ? error : "failed to load");
	}

	// Execute with instruction limit
	enable_instruction_limit(L);
	int status = lua_pcall(L, 0, 1, 0);
	disable_instruction_limit(L);

	if (status != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		lua_pop(L, 2); // pop error and _LOADED
		return luaL_error(L, "require '%s': %s", modname, error ? error : "execution failed");
	}

	// If nil was returned, use true as the cached value (standard Lua behavior)
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 1);
	}

	// Cache the result: _LOADED[full_path] = result
	lua_pushvalue(L, -1); // duplicate result for caching
	lua_setfield(L, -3, full_path); // _LOADED[full_path] = result
	lua_remove(L, -2); // remove _LOADED table

	return 1; // return the result
}

// Sandbox configuration - functions to remove from global environment
static const char *unsafe_functions[] = {"dofile", "loadfile", "load", "loadstring", NULL};

// Unsafe packages to remove. "jit" must go too: leaving it in would let a
// mod call jit.on() and re-enable trace compilation, defeating the
// interpreter-only enforcement that makes the instruction watchdog reliable.
static const char *unsafe_packages[] = {"io", "os", "debug", "package", "jit", NULL};

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

	// Remove metatable access
	lua_pushnil(L);
	lua_setglobal(L, "getmetatable");
	lua_pushnil(L);
	lua_setglobal(L, "setmetatable");

	// Remove environment manipulation (Lua 5.1/LuaJIT)
	lua_pushnil(L);
	lua_setglobal(L, "getfenv");
	lua_pushnil(L);
	lua_setglobal(L, "setfenv");

	// Remove other dangerous functions
	lua_pushnil(L);
	lua_setglobal(L, "collectgarbage");
	lua_pushnil(L);
	lua_setglobal(L, "newproxy");

	// Neuter string.dump
	lua_getglobal(L, "string");
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		lua_setfield(L, -2, "dump");
	}
	lua_pop(L, 1);

	// Initialize _LOADED table for module caching
	lua_newtable(L);
	lua_setglobal(L, "_LOADED");

	// Register safe require function
	lua_pushcfunction(L, l_safe_require);
	lua_setglobal(L, "require");

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
// Top-level code runs under the instruction watchdog too, so a hostile
// `while true do end` at file scope cannot hang the client during load.
static int load_script(const char *path)
{
	int result = luaL_loadfile(L, path);
	if (result == LUA_OK) {
		enable_instruction_limit(L);
		result = lua_pcall(L, 0, 0, 0);
		disable_instruction_limit(L);
	}
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

	// Set current mod path for safe require
	strncpy(current_mod_path, mod_path, sizeof(current_mod_path) - 1);
	current_mod_path[sizeof(current_mod_path) - 1] = '\0';

	// First, look for and load init.lua if it exists
	char init_path[512];
	int written = snprintf(init_path, sizeof(init_path), "%s/init.lua", mod_path);
	if (written < (int)sizeof(init_path)) {
		FILE *init_file = fopen(init_path, "r");
		if (init_file) {
			fclose(init_file);
			if (load_script(init_path)) {
				count++;
			}
		}
	}

	// Load all other .lua files (except init.lua)
	while ((entry = readdir(dir)) != NULL) {
		// Skip init.lua, we already loaded it
		if (strcmp(entry->d_name, "init.lua") == 0) {
			continue;
		}

		if (!is_lua_file(entry->d_name)) {
			continue;
		}

		char full_path[512];
		written = snprintf(full_path, sizeof(full_path), "%s/%s", mod_path, entry->d_name);
		if (written >= (int)sizeof(full_path)) {
			warn("Path too long, skipping: %s/%s", mod_path, entry->d_name);
			continue;
		}

		// stat() instead of d_type: mingw-w64's dirent has no d_type, and
		// some filesystems (XFS, NFS) report DT_UNKNOWN anyway. The scan
		// runs once per (re)load, so the extra syscall is irrelevant.
		struct stat st;
		if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode)) {
			continue;
		}

		if (load_script(full_path)) {
			count++;
		}
	}

	closedir(dir);

	// Clear current mod path (require only works during mod loading)
	current_mod_path[0] = '\0';

	// Track the mod name (truncation of very long directory names is fine)
	if (count > 0 && loaded_mod_count < MAX_MODS) {
		snprintf(loaded_mod_names[loaded_mod_count], sizeof(loaded_mod_names[0]), "%.*s",
		    (int)sizeof(loaded_mod_names[0]) - 1, mod_name);
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

	// Get SDL user data path (e.g., ~/.local/share/Astonia/mods/ on Linux)
	char *pref_path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
	if (pref_path) {
		snprintf(mods_path, sizeof(mods_path), "%s%s", pref_path, MODS_SUBDIR);
		SDL_free(pref_path);
		dir = opendir(mods_path);
	} else {
		// Fallback to relative path if SDL_GetPrefPath fails
		snprintf(mods_path, sizeof(mods_path), "%s", MODS_SUBDIR);
		dir = opendir(mods_path);
	}

	if (!dir) {
		note("Mods directory '%s' not found, no Lua mods will be loaded", mods_path);
		return 0;
	}

	// Iterate through subdirectories (each is a mod)
	while ((entry = readdir(dir)) != NULL) {
		// Skip . and ..
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		char mod_path[512];
		int written = snprintf(mod_path, sizeof(mod_path), "%s/%s", mods_path, entry->d_name);
		if (written >= (int)sizeof(mod_path)) {
			warn("Mod path too long, skipping: %s/%s", mods_path, entry->d_name);
			continue;
		}

		// stat() instead of d_type - see load_mod_scripts for rationale
		struct stat st;
		if (stat(mod_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}

		int scripts_loaded = load_mod_scripts(mod_path, entry->d_name);
		if (scripts_loaded > 0) {
			note("Loaded mod '%s' (%d scripts)", entry->d_name, scripts_loaded);
			total_scripts += scripts_loaded;
			mod_count++;
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

// Call all registered callbacks for an event (no arguments, no return)
static int call_lua_handler(const char *name)
{
	if (!L) {
		return 0;
	}

	// Get _callbacks table
	lua_getglobal(L, "_callbacks");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	// Get the callback array for this event
	lua_getfield(L, -1, name);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 2); // pop nil and _callbacks
		return 0;
	}

	// Iterate through all registered callbacks
	int len = (int)lua_objlen(L, -1);
	int called = 0;
	for (int i = 1; i <= len; i++) {
		lua_rawgeti(L, -1, i);
		if (lua_isfunction(L, -1)) {
			enable_instruction_limit(L);
			int status = lua_pcall(L, 0, 0, 0);
			disable_instruction_limit(L);

			if (status != LUA_OK) {
				const char *error = lua_tostring(L, -1);
				warn("Lua error in %s callback %d: %s", name, i, error ? error : "unknown");
				lua_pop(L, 1);
			} else {
				called++;
			}
		} else {
			lua_pop(L, 1);
		}
	}

	lua_pop(L, 2); // pop callback array and _callbacks
	return called;
}

// Call all registered callbacks with integer arguments, returning combined result
// If any callback returns non-zero, that value is returned (first non-zero wins)
static int call_lua_handler_int(const char *name, int nargs, ...)
{
	if (!L) {
		return 0;
	}

	// Collect arguments first (va_list can only be traversed once)
	int arg_values[8]; // Max 8 args should be enough
	va_list args;
	va_start(args, nargs);
	for (int i = 0; i < nargs && i < 8; i++) {
		arg_values[i] = va_arg(args, int);
	}
	va_end(args);

	// Get _callbacks table
	lua_getglobal(L, "_callbacks");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	// Get the callback array for this event
	lua_getfield(L, -1, name);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 2);
		return 0;
	}

	// Iterate through all registered callbacks
	int len = (int)lua_objlen(L, -1);
	int result = 0;
	for (int i = 1; i <= len; i++) {
		lua_rawgeti(L, -1, i);
		if (lua_isfunction(L, -1)) {
			// Push arguments
			for (int j = 0; j < nargs && j < 8; j++) {
				lua_pushinteger(L, arg_values[j]);
			}

			enable_instruction_limit(L);
			int status = lua_pcall(L, nargs, 1, 0);
			disable_instruction_limit(L);

			if (status != LUA_OK) {
				const char *error = lua_tostring(L, -1);
				warn("Lua error in %s callback %d: %s", name, i, error ? error : "unknown");
				lua_pop(L, 1);
			} else {
				if (lua_isnumber(L, -1)) {
					int r = (int)lua_tointeger(L, -1);
					if (r != 0 && result == 0) {
						result = r; // First non-zero result wins
					}
				}
				lua_pop(L, 1);
			}
		} else {
			lua_pop(L, 1);
		}
	}

	lua_pop(L, 2); // pop callback array and _callbacks
	return result;
}

// Call all registered callbacks with string argument, returning combined result
static int call_lua_handler_str(const char *name, const char *str_arg)
{
	if (!L) {
		return 0;
	}

	// Get _callbacks table
	lua_getglobal(L, "_callbacks");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}

	// Get the callback array for this event
	lua_getfield(L, -1, name);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 2);
		return 0;
	}

	// Iterate through all registered callbacks
	int len = (int)lua_objlen(L, -1);
	int result = 0;
	for (int i = 1; i <= len; i++) {
		lua_rawgeti(L, -1, i);
		if (lua_isfunction(L, -1)) {
			lua_pushstring(L, str_arg);

			enable_instruction_limit(L);
			int status = lua_pcall(L, 1, 1, 0);
			disable_instruction_limit(L);

			if (status != LUA_OK) {
				const char *error = lua_tostring(L, -1);
				warn("Lua error in %s callback %d: %s", name, i, error ? error : "unknown");
				lua_pop(L, 1);
			} else {
				if (lua_isnumber(L, -1)) {
					int r = (int)lua_tointeger(L, -1);
					if (r != 0 && result == 0) {
						result = r;
					}
				}
				lua_pop(L, 1);
			}
		} else {
			lua_pop(L, 1);
		}
	}

	lua_pop(L, 2); // pop callback array and _callbacks
	return result;
}

// List of callback event names
static const char *callback_names[] = {"on_init", "on_exit", "on_gamestart", "on_tick", "on_frame", "on_mouse_move",
    "on_mouse_over", "on_mouse_click", "on_keydown", "on_keyup", "on_client_cmd", "on_areachange", "on_before_reload",
    "on_after_reload", NULL};

// Set up the callback registration system in Lua
static void setup_callback_system(void)
{
	// Create _callbacks table with empty arrays for each event type
	lua_newtable(L);
	for (int i = 0; callback_names[i] != NULL; i++) {
		lua_newtable(L);
		lua_setfield(L, -2, callback_names[i]);
	}
	lua_setglobal(L, "_callbacks");

	// Create the register() function in Lua
	const char *register_func = "function register(event_name, callback)\n"
	                            "    if type(callback) ~= 'function' then\n"
	                            "        client.warn('register: callback must be a function')\n"
	                            "        return false\n"
	                            "    end\n"
	                            "    if not _callbacks[event_name] then\n"
	                            "        client.warn('register: unknown event: ' .. tostring(event_name))\n"
	                            "        return false\n"
	                            "    end\n"
	                            "    table.insert(_callbacks[event_name], callback)\n"
	                            "    return true\n"
	                            "end\n";

	if (luaL_dostring(L, register_func) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		warn("Failed to create register function: %s", error ? error : "unknown");
		lua_pop(L, 1);
	}

	// Create the unregister() function in Lua
	const char *unregister_func = "function unregister(event_name, callback)\n"
	                              "    if not _callbacks[event_name] then\n"
	                              "        return false\n"
	                              "    end\n"
	                              "    for i = #_callbacks[event_name], 1, -1 do\n"
	                              "        if _callbacks[event_name][i] == callback then\n"
	                              "            table.remove(_callbacks[event_name], i)\n"
	                              "            return true\n"
	                              "        end\n"
	                              "    end\n"
	                              "    return false\n"
	                              "end\n";

	if (luaL_dostring(L, unregister_func) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		warn("Failed to create unregister function: %s", error ? error : "unknown");
		lua_pop(L, 1);
	}
}

// Clear all registered callbacks (for reload)
static void clear_callbacks(void)
{
	// Reset _callbacks table with empty arrays
	lua_newtable(L);
	for (int i = 0; callback_names[i] != NULL; i++) {
		lua_newtable(L);
		lua_setfield(L, -2, callback_names[i]);
	}
	lua_setglobal(L, "_callbacks");
}

bool lua_scripting_init(void)
{
	note("Initializing Lua scripting subsystem...");

	// Create new Lua state with a memory-capped allocator. On 64-bit this
	// requires a GC64 LuaJIT build (the default since LuaJIT 2.1); older
	// builds reject custom allocators, so fall back to the internal one and
	// enforce the cap by polling from lua_scripting_tick instead.
	lua_mem_used = 0;
	L = lua_newstate(l_limited_alloc, NULL);
	if (L) {
		lua_mem_hard_cap = true;
	} else {
		lua_mem_hard_cap = false;
		warn("LuaJIT build rejects custom allocators; memory cap enforced by GC polling instead");
		L = luaL_newstate();
	}
	if (!L) {
		fail("Failed to create Lua state");
		return false;
	}

	// Open standard libraries (we'll sandbox them next)
	luaL_openlibs(L);

#ifndef LUA_SCRIPTING_ALLOW_JIT
	// Run mod states interpreter-only. JIT-compiled traces do not honor
	// LUA_MASKCOUNT hooks, so the instruction watchdog would never fire on a
	// hot loop. Interpreter-only trades mod execution speed (typically well
	// under a frame's budget for overlay-style mods) for a watchdog that is
	// guaranteed to trigger. Build with LUA_ALLOW_JIT=1 to opt out.
	//
	// This MUST run *after* luaL_openlibs: opening the jit library
	// re-enables the compiler, so disabling it earlier is silently undone
	// (verified: the watchdog then never fires on an infinite loop).
	if (!luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)) {
		warn("Failed to disable LuaJIT compilation; instruction watchdog may not fire in hot loops");
	} else {
		note("LuaJIT compiler disabled for mod sandbox (interpreter-only)");
	}
#else
	note("LuaJIT compiler ENABLED for mods (LUA_SCRIPTING_ALLOW_JIT): instruction watchdog is best-effort");
#endif

	// Apply sandboxing
	apply_sandbox(L);

	// Register client API functions
	lua_api_register(L);

	// Set up callback registration system
	setup_callback_system();

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

bool lua_scripting_reload(void)
{
	if (!L) {
		return lua_scripting_init();
	}

	note("Reloading Lua scripts...");

	// Call pre-reload handler
	call_lua_handler("on_before_reload");

	// Clear all mod state, callbacks, and loaded modules cache
	lua_pushnil(L);
	lua_setglobal(L, "MOD");
	clear_callbacks();

	// Clear _LOADED cache for fresh module loading
	lua_newtable(L);
	lua_setglobal(L, "_LOADED");

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
	// Fallback memory-cap enforcement for LuaJIT builds without custom
	// allocator support: poll the GC-reported usage, try a full collection
	// first, and shut scripting down if mods still exceed the cap.
	if (L && !lua_mem_hard_cap) {
		size_t used_kb = (size_t)lua_gc(L, LUA_GCCOUNT, 0);
		if (used_kb * 1024u > LUA_MEMORY_LIMIT) {
			lua_gc(L, LUA_GCCOLLECT, 0);
			used_kb = (size_t)lua_gc(L, LUA_GCCOUNT, 0);
			if (used_kb * 1024u > LUA_MEMORY_LIMIT) {
				// %lu instead of %zu: msvcrt's printf does not understand %z
				warn("Lua mods exceed the %uMB memory limit (%luKB in use); disabling Lua scripting",
				    LUA_MEMORY_LIMIT / (1024u * 1024u), (unsigned long)used_kb);
				addline("Lua mods disabled: memory limit exceeded. Use #lua_reload to retry.");
				lua_close(L);
				L = NULL;
				loaded_script_count = 0;
				return;
			}
		}
	}

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

int lua_scripting_mouse_over(int x, int y)
{
	return call_lua_handler_int("on_mouse_over", 2, x, y);
}

int lua_scripting_keydown(uint32_t key)
{
	return call_lua_handler_int("on_keydown", 1, (int)key);
}

int lua_scripting_keyup(uint32_t key)
{
	return call_lua_handler_int("on_keyup", 1, (int)key);
}

int lua_scripting_client_cmd(const char *buf)
{
	if (!L) {
		return 0;
	}

	if (!buf) {
		return 0;
	}

	// Special command to reload scripts (allow trailing whitespace)
	if (strncmp(buf, "#lua_reload", 11) == 0 && (buf[11] == '\0' || buf[11] == ' ' || buf[11] == '\t')) {
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

void lua_scripting_set_dev_mode(bool enabled)
{
	dev_mode = enabled;
	if (enabled) {
		note("Lua developer mode enabled: scripts auto-reload on file change");
	}
}

// Check for hot-reload. Safe to call every tick: it only does work in
// developer mode (-dev) and self-throttles to about one stat() pass per second.
void lua_scripting_check_reload(void)
{
	static uint64_t last_check_ms = 0;

	if (!L || !dev_mode) {
		return;
	}

	uint64_t now = SDL_GetTicks();
	if (now - last_check_ms < 1000) {
		return;
	}
	last_check_ms = now;

	for (int i = 0; i < loaded_script_count; i++) {
		time_t current_mtime = get_file_mtime(loaded_scripts[i].path);
		if (current_mtime > loaded_scripts[i].mtime) {
			note("Detected change in %s, reloading...", loaded_scripts[i].path);
			lua_scripting_reload();
			return;
		}
	}
}
