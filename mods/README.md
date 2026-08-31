# Astonia Client Lua Mods

Place your mods in subdirectories here. Each mod should have its own folder.

## Directory Structure

```
mods/
  your_mod_name/
    init.lua         (loaded first if present)
    other_file.lua
    ...
  another_mod/
    init.lua
```

## Getting Started

1. Create a folder for your mod: `mods/my_mod/`
2. Create `init.lua` in your mod folder
3. Register callbacks using `register("event_name", function() ... end)`
4. Start the game - your mod will be loaded automatically

## Callback Registration

Use the `register()` function to add callbacks. Multiple mods can register callbacks
for the same event - all registered callbacks will be called.

```lua
-- Register an initialization callback
register("on_init", function()
    client.note("My mod initialized!")
end)

-- Register a tick callback
register("on_tick", function()
    -- Called every game tick
end)

-- Register a command handler
register("on_client_cmd", function(cmd)
    if cmd == "#mycommand" then
        client.addline("Command handled!")
        return 1  -- Event consumed
    end
    return 0  -- Event not handled
end)
```

## Hot Reload

Type `#lua_reload` in the chat to reload all mods without restarting the client.

When the client is started with the `-dev` flag, loaded scripts are also watched
for changes (about once per second) and reloaded automatically when a file's
modification time changes - save the file and the mod reloads in-game.

## Available Events

| Event | Description |
|-------|-------------|
| `on_init` | Called when scripts are loaded |
| `on_exit` | Called when client shuts down |
| `on_gamestart` | Called when connected to server |
| `on_tick` | Called every game tick (24/sec) |
| `on_frame` | Called every display frame |
| `on_mouse_move` | Called on mouse movement (x, y) |
| `on_mouse_over` | Return 1 if your overlay is under the mouse (x, y) - consumes hover |
| `on_mouse_click` | Return 1 to consume event (x, y, button) |
| `on_keydown` | Return 1 to consume event (key) |
| `on_keyup` | Return 1 to consume event (key) |
| `on_client_cmd` | Handle # commands, return 1 if handled (cmd) |
| `on_areachange` | Called when area changes |
| `on_before_reload` | Called before hot-reload |
| `on_after_reload` | Called after hot-reload |

## Client API

Access the client API through the `client` global table:

### Logging
- `client.note(message)` - Log to console
- `client.warn(message)` - Log warning to console
- `client.addline(message)` - Add message to chat window

### Game Data
- `client.get_hp()` - Get player health
- `client.get_mana()` - Get player mana
- `client.get_rage()` - Get rage value
- `client.get_endurance()` - Get endurance value
- `client.get_lifeshield()` - Get lifeshield value
- `client.get_experience()` - Get experience points
- `client.get_gold()` - Get gold amount
- `client.get_tick()` - Get current tick number
- `client.get_username()` - Get player name
- `client.get_origin()` - Returns x, y map origin
- `client.get_mouse()` - Returns x, y mouse position
- `client.stom(scrx, scry)` - Screen to map coords, returns mapx, mapy (or nil if outside)
- `client.mtos(mapx, mapy)` - Map to screen coords, returns scrx, scry
- `client.get_world_pos(mapx, mapy)` - Convert local map to world coords (like right-click shows)
- `client.get_plrmn()` - Get player map index
- `client.get_player_world_pos()` - Get player world position (x, y)
- `client.get_value(type, idx)` - Get character stat (type: 0=base, 1=current)
- `client.get_item(slot)` - Get inventory item sprite
- `client.get_item_flags(slot)` - Get inventory item flags
- `client.get_map_tile(x, y)` - Get map tile info (returns table)
- `client.get_player(idx)` - Get player info (returns table)

### Selection & Targeting
- `client.get_chrsel()` - Get selected character map index (or nil)
- `client.get_itmsel()` - Get selected item map index (or nil)
- `client.get_mapsel()` - Get selected map tile index (or nil)
- `client.get_action()` - Get current action {act, x, y}

### Look/Inspect
- `client.get_look_name()` - Get inspected target name
- `client.get_look_desc()` - Get inspected target description
- `client.get_lookinv(slot)` - Get look equipment sprite (0-11)

### Container
- `client.get_con_type()` - Get container type (0=none, 1=container, 2=shop)
- `client.get_con_name()` - Get container name
- `client.get_con_cnt()` - Get container item count
- `client.get_container(slot)` - Get container item sprite

### Player State
- `client.get_pspeed()` - Get speed state (0=normal, 1=fast, 2=stealth)
- `client.get_mil_exp()` - Get military experience
- `client.get_mil_rank([exp])` - Get military rank (optionally from specific exp)

### Skills
- `client.get_skill_name(idx)` - Get skill name
- `client.get_skill_desc(idx)` - Get skill description
- `client.get_skill_info(idx)` - Get skill info {name, base1, base2, base3, cost, start}
- `client.get_raise_cost(skill, level)` - Get cost to raise skill to level

### Quests
- `client.get_quest_count()` - Get number of quest definitions
- `client.get_quest_status(idx)` - Get quest status {done, flags}
- `client.get_quest_info(idx)` - Get quest info {name, minlevel, maxlevel, giver, area, exp, flags}

### Rendering
- `client.render_text(x, y, color, flags, text)` - Draw text, returns width
- `client.render_rect(sx, sy, ex, ey, color)` - Draw filled rectangle
- `client.render_line(fx, fy, tx, ty, color)` - Draw line
- `client.render_pixel(x, y, color)` - Draw single pixel
- `client.render_sprite(sprite, x, y, light, align)` - Draw sprite
- `client.render_text_length(flags, text)` - Get text width
- `client.rgb(r, g, b)` - Create color (0-31 per channel)

