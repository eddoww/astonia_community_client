--[[
    Comprehensive Demo Mod for Astonia Client

    This mod demonstrates ALL available API features.
    Use #demo in chat to see available commands.

    Features demonstrated:
    - All callbacks (init, tick, frame, input, etc.)
    - Rendering (text, rectangles, lines, pixels)
    - Game data access (HP, mana, stats, inventory, map)
    - GUI positioning helpers
    - Custom commands
    - Hot-reload support

    API Reference:
    ===============

    Callback Registration:
      register(event_name, callback_function)
      Events: on_init, on_exit, on_gamestart, on_tick, on_frame,
              on_mouse_move, on_mouse_click, on_keydown, on_keyup,
              on_client_cmd, on_areachange, on_before_reload, on_after_reload

    Logging:
      client.note(msg)      - Log to console
      client.warn(msg)      - Log warning
      client.addline(msg)   - Add message to chat

    Rendering:
      client.render_text(x, y, color, flags, text) -> width
      client.render_rect(sx, sy, ex, ey, color)
      client.render_line(fx, fy, tx, ty, color)
      client.render_pixel(x, y, color)
      client.render_sprite(sprite, x, y, light, align)
      client.render_text_length(flags, text) -> length
      client.rgb(r, g, b) -> color (0-31 per channel)

    Game Data:
      client.get_hp() -> hp
      client.get_mana() -> mana
      client.get_rage() -> rage
      client.get_endurance() -> endurance
      client.get_lifeshield() -> lifeshield
      client.get_experience() -> exp
      client.get_gold() -> gold
      client.get_tick() -> tick
      client.get_username() -> name
      client.get_origin() -> x, y
      client.get_mouse() -> x, y
      client.get_value(type, idx) -> value (type: 0=base, 1=current)
      client.get_item(slot) -> sprite
      client.get_item_flags(slot) -> flags
      client.get_map_tile(x, y) -> {gsprite, fsprite, isprite, csprite, cn, flags, health}
      client.get_player(idx) -> {name, sprite, level, clan, pk_status}

    GUI Helpers:
      client.dotx(idx), client.doty(idx) - Screen anchor points
      client.butx(idx), client.buty(idx) - Button positions

    Utilities:
      client.exp2level(exp) -> level
      client.level2exp(level) -> exp
      client.cmd_text(text) - Send text as player input

    Constants (C table):
      C.V_HP, C.V_MANA, C.V_ENDURANCE, C.V_STR, C.V_AGI, C.V_INT, C.V_WIS
      C.DOT_TL, C.DOT_BR, C.DOT_INV, C.DOT_SKL, C.DOT_TXT, C.DOT_MCT, C.DOT_TOP, C.DOT_BOT
      C.MAPDX, C.MAPDY, C.DIST, C.MAXCHARS, C.INVENTORYSIZE, C.TICKS

    Colors (colors table):
      colors.white, colors.red, colors.green, colors.blue
      colors.text, colors.health, colors.mana
]]

-- ============================================================================
-- MOD STATE
-- ============================================================================

local Demo = {
    name = "Demo Mod",
    version = "1.0.0",

    -- Runtime state
    initialized = false,
    game_started = false,
    tick_count = 0,
    frame_count = 0,

    -- Feature toggles (controlled via commands)
    show_overlay = false,
    show_stats = false,
    show_mouse_tracker = false,
    show_map_info = false,
    show_nearby_players = false,

    -- Mouse tracking
    last_mouse_x = 0,
    last_mouse_y = 0,
    mouse_clicks = 0,

    -- Key tracking
    keys_pressed = {},
    last_key = 0,

    -- Performance tracking
    last_tick_time = 0,
    ticks_per_second = 0,
}

-- ============================================================================
-- CUSTOM COLORS (demonstrates client.rgb)
-- ============================================================================

