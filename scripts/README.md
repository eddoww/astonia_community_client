# Astonia Client Lua Scripting

This directory contains Lua scripts that are loaded by the client at startup.

## Getting Started

1. Place your `.lua` files in this `scripts/` directory
2. Scripts are loaded automatically when the client starts
3. `init.lua` is always loaded first (if present)
4. Other `.lua` files are loaded alphabetically

## Hot Reload

During development, you can reload all scripts without restarting the client:
- Type `#lua_reload` in the chat window

## API Reference

### Global Tables

- `client` - Client API functions
- `C` - Game constants (V_HP, MAPDX, etc.)
- `colors` - Pre-defined colors (white, red, green, blue, etc.)
- `MOD` - Mod registry and hook system

### Client Functions

#### Logging
- `client.note(msg)` - Log info message
- `client.warn(msg)` - Log warning message
- `client.addline(msg)` - Add message to chat window

#### Rendering
- `client.render_text(x, y, color, flags, text)` - Draw text
- `client.render_rect(x1, y1, x2, y2, color)` - Draw filled rectangle
- `client.render_line(x1, y1, x2, y2, color)` - Draw line
- `client.render_pixel(x, y, color)` - Draw single pixel
- `client.render_sprite(sprite_id, x, y, light, align)` - Draw sprite
- `client.render_text_length(flags, text)` - Get text width in pixels

#### Colors
- `client.rgb(r, g, b)` - Create color (values 0-31)

#### Game Data
- `client.get_hp()` - Get current HP
- `client.get_mana()` - Get current mana
- `client.get_rage()` - Get current rage
- `client.get_endurance()` - Get current endurance
- `client.get_lifeshield()` - Get current life shield
- `client.get_experience()` - Get experience points
- `client.get_gold()` - Get gold amount
- `client.get_tick()` - Get current game tick
- `client.get_username()` - Get player username
- `client.get_origin()` - Returns x, y position
- `client.get_mouse()` - Returns mouse x, y
- `client.get_value(type, index)` - Get character stat (type: 0=base, 1=current)
- `client.get_item(slot)` - Get inventory item sprite at slot
- `client.get_item_flags(slot)` - Get inventory item flags
- `client.get_map_tile(x, y)` - Get map tile info (returns table)
- `client.get_player(index)` - Get player info (returns table or nil)

#### GUI Helpers
- `client.dotx(index)` - Get UI element X coordinate
- `client.doty(index)` - Get UI element Y coordinate
- `client.butx(index)` - Get button X coordinate
- `client.buty(index)` - Get button Y coordinate

#### Utilities
- `client.exp2level(exp)` - Convert experience to level
- `client.level2exp(level)` - Get experience needed for level

#### Commands
- `client.cmd_text(text)` - Send text command to server

### Event Hooks

Register callbacks using `MOD.register(event, callback)`:

```lua
-- Called every game tick (24/sec)
MOD.register("tick", function()
    -- your code here
end)

-- Called every display frame
MOD.register("frame", function()
    -- render overlays here
end)

-- Called on key press (return 1 to consume event)
MOD.register("keydown", function(key)
    if key == string.byte("F") then
        client.addline("F pressed!")
        return 1  -- consumed
    end
    return 0
end)

-- Called on key release
MOD.register("keyup", function(key)
    return 0
end)

-- Called on mouse click
MOD.register("mouse_click", function(x, y, button)
    return 0
end)

-- Called on mouse movement
MOD.register("mouse_move", function(x, y)
end)

-- Called for #commands
MOD.register("client_cmd", function(cmd)
    if cmd == "#mycommand" then
        client.addline("My command executed!")
        return 1
    end
    return 0
end)

-- Called when connected to server
MOD.register("gamestart", function()
    client.addline("Connected!")
end)

-- Called on area change (teleport)
MOD.register("areachange", function()
end)
```

### Constants (C table)

#### Stat Indices
- `C.V_HP`, `C.V_MANA`, `C.V_ENDURANCE`
- `C.V_WIS`, `C.V_INT`, `C.V_AGI`, `C.V_STR`

#### UI Dot Indices
- `C.DOT_TL`, `C.DOT_BR` - Top-left, bottom-right
- `C.DOT_INV`, `C.DOT_SKL` - Inventory, skills
- `C.DOT_TXT`, `C.DOT_MCT` - Chat, map center
- `C.DOT_TOP`, `C.DOT_BOT` - Top/bottom windows

#### Map Constants
- `C.MAPDX`, `C.MAPDY` - Map dimensions
- `C.DIST` - Visibility distance
- `C.MAXCHARS` - Max characters
- `C.INVENTORYSIZE` - Inventory size
- `C.TICKS` - Ticks per second (24)

## Security

The Lua environment is sandboxed for safety:
- No file I/O (`io` library removed)
- No `os.execute`, `os.remove`, etc.
- No `debug` library
- No `loadfile`, `dofile`, `load`
- Only safe `os` functions: `time`, `date`, `difftime`, `clock`

## Example Mod

See `example_mod.lua` for a complete example demonstrating:
- Health/mana overlay display
- Custom client commands (#example, #example_toggle, etc.)
- Periodic tick updates
- Low health warnings
