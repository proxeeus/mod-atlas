-- Atlas Chronicle: the Chronicle of Echoes kept by the Keepers of Time.
-- Talks to mod-atlas with addon whispers to the player's own character
-- (prefix "ATLAS"); the protocol is documented in atlas_chronicle_script.cpp.

local PREFIX = "ATLAS"
local PROTOCOL = 1
local ROW_H = 22
local ROWS = 12

-- Palette: ink on parchment, bronze for the Keepers.
local INK = { 0.24, 0.15, 0.06 }
local INK_SOFT = { 0.42, 0.30, 0.16 }
local BRONZE = { 0.80, 0.50, 0.20 }
local RANK_COLOURS = {
    [1] = "|cff9c6d00", -- gilded
    [2] = "|cff5c5c66", -- silvered
    [3] = "|cff8a4b14", -- bronzed
}

-- Original lines in the Chronicler's voice; one is shown at the foot of the page.
local SAYINGS = {
    "Every echo mended is a thread made whole again.",
    "The sands remember what mortals forget.",
    "Haste unravels more than it mends. And yet, the swiftest are written first.",
    "Not every timeline deserves saving. These ones did.",
    "Deeper still, the timeways grow thin. Few return to tell of it. These did.",
    "Nozdormu sees every ending. We write down the ones that did not happen.",
}

local EXPANSION_NAMES = {
    [0] = "Before the Dark Portal",
    [1] = "Beyond the Dark Portal",
    [2] = "Northrend",
}

local state = {
    bound = false,      -- the server answered our hello
    dungeons = {},      -- mapId -> { name =, exp = }
    dungeonOrder = {},  -- mapIds sorted by expansion then name
    tiers = {},         -- id -> name
    endless = true,
    tab = 1,
    echoFilter = 0,     -- mapId, 0 = every echo
    descentSort = "T",  -- T = deepest, F = most echoes
    req = 0,
    pending = {},       -- request token -> kind
    echoRows = {},
    descentRows = {},
    record = nil,
}

-----------------------------------------
-- helpers

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

local function formatDuration(seconds)
    seconds = tonumber(seconds) or 0
    local h = math.floor(seconds / 3600)
    local m = math.floor((seconds % 3600) / 60)
    local s = seconds % 60
    if h > 0 then
        return string.format("%d:%02d:%02d", h, m, s)
    end
    return string.format("%d:%02d", m, s)
end

local function ordinal(n)
    local last2 = n % 100
    if last2 >= 11 and last2 <= 13 then
        return n .. "th"
    end
    local last = n % 10
    if last == 1 then return n .. "st" end
    if last == 2 then return n .. "nd" end
    if last == 3 then return n .. "rd" end
    return n .. "th"
end

local function send(command, arg)
    state.req = state.req + 1
    local token = tostring(state.req)
    state.pending[token] = command
    local msg = command .. ":" .. token
    if arg then
        msg = msg .. ":" .. arg
    end
    SendAddonMessage(PREFIX, msg, "WHISPER", UnitName("player"))
    return token
end

-- Waits a few seconds for the hello's answer before calling the realm silent.
local helloWait = CreateFrame("Frame")
helloWait:Hide()

local function sayHello()
    helloWait.elapsed = 0
    helloWait:Show()
    send("H")
end

-----------------------------------------
-- frame

local frame = CreateFrame("Frame", "AtlasChronicleFrame", UIParent)
frame:SetSize(620, 470)
frame:SetPoint("CENTER")
frame:SetFrameStrata("HIGH")
frame:SetToplevel(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
frame:SetBackdropBorderColor(BRONZE[1] + 0.1, BRONZE[2] + 0.15, BRONZE[3] + 0.1)
frame:Hide()
tinsert(UISpecialFrames, "AtlasChronicleFrame")

-- Header: the bronze dragonflight's seal, the title, and who keeps the record.
local seal = frame:CreateTexture(nil, "ARTWORK")
seal:SetSize(52, 52)
seal:SetPoint("TOPLEFT", frame, "TOPLEFT", 18, -14)
SetPortraitToTexture(seal, "Interface\\Icons\\INV_Misc_Head_Dragon_Bronze")

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", seal, "TOPRIGHT", 10, -6)
title:SetText("The Chronicle of Echoes")

local subtitle = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -4)
subtitle:SetTextColor(0.86, 0.74, 0.52)
subtitle:SetText("Set down by the Keepers of Time, in the Caverns of Time")

