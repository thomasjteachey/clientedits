-- BPlusItemQualityColors.lua
--
-- Two jobs, both about item quality colour.
--
-- 1. FILL THE GAP BLIZZARD LEFT. UIParent.lua builds ITEM_QUALITY_COLORS with
--    "for i = -1, 6", so heirloom (7) has no entry at all even though heirlooms
--    exist in 3.3.5. Anything that indexes the table by an item's quality -
--    LootFrame, the loot roll popups, the bind confirmation - then indexes nil
--    and throws. That is what the old CENTURION_HeirloomColorFix addon was for.
--
-- 2. COLOUR ITEM LINKS IN CHAT. A link carries its colour inside it, decided by
--    whoever built the string. Links the server sends, and links built before a
--    quality's colour was known, arrive wearing the wrong one. A chat filter
--    rewrites the colour of every item link to match the item's actual quality,
--    so a link looks the same as the item does everywhere else.
--
-- The values are read from the client with GetItemQualityColor rather than
-- copied in as numbers, so nothing here can drift from what the rest of the UI
-- draws. QUALITY_OVERRIDES is the one place that deliberately disagrees.

local QUALITY_ARTIFACT = 6
local QUALITY_HEIRLOOM = 7

-- Deliberate disagreements with the client's own palette.
--
-- Artifact is the realm's marker for temporal field-kit gear and nothing else
-- uses it, so it is worth making unmistakable. The client paints artifact the
-- same pale gold as heirloom, which is the one colour it should not share with
-- anything - a loaner that is destroyed on replacement should not look like the
-- rarest thing in the game. Set to nil to take the client's colour back.
local QUALITY_OVERRIDES = {
    [QUALITY_ARTIFACT] = { r = 0.90, g = 0.27, b = 0.27 },   -- e64545, plainly red
}

-- Blizzard's convention: the `hex` field of an ITEM_QUALITY_COLORS entry is a
-- COMPLETE escape and carries its own "|c" - that is what GetItemQualityColor
-- returns and what every consumer expects, since the usual idiom is
-- `hex .. text .. FONT_COLOR_CODE_CLOSE`. Building it without the "|c" left the
-- overridden qualities in a different shape from the stock ones, which is what
-- made the colouring look inconsistent depending on the item.
--
-- Rounded rather than truncated: 0.90 * 255 is 229.5, and truncating gave e5
-- where this file documents e6.
local function BuildHex(r, g, b)
    return string.format("|cff%02x%02x%02x",
        math.floor(r * 255 + 0.5), math.floor(g * 255 + 0.5), math.floor(b * 255 + 0.5))
end

local function ApplyQualityColor(quality)
    if type(ITEM_QUALITY_COLORS) ~= "table" then
        ITEM_QUALITY_COLORS = {}
    end
    if type(ITEM_QUALITY_COLORS[quality]) ~= "table" then
        ITEM_QUALITY_COLORS[quality] = {}
    end

    local entry = ITEM_QUALITY_COLORS[quality]
    local override = QUALITY_OVERRIDES[quality]
    if override then
        entry.r, entry.g, entry.b = override.r, override.g, override.b
    else
        -- Ask the client rather than hardcoding: whatever it draws elsewhere is
        -- by definition the right answer for the entries we are only filling in.
        local r, g, b = GetItemQualityColor(quality)
        if not r then
            return
        end
        entry.r, entry.g, entry.b = r, g, b
    end
    entry.hex = BuildHex(entry.r, entry.g, entry.b)
end

local function ApplyAllQualityColors()
    -- Heirloom is the missing one; artifact is present but overridden above.
    -- Anything else the client grows later is picked up by widening this list.
    ApplyQualityColor(QUALITY_HEIRLOOM)
    ApplyQualityColor(QUALITY_ARTIFACT)
end

-- Recolour item links inside a chat message.
--
-- An item link is |cAARRGGBB|Hitem:id:...|h[Name]|h|r. Only the colour is
-- replaced; the hyperlink body is left exactly as it came in, so the link keeps
-- working even if the rewrite is wrong about the colour.
--
-- GetItemInfo returns nil for an item the client has never seen. That is normal
-- rather than an error, and the link is left alone until the item is cached.
local function RecolorItemLinks(message)
    if not message or not string.find(message, "|Hitem:", 1, true) then
        return message
    end

    return (string.gsub(message, "|c%x%x%x%x%x%x%x%x(|Hitem:(%d+):[^|]*|h)", function(link, itemId)
        local _, _, quality = GetItemInfo(tonumber(itemId))
        if not quality then
            return nil                      -- not cached: leave the link untouched
        end
        local color = ITEM_QUALITY_COLORS[quality]
        if not color or not color.hex then
            return nil
        end
        -- hex already carries its own "|c". Adding another emitted "|c|cffxxxxxx",
        -- which the client draws as a literal "|c" in front of the link.
        return color.hex .. link
    end))
end

local function ChatFilter(_, _, message, ...)
    return false, RecolorItemLinks(message), ...
end

local CHAT_EVENTS = {
    "CHAT_MSG_SAY", "CHAT_MSG_YELL", "CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER",
    "CHAT_MSG_GUILD", "CHAT_MSG_OFFICER", "CHAT_MSG_RAID", "CHAT_MSG_RAID_LEADER",
    "CHAT_MSG_RAID_WARNING", "CHAT_MSG_WHISPER", "CHAT_MSG_WHISPER_INFORM",
    "CHAT_MSG_BN_WHISPER", "CHAT_MSG_BN_WHISPER_INFORM", "CHAT_MSG_CHANNEL",
    "CHAT_MSG_LOOT", "CHAT_MSG_SYSTEM", "CHAT_MSG_BATTLEGROUND",
    "CHAT_MSG_BATTLEGROUND_LEADER", "CHAT_MSG_ACHIEVEMENT",
}

local function InstallChatFilter()
    if not ChatFrame_AddMessageEventFilter then
        return
    end
    for _, event in ipairs(CHAT_EVENTS) do
        ChatFrame_AddMessageEventFilter(event, ChatFilter)
    end
end

-- VARIABLES_LOADED is late enough that ITEM_QUALITY_COLORS and the chat filter
-- API both exist. The immediate call covers the case where this file is loaded
-- after UIParent has already run, which is how it behaves as a FrameXML module.
local frame = CreateFrame("Frame")
frame:RegisterEvent("VARIABLES_LOADED")
frame:SetScript("OnEvent", function()
    ApplyAllQualityColors()
    InstallChatFilter()
end)

if ITEM_QUALITY_COLORS then
    ApplyAllQualityColors()
end
