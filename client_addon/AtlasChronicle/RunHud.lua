-- Atlas Chronicle: the run HUD (PLAN 1.7).
-- A small bronze frame while you walk an echo, a map or the Endless Timeways:
-- the dungeon and tier, the master's bar (slay enough of the echo's forces and
-- its master comes for you), foes slain, rifts left, and the run's affixes as
-- icons with their real names and values on hover. The server pushes it
-- (UH / UA / UZ / UX, see atlas_chronicle_script.cpp); it closes by itself
-- outside a dungeon instance. Drag to move; /atlashud hides or shows it.

local PREFIX = "ATLAS"
local ICON_SIZE = 22
local ICONS_PER_ROW = 8
local BRONZE = { 0.80, 0.50, 0.20 }
local RARITY_COLOURS = { [0] = "|cffffffff", [1] = "|cff1eff00", [2] = "|cff0070dd" }

-- Icons per affix (all from the client's SpellIcon.dbc), then per family.
local ICONS = {
    M01 = "Ability_Warrior_Rampage",       M02 = "Spell_Fire_Immolation",
    M03 = "Spell_Frost_FrostArmor02",      M04 = "Spell_Shadow_ShadowWordPain",
    M05 = "Ability_Creature_Disease_02",   M08 = "Ability_Racial_BloodRage",
    M09 = "Ability_Rogue_Sprint",          M11 = "Spell_Nature_Rejuvenation",
    M12 = "Spell_Holy_DevotionAura",       M13 = "Spell_Arcane_ArcaneResilience",
    M14 = "Spell_Ice_Lament",              M24 = "Spell_Shadow_Corpseexplode",
    M25 = "Spell_Shadow_DeathAndDecay",    M26 = "Spell_Shadow_UnholyFrenzy",
    P01 = "Spell_Shadow_CurseOfTounges",   P02 = "Spell_Shadow_CurseOfMannoroth",
    P03 = "Spell_Shadow_CurseOfSargeras",  P04 = "Spell_Shadow_ChillTouch",
    P06 = "Spell_Nature_Slow",             P08 = "Spell_Shadow_ManaBurn",
    P11 = "Ability_Warrior_Disarm",        A02 = "Spell_Frost_IceStorm",
    A03 = "Spell_Shadow_PlagueCloud",      C01 = "Spell_Shadow_RaiseDead",
    C02 = "Ability_Hunter_Pet_Spider",     C03 = "Ability_Creature_Cursed_01",
    C04 = "Ability_Creature_Cursed_05",    B01 = "Spell_Nature_Strength",
    B02 = "Ability_MeleeDamage",           B05 = "Spell_Shadow_SummonVoidWalker",
}
local FAMILY_ICONS = {
    M = "Ability_Creature_Cursed_02", P = "Spell_Shadow_AntiShadow", A = "Spell_Nature_Earthquake",
    C = "Ability_Warrior_BattleShout", B = "Ability_Warrior_Innerrage",
}

local function iconFor(code)
    local name = ICONS[code] or FAMILY_ICONS[string.sub(code or "", 1, 1)] or "INV_Misc_QuestionMark"
    return "Interface\\Icons\\" .. name
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

-----------------------------------------
-- frame

local hud = CreateFrame("Frame", "AtlasRunHud", UIParent)
hud:SetWidth(ICON_SIZE * ICONS_PER_ROW + 4 * (ICONS_PER_ROW - 1) + 24)
hud:SetHeight(90)
hud:SetPoint("TOP", UIParent, "TOP", 0, -110)
hud:SetFrameStrata("MEDIUM")
hud:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
hud:SetBackdropColor(0.10, 0.07, 0.03, 0.88)
hud:SetBackdropBorderColor(BRONZE[1], BRONZE[2], BRONZE[3])
hud:SetMovable(true)
hud:EnableMouse(true)
hud:RegisterForDrag("LeftButton")
hud:SetClampedToScreen(true)
hud:Hide()

hud:SetScript("OnDragStart", function (self) self:StartMoving() end)
hud:SetScript("OnDragStop", function (self)
    self:StopMovingOrSizing()
    local point, _, relPoint, x, y = self:GetPoint()
    AtlasChronicleHUD = AtlasChronicleHUD or {}
    AtlasChronicleHUD.point = { point, relPoint, x, y }
end)

local title = hud:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOPLEFT", 12, -10)
title:SetPoint("TOPRIGHT", -12, -10)
title:SetJustifyH("LEFT")

local subtitle = hud:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -2)
subtitle:SetPoint("TOPRIGHT", title, "BOTTOMRIGHT", 0, -2)
subtitle:SetJustifyH("LEFT")
subtitle:SetTextColor(0.75, 0.75, 0.75)

local bar = CreateFrame("StatusBar", nil, hud)
bar:SetPoint("TOPLEFT", subtitle, "BOTTOMLEFT", 0, -6)
bar:SetPoint("TOPRIGHT", subtitle, "BOTTOMRIGHT", 0, -6)
bar:SetHeight(14)
bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
bar:SetMinMaxValues(0, 100)
local barBack = bar:CreateTexture(nil, "BACKGROUND")
barBack:SetAllPoints()
barBack:SetTexture(0, 0, 0, 0.6)
local barText = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
barText:SetPoint("CENTER")

local info = hud:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
info:SetPoint("TOPLEFT", bar, "BOTTOMLEFT", 0, -5)
info:SetPoint("TOPRIGHT", bar, "BOTTOMRIGHT", 0, -5)
info:SetJustifyH("LEFT")