local custom_colors = {
    orange = client.rgb(31, 16, 0),
    purple = client.rgb(20, 0, 31),
    cyan = client.rgb(0, 31, 31),
    yellow = client.rgb(31, 31, 0),
    dark_gray = client.rgb(8, 8, 8),
    panel_bg = client.rgb(4, 4, 6),
    panel_border = client.rgb(12, 12, 16),
}

-- ============================================================================
-- UI HELPER FUNCTIONS
-- ============================================================================

-- Draw a panel with background and border
local function draw_panel(x, y, w, h, title)
    -- Background (demonstrates render_rect)
    client.render_rect(x, y, x + w, y + h, custom_colors.panel_bg)

    -- Border using render_line
    client.render_line(x, y, x + w, y, custom_colors.panel_border)         -- top
    client.render_line(x, y + h, x + w, y + h, custom_colors.panel_border) -- bottom
    client.render_line(x, y, x, y + h, custom_colors.panel_border)         -- left
    client.render_line(x + w, y, x + w, y + h, custom_colors.panel_border) -- right

    -- Title bar
    if title then
        client.render_rect(x + 1, y + 1, x + w - 1, y + 14, custom_colors.panel_border)
        client.render_text(x + 4, y + 2, colors.white, 0, title)
    end
end

-- Draw a progress bar
local function draw_bar(x, y, w, h, value, max_value, fill_color, bg_color)
    bg_color = bg_color or custom_colors.dark_gray

    -- Background
    client.render_rect(x, y, x + w, y + h, bg_color)

    -- Fill based on percentage
    if max_value > 0 and value > 0 then
        local fill_w = math.floor((value / max_value) * w)
        if fill_w > 0 then
            client.render_rect(x, y, x + fill_w, y + h, fill_color)
        end
    end

    -- Border
    client.render_line(x, y, x + w, y, custom_colors.panel_border)
    client.render_line(x, y + h, x + w, y + h, custom_colors.panel_border)
end

-- ============================================================================
-- RENDER FUNCTIONS
-- ============================================================================

local function render_main_overlay()
    -- Position using GUI helpers (dotx/doty)
    local x = client.dotx(C.DOT_TL) + 10
    local y = client.doty(C.DOT_TL) + 10

    draw_panel(x, y, 220, 100, "Demo Overlay (F9)")

    local text_y = y + 18

    -- Username (demonstrates get_username)
    local username = client.get_username()
    client.render_text(x + 4, text_y, colors.text, 0, "Player: " .. username)
    text_y = text_y + 12

    -- Current tick (demonstrates get_tick)
    local tick = client.get_tick()
    client.render_text(x + 4, text_y, colors.text, 0, string.format("Tick: %d", tick))
    text_y = text_y + 12

    -- Origin position (demonstrates get_origin returning 2 values)
    local ox, oy = client.get_origin()
    client.render_text(x + 4, text_y, colors.text, 0, string.format("Origin: %d, %d", ox, oy))
    text_y = text_y + 12

    -- HP Bar (demonstrates get_hp + get_value)
    local hp = client.get_hp()
    local max_hp = client.get_value(0, C.V_HP)  -- 0 = base value
    client.render_text(x + 4, text_y, colors.health, 0, string.format("HP: %d/%d", hp, max_hp))
    draw_bar(x + 80, text_y, 130, 10, hp, max_hp, colors.health)
    text_y = text_y + 14

    -- Mana Bar (demonstrates get_mana)
    local mana = client.get_mana()
    local max_mana = client.get_value(0, C.V_MANA)
    client.render_text(x + 4, text_y, colors.mana, 0, string.format("Mana: %d/%d", mana, max_mana))
    draw_bar(x + 80, text_y, 130, 10, mana, max_mana, colors.mana)
end

