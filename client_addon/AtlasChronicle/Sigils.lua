-- Atlas Chronicle: Echo Sigils in the bags.
--  * Tooltips: a sigil's item link says only its tier and rarity; the echo it
--    holds (dungeon, timeline, instabilities) is asked of the server
--    (M:<req>:<bag>:<slot>, see atlas_chronicle_script.cpp).
--  * Icons: the unpatched client has no Item.dbc rows for sigils and shows a
--    question mark on the bag button; this puts the sigil's rune there, on
--    Blizzard's bags, the bank and AdiBags.

local PREFIX = "ATLAS"
local SIGIL_BASE = 90000
local ICONS = {
    [0] = "Interface\\Icons\\INV_Misc_Rune_01",   -- normal
    [1] = "Interface\\Icons\\INV_Misc_Rune_04",   -- magic
    [2] = "Interface\\Icons\\INV_Misc_Rune_09",   -- rare
}
local BRONZE = { 0.80, 0.50, 0.20 }
local SOFT = { 0.75, 0.75, 0.75 }

local cache = {}     -- "bag:slot" -> { id =, dungeon =, theme =, affixes = {} }
local pending = {}   -- request token -> { key =, id =, info = }
local req = 0

-- Tier and rarity from a sigil's entry, or nil.
local function decode(itemId)
    if not itemId or itemId <= SIGIL_BASE then
        return nil
    end
    local offset = itemId - SIGIL_BASE
    local rarity = math.floor(offset / 100)
    local tier = offset % 100
    if rarity > 2 or tier < 1 or tier > 16 then
        return nil
    end
    return tier, rarity
end

local function itemIdAt(bag, slot)
    local link = GetContainerItemLink(bag, slot)
    return link and tonumber(string.match(link, "item:(%d+)"))
end

local function unescape(text)
    return (text:gsub("%%(%x%x)", function (hex) return string.char(tonumber(hex, 16)) end))
end

local function split(text, sep)
    local out = {}
    if text == nil or text == "" then
        return out
    end
    local start = 1
    while true do
        local i = string.find(text, sep, start, true)
        if not i then
            table.insert(out, string.sub(text, start))
            break
        end
        table.insert(out, string.sub(text, start, i - 1))
        start = i + 1
    end
    return out
end

local function ask(bag, slot, itemId)
    local key = bag .. ":" .. slot
    for _, p in pairs(pending) do
        if p.key == key and p.id == itemId then
            return
        end
    end
    req = req + 1
    local token = "s" .. req
    pending[token] = { key = key, id = itemId, info = { id = itemId, affixes = {} } }
    SendAddonMessage(PREFIX, "M:" .. token .. ":" .. bag .. ":" .. slot, "WHISPER", UnitName("player"))
end

local function addLines(tooltip, info)
    tooltip:AddLine(" ")
    tooltip:AddLine("Echo of " .. (info.dungeon or "an unknown place"), BRONZE[1], BRONZE[2], BRONZE[3])
    if info.theme and info.theme ~= "" then
        tooltip:AddLine(info.theme, SOFT[1], SOFT[2], SOFT[3], true)
    end
    if #info.affixes > 0 then
        tooltip:AddLine("Instabilities:", 1, 1, 1)
        for _, line in ipairs(info.affixes) do
            tooltip:AddLine(line, 1, 1, 1, true)
        end
    else
        tooltip:AddLine("Stable: no instabilities", SOFT[1], SOFT[2], SOFT[3])
    end
end

hooksecurefunc(GameTooltip, "SetBagItem", function (tooltip, bag, slot)
    local itemId = itemIdAt(bag, slot)
    if not decode(itemId) then
        return
    end
    local info = cache[bag .. ":" .. slot]
    if info and info.id == itemId then
        addLines(tooltip, info)
    else
        tooltip:AddLine("The Lens is reading this sigil...", SOFT[1], SOFT[2], SOFT[3])
        ask(bag, slot, itemId)
    end
    tooltip:Show()
end)

-- The rune on the bag button.
local function fixButton(button, bag, slot)
    local tier, rarity = decode(itemIdAt(bag, slot))
    if tier then
        SetItemButtonTexture(button, ICONS[rarity])
    end
end

hooksecurefunc("ContainerFrame_Update", function (frame)
    local bag = frame:GetID()
    local name = frame:GetName()
    for i = 1, frame.size or 0 do
        local button = _G[name .. "Item" .. i]
        if button then
            fixButton(button, bag, button:GetID())
        end
    end
end)

if BankFrameItemButton_Update then
    hooksecurefunc("BankFrameItemButton_Update", function (button)
        if not button.isBag then
            fixButton(button, BANK_CONTAINER, button:GetID())
        end
    end)
end

-- AdiBags draws its own buttons and says so after each redraw.
local function hookAdiBags()
    local ace = LibStub and LibStub("AceAddon-3.0", true)
    local adi = ace and ace:GetAddon("AdiBags", true)
    if not adi or not adi.RegisterMessage then
        return
    end
    local listener = {}
    adi.RegisterMessage(listener, "AdiBags_UpdateButton", function (_, button)
        local tier, rarity = decode(button.itemId)
        if tier and button.IconTexture then
            button.IconTexture:SetTexture(ICONS[rarity])
            button.IconTexture:SetTexCoord(0, 1, 0, 1)
        end
    end)
end

-- When the server answers, refresh the tooltip still showing that slot.
local function refresh(key)
    if not GameTooltip:IsShown() then
        return
    end
    local owner = GameTooltip:GetOwner()
    if owner and owner.GetID and owner:GetParent() and owner:GetParent().GetID then
        local bag, slot = owner:GetParent():GetID(), owner:GetID()
        if bag .. ":" .. slot == key then
            GameTooltip:SetBagItem(bag, slot)
        end
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("BAG_UPDATE")
events:RegisterEvent("PLAYER_LOGIN")
events:SetScript("OnEvent", function (self, event, prefix, message, channel, sender)
    if event == "PLAYER_LOGIN" then
        hookAdiBags()
        return
    end
    if event == "BAG_UPDATE" then
        local bag = prefix
        for key in pairs(cache) do
            if string.match(key, "^(-?%d+):") == tostring(bag) then
                cache[key] = nil
            end
        end
        return
    end
    if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then
        return
    end

    local kind, token, payload = string.match(message, "^(%u+):([^:]*):?(.*)$")
    local p = token and pending[token]
    if not p then
        return
    end
    if kind == "MH" then
        local f = split(payload, ",")
        p.info.dungeon = unescape(f[6] or "")
        p.info.theme = unescape(f[7] or "")
        p.info.known = true
    elseif kind == "MA" then
        for _, row in ipairs(split(payload, ";")) do
            table.insert(p.info.affixes, unescape(row))
        end
    elseif kind == "MZ" then
        pending[token] = nil
        if p.info.known then
            cache[p.key] = p.info
            refresh(p.key)
        end
    end
end)