local hourglass = frame:CreateTexture(nil, "ARTWORK")
hourglass:SetSize(28, 28)
hourglass:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -40, -22)
hourglass:SetTexture("Interface\\Icons\\Spell_Holy_BorrowedTime")

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -6, -6)

-- The page. A plain sand colour sits under the parchment so the ink stays
-- readable even if the parchment art fails to load.
local page = CreateFrame("Frame", nil, frame)
page:SetPoint("TOPLEFT", frame, "TOPLEFT", 16, -76)
page:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -16, 40)
page:SetBackdrop({
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 14,
    insets = { left = 3, right = 3, top = 3, bottom = 3 },
})
page:SetBackdropBorderColor(BRONZE[1], BRONZE[2], BRONZE[3], 1)

local sand = page:CreateTexture(nil, "BACKGROUND")
sand:SetPoint("TOPLEFT", page, "TOPLEFT", 3, -3)
sand:SetPoint("BOTTOMRIGHT", page, "BOTTOMRIGHT", -3, 3)
sand:SetTexture(0.84, 0.75, 0.57, 1)

local parchment = page:CreateTexture(nil, "BORDER")
parchment:SetAllPoints(sand)
parchment:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Parchment-Horizontal")
parchment:SetVertexColor(1, 0.93, 0.78, 0.9)

local saying = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
saying:SetPoint("BOTTOM", frame, "BOTTOM", 0, 22)
saying:SetWidth(560)
saying:SetTextColor(0.86, 0.74, 0.52)

local function inkLine(parent, template, colour)
    local fs = parent:CreateFontString(nil, "OVERLAY", template or "GameFontNormal")
    local c = colour or INK
    fs:SetTextColor(c[1], c[2], c[3])
    fs:SetShadowOffset(0, 0)
    return fs
end

local function rule(parent, anchor, offsetY)
    local t = parent:CreateTexture(nil, "ARTWORK")
    t:SetHeight(1)
    t:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", 0, offsetY or -2)
    t:SetPoint("RIGHT", page, "RIGHT", -12, 0)
    t:SetTexture(INK_SOFT[1], INK_SOFT[2], INK_SOFT[3], 0.6)
    return t
end

-- What the page says while the sands answer, or when they hold nothing.
local notice = inkLine(page, "GameFontNormal", INK_SOFT)
notice:SetPoint("CENTER", page, "CENTER", 0, -10)
notice:SetWidth(480)

-----------------------------------------
-- a ruled table, shared by the two boards

