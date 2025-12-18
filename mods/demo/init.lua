--[[
    Comprehensive Demo Mod for Astonia Client

    This mod demonstrates ALL available Lua API features.
    Use #demo in chat to see available commands.

    Overlays (toggle with commands or F9/F10):
    ==========================================
    #demo_overlay (F9) - Player basics: username, world position, tick, FPS, HP/MP bars
    #demo_stats (F10)  - Character sheet: stats with buff colors, resources, exp bar, gold
    #demo_mouse        - Coordinate conversion: screen->local->world, cursor crosshair
    #demo_map          - World scanner: tile under cursor, items on ground nearby
    #demo_players      - Character scanner: scan map tiles for NPCs/players, target brackets
    #demo_select       - Target info: selection details, look/inspect, equipment check
    #demo_skills       - Skills & items: raise costs, containers, quest progress, inventory

    API Features Demonstrated:
    ==========================
    - All callbacks (init, tick, frame, input, etc.)
    - Rendering (text, rectangles, lines, pixels, colors)
    - Game data access (HP, mana, stats, inventory, map tiles)
    - Coordinate conversion (screen <-> map <-> world)
    - Character/NPC scanning via map tiles
    - Selection and targeting system
    - Look/inspect system with equipment
    - Container and shop access
    - Quest status tracking
    - Skill info and raise costs
    - GUI positioning helpers
    - Custom chat commands
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
      client.get_origin() -> x, y (map origin)
      client.get_mouse() -> x, y (screen coords)
      client.stom(scrx, scry) -> mapx, mapy (screen to map)
      client.mtos(mapx, mapy) -> scrx, scry (map to screen)
      client.get_world_pos(mapx, mapy) -> worldx, worldy (like right-click coords)
      client.get_plrmn() -> mn (player map index)
      client.get_player_world_pos() -> worldx, worldy
      client.get_value(type, idx) -> value (type: 0=modified, 1=base)
      client.get_item(slot) -> sprite
      client.get_item_flags(slot) -> flags
      client.get_map_tile(x, y) -> {gsprite, fsprite, isprite, csprite, cn, flags, health}
      client.get_player(idx) -> {name, sprite, level, clan, pk_status}

    Selection & Targeting:
      client.get_chrsel() -> mn or nil (selected character)
      client.get_itmsel() -> mn or nil (selected item)
      client.get_mapsel() -> mn or nil (selected map tile)
      client.get_action() -> {act, x, y} (current action)

    Look/Inspect:
      client.get_look_name() -> string (inspected target name)
      client.get_look_desc() -> string (inspected target description)
      client.get_lookinv(slot) -> sprite (look equipment)

    Container:
      client.get_con_type() -> 0=none, 1=container, 2=shop
      client.get_con_name() -> string
      client.get_con_cnt() -> item count
      client.get_container(slot) -> sprite

    Player State:
      client.get_pspeed() -> 0=normal, 1=fast, 2=stealth
      client.get_mil_exp() -> military exp
      client.get_mil_rank([exp]) -> rank number

    Clipboard:
      client.set_clipboard(text) -> success (boolean)
      client.get_clipboard() -> text or nil

    Skills:
      client.get_skill_name(idx) -> string
      client.get_skill_desc(idx) -> string
      client.get_skill_info(idx) -> {name, base1, base2, base3, cost, start}
      client.get_raise_cost(skill, level) -> exp cost

    Quests:
      client.get_quest_count() -> number of quest definitions
      client.get_quest_status(idx) -> {done, flags}
      client.get_quest_info(idx) -> {name, minlevel, maxlevel, giver, area, exp, flags}

    GUI Helpers:
      client.dotx(idx), client.doty(idx) - Screen anchor points
      client.butx(idx), client.buty(idx) - Button positions

    Utilities:
      client.exp2level(exp) -> level
      client.level2exp(level) -> exp
      client.cmd_text(text) - Send text as player input

    Constants (C table):
      Primary: C.V_HP, C.V_MANA, C.V_ENDURANCE, C.V_STR, C.V_AGI, C.V_INT, C.V_WIS
      Combat: C.V_ARMOR, C.V_WEAPON, C.V_LIGHT, C.V_SPEED
      Weapons: C.V_PULSE, C.V_DAGGER, C.V_HAND, C.V_STAFF, C.V_SWORD, C.V_TWOHAND
      Skills: C.V_ARMORSKILL, C.V_ATTACK, C.V_PARRY, C.V_WARCRY, C.V_TACTICS,
              C.V_SURROUND, C.V_BODYCONTROL, C.V_SPEEDSKILL
      Utility: C.V_BARTER, C.V_PERCEPT, C.V_STEALTH
      Magic: C.V_BLESS, C.V_HEAL, C.V_FREEZE, C.V_MAGICSHIELD, C.V_FLASH,
             C.V_FIREBALL, C.V_REGENERATE, C.V_MEDITATE, C.V_IMMUNITY
      Other: C.V_DEMON, C.V_DURATION, C.V_RAGE, C.V_COLD, C.V_PROFESSION
      UI: C.DOT_TL, C.DOT_BR, C.DOT_INV, C.DOT_SKL, C.DOT_TXT, C.DOT_MCT, C.DOT_TOP, C.DOT_BOT
      Map: C.MAPDX, C.MAPDY, C.DIST, C.MAXCHARS, C.INVENTORYSIZE, C.CONTAINERSIZE, C.TICKS
      Limits: C.V_MAX, C.MAXQUEST, C.MAXMN
      Quest flags: C.QF_OPEN, C.QF_DONE
      Speed: C.SPEED_NORMAL (0), C.SPEED_FAST (1), C.SPEED_STEALTH (2)

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
    show_selection = false,
    show_skills = false,

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

    -- Stats panel page (0-3)
    stats_page = 0,
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

-- Main overlay: Player basics with FPS counter and resources
local function render_main_overlay()
    local x = client.dotx(C.DOT_TL) + 10
    local y = client.doty(C.DOT_TL) + 10

    draw_panel(x, y, 220, 190, "Demo Mod (F9)")

    local text_y = y + 18

    -- Username and level
    local username = client.get_username()
    local exp = client.get_experience()
    local level = client.exp2level(exp)
    client.render_text(x + 4, text_y, colors.white, 0, string.format("%s (Lv.%d)", username, level))
    text_y = text_y + 12

    -- World position (demonstrates get_player_world_pos)
    local pwx, pwy = client.get_player_world_pos()
    client.render_text(x + 4, text_y, custom_colors.cyan, 0, string.format("Position: %d, %d", pwx, pwy))
    text_y = text_y + 12

    -- Game tick + approximate FPS
    local tick = client.get_tick()
    local fps = math.floor(Demo.frame_count / math.max(1, Demo.tick_count / C.TICKS))
    client.render_text(x + 4, text_y, colors.text, 0, string.format("Tick: %d  ~%d FPS", tick, fps))
    text_y = text_y + 14

    -- HP Bar
    local hp = client.get_hp()
    local max_hp = client.get_value(0, C.V_HP)
    client.render_text(x + 4, text_y, colors.health, 0, string.format("HP: %d/%d", hp, max_hp))
    draw_bar(x + 75, text_y, 135, 10, hp, max_hp, colors.health)
    text_y = text_y + 14

    -- Mana Bar
    local mana = client.get_mana()
    local max_mana = client.get_value(0, C.V_MANA)
    client.render_text(x + 4, text_y, colors.mana, 0, string.format("MP: %d/%d", mana, max_mana))
    draw_bar(x + 75, text_y, 135, 10, mana, max_mana, colors.mana)
    text_y = text_y + 14

    -- Experience progress bar
    local curr_level_exp = client.level2exp(level)
    local next_level_exp = client.level2exp(level + 1)
    local exp_progress = exp - curr_level_exp
    local exp_needed = next_level_exp - curr_level_exp
    client.render_text(x + 4, text_y, custom_colors.purple, 0, "EXP:")
    draw_bar(x + 35, text_y, 175, 10, exp_progress, exp_needed, custom_colors.purple)
    text_y = text_y + 14

    -- Resources row: Rage, Endurance, Lifeshield
    local rage = client.get_rage()
    local endurance = client.get_endurance()
    local lifeshield = client.get_lifeshield()
    client.render_text(x + 4, text_y, colors.red, 0, string.format("Rage:%d", rage))
    client.render_text(x + 70, text_y, custom_colors.orange, 0, string.format("End:%d", endurance))
    client.render_text(x + 135, text_y, custom_colors.cyan, 0, string.format("Shield:%d", lifeshield))
    text_y = text_y + 14

    -- Gold, mil rank, speed
    local gold = client.get_gold()
    local mil_rank = client.get_mil_rank()
    local pspeed = client.get_pspeed()
    local speed_names = {[0] = "Normal", [1] = "Fast", [2] = "Stealth"}
    local speed_colors = {[0] = colors.text, [1] = colors.green, [2] = custom_colors.purple}

    client.render_text(x + 4, text_y, custom_colors.yellow, 0, string.format("Gold: %d", gold))
    client.render_text(x + 90, text_y, custom_colors.cyan, 0, string.format("Mil:%d", mil_rank))
    client.render_text(x + 140, text_y, speed_colors[pspeed] or colors.text, 0, speed_names[pspeed] or "?")
end

-- Stats panel: Detailed stat breakdown with pages
-- All stat definitions organized by category
local stat_pages = {
    {
        name = "Attributes",
        stats = {
            {name = "Hit Points", idx = C.V_HP},
            {name = "Endurance", idx = C.V_ENDURANCE},
            {name = "Mana", idx = C.V_MANA},
            {name = "Wisdom", idx = C.V_WIS},
            {name = "Intelligence", idx = C.V_INT},
            {name = "Agility", idx = C.V_AGI},
            {name = "Strength", idx = C.V_STR},
            {name = "Armor Value", idx = C.V_ARMOR},
            {name = "Weapon Value", idx = C.V_WEAPON},
            {name = "Speed", idx = C.V_SPEED},
        }
    },
    {
        name = "Combat Skills",
        stats = {
            {name = "Attack", idx = C.V_ATTACK},
            {name = "Parry", idx = C.V_PARRY},
            {name = "Tactics", idx = C.V_TACTICS},
            {name = "Warcry", idx = C.V_WARCRY},
            {name = "Surround Hit", idx = C.V_SURROUND},
            {name = "Body Control", idx = C.V_BODYCONTROL},
            {name = "Speed Skill", idx = C.V_SPEEDSKILL},
            {name = "Armor Skill", idx = C.V_ARMORSKILL},
        }
    },
    {
        name = "Weapon Skills",
        stats = {
            {name = "Dagger", idx = C.V_DAGGER},
            {name = "Hand to Hand", idx = C.V_HAND},
            {name = "Staff", idx = C.V_STAFF},
            {name = "Sword", idx = C.V_SWORD},
            {name = "Two-Handed", idx = C.V_TWOHAND},
        }
    },
    {
        name = "Magic",
        stats = {
            {name = "Bless", idx = C.V_BLESS},
            {name = "Heal", idx = C.V_HEAL},
            {name = "Freeze", idx = C.V_FREEZE},
            {name = "Magic Shield", idx = C.V_MAGICSHIELD},
            {name = "Flash", idx = C.V_FLASH},
            {name = "Fireball", idx = C.V_FIREBALL},
            {name = "Pulse", idx = C.V_PULSE},
            {name = "Immunity", idx = C.V_IMMUNITY},
            {name = "Duration", idx = C.V_DURATION},
        }
    },
    {
        name = "Utility",
        stats = {
            {name = "Barter", idx = C.V_BARTER},
            {name = "Perception", idx = C.V_PERCEPT},
            {name = "Stealth", idx = C.V_STEALTH},
            {name = "Regenerate", idx = C.V_REGENERATE},
            {name = "Meditate", idx = C.V_MEDITATE},
        }
    },
}

-- Check if a page has any visible skills (base > 0, or Attributes page which always shows)
local function page_has_skills(page_idx)
    if page_idx == 1 then return true end  -- Attributes always visible
    local page = stat_pages[page_idx]
    if not page then return false end
    for _, stat in ipairs(page.stats) do
        if stat.idx then
            local base = client.get_value(1, stat.idx)
            if base > 0 then return true end
        end
    end
    return false
end

-- Get list of visible page indices
local function get_visible_pages()
    local visible = {}
    for i = 1, #stat_pages do
        if page_has_skills(i) then
            table.insert(visible, i)
        end
    end
    return visible
end

-- Generate full stats text for clipboard export
local function generate_stats_text()
    local lines = {}
    local username = client.get_username()
    local exp = client.get_experience()
    local level = client.exp2level(exp)

    table.insert(lines, string.format("=== %s (Level %d) ===", username, level))
    table.insert(lines, string.format("Experience: %d", exp))
    table.insert(lines, string.format("Gold: %d | Military Rank: %d", client.get_gold(), client.get_mil_rank()))
    table.insert(lines, "")

    for page_idx, page in ipairs(stat_pages) do
        local page_lines = {}
        for _, stat in ipairs(page.stats) do
            if stat.idx then
                local base = client.get_value(1, stat.idx)  -- 1 = base/trained
                -- Skip skills with 0 base (except for Attributes page)
                if base > 0 or page_idx == 1 then
                    local mod = client.get_value(0, stat.idx)   -- 0 = modified (with gear/buffs)
                    local diff = mod - base
                    local diff_str = diff > 0 and string.format(" (+%d from gear/buffs)", diff) or (diff < 0 and string.format(" (%d)", diff) or "")
                    table.insert(page_lines, string.format("  %s: %d (base: %d)%s", stat.name, mod, base, diff_str))
                end
            end
        end
        -- Only add page header if there are skills to show
        if #page_lines > 0 then
            table.insert(lines, "--- " .. page.name .. " ---")
            for _, line in ipairs(page_lines) do
                table.insert(lines, line)
            end
            table.insert(lines, "")
        end
    end

    return table.concat(lines, "\n")
end

local function render_stats_panel()
    local x = client.dotx(C.DOT_BR) - 240
    local y = client.doty(C.DOT_TL) + 10

    -- Get visible pages and find current position
    local visible = get_visible_pages()
    if #visible == 0 then return end  -- No pages to show (shouldn't happen)

    -- Ensure stats_page points to a visible page
    local current_visible_idx = nil
    for i, page_idx in ipairs(visible) do
        if page_idx == Demo.stats_page + 1 then
            current_visible_idx = i
            break
        end
    end
    -- If current page not visible, reset to first visible
    if not current_visible_idx then
        Demo.stats_page = visible[1] - 1
        current_visible_idx = 1
    end

    local page = stat_pages[Demo.stats_page + 1]

    draw_panel(x, y, 230, 185, string.format("Stats: %s (F10)", page.name))

    local text_y = y + 18

    -- Navigation hint (show position among visible pages only)
    client.render_text(x + 4, text_y, custom_colors.cyan, 0, string.format("Page %d/%d", current_visible_idx, #visible))
    client.render_text(x + 75, text_y, colors.text, 0, ", . / c=Copy")
    text_y = text_y + 14

    -- Column headers: Base = trained, Mod = with gear/buffs
    client.render_text(x + 4, text_y, colors.text, 0, "Stat")
    client.render_text(x + 115, text_y, colors.text, 0, "Base")
    client.render_text(x + 155, text_y, colors.text, 0, "Mod")
    client.render_text(x + 195, text_y, colors.text, 0, "Gear")
    text_y = text_y + 12

    -- Separator line
    client.render_line(x + 4, text_y, x + 226, text_y, custom_colors.panel_border)
    text_y = text_y + 4

    -- Stat rows (max 10 visible)
    local max_display = 10
    local count = 0

    for _, stat in ipairs(page.stats) do
        if count >= max_display then break end
        if stat.idx then
            local base = client.get_value(1, stat.idx)  -- 1 = base/trained value

            -- Skip skills with 0 base (character doesn't have access)
            -- Exception: always show attributes page since they're core stats
            if base == 0 and Demo.stats_page > 0 then
                -- Skip this skill
            else
                local mod = client.get_value(0, stat.idx)   -- 0 = modified (with gear/buffs)
                local diff = mod - base                      -- positive = gear bonus

                -- Stat name (truncate if needed)
                local name = stat.name
                if string.len(name) > 14 then
                    name = string.sub(name, 1, 12) .. ".."
                end
                client.render_text(x + 4, text_y, colors.text, 0, name)

                -- Base value (what you trained)
                client.render_text(x + 115, text_y, colors.text, 0, tostring(base))

                -- Modified value (colored if different from base)
                local mod_color = diff > 0 and colors.green or (diff < 0 and colors.red or colors.text)
                client.render_text(x + 155, text_y, mod_color, 0, tostring(mod))

                -- Gear bonus (difference from base)
                if diff ~= 0 then
                    local diff_color = diff > 0 and colors.green or colors.red
                    local diff_str = diff > 0 and string.format("+%d", diff) or tostring(diff)
                    client.render_text(x + 195, text_y, diff_color, 0, diff_str)
                end

                text_y = text_y + 12
                count = count + 1
            end
        end
    end
end

-- Mouse tracker: Coordinate conversion demo with cursor info
local function render_mouse_tracker()
    local mx, my = client.get_mouse()
    local mapx, mapy = client.stom(mx, my)

    -- Crosshair at cursor
    if mx > 20 and my > 20 then
        client.render_line(mx - 8, my, mx + 8, my, colors.green)
        client.render_line(mx, my - 8, mx, my + 8, colors.green)

        -- Diamond pattern using pixels
        for i = -3, 3 do
            local dist = 3 - math.abs(i)
            client.render_pixel(mx + i * 4, my - dist * 4, custom_colors.cyan)
            client.render_pixel(mx + i * 4, my + dist * 4, custom_colors.cyan)
        end
    end

    -- Info panel (fixed position for stability)
    local px, py = client.dotx(C.DOT_TL) + 10, client.doty(C.DOT_TL) + 130
    draw_panel(px, py, 160, 86, "Cursor Info")

    local text_y = py + 18
    client.render_text(px + 4, text_y, colors.text, 0, string.format("Screen: %d, %d", mx, my))
    text_y = text_y + 12

    if mapx then
        client.render_text(px + 4, text_y, custom_colors.cyan, 0, string.format("Local: %d, %d", mapx, mapy))
        text_y = text_y + 12
        local worldx, worldy = client.get_world_pos(mapx, mapy)
        client.render_text(px + 4, text_y, custom_colors.yellow, 0, string.format("World: %d, %d", worldx, worldy))
    else
        client.render_text(px + 4, text_y, colors.red, 0, "(outside game area)")
    end
    text_y = text_y + 14
    client.render_text(px + 4, text_y, colors.text, 0, string.format("Clicks: %d  Key: %d", Demo.mouse_clicks, Demo.last_key))
end

-- World info: Tile under cursor + items on ground nearby
local function render_map_info()
    local x = 10
    local y = client.doty(C.DOT_BOT) - 140

    draw_panel(x, y, 180, 130, "World Scanner")

    local text_y = y + 18

    -- Get tile under mouse cursor
    local mx, my = client.get_mouse()
    local mapx, mapy = client.stom(mx, my)

    if mapx then
        local worldx, worldy = client.get_world_pos(mapx, mapy)
        client.render_text(x + 4, text_y, custom_colors.yellow, 0, string.format("Cursor: %d, %d", worldx, worldy))
        text_y = text_y + 12

        local tile = client.get_map_tile(mapx, mapy)
        if tile then
            client.render_text(x + 4, text_y, colors.text, 0, string.format("Ground: %d  Fore: %d", tile.gsprite, tile.fsprite))
            text_y = text_y + 12
            if tile.isprite > 0 then
                client.render_text(x + 4, text_y, colors.green, 0, string.format("Item on tile: %d", tile.isprite))
            elseif tile.csprite > 0 then
                client.render_text(x + 4, text_y, custom_colors.orange, 0, string.format("Character: cn=%d", tile.cn))
            else
                client.render_text(x + 4, text_y, colors.text, 0, "(empty tile)")
            end
        end
    else
        client.render_text(x + 4, text_y, colors.text, 0, "Move cursor over game")
    end

    text_y = text_y + 14

    -- Scan for items on ground nearby
    client.render_text(x + 4, text_y, custom_colors.cyan, 0, "Items on ground:")
    text_y = text_y + 12

    local items_found = 0
    local player_mn = client.get_plrmn()
    local pmx = player_mn % C.MAPDX
    local pmy = math.floor(player_mn / C.MAPDX)

    for dy = -5, 5 do
        for dx = -5, 5 do
            local tx, ty = pmx + dx, pmy + dy
            if tx >= 0 and tx < C.MAPDX and ty >= 0 and ty < C.MAPDY then
                local t = client.get_map_tile(tx, ty)
                if t and t.isprite > 0 then
                    items_found = items_found + 1
                end
            end
        end
    end

    if items_found > 0 then
        client.render_text(x + 4, text_y, colors.green, 0, string.format("%d items within 5 tiles", items_found))
    else
        client.render_text(x + 4, text_y, colors.text, 0, "None nearby")
    end
end

-- Shared character data for multiple render functions
local nearby_chars_cache = {}

-- Scan and cache nearby characters (called once per frame)
local function scan_nearby_characters()
    nearby_chars_cache = {}

    local player_mn = client.get_plrmn()
    local player_mx = player_mn % C.MAPDX
    local player_my = math.floor(player_mn / C.MAPDX)

    -- Get selected character for highlighting
    local chrsel = client.get_chrsel()
    local selected_cn = nil
    if chrsel then
        local sel_tile = client.get_map_tile(chrsel % C.MAPDX, math.floor(chrsel / C.MAPDX))
        if sel_tile then selected_cn = sel_tile.cn end
    end

    for my = 0, C.MAPDY - 1 do
        for mx = 0, C.MAPDX - 1 do
            local tile = client.get_map_tile(mx, my)
            if tile and tile.csprite > 0 then
                local dist = math.abs(mx - player_mx) + math.abs(my - player_my)

                if dist > 0 then
                    local p = client.get_player(tile.cn)
                    local scrx, scry = client.mtos(mx, my)

                    table.insert(nearby_chars_cache, {
                        mx = mx,
                        my = my,
                        cn = tile.cn,
                        sprite = tile.csprite,
                        health = tile.health,
                        dist = dist,
                        name = p and p.name or nil,
                        level = p and p.level or nil,
                        pk_status = p and p.pk_status or 0,
                        scrx = scrx,
                        scry = scry,
                        is_selected = (tile.cn == selected_cn),
                        is_player = (p and p.name and p.name ~= "")
                    })
                end
            end
        end
    end

    table.sort(nearby_chars_cache, function(a, b) return a.dist < b.dist end)
end

-- Get color for a character based on status
local function get_char_color(char)
    if char.pk_status == 1 then
        return colors.red
    elseif char.pk_status == 2 then
        return custom_colors.orange
    elseif char.health < 30 then
        return colors.red
    elseif char.health < 70 then
        return custom_colors.yellow
    else
        return colors.green
    end
end

-- Draw world markers (target brackets, decimal level next to roman numerals)
local function render_world_markers()
    for _, char in ipairs(nearby_chars_cache) do
        if char.scrx and char.scry then
            local color = get_char_color(char)

            -- Selected target: animated bracket highlight
            if char.is_selected then
                local s = 8
                local ox, oy = char.scrx, char.scry - 30

                -- Animated pulse
                local pulse = math.sin(os.clock() * 6) * 2
                s = s + pulse

                -- Corner brackets
                client.render_line(ox - s, oy - s, ox - s + 4, oy - s, colors.white)
                client.render_line(ox - s, oy - s, ox - s, oy - s + 4, colors.white)

                client.render_line(ox + s, oy - s, ox + s - 4, oy - s, colors.white)
                client.render_line(ox + s, oy - s, ox + s, oy - s + 4, colors.white)

                client.render_line(ox - s, oy + s, ox - s + 4, oy + s, colors.white)
                client.render_line(ox - s, oy + s, ox - s, oy + s - 4, colors.white)

                client.render_line(ox + s, oy + s, ox + s - 4, oy + s, colors.white)
                client.render_line(ox + s, oy + s, ox + s, oy + s - 4, colors.white)
            end

            -- Add decimal level next to the roman numerals (offset to the right)
            if char.is_player and char.level and char.level > 0 then
                client.render_text(char.scrx + 18, char.scry - 44, custom_colors.cyan,
                    0, string.format("(%d)", char.level))
            end
        end
    end
end

-- Main nearby players panel
local function render_nearby_players()
    -- First scan for characters (updates cache)
    scan_nearby_characters()

    -- Draw world markers (target brackets, decimal levels)
    render_world_markers()

    -- Panel position
    local x = client.dotx(C.DOT_BR) - 200
    local y = client.doty(C.DOT_BOT) - 180

    draw_panel(x, y, 190, 170, "Nearby Characters")

    local text_y = y + 18
    local count = 0
    local max_display = 9

    for _, char in ipairs(nearby_chars_cache) do
        if count >= max_display then break end

        local color = get_char_color(char)

        -- Highlight selected character in the list
        if char.is_selected then
            client.render_rect(x + 2, text_y - 1, x + 188, text_y + 11, custom_colors.dark_gray)
        end

        -- Format display
        local display
        if char.name and char.name ~= "" then
            local name = char.name
            if string.len(name) > 11 then
                name = string.sub(name, 1, 11) .. ".."
            end
            if char.level and char.level > 0 then
                display = string.format("L%d %s", char.level, name)
            else
                display = name
            end
        else
            display = string.format("NPC #%d", char.cn)
        end

        client.render_text(x + 4, text_y, color, 0, display)
        client.render_text(x + 130, text_y, colors.text, 0,
            string.format("d:%d %d%%", char.dist, char.health))
        text_y = text_y + 14
        count = count + 1
    end

    if #nearby_chars_cache == 0 then
        client.render_text(x + 4, text_y, colors.text, 0, "No characters nearby")
    else
        text_y = text_y + 2
        -- Summary with player/NPC breakdown
        local players = 0
        local npcs = 0
        for _, c in ipairs(nearby_chars_cache) do
            if c.is_player then players = players + 1 else npcs = npcs + 1 end
        end
        client.render_text(x + 4, text_y, custom_colors.cyan, 0,
            string.format("Total: %d (%d players, %d NPCs)", #nearby_chars_cache, players, npcs))
    end
end

-- Target info: Selection, look info, and equipment preview
local function render_selection_info()
    local x = 10
    local y = client.doty(C.DOT_TL) + 130

    draw_panel(x, y, 200, 155, "Target Info")

    local text_y = y + 18

    -- Current action
    local action = client.get_action()
    local action_names = {[0] = "Idle", [1] = "Move", [2] = "Attack", [3] = "Take", [4] = "Use", [5] = "Give"}
    local action_name = action_names[action.act] or string.format("Act#%d", action.act)
    client.render_text(x + 4, text_y, custom_colors.orange, 0, string.format("Action: %s", action_name))
    if action.act > 0 then
        client.render_text(x + 100, text_y, colors.text, 0, string.format("@ %d,%d", action.x, action.y))
    end
    text_y = text_y + 14

    -- Selection info with details
    local chrsel = client.get_chrsel()
    local itmsel = client.get_itmsel()

    if chrsel then
        local sel_mx = chrsel % C.MAPDX
        local sel_my = math.floor(chrsel / C.MAPDX)
        local sel_tile = client.get_map_tile(sel_mx, sel_my)
        if sel_tile and sel_tile.cn > 0 then
            local p = client.get_player(sel_tile.cn)
            if p and p.name ~= "" then
                client.render_text(x + 4, text_y, colors.green, 0, string.format("Target: %s (L%d)", p.name, p.level))
            else
                client.render_text(x + 4, text_y, custom_colors.orange, 0, string.format("Target: NPC #%d", sel_tile.cn))
            end
            text_y = text_y + 12
            client.render_text(x + 4, text_y, colors.text, 0, string.format("Health: %d%%", sel_tile.health))
        end
    elseif itmsel then
        client.render_text(x + 4, text_y, custom_colors.yellow, 0, "Item selected on ground")
    else
        client.render_text(x + 4, text_y, colors.text, 0, "No target selected")
    end
    text_y = text_y + 14

    -- Look info (what you're inspecting)
    local look_name = client.get_look_name()
    local look_desc = client.get_look_desc()

    if look_name and look_name ~= "" then
        client.render_text(x + 4, text_y, colors.white, 0, "Inspecting:")
        text_y = text_y + 12
        client.render_text(x + 4, text_y, custom_colors.cyan, 0, string.sub(look_name, 1, 24))
        text_y = text_y + 12

        -- Show equipment slots if looking at a character (demonstrates get_lookinv)
        local has_equip = false
        local equip_str = "Gear: "
        for slot = 0, 5 do
            local sprite = client.get_lookinv(slot)
            if sprite and sprite > 0 then
                has_equip = true
            end
        end
        if has_equip then
            client.render_text(x + 4, text_y, colors.text, 0, "(Has equipped items)")
        elseif look_desc and look_desc ~= "" then
            -- Show truncated description
            client.render_text(x + 4, text_y, colors.text, 0, string.sub(look_desc, 1, 26))
        end
    else
        text_y = text_y + 12
        client.render_text(x + 4, text_y, colors.text, 0, "(Right-click to inspect)")
    end
end

-- Skills & Inventory: Skill costs, containers, quests, inventory preview
local function render_skills_info()
    local x = client.dotx(C.DOT_BR) - 200
    local y = client.doty(C.DOT_TL) + 210

    draw_panel(x, y, 190, 165, "Skills & Items")

    local text_y = y + 18

    -- Show skill raise costs (demonstrates get_skill_info, get_raise_cost)
    client.render_text(x + 4, text_y, custom_colors.cyan, 0, "Next skill raise cost:")
    text_y = text_y + 12

    local skills_to_show = {{name = "STR", idx = C.V_STR}, {name = "WIS", idx = C.V_WIS}}
    for _, skill in ipairs(skills_to_show) do
        local curr = client.get_value(1, skill.idx)
        local cost = client.get_raise_cost(skill.idx, curr + 1)
        client.render_text(x + 4, text_y, colors.text, 0, string.format("%s %d->%d:", skill.name, curr, curr + 1))
        client.render_text(x + 80, text_y, custom_colors.yellow, 0, string.format("%d exp", cost))
        text_y = text_y + 12
    end
    text_y = text_y + 4

    -- Container info
    local con_type = client.get_con_type()
    local con_cnt = client.get_con_cnt()
    if con_cnt > 0 then
        local con_name = client.get_con_name()
        local type_names = {[0] = "None", [1] = "Container", [2] = "Shop"}
        client.render_text(x + 4, text_y, colors.green, 0, string.format("%s: %s (%d)", type_names[con_type] or "?", string.sub(con_name, 1, 10), con_cnt))
    else
        client.render_text(x + 4, text_y, colors.text, 0, "No container open")
    end
    text_y = text_y + 14

    -- Quest progress (demonstrates get_quest_count, get_quest_status)
    local quest_count = client.get_quest_count()
    local completed = 0
    for i = 0, quest_count - 1 do
        local status = client.get_quest_status(i)
        if status and status.done then
            completed = completed + 1
        end
    end
    client.render_text(x + 4, text_y, custom_colors.purple, 0, string.format("Quests: %d/%d done", completed, quest_count))
    text_y = text_y + 14

    -- Inventory preview (demonstrates get_item, get_item_flags)
    client.render_text(x + 4, text_y, custom_colors.cyan, 0, "Inventory (first 8):")
    text_y = text_y + 12

    local inv_str = ""
    local filled = 0
    for slot = 0, 7 do
        local sprite = client.get_item(slot)
        if sprite and sprite > 0 then
            filled = filled + 1
            inv_str = inv_str .. "#"
        else
            inv_str = inv_str .. "."
        end
    end
    client.render_text(x + 4, text_y, colors.text, 0, string.format("[%s] %d items", inv_str, filled))
end

-- ============================================================================
-- COMMAND HANDLERS
-- ============================================================================

local function show_help()
    client.addline("=== Demo Mod Commands ===")
    client.addline("#demo_overlay  - Player info, HP/MP/EXP bars, resources (F9)")
    client.addline("#demo_stats    - All stats: base/mod/gear bonus (F10)")
    client.addline("                 , . to change page, c to copy")
    client.addline("#demo_mouse    - Cursor coordinates + crosshair")
    client.addline("#demo_map      - World scanner (tile info, items)")
    client.addline("#demo_players  - Nearby characters radar")
    client.addline("#demo_select   - Target info, inspect details")
    client.addline("#demo_skills   - Skills, quests, inventory")
    client.addline("#demo_all      - Enable all overlays")
    client.addline("#demo_off      - Disable all overlays")
    client.addline("#demo_info     - Show mod state info")
    client.addline("#demo_copy     - Copy all stats to clipboard")
    client.addline("#demo_say <msg>- Send chat message (cmd_text)")
    client.addline("#demo_test     - Run comprehensive API tests")
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
    if Demo.show_selection then table.insert(overlays, "select") end
    if Demo.show_skills then table.insert(overlays, "skills") end

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

    -- NEW: Test world coordinate functions
    local pwx, pwy = client.get_player_world_pos()
    test("get_player_world_pos() returns two numbers", type(pwx) == "number" and type(pwy) == "number")

    local plrmn = client.get_plrmn()
    test("get_plrmn() returns number", type(plrmn) == "number")

    local wx, wy = client.get_world_pos(10, 10)
    test("get_world_pos() returns two numbers", type(wx) == "number" and type(wy) == "number")

    -- NEW: Test selection functions
    local chrsel = client.get_chrsel()
    test("get_chrsel() returns number or nil", type(chrsel) == "number" or chrsel == nil)

    local itmsel = client.get_itmsel()
    test("get_itmsel() returns number or nil", type(itmsel) == "number" or itmsel == nil)

    local action = client.get_action()
    test("get_action() returns table", type(action) == "table")
    if action then
        test("action.act exists", action.act ~= nil)
        test("action.x exists", action.x ~= nil)
        test("action.y exists", action.y ~= nil)
    end

    -- NEW: Test look functions
    local look_name = client.get_look_name()
    test("get_look_name() returns string", type(look_name) == "string")

    local look_desc = client.get_look_desc()
    test("get_look_desc() returns string", type(look_desc) == "string")

    -- NEW: Test container functions
    test("get_con_type() returns number", type(client.get_con_type()) == "number")
    test("get_con_cnt() returns number", type(client.get_con_cnt()) == "number")
    test("get_con_name() returns string", type(client.get_con_name()) == "string")

    -- NEW: Test player state functions
    test("get_pspeed() returns number", type(client.get_pspeed()) == "number")
    test("get_mil_exp() returns number", type(client.get_mil_exp()) == "number")
    test("get_mil_rank() returns number", type(client.get_mil_rank()) == "number")

    -- NEW: Test skill functions
    local skill_info = client.get_skill_info(C.V_STR)
    test("get_skill_info() returns table or nil", type(skill_info) == "table" or skill_info == nil)
    if skill_info then
        test("skill_info.name exists", skill_info.name ~= nil)
        test("skill_info.cost exists", skill_info.cost ~= nil)
    end

    local skill_name = client.get_skill_name(C.V_STR)
    test("get_skill_name() returns string or nil", type(skill_name) == "string" or skill_name == nil)

    local raise_cost = client.get_raise_cost(C.V_STR, 10)
    test("get_raise_cost() returns number", type(raise_cost) == "number")

    -- NEW: Test quest functions
    test("get_quest_count() returns number", type(client.get_quest_count()) == "number")

    local quest_status = client.get_quest_status(0)
    test("get_quest_status() returns table or nil", type(quest_status) == "table" or quest_status == nil)

    -- NEW: Test new constants
    test("C.CONTAINERSIZE exists", C.CONTAINERSIZE ~= nil)
    test("C.V_MAX exists", C.V_MAX ~= nil)
    test("C.MAXQUEST exists", C.MAXQUEST ~= nil)
    test("C.MAXMN exists", C.MAXMN ~= nil)
    test("C.QF_OPEN exists", C.QF_OPEN ~= nil)
    test("C.QF_DONE exists", C.QF_DONE ~= nil)
    test("C.SPEED_NORMAL exists", C.SPEED_NORMAL ~= nil)

    -- Test new stat constants
    test("C.V_ARMOR exists", C.V_ARMOR ~= nil)
    test("C.V_ATTACK exists", C.V_ATTACK ~= nil)
    test("C.V_BLESS exists", C.V_BLESS ~= nil)
    test("C.V_TACTICS exists", C.V_TACTICS ~= nil)

    -- Test clipboard functions (only if available - requires client rebuild)
    if client.set_clipboard then
        test("set_clipboard exists", true)
        test("get_clipboard exists", client.get_clipboard ~= nil)
        local test_str = "Demo Mod Clipboard Test"
        local set_ok = client.set_clipboard(test_str)
        test("set_clipboard returns boolean", type(set_ok) == "boolean")
        if set_ok then
            local got_str = client.get_clipboard()
            test("get_clipboard returns set text", got_str == test_str)
        end
    else
        client.note("Clipboard functions not available - rebuild client to enable")
    end

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

    if cmd == "#demo_select" then
        Demo.show_selection = not Demo.show_selection
        client.addline("Selection info: " .. (Demo.show_selection and "ON" or "OFF"))
        return 1
    end

    if cmd == "#demo_skills" then
        Demo.show_skills = not Demo.show_skills
        client.addline("Skills info: " .. (Demo.show_skills and "ON" or "OFF"))
        return 1
    end

    if cmd == "#demo_all" then
        Demo.show_overlay = true
        Demo.show_stats = true
        Demo.show_mouse_tracker = true
        Demo.show_map_info = true
        Demo.show_nearby_players = true
        Demo.show_selection = true
        Demo.show_skills = true
        client.addline("All overlays enabled!")
        return 1
    end

    if cmd == "#demo_off" then
        Demo.show_overlay = false
        Demo.show_stats = false
        Demo.show_mouse_tracker = false
        Demo.show_map_info = false
        Demo.show_nearby_players = false
        Demo.show_selection = false
        Demo.show_skills = false
        client.addline("All overlays disabled.")
        return 1
    end

    if cmd == "#demo_info" then
        show_mod_info()
        return 1
    end

    if cmd == "#demo_copy" then
        local stats_text = generate_stats_text()
        if client.set_clipboard then
            if client.set_clipboard(stats_text) then
                client.addline("All stats copied to clipboard!")
            else
                client.addline("Failed to copy to clipboard")
            end
        else
            client.addline("Clipboard not available - rebuild client")
            client.note(stats_text)  -- Print to console instead
        end
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
    if Demo.show_selection then render_selection_info() end
    if Demo.show_skills then render_skills_info() end
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

    -- Stats panel navigation (only when stats panel is visible)
    if Demo.show_stats then
        local visible = get_visible_pages()
        if #visible > 0 then
            -- Find current position in visible pages
            local current_idx = 1
            for i, page_idx in ipairs(visible) do
                if page_idx == Demo.stats_page + 1 then
                    current_idx = i
                    break
                end
            end

            -- ',' key (44) - previous page
            if key == 44 then
                current_idx = current_idx - 1
                if current_idx < 1 then current_idx = #visible end
                Demo.stats_page = visible[current_idx] - 1
                return 1
            end
            -- '.' key (46) - next page
            if key == 46 then
                current_idx = current_idx + 1
                if current_idx > #visible then current_idx = 1 end
                Demo.stats_page = visible[current_idx] - 1
                return 1
            end
        end
        -- 'C' key (99) - copy stats to clipboard (lowercase c)
        if key == 99 then
            local stats_text = generate_stats_text()
            if client.set_clipboard then
                if client.set_clipboard(stats_text) then
                    client.addline("Stats copied to clipboard!")
                else
                    client.addline("Failed to copy to clipboard")
                end
            else
                client.addline("Clipboard not available - rebuild client")
            end
            return 1
        end
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