local function render_stats_panel()
    local x = client.dotx(C.DOT_BR) - 230
    local y = client.doty(C.DOT_TL) + 10

    draw_panel(x, y, 220, 180, "Character Stats (F10)")

    local text_y = y + 18

    -- Primary stats with buff/debuff coloring (demonstrates get_value with constants)
    local stats = {
        {name = "Strength", idx = C.V_STR},
        {name = "Agility", idx = C.V_AGI},
        {name = "Intelligence", idx = C.V_INT},
        {name = "Wisdom", idx = C.V_WIS},
    }

    for _, stat in ipairs(stats) do
        local base = client.get_value(0, stat.idx)    -- Base value
        local current = client.get_value(1, stat.idx) -- Current value
        local color = colors.text
        if current > base then
            color = colors.green  -- Buffed
        elseif current < base then
            color = colors.red    -- Debuffed
        end
        client.render_text(x + 4, text_y, colors.text, 0, stat.name .. ":")
        client.render_text(x + 100, text_y, color, 0, string.format("%d (%d)", current, base))
        text_y = text_y + 12
    end

    text_y = text_y + 4
    client.render_text(x + 4, text_y, custom_colors.yellow, 0, "--- Resources ---")
    text_y = text_y + 12

    -- Secondary resources (demonstrates get_rage, get_endurance, get_lifeshield)
    local rage = client.get_rage()
    local endurance = client.get_endurance()
    local lifeshield = client.get_lifeshield()

    client.render_text(x + 4, text_y, colors.red, 0, string.format("Rage: %d", rage))
    text_y = text_y + 12
    client.render_text(x + 4, text_y, custom_colors.orange, 0, string.format("Endurance: %d", endurance))
    text_y = text_y + 12
    client.render_text(x + 4, text_y, custom_colors.cyan, 0, string.format("Lifeshield: %d", lifeshield))
    text_y = text_y + 14

    -- Experience and level (demonstrates get_experience, exp2level, level2exp)
    local exp = client.get_experience()
    local gold = client.get_gold()
    local level = client.exp2level(exp)
    local next_level_exp = client.level2exp(level + 1)

    client.render_text(x + 4, text_y, custom_colors.purple, 0, string.format("Level: %d", level))
    text_y = text_y + 12
    client.render_text(x + 4, text_y, colors.text, 0, string.format("Exp: %d / %d", exp, next_level_exp))
    text_y = text_y + 12
    client.render_text(x + 4, text_y, custom_colors.yellow, 0, string.format("Gold: %d", gold))
end

local function render_mouse_tracker()
    -- Get mouse position (demonstrates get_mouse returning 2 values)
    local mx, my = client.get_mouse()

    -- Only draw if mouse is in a safe area (avoid edge issues)
    if mx < 20 or my < 20 then
        -- Just show info panel in a fixed position when mouse is near edges
        local panel_x = 100
        local panel_y = 100
        draw_panel(panel_x, panel_y, 120, 50, nil)
        client.render_text(panel_x + 4, panel_y + 4, colors.text, 0, string.format("Mouse: %d, %d", mx, my))
        client.render_text(panel_x + 4, panel_y + 16, colors.text, 0, string.format("Clicks: %d", Demo.mouse_clicks))
        client.render_text(panel_x + 4, panel_y + 28, colors.text, 0, string.format("Last key: %d", Demo.last_key))
        return
    end

    -- Crosshair using render_line (safe since we checked bounds above)
    local size = 10
    client.render_line(mx - size, my, mx + size, my, colors.green)
    client.render_line(mx, my - size, mx, my + size, colors.green)

    -- Pixel pattern around cursor (demonstrates render_pixel)
    for i = -2, 2 do
        for j = -2, 2 do
            if math.abs(i) + math.abs(j) == 2 then
                local px, py = mx + i * 5, my + j * 5
                if px > 0 and py > 0 then
                    client.render_pixel(px, py, custom_colors.cyan)
                end
            end
        end
    end

    -- Info panel following cursor
    local panel_x = mx + 15
    local panel_y = my + 15

    draw_panel(panel_x, panel_y, 120, 50, nil)

    client.render_text(panel_x + 4, panel_y + 4, colors.text, 0, string.format("Mouse: %d, %d", mx, my))
    client.render_text(panel_x + 4, panel_y + 16, colors.text, 0, string.format("Clicks: %d", Demo.mouse_clicks))
    client.render_text(panel_x + 4, panel_y + 28, colors.text, 0, string.format("Last key: %d", Demo.last_key))
