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

local function setupCard(card, voc)
    card:setId('voc' .. voc.id)
    card:recursiveGetChildById('name'):setText(voc.name)
    card:recursiveGetChildById('description'):setText(voc.description)
    card:recursiveGetChildById('level'):setText(tostring(voc.level))
    card:setBorderColor(voc.active and '#d9b45a' or '#5a5a5a')
    card:setOpacity(voc.unlocked and 1.0 or 0.65)

    local outfit = card:recursiveGetChildById('outfit')
    outfit:setOutfit(OUTFITS[voc.id] or {type = 128})

    card:recursiveGetChildById('fragmentsText'):setText(
        tr('Fragments') .. ': ' .. voc.fragments .. ' / ' .. voc.required)

    local bar = card:recursiveGetChildById('fragments')
    bar:setValue(voc.fragments, 0, voc.required)
    bar:setText(voc.unlocked and tr('Unlocked') or (voc.fragments .. ' / ' .. voc.required))

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
