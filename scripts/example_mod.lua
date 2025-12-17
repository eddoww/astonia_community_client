-- Example Lua Mod for Astonia Client
-- Demonstrates the Lua scripting API capabilities
--
-- Features demonstrated:
-- - Health/mana overlay display
-- - Custom client commands
-- - Keyboard shortcuts
-- - Periodic tick updates

local example = {
    name = "Example Mod",
    version = "1.0.0",
    enabled = true,
    show_overlay = true,
    last_hp = 0,
    low_health_warning = false
}

-- Register the mod
table.insert(MOD.mods, example)

-- Helper: Create a colored progress bar
local function draw_bar(x, y, width, height, percent, color, bg_color)
    -- Draw background
    client.render_rect(x, y, x + width, y + height, bg_color or client.rgb(5, 5, 5))
    -- Draw filled portion
    local fill_width = math.floor(width * percent / 100)
    if fill_width > 0 then
        client.render_rect(x, y, x + fill_width, y + height, color)
    end
    -- Draw border
    client.render_line(x, y, x + width, y, client.rgb(15, 15, 15))
    client.render_line(x, y + height, x + width, y + height, client.rgb(15, 15, 15))
    client.render_line(x, y, x, y + height, client.rgb(15, 15, 15))
    client.render_line(x + width, y, x + width, y + height, client.rgb(15, 15, 15))
end

-- Frame callback: Draw custom overlay
local function on_frame()
    if not example.enabled or not example.show_overlay then
        return
    end

    local hp = client.get_hp()
    local mana = client.get_mana()
    local endurance = client.get_endurance()

    -- Get max values from character stats
    local max_hp = client.get_value(0, C.V_HP) -- base HP
    local max_mana = client.get_value(0, C.V_MANA)
    local max_end = client.get_value(0, C.V_ENDURANCE)

    -- Avoid division by zero
    if max_hp == 0 then max_hp = 1 end
    if max_mana == 0 then max_mana = 1 end
    if max_end == 0 then max_end = 1 end

    -- Calculate percentages
    local hp_pct = math.min(100, math.floor(hp * 100 / max_hp))
    local mana_pct = math.min(100, math.floor(mana * 100 / max_mana))
    local end_pct = math.min(100, math.floor(endurance * 100 / max_end))

    -- Draw overlay in top-left area
    local base_x = 10
    local base_y = 50
    local bar_width = 120
    local bar_height = 8
    local spacing = 14

    -- Title
    client.render_text(base_x, base_y - 15, colors.text, 0, "Status")

    -- HP bar
    local hp_color = hp_pct < 25 and client.rgb(31, 0, 0) or colors.health
    draw_bar(base_x, base_y, bar_width, bar_height, hp_pct, hp_color)
    client.render_text(base_x + bar_width + 5, base_y - 1, colors.white, 0,
        string.format("HP: %d/%d", hp, max_hp))

    -- Mana bar
    draw_bar(base_x, base_y + spacing, bar_width, bar_height, mana_pct, colors.mana)
    client.render_text(base_x + bar_width + 5, base_y + spacing - 1, colors.white, 0,
        string.format("MP: %d/%d", mana, max_mana))

    -- Endurance bar
    draw_bar(base_x, base_y + spacing * 2, bar_width, bar_height, end_pct, client.rgb(31, 31, 5))
    client.render_text(base_x + bar_width + 5, base_y + spacing * 2 - 1, colors.white, 0,
        string.format("EN: %d/%d", endurance, max_end))

    -- Gold display
    local gold = client.get_gold()
    client.render_text(base_x, base_y + spacing * 3 + 5, client.rgb(31, 25, 0), 0,
        string.format("Gold: %d", gold))
end

-- Tick callback: Check for low health warning
local function on_tick()
    if not example.enabled then
        return
    end

    local hp = client.get_hp()
    local max_hp = client.get_value(0, C.V_HP)

    if max_hp > 0 then
        local hp_pct = hp * 100 / max_hp

        -- Warn when health drops below 20%
        if hp_pct < 20 and not example.low_health_warning then
            example.low_health_warning = true
            client.addline("WARNING: Health is critically low!")
        elseif hp_pct >= 20 then
            example.low_health_warning = false
        end
    end

    example.last_hp = hp
end

-- Client command handler
local function on_client_cmd(cmd)
    -- Handle #example command
    if cmd == "#example" then
        client.addline("Example Mod v" .. example.version)
        client.addline("Commands: #example_toggle, #example_overlay")
        return 1 -- consumed
    end

    -- Toggle mod
    if cmd == "#example_toggle" then
        example.enabled = not example.enabled
        client.addline("Example Mod: " .. (example.enabled and "ENABLED" or "DISABLED"))
        return 1
    end

    -- Toggle overlay
    if cmd == "#example_overlay" then
        example.show_overlay = not example.show_overlay
        client.addline("Overlay: " .. (example.show_overlay and "ON" or "OFF"))
        return 1
    end

    -- Show player position
    if cmd == "#example_pos" then
        local ox, oy = client.get_origin()
        client.addline(string.format("Position: %d, %d", ox, oy))
        return 1
    end

    -- Show experience info
    if cmd == "#example_exp" then
        local exp = client.get_experience()
        local level = client.exp2level(exp)
        local next_level_exp = client.level2exp(level + 1)
        client.addline(string.format("Level %d - Exp: %d / %d", level, exp, next_level_exp))
        return 1
    end

    return 0 -- not handled
end

-- Gamestart callback
local function on_gamestart()
    client.addline("Example Mod loaded! Type #example for help.")
end

-- Register all hooks
MOD.register("frame", on_frame)
MOD.register("tick", on_tick)
MOD.register("client_cmd", on_client_cmd)
MOD.register("gamestart", on_gamestart)

client.note("example_mod.lua loaded")
