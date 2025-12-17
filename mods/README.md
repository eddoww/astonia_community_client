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

## Available Events

| Event | Description |
|-------|-------------|
| `on_init` | Called when scripts are loaded |
| `on_exit` | Called when client shuts down |
| `on_gamestart` | Called when connected to server |
| `on_tick` | Called every game tick (24/sec) |
| `on_frame` | Called every display frame |
| `on_mouse_move` | Called on mouse movement (x, y) |
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
- `client.get_value(type, idx)` - Get character stat (type: 0=base, 1=current)
- `client.get_item(slot)` - Get inventory item sprite
- `client.get_item_flags(slot)` - Get inventory item flags
- `client.get_map_tile(x, y)` - Get map tile info (returns table)
- `client.get_player(idx)` - Get player info (returns table)

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

### Utilities
- `client.exp2level(exp)` - Convert experience to level
- `client.level2exp(level)` - Convert level to experience needed
- `client.cmd_text(text)` - Send text as if player typed it

### Colors
The `colors` table provides predefined colors:
- `colors.white`, `colors.red`, `colors.green`, `colors.blue`
- `colors.text`, `colors.health`, `colors.mana`

### Constants
The `C` table provides game constants:
- `C.V_HP`, `C.V_MANA`, `C.V_STR`, `C.V_AGI`, `C.V_INT`, `C.V_WIS`
- `C.DOT_TL`, `C.DOT_BR`, `C.DOT_INV`, `C.DOT_SKL`, etc.
- `C.MAPDX`, `C.MAPDY`, `C.DIST`, `C.MAXCHARS`, `C.INVENTORYSIZE`, `C.TICKS`

## Sandbox

For security, the following Lua features are disabled:
- File I/O (`io` library)
- OS commands (`os.execute`, etc. - only `os.time`, `os.date`, `os.difftime`, `os.clock` available)
- Debug library
- Dynamic code loading (`loadfile`, `dofile`, `load`, `loadstring`)

## Example

See the `example/` folder for a complete example mod.
