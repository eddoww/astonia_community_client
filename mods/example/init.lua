--[[
    Example Mod for Astonia Client

    This demonstrates the LuaJIT modding API.
    Place your mods in mods/YOUR_MOD_NAME/ directory.

    Register callbacks using: register("event_name", function)

    Available events:
    - on_init           Called when scripts are loaded
    - on_exit           Called when client shuts down
    - on_gamestart      Called when connected to server
    - on_tick           Called every game tick (24/sec)
    - on_frame          Called every display frame
    - on_mouse_move     Called on mouse movement (x, y)
    - on_mouse_click    Return 1 to consume event (x, y, button)
    - on_keydown        Return 1 to consume event (key)
    - on_keyup          Return 1 to consume event (key)
    - on_client_cmd     Handle # commands, return 1 if handled (cmd)
    - on_areachange     Called when area changes (teleport)
    - on_before_reload  Called before hot-reload
    - on_after_reload   Called after hot-reload

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
register("on_init", function()
    client.note("Example Mod v" .. ExampleMod.version .. " initialized!")
end)

-- Called when game starts
register("on_gamestart", function()
    client.note("Example Mod: Game started!")
end)

-- Called every tick (24 times per second)
register("on_tick", function()
    ExampleMod.tick_count = ExampleMod.tick_count + 1

    -- Example: Log every 240 ticks (10 seconds)
    if ExampleMod.tick_count % 240 == 0 then
        -- Uncomment to see periodic messages:
        -- client.note("Example Mod: " .. ExampleMod.tick_count .. " ticks elapsed")
    end
end)

-- Handle custom commands (starting with #)
register("on_client_cmd", function(cmd)
    if cmd == "#example" then
        client.addline("Example Mod is running! Tick count: " .. ExampleMod.tick_count)
        return 1  -- Command handled
    elseif cmd == "#example_help" then
        client.addline("Example Mod Commands:")
        client.addline("  #example - Show mod status")
        client.addline("  #example_help - Show this help")
        return 1
    end
    return 0  -- Command not handled
end)

-- Log when area changes
register("on_areachange", function()
    client.note("Example Mod: Area changed!")
end)

-- Clean up on exit
register("on_exit", function()
    client.note("Example Mod shutting down...")
end)

-- Store mod reference globally for other scripts to access
MOD = MOD or {}
MOD.example = ExampleMod