end

local function render_map_info()
    -- Demonstrates get_map_tile returning a table
    local map_x = C.MAPDX / 2
    local map_y = C.MAPDY / 2

    local tile = client.get_map_tile(map_x, map_y)

    local x = 10
    local y = client.doty(C.DOT_BOT) - 100

    draw_panel(x, y, 180, 90, "Map Tile Info")

    local text_y = y + 18

    if tile then
        client.render_text(x + 4, text_y, colors.text, 0, string.format("Pos: %d, %d", map_x, map_y))
        text_y = text_y + 12
        client.render_text(x + 4, text_y, colors.text, 0, string.format("Ground: %d", tile.gsprite))
        text_y = text_y + 12
        client.render_text(x + 4, text_y, colors.text, 0, string.format("Floor: %d", tile.fsprite))
        text_y = text_y + 12
        client.render_text(x + 4, text_y, colors.text, 0, string.format("Item: %d", tile.isprite))
        text_y = text_y + 12
        client.render_text(x + 4, text_y, colors.text, 0, string.format("Char: %d, HP: %d", tile.cn, tile.health))
    else
        client.render_text(x + 4, text_y, colors.red, 0, "No tile data")
    end
end

local function render_nearby_players()
    local x = client.dotx(C.DOT_BR) - 180
    local y = client.doty(C.DOT_BOT) - 150

    draw_panel(x, y, 170, 140, "Nearby Players")

    local text_y = y + 18
    local count = 0
    local max_display = 8

    -- Iterate through player slots (demonstrates get_player returning a table)
    for i = 0, C.MAXCHARS - 1 do
        local p = client.get_player(i)
        if p and count < max_display then
            local color = colors.text
            if p.pk_status == 1 then
                color = colors.red
            elseif p.pk_status == 2 then
                color = custom_colors.orange
            end

            local name_display = p.name
            if string.len(name_display) > 12 then
                name_display = string.sub(name_display, 1, 12) .. ".."
            end

            client.render_text(x + 4, text_y, color, 0,
                string.format("L%d %s", p.level, name_display))
            text_y = text_y + 12
            count = count + 1
        end
    end

    if count == 0 then
        client.render_text(x + 4, text_y, colors.text, 0, "No players nearby")
    end
end

-- ============================================================================
-- COMMAND HANDLERS
-- ============================================================================

local function show_help()
    client.addline("=== Demo Mod Commands ===")
    client.addline("#demo_overlay  - Toggle main overlay (F9)")
    client.addline("#demo_stats    - Toggle stats panel (F10)")
    client.addline("#demo_mouse    - Toggle mouse tracker")
    client.addline("#demo_map      - Toggle map tile info")
    client.addline("#demo_players  - Toggle nearby players")
    client.addline("#demo_all      - Enable all overlays")
    client.addline("#demo_off      - Disable all overlays")
    client.addline("#demo_info     - Show mod state info")
    client.addline("#demo_say <msg>- Send chat message")
    client.addline("#demo_test     - Run API test suite")
    client.addline("#lua_reload    - Hot-reload all mods")
end

