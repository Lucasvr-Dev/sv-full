-- Vocation collection window. Talks to data/lib/core/vocation_collection.lua through opcode 210.
local OPCODE = 210

local window = nil
local button = nil

-- Base Tibia outfits used as the card portrait (looktype, head, body, legs, feet)
local OUTFITS = {
    [1] = {type = 130, head = 78, body = 88, legs = 88, feet = 95}, -- Sorcerer (mage)
    [2] = {type = 133, head = 78, body = 68, legs = 58, feet = 76}, -- Druid (summoner)
    [3] = {type = 129, head = 78, body = 69, legs = 58, feet = 76}, -- Paladin (hunter)
    [4] = {type = 131, head = 78, body = 69, legs = 58, feet = 76}  -- Knight
}

function init()
    connect(g_game, {onGameStart = online, onGameEnd = offline})
    ProtocolGame.registerExtendedOpcode(OPCODE, onExtendedOpcode)
    if g_game.isOnline() then
        online()
    end
end

function terminate()
    disconnect(g_game, {onGameStart = online, onGameEnd = offline})
    ProtocolGame.unregisterExtendedOpcode(OPCODE, onExtendedOpcode)
    offline()
end

function online()
    if window then
        return
    end
    window = g_ui.displayUI('game_vocations')
    window:hide()
    button = modules.client_topmenu.addRightGameToggleButton('vocationsButton', tr('Characters'),
        '/images/topbuttons/skills', toggle, false)
    button:setOn(false)
end

function offline()
    if button then
        button:destroy()
        button = nil
    end
    if window then
        window:destroy()
        window = nil
    end
end

function send(action, vocation)
    local protocol = g_game.getProtocolGame()
    if protocol then
        protocol:sendExtendedOpcode(OPCODE, json.encode({action = action, vocation = vocation}))
    end
end

function show()
    if not window then
        return
    end
    window:show()
    window:raise()
    window:focus()
    button:setOn(true)
    send('fetch')
end

function hide()
    if not window then
        return
    end
    window:hide()
    button:setOn(false)
end

function toggle()
    if window and window:isVisible() then
        hide()
    else
        show()
    end
end

local STAR_FULL = '/game_vocations/images/star_full'
local STAR_EMPTY = '/game_vocations/images/star_empty'
local STAR_SIZE = 18
local CARD_WIDTH = 150

local function percent(value)
    return string.format('%g', value or 0)
end

-- Row of stars at the bottom of the card: `stars` filled, the rest empty (all values come from the server).
local function setupStars(card, voc)
    local panel = card:recursiveGetChildById('stars')
    local maxStars = math.max(1, voc.maxStars or 1)
    local step = math.min(STAR_SIZE + 2, math.floor((CARD_WIDTH - 8) / maxStars))
    local offset = math.floor((CARD_WIDTH - ((maxStars - 1) * step + STAR_SIZE)) / 2)
    for i = 1, maxStars do
        local star = g_ui.createWidget('VocationStar', panel)
        star:addAnchor(AnchorLeft, 'parent', AnchorLeft)
        star:setMarginLeft(offset + (i - 1) * step)
        star:setImageSource(i <= voc.stars and STAR_FULL or STAR_EMPTY)
    end

    local tip
    if not voc.unlocked then
        tip = tr('Vocation locked') .. '\n' ..
            tr('Fragments to unlock') .. ': ' .. voc.fragments .. ' / ' .. voc.required
    else
        tip = tr('Stars') .. ': ' .. voc.stars .. ' / ' .. voc.maxStars .. '\n' ..
            tr('Fragments available') .. ': ' .. voc.fragments .. '\n'
        if voc.nextCost then
            tip = tip .. tr('Fragments for next star') .. ': ' .. voc.nextCost .. '\n'
        end
        tip = tip .. tr('Total attribute bonus') .. ': +' .. percent(voc.bonus) .. '%'
        if voc.nextBonus then
            tip = tip .. '\n' .. tr('Next star bonus') .. ': +' .. percent(voc.nextBonus) .. '%'
        else
            tip = tip .. '\n' .. tr('Maximum evolution reached')
        end
    end
    panel:setTooltip(tip)
end

local function setupCard(card, voc)
    card:setId('voc' .. voc.id)
    card:recursiveGetChildById('name'):setText(voc.name)
    card:recursiveGetChildById('description'):setText(voc.description)
    card:recursiveGetChildById('level'):setText(tostring(voc.level))
    card:setBorderColor(voc.active and '#d9b45a' or '#5a5a5a')
    card:setOpacity(voc.unlocked and 1.0 or 0.65)

    local outfit = card:recursiveGetChildById('outfit')
    outfit:setOutfit(OUTFITS[voc.id] or {type = 128})

    setupStars(card, voc)

    -- locked: progress towards the unlock; unlocked: progress towards the next star (or max level)
    local fragmentsText = card:recursiveGetChildById('fragmentsText')
    local bar = card:recursiveGetChildById('fragments')
    local evolve = card:recursiveGetChildById('evolve')
    evolve:setVisible(false)
    if not voc.unlocked then
        fragmentsText:setText(tr('Fragments') .. ': ' .. voc.fragments .. ' / ' .. voc.required)
        bar:setValue(voc.fragments, 0, voc.required)
        bar:setText(voc.fragments .. ' / ' .. voc.required)
    elseif voc.nextCost then
        fragmentsText:setText(tr('Fragments') .. ': ' .. voc.fragments .. ' / ' .. voc.nextCost)
        bar:setValue(math.min(voc.fragments, voc.nextCost), 0, voc.nextCost)
        bar:setText(voc.fragments .. ' / ' .. voc.nextCost)
        evolve:setVisible(true)
        evolve:setText(tr('Evolve'))
        evolve:setEnabled(voc.fragments >= voc.nextCost)
        evolve.onClick = function() send('evolve', voc.id) end
    else
        fragmentsText:setText(tr('Max evolution'))
        bar:setValue(1, 0, 1)
        bar:setText(tr('MAX'))
    end

    local action = card:recursiveGetChildById('action')
    if voc.active then
        action:setText(tr('In use'))
        action:setEnabled(false)
    elseif voc.unlocked then
        action:setText(tr('Select'))
        action:setEnabled(true)
        action.onClick = function() send('select', voc.id) end
    elseif voc.fragments >= voc.required then
        action:setText(tr('Unlock'))
        action:setEnabled(true)
        action.onClick = function() send('unlock', voc.id) end
    else
        action:setText(tr('Locked'))
        action:setEnabled(false)
    end
end

local function onList(data)
    if not window then
        return
    end
    local list = window:getChildById('cardList')
    list:destroyChildren()
    for _, voc in ipairs(data.vocations) do
        setupCard(g_ui.createWidget('VocationCard', list), voc)
    end
end

local function onMsg(data)
    if not window then
        return
    end
    local label = window:getChildById('message')
    label:setText(data.text)
    label:setColor(data.ok and '#5aff5a' or '#ff6a5a')
end

function onExtendedOpcode(protocol, code, buffer)
    local ok, packet = pcall(function() return json.decode(buffer) end)
    if not ok or type(packet) ~= 'table' then
        g_logger.error('VOCATIONS json error: ' .. tostring(packet))
        return
    end
    if packet.action == 'list' then
        onList(packet.data)
    elseif packet.action == 'msg' then
        onMsg(packet.data)
    end
end
