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
3. Implement any callbacks you need
4. Start the game - your mod will be loaded automatically

## Hot Reload

Type `#lua_reload` in the chat to reload all mods without restarting the client.

## Available Callbacks

| Callback | Description |
|----------|-------------|
| `on_init()` | Called when scripts are loaded |
| `on_exit()` | Called when client shuts down |
| `on_gamestart()` | Called when connected to server |
| `on_tick()` | Called every game tick (24/sec) |
| `on_frame()` | Called every display frame |
| `on_mouse_move(x, y)` | Called on mouse movement |
| `on_mouse_click(x, y, button)` | Return 1 to consume event |
| `on_keydown(key)` | Return 1 to consume event |
| `on_keyup(key)` | Return 1 to consume event |
| `on_client_cmd(cmd)` | Handle # commands, return 1 if handled |
| `on_areachange()` | Called when area changes |
| `on_before_reload()` | Called before hot-reload |
| `on_after_reload()` | Called after hot-reload |

## Client API

Access the client API through the `client` global table:

### Logging
- `client.log(message)` - Log to console
- `client.chat(message)` - Send chat message

### Game Data
- `client.get_player_hp()` - Get player health
- `client.get_player_mana()` - Get player mana
- `client.get_player_pos()` - Returns x, y position
- `client.get_player_name()` - Get player name
- `client.get_tick()` - Get current tick number
- `client.get_map_at(x, y)` - Get map tile info at position

### Rendering
- `client.draw_text(x, y, text, color)` - Draw text on screen
- `client.draw_rect(x, y, w, h, color)` - Draw rectangle
- `client.draw_sprite(x, y, sprite, light, align)` - Draw sprite

### Colors
The `colors` table provides predefined colors:
- `colors.white`, `colors.black`, `colors.red`, `colors.green`, `colors.blue`
- `colors.yellow`, `colors.cyan`, `colors.magenta`, `colors.gray`

## Sandbox

For security, the following Lua features are disabled:
- File I/O (`io` library)
- OS commands (`os.execute`, etc. - only `os.time`, `os.date`, `os.difftime`, `os.clock` available)
- Debug library
- Dynamic code loading (`loadfile`, `dofile`, `load`, `loadstring`)

## Example

See the `example/` folder for a complete example mod.