local function show_mod_info()
    client.addline("=== Demo Mod Info ===")
    client.addline(string.format("Version: %s", Demo.version))
    client.addline(string.format("Initialized: %s", tostring(Demo.initialized)))
    client.addline(string.format("Game started: %s", tostring(Demo.game_started)))
    client.addline(string.format("Ticks: %d", Demo.tick_count))
    client.addline(string.format("Frames: %d", Demo.frame_count))
    client.addline(string.format("Mouse clicks: %d", Demo.mouse_clicks))
    client.addline(string.format("TPS estimate: %d", Demo.ticks_per_second))

    local overlays = {}
    if Demo.show_overlay then table.insert(overlays, "main") end
    if Demo.show_stats then table.insert(overlays, "stats") end
    if Demo.show_mouse_tracker then table.insert(overlays, "mouse") end
    if Demo.show_map_info then table.insert(overlays, "map") end
    if Demo.show_nearby_players then table.insert(overlays, "players") end

    if #overlays > 0 then
        client.addline("Active overlays: " .. table.concat(overlays, ", "))
    else
        client.addline("Active overlays: (none)")
    end
end

local function run_api_tests()
    client.addline("=== Running API Tests ===")

    local tests_passed = 0
    local tests_failed = 0

    local function test(name, condition)
        if condition then
            client.note("PASS: " .. name)
            tests_passed = tests_passed + 1
        else
            client.warn("FAIL: " .. name)
            tests_failed = tests_failed + 1
        end
    end

    -- Test logging
    client.note("Testing logging functions...")
    client.warn("Testing warn() - this is expected")
    test("note() works", true)
    test("warn() works", true)

    -- Test game data accessors
    test("get_hp() returns number", type(client.get_hp()) == "number")
    test("get_mana() returns number", type(client.get_mana()) == "number")
    test("get_rage() returns number", type(client.get_rage()) == "number")
    test("get_endurance() returns number", type(client.get_endurance()) == "number")
    test("get_lifeshield() returns number", type(client.get_lifeshield()) == "number")
    test("get_experience() returns number", type(client.get_experience()) == "number")
    test("get_gold() returns number", type(client.get_gold()) == "number")
    test("get_tick() returns number", type(client.get_tick()) == "number")
    test("get_username() returns string", type(client.get_username()) == "string")

    local ox, oy = client.get_origin()
    test("get_origin() returns two numbers", type(ox) == "number" and type(oy) == "number")

    local mx, my = client.get_mouse()
    test("get_mouse() returns two numbers", type(mx) == "number" and type(my) == "number")

    -- Test get_value with constants
    test("get_value(0, V_HP) works", type(client.get_value(0, C.V_HP)) == "number")
    test("get_value(1, V_HP) works", type(client.get_value(1, C.V_HP)) == "number")

    -- Test get_item
    local item0 = client.get_item(0)
    test("get_item(0) returns number or nil", type(item0) == "number" or item0 == nil)

    -- Test get_map_tile
    local tile = client.get_map_tile(C.MAPDX / 2, C.MAPDY / 2)
    test("get_map_tile() returns table", type(tile) == "table")
    if tile then
        test("tile.gsprite exists", tile.gsprite ~= nil)
        test("tile.flags exists", tile.flags ~= nil)
        test("tile.health exists", tile.health ~= nil)
    end

    -- Test GUI helpers
    test("dotx(DOT_TL) returns number", type(client.dotx(C.DOT_TL)) == "number")
    test("doty(DOT_TL) returns number", type(client.doty(C.DOT_TL)) == "number")
    test("butx(0) returns number", type(client.butx(0)) == "number")
    test("buty(0) returns number", type(client.buty(0)) == "number")

    -- Test utilities
    test("exp2level(1000) returns number", type(client.exp2level(1000)) == "number")
    test("level2exp(10) returns number", type(client.level2exp(10)) == "number")

    -- Test color functions
    test("rgb(15,20,25) returns number", type(client.rgb(15, 20, 25)) == "number")

    -- Test render_text_length
    test("render_text_length() returns number", type(client.render_text_length(0, "Test")) == "number")

    -- Test all constants exist
    test("C.MAPDX exists", C.MAPDX ~= nil)
    test("C.MAPDY exists", C.MAPDY ~= nil)
    test("C.DIST exists", C.DIST ~= nil)
    test("C.MAXCHARS exists", C.MAXCHARS ~= nil)
    test("C.INVENTORYSIZE exists", C.INVENTORYSIZE ~= nil)
    test("C.TICKS exists", C.TICKS ~= nil)
    test("C.V_HP exists", C.V_HP ~= nil)
    test("C.V_STR exists", C.V_STR ~= nil)
    test("C.DOT_TL exists", C.DOT_TL ~= nil)
    test("C.DOT_BR exists", C.DOT_BR ~= nil)

    -- Test colors table
    test("colors.white exists", colors.white ~= nil)
    test("colors.red exists", colors.red ~= nil)
    test("colors.green exists", colors.green ~= nil)
    test("colors.blue exists", colors.blue ~= nil)
    test("colors.text exists", colors.text ~= nil)
    test("colors.health exists", colors.health ~= nil)
    test("colors.mana exists", colors.mana ~= nil)

    -- Test sandbox is working
    test("os.time exists (allowed)", os.time ~= nil)
    test("os.date exists (allowed)", os.date ~= nil)
    test("os.clock exists (allowed)", os.clock ~= nil)
    test("os.execute is nil (sandboxed)", os.execute == nil)
    test("io is nil (sandboxed)", io == nil)
    test("debug is nil (sandboxed)", debug == nil)
    test("loadfile is nil (sandboxed)", loadfile == nil)
    test("load is nil (sandboxed)", load == nil)

    -- Summary
    client.addline("=== Test Results ===")
    client.addline(string.format("Passed: %d, Failed: %d", tests_passed, tests_failed))
    if tests_failed == 0 then
        client.addline("All tests passed!")
    else
        client.addline("Some tests failed - check console log")
    end