local function makeBoard(name, columns, tooltip)
    local board = CreateFrame("Frame", nil, page)
    board:SetPoint("TOPLEFT", page, "TOPLEFT", 12, -44)
    board:SetPoint("BOTTOMRIGHT", page, "BOTTOMRIGHT", -12, 8)

    local x = 0
    local header
    for i, col in ipairs(columns) do
        local fs = inkLine(board, "GameFontNormalSmall")
        fs:SetPoint("TOPLEFT", board, "TOPLEFT", x + 4, 0)
        fs:SetWidth(col.width - 8)
        fs:SetJustifyH(col.justify or "LEFT")
        fs:SetText(col.title)
        if i == 1 then
            header = fs
        end
        x = x + col.width
    end
    rule(board, header, -4)

    board.rows = {}
    for r = 1, ROWS do
        local row = CreateFrame("Button", nil, board)
        row:SetHeight(ROW_H)
        row:SetPoint("TOPLEFT", board, "TOPLEFT", 0, -16 - (r - 1) * ROW_H)
        row:SetPoint("RIGHT", board, "RIGHT", -22, 0)

        row.mine = row:CreateTexture(nil, "BACKGROUND")
        row.mine:SetAllPoints(row)
        row.mine:SetTexture(BRONZE[1], BRONZE[2], BRONZE[3], 0.25)

        local hl = row:CreateTexture(nil, "HIGHLIGHT")
        hl:SetAllPoints(row)
        hl:SetTexture(1, 0.85, 0.5, 0.25)

        row.cells = {}
        local cx = 0
        for i, col in ipairs(columns) do
            local fs = inkLine(row, i == 2 and "GameFontNormal" or "GameFontHighlight")
            fs:SetPoint("LEFT", row, "LEFT", cx + 4, 0)
            fs:SetWidth(col.width - 8)
            fs:SetJustifyH(col.justify or "LEFT")
            fs:SetTextColor(INK[1], INK[2], INK[3])
            row.cells[i] = fs
            cx = cx + col.width
        end

        row:SetScript("OnEnter", function (self)
            if not self.data or not tooltip then
                return
            end
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            tooltip(self.data)
            GameTooltip:Show()
        end)
        row:SetScript("OnLeave", function () GameTooltip:Hide() end)
        board.rows[r] = row
    end

    local scroll = CreateFrame("ScrollFrame", name .. "Scroll", board, "FauxScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", board, "TOPLEFT", 0, -16)
    scroll:SetPoint("BOTTOMRIGHT", board, "BOTTOMRIGHT", -24, 0)
    board.scroll = scroll
    board.data = {}

    -- Rows sit above the scroll frame so they get the mouse; pass the wheel on.
    for _, row in ipairs(board.rows) do
        row:SetFrameLevel(scroll:GetFrameLevel() + 2)
        row:EnableMouseWheel(true)
        row:SetScript("OnMouseWheel", function (_, delta)
            ScrollFrameTemplate_OnMouseWheel(scroll, delta)
        end)
    end

    function board:Refresh()
        local data = self.data
        FauxScrollFrame_Update(self.scroll, #data, ROWS, ROW_H)
        local offset = FauxScrollFrame_GetOffset(self.scroll)
        for r = 1, ROWS do
            local row = self.rows[r]
            local entry = data[r + offset]
            row.data = entry
            if entry then
                for i, text in ipairs(entry.cells) do
                    row.cells[i]:SetText(text)
                end
                if entry.mine then row.mine:Show() else row.mine:Hide() end
                row:Show()
            else
                row:Hide()
            end
        end
    end

    scroll:SetScript("OnVerticalScroll", function (self, offset)
        FauxScrollFrame_OnVerticalScroll(self, offset, ROW_H, function () board:Refresh() end)
    end)

    board:Hide()
    return board
end

local function rankText(rank)
    local colour = RANK_COLOURS[rank]
    if colour then
        return colour .. rank .. "|r"
    end
    return tostring(rank)
end

-----------------------------------------
-- tab 1: Echoes Mended

local echoes = makeBoard("AtlasChronicleEchoes", {
    { title = "#", width = 34, justify = "RIGHT" },
    { title = "Champion", width = 110 },
    { title = "Echo", width = 140 },
    { title = "Strength", width = 92 },
    { title = "Mended in", width = 70, justify = "RIGHT" },
    { title = "Fallen", width = 46, justify = "RIGHT" },
    { title = "Party", width = 44, justify = "RIGHT" },
}, function (d)
    GameTooltip:AddLine(d.name, 1, 0.82, 0)
    GameTooltip:AddLine(d.echo, 1, 1, 1)
    GameTooltip:AddLine(d.strength .. (d.attuned and ", attuned to the party's strength" or ""), 0.8, 0.8, 0.8)
    GameTooltip:AddLine(string.format("Mended in %s by a party of %d; %d fell along the way.",
        formatDuration(d.seconds), d.party, d.fallen), 0.8, 0.8, 0.8, true)
end)

local dropdown = CreateFrame("Frame", "AtlasChronicleEchoDropDown", page, "UIDropDownMenuTemplate")
dropdown:SetPoint("TOPLEFT", page, "TOPLEFT", -6, -8)
UIDropDownMenu_SetWidth(dropdown, 200)

local echoesLabel = inkLine(page, "GameFontNormal", INK_SOFT)
echoesLabel:SetPoint("LEFT", dropdown, "RIGHT", -4, 2)
echoesLabel:SetText("The swiftest mendings on record")

local requestEchoes

local function setEchoFilter(mapId)
    state.echoFilter = mapId
    local d = state.dungeons[mapId]
    UIDropDownMenu_SetText(dropdown, d and d.name or "Every echo")
    CloseDropDownMenus()
    requestEchoes()
end

UIDropDownMenu_Initialize(dropdown, function (self, level)
    level = level or 1
    local info = UIDropDownMenu_CreateInfo()
    if level == 1 then
        info.text = "Every echo"
        info.checked = state.echoFilter == 0
        info.func = function () setEchoFilter(0) end
        UIDropDownMenu_AddButton(info, level)
        for exp = 0, 2 do
            info = UIDropDownMenu_CreateInfo()
            info.text = EXPANSION_NAMES[exp]
            info.hasArrow = true
            info.notCheckable = true
            info.value = exp
            UIDropDownMenu_AddButton(info, level)
        end
    elseif level == 2 then
        for _, mapId in ipairs(state.dungeonOrder) do
            local d = state.dungeons[mapId]
            if d.exp == UIDROPDOWNMENU_MENU_VALUE then
                info = UIDropDownMenu_CreateInfo()
                info.text = d.name
                info.checked = state.echoFilter == mapId
                info.func = function () setEchoFilter(mapId) end
                UIDropDownMenu_AddButton(info, level)
            end
        end
    end
end)
UIDropDownMenu_SetText(dropdown, "Every echo")

-----------------------------------------
-- tab 2: the Endless Timeways

local descents = makeBoard("AtlasChronicleDescents", {
    { title = "#", width = 34, justify = "RIGHT" },
    { title = "Champion", width = 130 },
    { title = "Depth", width = 60, justify = "RIGHT" },
    { title = "Echoes", width = 64, justify = "RIGHT" },
    { title = "Foes slain", width = 80, justify = "RIGHT" },
    { title = "The descent lasted", width = 112, justify = "RIGHT" },
    { title = "Party", width = 56, justify = "RIGHT" },
}, function (d)
    GameTooltip:AddLine(d.name, 1, 0.82, 0)
    GameTooltip:AddLine(string.format("Reached depth %d and mended %d echo%s.", d.depth, d.echoes,
        d.echoes == 1 and "" or "es"), 1, 1, 1, true)
    GameTooltip:AddLine(string.format("%d foes slain in %s, with a party of %d.", d.slain,
        formatDuration(d.seconds), d.party), 0.8, 0.8, 0.8, true)
end)

local sortButtons = {}
local requestDescents

local function makeSortButton(text, key, anchorTo)
    local b = CreateFrame("Button", nil, page, "UIPanelButtonTemplate")
    b:SetSize(150, 22)
    b:SetText(text)
    if anchorTo then
        b:SetPoint("LEFT", anchorTo, "RIGHT", 6, 0)
    else
        b:SetPoint("TOPLEFT", page, "TOPLEFT", 12, -12)
    end
    b:SetScript("OnClick", function ()
        state.descentSort = key
        requestDescents()
    end)
    b.key = key
    sortButtons[key] = b
    return b
end

local deepest = makeSortButton("The deepest descents", "T")
makeSortButton("The most echoes mended", "F", deepest)

local function updateSortButtons()
    for key, b in pairs(sortButtons) do
        if key == state.descentSort then b:Disable() else b:Enable() end
    end
end

-----------------------------------------
-- tab 3: Your Thread

local thread = CreateFrame("Frame", nil, page)
thread:SetPoint("TOPLEFT", page, "TOPLEFT", 16, -14)
thread:SetPoint("BOTTOMRIGHT", page, "BOTTOMRIGHT", -16, 12)
thread:Hide()

local threadName = inkLine(thread, "GameFontNormalLarge")
threadName:SetPoint("TOP", thread, "TOP", 0, 0)

local threadPlace = inkLine(thread, "GameFontNormal", INK_SOFT)
threadPlace:SetPoint("TOP", threadName, "BOTTOM", 0, -6)
threadPlace:SetWidth(540)

local function makeColumn(offsetX, heading)
    local col = CreateFrame("Frame", nil, thread)
    col:SetSize(260, 260)
    col:SetPoint("TOP", thread, "TOP", offsetX, -62)
    local h = inkLine(col, "GameFontNormal")
    h:SetPoint("TOPLEFT", col, "TOPLEFT", 0, 0)
    h:SetText(heading)
    rule(col, h, -4)
    col.lines = {}
    for i = 1, 8 do
        local label = inkLine(col, "GameFontHighlight", INK_SOFT)
        label:SetPoint("TOPLEFT", col, "TOPLEFT", 4, -16 - i * 20)
        local value = inkLine(col, "GameFontHighlight")
        value:SetPoint("TOPRIGHT", col, "TOPRIGHT", -4, -16 - i * 20)
        col.lines[i] = { label = label, value = value }
    end
    function col:Fill(pairsList)
        for i, line in ipairs(self.lines) do
            local p = pairsList[i]
            line.label:SetText(p and p[1] or "")
            line.value:SetText(p and p[2] or "")
        end
    end
    return col
end

local echoColumn = makeColumn(-140, "Echoes")
local endlessColumn = makeColumn(140, "The Endless Timeways")

local function fillThread()
    local rec = state.record
    threadName:SetText(UnitName("player") .. ", as the sands remember you")
    if not rec then
        return
    end
    local n, e = rec.normal, rec.endless

    local places = {}
    if rec.echoRank > 0 then
        table.insert(places, "the " .. ordinal(rec.echoRank) .. " swiftest mending")
    end
    if rec.descentRank > 0 then
        table.insert(places, "the " .. ordinal(rec.descentRank) .. " deepest descent")
    end
    if #places > 0 then
        threadPlace:SetText("The Chronicle names you for " .. table.concat(places, " and ") .. ".")
    else
        threadPlace:SetText("The Chronicle does not name you yet. Mend an echo and it will.")
    end

    local rate = n[1] > 0 and string.format("%d%%", math.floor(n[2] * 100 / n[1] + 0.5)) or "-"
    echoColumn:Fill({
        { "Echoes walked", n[1] },
        { "Mended", n[2] },
        { "Lost to the sands", n[3] },
        { "Mended, of those walked", rate },
        { "Foes slain", n[4] },
        { "Masters slain", n[5] },
        { "Times fallen", n[6] },
        { "Swiftest mending", n[7] > 0 and formatDuration(n[7]) or "-" },
    })

    if state.endless then
        endlessColumn:Show()
        endlessColumn:Fill({
            { "Descents", e[1] },
            { "Deepest reached", e[2] > 0 and ("depth " .. e[2]) or "-" },
            { "Most echoes in one descent", e[3] },
            { "Echoes mended", e[4] },
            { "Foes slain", e[5] },
            { "Masters slain", e[6] },
            { "Times fallen", e[7] },
            { "Longest descent", e[8] > 0 and formatDuration(e[8]) or "-" },
        })
    else
        endlessColumn:Hide()
    end
end

-----------------------------------------
-- tabs

local TAB_NAMES = { "Echoes Mended", "The Endless Timeways", "Your Thread" }
local tabs = {}
for i, text in ipairs(TAB_NAMES) do
    local tab = CreateFrame("Button", "AtlasChronicleFrameTab" .. i, frame, "CharacterFrameTabButtonTemplate")
    tab:SetID(i)
    tab:SetText(text)
    if i == 1 then
        tab:SetPoint("TOPLEFT", frame, "BOTTOMLEFT", 14, 8)
    else
        tab:SetPoint("LEFT", tabs[i - 1], "RIGHT", -16, 0)
    end
    tabs[i] = tab
end
PanelTemplates_SetNumTabs(frame, #tabs)

local function setNotice(text)
    if text then
        notice:SetText(text)
        notice:Show()
    else
        notice:Hide()
    end
end

local function showTab(id)
    if id == 2 and not state.endless then
        id = 1
    end
    state.tab = id
    PanelTemplates_SetTab(frame, id)

    echoes:Hide()
    descents:Hide()
    thread:Hide()
    dropdown:Hide()
    echoesLabel:Hide()
    for _, b in pairs(sortButtons) do b:Hide() end
    setNotice(nil)

    if not state.bound then
        if helloWait:IsShown() then
            setNotice("The sands are shifting...")
        else
            setNotice("The Chronicle is silent. This realm keeps no record of echoes.")
        end
        return
    end

    if id == 1 then
        dropdown:Show()
        echoesLabel:Show()
        echoes:Show()
        requestEchoes()
    elseif id == 2 then
        for _, b in pairs(sortButtons) do b:Show() end
        updateSortButtons()
        descents:Show()
        requestDescents()
    else
        thread:Show()
        state.record = nil
        fillThread()
        send("R")
    end
end

for i, tab in ipairs(tabs) do
    tab:SetScript("OnClick", function (self)
        PlaySound("igCharacterInfoTab")
        showTab(self:GetID())
    end)
end

-----------------------------------------
-- requests and answers

requestEchoes = function ()
    state.echoRows = {}
    echoes.data = {}
    echoes:Refresh()
    setNotice("The sands are shifting...")
    state.echoToken = send("E", tostring(state.echoFilter))
end

requestDescents = function ()
    updateSortButtons()
    state.descentRows = {}
    descents.data = {}
    descents:Refresh()
    setNotice("The sands are shifting...")
    state.descentToken = send("D", state.descentSort)
end

local function rebuildDungeonOrder()
    local order = {}
    for mapId in pairs(state.dungeons) do
        table.insert(order, mapId)
    end
    table.sort(order, function (a, b)
        local da, db = state.dungeons[a], state.dungeons[b]
        if da.exp ~= db.exp then
            return da.exp < db.exp
        end
        return da.name < db.name
    end)
    state.dungeonOrder = order
end

local function onEchoRows(payload)
    for _, row in ipairs(split(payload, ";")) do
        local f = split(row, ",")
        local mapId, diffId = tonumber(f[4]), tonumber(f[5])
        local d = state.dungeons[mapId]
        local entry = {
            rank = tonumber(f[1]),
            name = unescape(f[2]),
            seconds = tonumber(f[3]),
            echo = d and d.name or "An unknown echo",
            strength = state.tiers[diffId] or "?",
            party = tonumber(f[6]) or 1,
            attuned = f[7] == "1",
            fallen = tonumber(f[8]) or 0,
            mine = f[9] == "1",
        }
        entry.cells = {
            rankText(entry.rank), entry.name, entry.echo,
            entry.strength .. (entry.attuned and "*" or ""),
            formatDuration(entry.seconds), entry.fallen, entry.party,
        }
        table.insert(state.echoRows, entry)
    end
end

local function onDescentRows(payload)
    for _, row in ipairs(split(payload, ";")) do
        local f = split(row, ",")
        local entry = {
            rank = tonumber(f[1]),
            name = unescape(f[2]),
            depth = tonumber(f[3]) or 0,
            echoes = tonumber(f[4]) or 0,
            slain = tonumber(f[5]) or 0,
            seconds = tonumber(f[6]) or 0,
            party = tonumber(f[7]) or 1,
            mine = f[8] == "1",
        }
        entry.cells = {
            rankText(entry.rank), entry.name, entry.depth, entry.echoes, entry.slain,
            formatDuration(entry.seconds), entry.party,
        }
        table.insert(state.descentRows, entry)
    end
end

local function numbers(text)
    local out = {}
    for i, v in ipairs(split(text, ",")) do
        out[i] = tonumber(v) or 0
    end
    return out
end

local function onMessage(message)
    if message == "O" then
        if not frame:IsShown() then
            frame:Show()
        end
        return
    end
    if message == "OFF" then
        state.bound = false
        if frame:IsShown() then showTab(state.tab) end
        return
    end

    -- kind:token[:payload]; the payload may itself hold colons only when escaped.
    local kind, token, payload = string.match(message, "^(%u+):([^:]*):?(.*)$")
    if not kind then
        return
    end

    if kind == "H" then
        local f = split(payload, ":")
        state.bound = tonumber(f[1]) == PROTOCOL
        state.endless = f[2] == "1"
        state.dungeons, state.tiers = {}, {}
    elseif kind == "DG" then
        for _, row in ipairs(split(payload, ";")) do
            local f = split(row, ",")
            local mapId = tonumber(f[1])
            if mapId then
                state.dungeons[mapId] = { exp = tonumber(f[2]) or 0, name = unescape(f[3] or "") }
            end
        end
    elseif kind == "DF" then
        for _, row in ipairs(split(payload, ";")) do
            local f = split(row, ",")
            local id = tonumber(f[1])
            if id then
                state.tiers[id] = unescape(f[2] or "")
            end
        end
    elseif kind == "HZ" then
        helloWait:Hide()
        rebuildDungeonOrder()
        if state.endless then tabs[2]:Show() else tabs[2]:Hide() end
        if frame:IsShown() then showTab(state.tab) end
    elseif kind == "E" and token == state.echoToken then
        onEchoRows(payload)
    elseif kind == "EZ" and token == state.echoToken then
        echoes.data = state.echoRows
        echoes:Refresh()
        setNotice(#state.echoRows == 0 and "No mending is written here yet. The sands are still." or nil)
    elseif kind == "D" and token == state.descentToken then
        onDescentRows(payload)
    elseif kind == "DZ" and token == state.descentToken then
        descents.data = state.descentRows
        descents:Refresh()
        setNotice(#state.descentRows == 0 and "No one has yet descended into the Endless Timeways." or nil)
    elseif kind == "RN" then
        state.recordNormal = numbers(payload)
    elseif kind == "RE" then
        state.recordEndless = numbers(payload)
    elseif kind == "RZ" then
        local f = split(payload, ":")
        state.record = {
            normal = state.recordNormal or {},
            endless = state.recordEndless or {},
            echoRank = tonumber(f[1]) or 0,
            descentRank = tonumber(f[2]) or 0,
        }
        if state.tab == 3 then fillThread() end
    end
end

-----------------------------------------
-- events

frame:SetScript("OnShow", function ()
    PlaySound("igQuestLogOpen")
    saying:SetText("\"" .. SAYINGS[math.random(#SAYINGS)] .. "\"  - Chronicler Esmadormu")
    if not state.bound and not helloWait:IsShown() then
        sayHello()
    end
    showTab(state.tab)
end)
frame:SetScript("OnHide", function ()
    PlaySound("igQuestLogClose")
    CloseDropDownMenus()
end)

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function (self, event, prefix, message, channel, sender)
    if event == "PLAYER_LOGIN" then
        -- Say hello at once so the Chronicler can offer the window.
        sayHello()
    elseif event == "CHAT_MSG_ADDON" then
        if prefix == PREFIX and channel == "WHISPER" and sender == UnitName("player") then
            onMessage(message)
        end
    end
end)

helloWait:SetScript("OnUpdate", function (self, elapsed)
    self.elapsed = self.elapsed + elapsed
    if self.elapsed > 5 then
        self:Hide()
        if frame:IsShown() then showTab(state.tab) end
    end
end)

SLASH_ATLASCHRONICLE1 = "/chronicle"
SlashCmdList["ATLASCHRONICLE"] = function ()
    if frame:IsShown() then
        frame:Hide()
    else
        frame:Show()
    end
end
