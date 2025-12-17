--[[
    Example Mod for Astonia Client

    This demonstrates the LuaJIT modding API.
    Place your mods in mods/YOUR_MOD_NAME/ directory.

    Available callbacks:
    - on_init()           Called when scripts are loaded
    - on_exit()           Called when client shuts down
    - on_gamestart()      Called when connected to server
    - on_tick()           Called every game tick (24/sec)
    - on_frame()          Called every display frame
    - on_mouse_move(x, y) Called on mouse movement
    - on_mouse_click(x, y, button) Return 1 to consume event
    - on_keydown(key)     Return 1 to consume event
    - on_keyup(key)       Return 1 to consume event
    - on_client_cmd(cmd)  Handle # commands, return 1 if handled
    - on_areachange()     Called when area changes (teleport)
    - on_before_reload()  Called before hot-reload
    - on_after_reload()   Called after hot-reload

    Use #lua_reload in chat to hot-reload all mods.
]]

-- Mod state
local ExampleMod = {
    name = "Example Mod",
    version = "1.0.0",
    enabled = true,
    tick_count = 0
}

-- Initialize mod
function on_init()
    client.log("Example Mod v" .. ExampleMod.version .. " initialized!")
end

-- Called when game starts
function on_gamestart()
    client.log("Example Mod: Game started!")
end

-- Called every tick (24 times per second)
function on_tick()
    ExampleMod.tick_count = ExampleMod.tick_count + 1

    -- Example: Log every 240 ticks (10 seconds)
    if ExampleMod.tick_count % 240 == 0 then
        -- Uncomment to see periodic messages:
        -- client.log("Example Mod: " .. ExampleMod.tick_count .. " ticks elapsed")
    end
end

-- Handle custom commands (starting with #)
function on_client_cmd(cmd)
    if cmd == "#example" then
        client.chat("Example Mod is running! Tick count: " .. ExampleMod.tick_count)
        return 1  -- Command handled
    elseif cmd == "#example_help" then
        client.chat("Example Mod Commands:")
        client.chat("  #example - Show mod status")
        client.chat("  #example_help - Show this help")
        return 1
    end
    return 0  -- Command not handled
end

-- Log when area changes
function on_areachange()
    client.log("Example Mod: Area changed!")
end

-- Clean up on exit
function on_exit()
    client.log("Example Mod shutting down...")
end

-- Store mod reference globally for other scripts to access
MOD = MOD or {}
MOD.example = ExampleMod