### GUI Helpers
- `client.dotx(idx)`, `client.doty(idx)` - Get screen anchor positions
- `client.butx(idx)`, `client.buty(idx)` - Get button positions

Dot and button positions already include panel drag offsets: overlays anchored
to a dot follow the panel when the player moves it.

### Panels & Windows (read-only)
- `client.has_open_window()` - True if any dismissable client window is open
  (look window, teleporter, spellbook, chat input, native mod windows, ...)
- `client.gui_overlay_visible()` - False while the master GUI overlay is hidden
- `client.get_fullscreen_world()` - True in fullscreen-world mode (map spans the
  whole canvas, GUI floats above it)
- `client.panel_shown(p)` - Effective visibility of panel `p` (`C.PANEL_*`)
- `client.panel_offset(p)` - Returns dx, dy drag offset of panel `p` from its
  default position (for overlays doing their own layout math)

### Hotbar (read-only)
- `client.get_hotbar_rows()` - Number of hotbar rows (1-3)
- `client.get_hotbar_visible_slots()` - Visible slots per row
- `client.get_hotbar_slot(idx)` - Slot contents (0-based), nil if empty:
  `{type, sprite, name, inv_index, item_type, action_slot}` where `type` is
  `C.HOTBAR_ITEM` or `C.HOTBAR_SPELL`

### Utilities
- `client.exp2level(exp)` - Convert experience to level
- `client.level2exp(level)` - Convert level to experience needed
- `client.cmd_text(text)` - Send text as if player typed it. Rate limited (see
  below); returns `true` if the command was sent, `false` if it was dropped.

### Colors
The `colors` table provides predefined colors:
- `colors.white`, `colors.red`, `colors.green`, `colors.blue`
- `colors.text`, `colors.health`, `colors.mana`

### Constants
The `C` table provides game constants:
- `C.V_HP`, `C.V_MANA`, `C.V_ENDURANCE`, `C.V_STR`, `C.V_AGI`, `C.V_INT`, `C.V_WIS`
- `C.DOT_TL`, `C.DOT_BR`, `C.DOT_INV`, `C.DOT_SKL`, `C.DOT_TXT`, `C.DOT_MCT`, `C.DOT_TOP`, `C.DOT_BOT`
- `C.MAPDX`, `C.MAPDY`, `C.DIST`, `C.MAXCHARS`, `C.INVENTORYSIZE`, `C.CONTAINERSIZE`, `C.TICKS`
- `C.V_MAX`, `C.MAXQUEST`, `C.MAXMN`
- `C.QF_OPEN`, `C.QF_DONE` (quest flags)
- `C.SPEED_NORMAL`, `C.SPEED_FAST`, `C.SPEED_STEALTH`
- `C.PANEL_SKILLS`, `C.PANEL_CHAT`, `C.PANEL_INVENTORY`, `C.PANEL_GOLD`,
  `C.PANEL_SPEED`, `C.PANEL_BUFFS`, `C.PANEL_HOTBAR`, `C.PANEL_EQUIPMENT`,
  `C.MAX_PANEL`
- `C.HOTBAR_MAX_SLOTS`, `C.HOTBAR_SLOTS_PER_ROW`, `C.HOTBAR_EMPTY`,
  `C.HOTBAR_ITEM`, `C.HOTBAR_SPELL`

## Sandbox

For security, the following Lua features are disabled:
- File I/O (`io` library)
- OS commands (`os.execute`, etc. - only `os.time`, `os.date`, `os.difftime`, `os.clock` available)
- Debug library and the `jit` library
- Dynamic code loading (`loadfile`, `dofile`, `load`, `loadstring`)
- `require` is replaced by a safe variant that only loads `.lua` files from
  your own mod directory (no path traversal, no absolute paths)

## Resource Limits

Mods run inside hard resource limits. Well-behaved mods will never notice
them; hostile or buggy ones cannot take the client down.

### Instruction watchdog

Every callback (and each script's top-level code at load time) may execute at
most ~10 million VM instructions per invocation. An infinite loop is aborted
with a `script exceeded instruction limit` error; the client keeps running and
other mods are unaffected.

To make the watchdog reliable, **mod states run interpreter-only**: LuaJIT's
trace compiler is disabled (`jit.off`), because JIT-compiled loops do not honor
the instruction-count hook and could run forever. The performance tradeoff is
deliberate - overlay-style mods spend a tiny fraction of a frame in Lua, and
the interpreter is still fast. If you are building the client yourself and
want maximum Lua throughput for a trusted mod, compile with
`make LUA_ALLOW_JIT=1` to re-enable the JIT (the watchdog then becomes
best-effort: it cannot interrupt compiled hot loops).

### Memory cap

All Lua mods together may allocate at most **64MB**. Allocations over the cap
fail: the offending script sees an ordinary `not enough memory` error (catch
it with `pcall` if you want to degrade gracefully). The client itself is not
affected.

### Command rate limit

`client.cmd_text()` is rate limited to **10 commands per second** with a burst
allowance of 10 (token bucket). This is an anti-automation policy: mods are
meant to display information and streamline the UI, not to play the game.
Commands over the limit are dropped and logged; `cmd_text` returns `false`
for a dropped command, `true` for a sent one. Do not build mods that try to
work around this limit - they will not be accepted into the mod registry.

## Examples

- `example/` - Simple example mod showing basic callback usage
- `demo/` - Comprehensive demo showcasing all API features (use `#demo` for commands)
