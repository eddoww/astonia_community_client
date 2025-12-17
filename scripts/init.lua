-- Astonia Client Lua Scripting - Initialization
-- This file is loaded first when the Lua scripting subsystem starts

-- Global mod registry for tracking loaded mods
MOD = MOD or {
    name = "Astonia Lua Mods",
    version = "1.0.0",
    mods = {},
    hooks = {
        tick = {},
        frame = {},
        keydown = {},
        keyup = {},
        mouse_click = {},
        mouse_move = {},
        client_cmd = {},
        gamestart = {},
        areachange = {}
    }
}

-- Register a hook for an event
-- Example: MOD.register("tick", function() ... end)
function MOD.register(event, callback)
    if MOD.hooks[event] then
        table.insert(MOD.hooks[event], callback)
    else
        client.warn("Unknown event type: " .. tostring(event))
    end
end

-- Unregister all hooks (useful for reload)
function MOD.clear_hooks()
    for event, _ in pairs(MOD.hooks) do
        MOD.hooks[event] = {}
    end
end

-- Called when Lua subsystem initializes
function on_init()
    client.note("Lua scripting initialized")
    client.addline("Lua mods loaded. Type #lua_reload to reload scripts.")
end

-- Called when Lua subsystem exits
function on_exit()
    client.note("Lua scripting shutting down")
end

-- Called before scripts are reloaded
function on_before_reload()
    MOD.clear_hooks()
end

-- Called after scripts are reloaded
function on_after_reload()
    client.note("Lua scripts reloaded successfully")
end

-- Called when connected to server
function on_gamestart()
    for _, callback in ipairs(MOD.hooks.gamestart) do
        callback()
    end
end

-- Called every game tick (24 times per second)
function on_tick()
    for _, callback in ipairs(MOD.hooks.tick) do
        callback()
    end
end

-- Called every display frame
function on_frame()
    for _, callback in ipairs(MOD.hooks.frame) do
        callback()
    end
end

-- Called on mouse movement
function on_mouse_move(x, y)
    for _, callback in ipairs(MOD.hooks.mouse_move) do
        callback(x, y)
    end
end

-- Called on mouse click
-- Return 1 to consume event, -1 to consume but allow other handlers, 0 otherwise
function on_mouse_click(x, y, button)
    for _, callback in ipairs(MOD.hooks.mouse_click) do
        local result = callback(x, y, button)
        if result and result ~= 0 then
            return result
        end
    end
    return 0
end

-- Called on key down
function on_keydown(key)
    for _, callback in ipairs(MOD.hooks.keydown) do
        local result = callback(key)
        if result and result ~= 0 then
            return result
        end
    end
    return 0
end

-- Called on key up
function on_keyup(key)
    for _, callback in ipairs(MOD.hooks.keyup) do
        local result = callback(key)
        if result and result ~= 0 then
            return result
        end
    end
    return 0
end

-- Called for client commands (text starting with #)
function on_client_cmd(cmd)
    for _, callback in ipairs(MOD.hooks.client_cmd) do
        local result = callback(cmd)
        if result and result ~= 0 then
            return result
        end
    end
    return 0
end

-- Called when area changes (teleport, etc.)
function on_areachange()
    for _, callback in ipairs(MOD.hooks.areachange) do
        callback()
    end
end

client.note("init.lua loaded")
