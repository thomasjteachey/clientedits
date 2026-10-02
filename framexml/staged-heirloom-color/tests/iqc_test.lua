-- Heirloom link colour: reproduce "You receive item: ffe6cc80[Legionnaire Mark of Honor]."
-- then prove the fixed addon. Lua 5.3 via lua_run.py; globals BPLUS, ADDON_LIVE, ADDON_FIXED
-- are file paths.
--
-- Client facts mirrored here (Wow.exe 12340, verified by disassembly):
--   GetItemQualityColor 0x510E20 -> r,g,b from 0xAD2D84, hex from 0xAD2A84, index >= 8
--   (unsigned, so negatives too) falls back to 1. Qualities 6 and 7 are both e6cc80 and
--   hex INCLUDES "|c".
--   Stock UIParent.lua: for i = -1, 6 do ... .hex = GetItemQualityColor(i) end
--   Stock ChatFrame.lua filter loop: filter, newarg1.. = f(self, event, arg1..); if newarg1
--   then args = newargs.

local lines, fails = {}, 0
local function log(s) lines[#lines + 1] = s end
local function check(name, got, want)
    local ok = got == want
    if not ok then fails = fails + 1 end
    log(string.format("%s %s\n     got:  %s\n     want: %s", ok and "PASS" or "FAIL", name,
        tostring(got), tostring(want)))
end

local RGB = {
    [0] = {0x9d, 0x9d, 0x9d}, [1] = {0xff, 0xff, 0xff}, [2] = {0x1e, 0xff, 0x00},
    [3] = {0x00, 0x70, 0xdd}, [4] = {0xa3, 0x35, 0xee}, [5] = {0xff, 0x80, 0x00},
    [6] = {0xe6, 0xcc, 0x80}, [7] = {0xe6, 0xcc, 0x80},
}

local ITEMS = {
    [201448] = {"Legionnaire Mark of Honor", 7},   -- heirloom
    [300001] = {"Temporal Blade", 6},              -- artifact / field kit
    [40752]  = {"Warchief's Socks", 4},            -- epic, untouched by everything
}

-- Link exactly as the client builds it from its own table (so heirloom arrives e6cc80).
local function ClientLink(id)
    local item = ITEMS[id]
    local c = RGB[item[2]]
    return string.format("|cff%02x%02x%02x|Hitem:%d:0:0:0:0:0:0:0:80|h[%s]|h|r",
        c[1], c[2], c[3], id, item[1])
end

local function NewEnv()
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.tinsert = table.insert

    env.GetItemQualityColor = function(q)
        q = math.tointeger(q) or 1
        if q < 0 or q >= 8 then q = 1 end
        local c = RGB[q]
        return c[1] / 255, c[2] / 255, c[3] / 255, string.format("|cff%02x%02x%02x", c[1], c[2], c[3])
    end

    env.GetItemInfo = function(id)
        local item = ITEMS[tonumber(id)]
        if not item then return nil end
        return item[1], ClientLink(tonumber(id)), item[2]
    end

    env.__registry = {}
    env.CreateFrame = function()
        local frame = {scripts = {}}
        function frame:RegisterEvent(event)
            env.__registry[event] = env.__registry[event] or {}
            table.insert(env.__registry[event], self)
        end
        function frame:SetScript(kind, fn) self.scripts[kind] = fn end
        return frame
    end

    local chatFilters = {}
    env.ChatFrame_AddMessageEventFilter = function(event, filter)
        chatFilters[event] = chatFilters[event] or {}
        for _, f in ipairs(chatFilters[event]) do
            if f == filter then return end
        end
        table.insert(chatFilters[event], filter)
    end
    env.__RunChat = function(event, arg1)
        local args = {arg1, "", "", "", "", "", 0, 0, "", 0, 0, nil}
        for _, filterFunc in ipairs(chatFilters[event] or {}) do
            local res = {filterFunc({}, event, table.unpack(args, 1, 12))}
            if res[1] then return nil end
            if res[2] then args = {table.unpack(res, 2, 13)} end
        end
        return args[1]
    end
    return env
end

local function Run(env, path)
    local chunk, err = loadfile(path, "t", env)
    if not chunk then error(err) end
    chunk()
end

local function Fire(env, event, reverse)
    local list = env.__registry[event] or {}
    local first, last, step = 1, #list, 1
    if reverse then first, last, step = #list, 1, -1 end
    for i = first, last, step do
        local f = list[i]
        if f.scripts.OnEvent then f.scripts.OnEvent(f, event) end
    end
end

-- One client session: stock UIParent, then (B+/Centurion only) the FrameXML module,
-- then the addon, then VARIABLES_LOADED.
local function Session(opts)
    local env = NewEnv()
    local stock = load([[
        ITEM_QUALITY_COLORS = { };
        for i = -1, 6 do
            ITEM_QUALITY_COLORS[i] = { };
            ITEM_QUALITY_COLORS[i].r, ITEM_QUALITY_COLORS[i].g, ITEM_QUALITY_COLORS[i].b,
            ITEM_QUALITY_COLORS[i].hex = GetItemQualityColor(i);
        end
    ]], "=UIParent", "t", env)
    stock()
    if opts.frameXML then Run(env, BPLUS) end
    Run(env, opts.addon)
    Fire(env, "VARIABLES_LOADED", opts.reverse)
    return env
end

local function Loot(env, id)
    return env.__RunChat("CHAT_MSG_LOOT", "You receive item: " .. ClientLink(id) .. ".")
end

local HEIRLOOM_OUT = "You receive item: |cffe6cc80|Hitem:201448:0:0:0:0:0:0:0:80|h[Legionnaire Mark of Honor]|h|r."
local ARTIFACT_OUT = "You receive item: |cffe64545|Hitem:300001:0:0:0:0:0:0:0:80|h[Temporal Blade]|h|r."
local EPIC_OUT     = "You receive item: |cffa335ee|Hitem:40752:0:0:0:0:0:0:0:80|h[Warchief's Socks]|h|r."

-- 1. Reproduce the report on the live files (B+/Centurion chain).
log("== live addon + live FrameXML (B+/Centurion), events in registration order")
do
    local env = Session{frameXML = true, addon = ADDON_LIVE}
    check("REPRO heirloom loot line is the reported one", Loot(env, 201448),
        "You receive item: ffe6cc80|Hitem:201448:0:0:0:0:0:0:0:80|h[Legionnaire Mark of Honor]|h|r.")
    check("artifact still recoloured", Loot(env, 300001), ARTIFACT_OUT)
    check("epic untouched", Loot(env, 40752), EPIC_OUT)
end

-- 2. The fixed addon on the same chain, both dispatch orders.
for _, reverse in ipairs({false, true}) do
    log("== FIXED addon + live FrameXML (B+/Centurion), reverse=" .. tostring(reverse))
    local env = Session{frameXML = true, addon = ADDON_FIXED, reverse = reverse}
    check("heirloom loot line", Loot(env, 201448), HEIRLOOM_OUT)
    check("artifact loot line (only artifact recoloured)", Loot(env, 300001), ARTIFACT_OUT)
    check("epic loot line", Loot(env, 40752), EPIC_OUT)
    check("heirloom hex", env.ITEM_QUALITY_COLORS[7].hex, "|cffe6cc80")
    check("LOOT_BIND text for heirloom", env.ITEM_QUALITY_COLORS[7].hex .. "Legionnaire Mark of Honor|r",
        "|cffe6cc80Legionnaire Mark of Honor|r")
end

-- 3. Realms without the FrameXML module (L+ and the rest): the addon is the only filler.
log("== live addon alone (L+ etc.)")
do
    local env = Session{frameXML = false, addon = ADDON_LIVE}
    check("REPRO heirloom popup text broken there too", env.ITEM_QUALITY_COLORS[7].hex .. "X|r", "ffe6cc80X|r")
end
log("== FIXED addon alone (L+ etc.)")
do
    local env = Session{frameXML = false, addon = ADDON_FIXED}
    local e = env.ITEM_QUALITY_COLORS[7]
    check("heirloom entry exists", type(e), "table")
    check("heirloom hex", e.hex, "|cffe6cc80")
    check("heirloom rgb", string.format("%.4f %.4f %.4f", e.r, e.g, e.b),
        string.format("%.4f %.4f %.4f", 230 / 255, 204 / 255, 128 / 255))
    check("no chat filter there, heirloom link untouched", Loot(env, 201448), HEIRLOOM_OUT)
end

OUT = table.concat(lines, "\n") .. "\nfail=" .. fails
