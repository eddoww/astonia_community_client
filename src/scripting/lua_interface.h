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

// Called on key down
// key: SDL key code
// Returns: 1 if event consumed, -1 if consumed but allow other handlers, 0 otherwise
int lua_scripting_keydown(int key);

// Called on key up
// key: SDL key code
// Returns: 1 if event consumed, -1 if consumed but allow other handlers, 0 otherwise
int lua_scripting_keyup(int key);

// Called on client command (text starting with #)
// buf: command text
// Returns: 1 if command handled, 0 otherwise
int lua_scripting_client_cmd(const char *buf);

// Called when area changes (e.g., teleport)
void lua_scripting_areachange(void);

// Get the version string of loaded Lua mods
const char *lua_scripting_version(void);

// Check for script file changes and reload if needed (for hot-reload)
void lua_scripting_check_reload(void);

#endif // LUA_INTERFACE_H
