-- CENTURION_HeirloomColorFix.lua
-- WotLK 3.3.5a (Interface 30300)
-- Ensures ITEM_QUALITY_COLORS[7] exists so LootFrame can color BoA items safely.
--
-- UIParent.lua only builds entries -1..6, so heirloom (7) is missing and anything
-- that indexes the table by an item's quality indexes nil.
--
-- Two rules this file used to break:
--
-- * hex is a COMPLETE escape - "|cffe6cc80", exactly what GetItemQualityColor
--   returns - because stock code writes ITEM_QUALITY_COLORS[q].hex .. name .. "|r".
--   Storing "ffe6cc80" drew those digits as text in front of the name.
--
-- * Only fill an entry nobody has filled. On Barracks+ and Centurion the FrameXML
--   module BPlusItemQualityColors.lua fills heirloom itself and recolours chat
--   links from it. This addon loads after FrameXML, so overwriting that entry is
--   what turned every heirloom link in chat into "ffe6cc80[Name]".

local QUALITY_HEIRLOOM = 7

local function SetHeirloomColor()
    if type(ITEM_QUALITY_COLORS) ~= "table" then
        ITEM_QUALITY_COLORS = {}
    end
    if type(ITEM_QUALITY_COLORS[QUALITY_HEIRLOOM]) ~= "table" then
        ITEM_QUALITY_COLORS[QUALITY_HEIRLOOM] = {}
    end

    local entry = ITEM_QUALITY_COLORS[QUALITY_HEIRLOOM]
    if entry.hex then
        return
    end

    -- The client's own heirloom colour (e6cc80), in the client's own shape.
    entry.r, entry.g, entry.b, entry.hex = GetItemQualityColor(QUALITY_HEIRLOOM)
end

local f = CreateFrame("Frame")
f:RegisterEvent("VARIABLES_LOADED")
f:SetScript("OnEvent", function()
    SetHeirloomColor()
end)

-- Also attempt immediately (in case table is already present)
if ITEM_QUALITY_COLORS then
    SetHeirloomColor()
end