end

local function handle_demo_commands(cmd)
    if cmd == "#demo" or cmd == "#demo_help" then
        show_help()
        return 1
    end

    if cmd == "#demo_overlay" then
        Demo.show_overlay = not Demo.show_overlay
        client.addline("Overlay: " .. (Demo.show_overlay and "ON" or "OFF") .. " (or press F9)")
        return 1
    end

    if cmd == "#demo_stats" then
        Demo.show_stats = not Demo.show_stats
        client.addline("Stats panel: " .. (Demo.show_stats and "ON" or "OFF") .. " (or press F10)")
        return 1
    end

    if cmd == "#demo_mouse" then
        Demo.show_mouse_tracker = not Demo.show_mouse_tracker
        client.addline("Mouse tracker: " .. (Demo.show_mouse_tracker and "ON" or "OFF"))
        return 1
    end

    if cmd == "#demo_map" then
        Demo.show_map_info = not Demo.show_map_info
        client.addline("Map info: " .. (Demo.show_map_info and "ON" or "OFF"))
        return 1
    end

    if cmd == "#demo_players" then
        Demo.show_nearby_players = not Demo.show_nearby_players
        client.addline("Nearby players: " .. (Demo.show_nearby_players and "ON" or "OFF"))
        return 1
    end

    if cmd == "#demo_all" then
        Demo.show_overlay = true
        Demo.show_stats = true
        Demo.show_mouse_tracker = true
        Demo.show_map_info = true
        Demo.show_nearby_players = true
        client.addline("All overlays enabled!")
        return 1
    end

    if cmd == "#demo_off" then
        Demo.show_overlay = false
        Demo.show_stats = false
        Demo.show_mouse_tracker = false
        Demo.show_map_info = false
        Demo.show_nearby_players = false
        client.addline("All overlays disabled.")
        return 1
    end

    if cmd == "#demo_info" then
        show_mod_info()
        return 1
    end

    -- Say command (demonstrates cmd_text API)
    if string.sub(cmd, 1, 10) == "#demo_say " then
        local message = string.sub(cmd, 11)
        if message and message ~= "" then
            client.cmd_text(message)
            client.note("Sent message via cmd_text: " .. message)
        else
            client.addline("Usage: #demo_say <message>")
        end
        return 1
    end

    if cmd == "#demo_test" then
        run_api_tests()
        return 1
    end

    return 0  -- Not our command
