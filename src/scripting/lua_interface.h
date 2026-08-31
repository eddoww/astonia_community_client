/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Lua Scripting Interface
 *
 * Provides LuaJIT-based scripting support for mods with sandboxed execution.
 */

#ifndef LUA_INTERFACE_H
#define LUA_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>

// Initialize the Lua scripting subsystem
// Returns true on success, false on failure
bool lua_scripting_init(void);

// Shutdown the Lua scripting subsystem
void lua_scripting_exit(void);

// Reload all Lua scripts (hot-reload support)
// Returns true on success, false on failure
bool lua_scripting_reload(void);

// Called when the game starts (connected to server)
void lua_scripting_gamestart(void);

// Called every game tick (24 times per second)
void lua_scripting_tick(void);

// Called every display frame
void lua_scripting_frame(void);

// Called on mouse movement
// x, y: screen coordinates
void lua_scripting_mouse_move(int x, int y);

// Called on mouse click
// x, y: screen coordinates
// what: button/action type
// Returns: 1 if event consumed, -1 if consumed but allow other handlers, 0 otherwise
int lua_scripting_mouse_click(int x, int y, int what);

// Called from the client's hover logic to ask whether a Lua overlay is under
// the mouse. Returns non-zero if a mod claims the position (hover consumed).
int lua_scripting_mouse_over(int x, int y);

// Called on key down
// key: SDL key code (SDL_Keycode is unsigned in SDL3)
// Returns: 1 if event consumed, -1 if consumed but allow other handlers, 0 otherwise
int lua_scripting_keydown(uint32_t key);

// Called on key up
// key: SDL key code (SDL_Keycode is unsigned in SDL3)
// Returns: 1 if event consumed, -1 if consumed but allow other handlers, 0 otherwise
int lua_scripting_keyup(uint32_t key);

// Called on client command (text starting with #)
// buf: command text
// Returns: 1 if command handled, 0 otherwise
int lua_scripting_client_cmd(const char *buf);

// Called when area changes (e.g., teleport)
void lua_scripting_areachange(void);

// Get the version string of loaded Lua mods
const char *lua_scripting_version(void);

// Enable/disable developer mode (-dev flag): mtime-based script auto-reload
void lua_scripting_set_dev_mode(bool enabled);

// Check for script file changes and reload if needed (for hot-reload).
// Safe to call every tick: only active in developer mode, self-throttled
// to roughly one filesystem check per second.
void lua_scripting_check_reload(void);

#endif // LUA_INTERFACE_H