local icons = {}
local function iconButton(i)
    if icons[i] then
        return icons[i]
    end
    local b = CreateFrame("Frame", nil, hud)
    b:SetWidth(ICON_SIZE)
    b:SetHeight(ICON_SIZE)
    b.tex = b:CreateTexture(nil, "ARTWORK")
    b.tex:SetAllPoints()
    b:EnableMouse(true)
    b:SetScript("OnEnter", function (self)
        GameTooltip:SetOwner(self, "ANCHOR_BOTTOMRIGHT")
        GameTooltip:AddLine(self.name or "?", 1, 0.53, 0)
        if self.text then
            GameTooltip:AddLine(self.text, 1, 1, 1, true)
        end
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function () GameTooltip:Hide() end)
    icons[i] = b
    return b
end

-----------------------------------------
-- state

local state = { header = nil, affixes = {}, collecting = nil, hidden = false }

local function render()
    local h = state.header
    if not h then
        hud:Hide()
        return
    end

    if h.kind == 1 then
        title:SetText((RARITY_COLOURS[h.rarity] or "|cffffffff") .. "Tier " .. h.tier .. "|r  " .. h.dungeon)
    elseif h.kind == 2 then
        title:SetText("Depth " .. h.tier .. "  " .. h.dungeon)
    else
        title:SetText("Echo of " .. h.dungeon)
    end
    subtitle:SetText(h.theme ~= "" and h.theme or " ")

    if h.master == 2 then
        bar:SetValue(100)
        bar:SetStatusBarColor(0.25, 0.75, 0.25)
        barText:SetText("Mended")
    elseif h.master == 1 then
        bar:SetValue(100)
        bar:SetStatusBarColor(0.80, 0.15, 0.10)
        barText:SetText("The master walks the echo")
    elseif h.master == 3 then
        bar:SetValue(h.total > 0 and math.floor(h.slain * 100 / h.total) or 0)
        bar:SetStatusBarColor(0.45, 0.45, 0.45)
        barText:SetText("The master waits within")
    else
        bar:SetValue(h.forces)
        bar:SetStatusBarColor(BRONZE[1], BRONZE[2], BRONZE[3])
        barText:SetText("The master stirs: " .. h.forces .. "%")
    end

    local line = "Foes slain: " .. h.slain .. " / " .. h.total
    if h.portals >= 0 then
        line = line .. "      Rifts: " .. h.portals
    end
    info:SetText(line)

    for i, a in ipairs(state.affixes) do
        local b = iconButton(i)
        local row, col = math.floor((i - 1) / ICONS_PER_ROW), (i - 1) % ICONS_PER_ROW
        b:ClearAllPoints()
        b:SetPoint("TOPLEFT", info, "BOTTOMLEFT", col * (ICON_SIZE + 4), -6 - row * (ICON_SIZE + 4))
        b.tex:SetTexture(iconFor(a.code))
        b.name, b.text = a.name, a.text
        b:Show()
    end
    for i = #state.affixes + 1, #icons do
        icons[i]:Hide()
    end

    local rows = math.ceil(#state.affixes / ICONS_PER_ROW)
    hud:SetHeight(78 + (rows > 0 and (rows * (ICON_SIZE + 4) + 4) or 0))
    if not state.hidden then
        hud:Show()
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", function (self, event, prefix, message, channel, sender)
    -- Out of the dungeon (a rift home, a hearthstone, the echo closing): the
    -- HUD goes. The server pushes it again on every arrival in the run, and
    -- its "run over" message may be lost during a loading screen.
    if event == "PLAYER_ENTERING_WORLD" then
        local inInstance, instanceType = IsInInstance()
        if not inInstance or instanceType ~= "party" then
            state.header, state.affixes, state.collecting = nil, {}, nil
            hud:Hide()
        end
        return
    end
    if event == "PLAYER_LOGIN" then
        local p = AtlasChronicleHUD and AtlasChronicleHUD.point
        if p then
            hud:ClearAllPoints()
            hud:SetPoint(p[1], UIParent, p[2], p[3], p[4])
        end
        return
    end
    if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then
        return
    end

    local kind, _, payload = string.match(message, "^(%u+):([^:]*):?(.*)$")
    if kind == "UH" then
        local f = split(payload, ",")
        state.header = {
            kind = tonumber(f[1]) or 0, tier = tonumber(f[2]) or 1, rarity = tonumber(f[3]) or 0,
            portals = tonumber(f[4]) or -1, forces = tonumber(f[5]) or 0, master = tonumber(f[6]) or 0,
            slain = tonumber(f[7]) or 0, total = tonumber(f[8]) or 0,
            dungeon = unescape(f[9] or ""), theme = unescape(f[10] or ""),
        }
        render()
    elseif kind == "UA" then
        state.collecting = state.collecting or {}
        for _, row in ipairs(split(payload, ";")) do
            local f = split(row, ",")
            table.insert(state.collecting, { code = unescape(f[1] or ""), name = unescape(f[2] or ""),
                text = unescape(f[3] or "") })
        end
    elseif kind == "UZ" then
        state.affixes = state.collecting or {}
        state.collecting = nil
        render()
    elseif kind == "UX" then
        state.header, state.affixes, state.collecting = nil, {}, nil
        hud:Hide()
    end
end)

SLASH_ATLASHUD1 = "/atlashud"
SlashCmdList["ATLASHUD"] = function ()
    state.hidden = not state.hidden
    if state.hidden then
        hud:Hide()
    else
        render()
    end
end