end

-- ============================================================================
-- LIFECYCLE CALLBACKS (using register)
-- ============================================================================

register("on_init", function()
    client.note("Demo Mod v" .. Demo.version .. " initializing...")
    client.note(string.format("Map size: %dx%d, DIST=%d", C.MAPDX, C.MAPDY, C.DIST))
    client.note(string.format("Max chars: %d, Inventory: %d slots", C.MAXCHARS, C.INVENTORYSIZE))
    client.note(string.format("Game runs at %d ticks/second", C.TICKS))
    Demo.initialized = true
    client.note("Demo Mod initialized! Type #demo for help.")
end)

register("on_exit", function()
    client.note("Demo Mod shutting down...")
    client.note(string.format("Session stats: %d ticks, %d frames, %d clicks",
        Demo.tick_count, Demo.frame_count, Demo.mouse_clicks))
end)

register("on_gamestart", function()
    Demo.game_started = true
    local username = client.get_username()
    client.note("Game started! Welcome, " .. username)
    client.addline("Demo Mod active. Type #demo for commands.")
end)

register("on_areachange", function()
    local ox, oy = client.get_origin()
    client.note(string.format("Area changed! New origin: %d, %d", ox, oy))
end)

register("on_before_reload", function()
    client.note("Demo Mod: Preparing for hot-reload...")
end)

register("on_after_reload", function()
    client.note("Demo Mod: Hot-reload complete!")
    Demo.initialized = true
end)

-- ============================================================================
-- UPDATE CALLBACKS
-- ============================================================================

register("on_tick", function()
    Demo.tick_count = Demo.tick_count + 1

    -- Calculate TPS every second
    if Demo.tick_count % C.TICKS == 0 then
        local current_time = os.clock()
        if Demo.last_tick_time > 0 then
            local elapsed = current_time - Demo.last_tick_time
            if elapsed > 0 then
                Demo.ticks_per_second = math.floor(C.TICKS / elapsed)
            end
        end
        Demo.last_tick_time = current_time
    end
end)

register("on_frame", function()
    Demo.frame_count = Demo.frame_count + 1

    -- Render enabled overlays
    if Demo.show_overlay then render_main_overlay() end
    if Demo.show_stats then render_stats_panel() end
    if Demo.show_mouse_tracker then render_mouse_tracker() end
    if Demo.show_map_info then render_map_info() end
    if Demo.show_nearby_players then render_nearby_players() end
end)

-- ============================================================================
-- INPUT CALLBACKS
-- ============================================================================

register("on_mouse_move", function(x, y)
    Demo.last_mouse_x = x
    Demo.last_mouse_y = y
    return 0  -- Don't consume
end)

register("on_mouse_click", function(x, y, button)
    Demo.mouse_clicks = Demo.mouse_clicks + 1
    if Demo.show_mouse_tracker then
        client.note(string.format("Click at (%d, %d) button=%d", x, y, button))
    end
    return 0  -- Don't consume (1=consume, -1=consume but allow others)
end)

register("on_keydown", function(key)
    Demo.keys_pressed[key] = true
    Demo.last_key = key

    -- F9 toggles main overlay
    if key == 290 then
        Demo.show_overlay = not Demo.show_overlay
        client.addline("Overlay: " .. (Demo.show_overlay and "ON" or "OFF"))
        return 1  -- Consume
    end

    -- F10 toggles stats
    if key == 291 then
        Demo.show_stats = not Demo.show_stats
        client.addline("Stats panel: " .. (Demo.show_stats and "ON" or "OFF"))
        return 1  -- Consume
    end

    return 0  -- Don't consume
end)

register("on_keyup", function(key)
    Demo.keys_pressed[key] = false
    return 0
end)

register("on_client_cmd", function(cmd)
    return handle_demo_commands(cmd)
end)

-- ============================================================================
-- EXPORT
-- ============================================================================

MOD = MOD or {}
MOD.demo = Demo
