UIGameMap = extends(UIMap, 'UIGameMap')

-- Janela de confirmação do drop (apenas uma por vez)
local dropConfirmWindow = nil

-- Executa o drop exatamente como era feito antes da confirmação existir
local function executeDrop(thing, toPos)
    if thing:isItem() and thing:getCount() > 1 then
        modules.game_interface.moveStackableItem(thing, toPos)
    else
        g_game.move(thing, toPos, 1)
    end
end

-- Exibe a confirmação antes de soltar o item no chão.
-- Confirmar: segue com o drop. Cancelar: nada é alterado.
local function confirmDropItem(thing, fromPos, toPos)
    if dropConfirmWindow then
        return
    end

    local function closeWindow()
        if dropConfirmWindow then
            dropConfirmWindow:destroy()
            dropConfirmWindow = nil
        end
    end

    local function onConfirm()
        closeWindow()

        -- O item pode ter sido movido enquanto a janela estava aberta: aborta
        if not g_game.isOnline() then
            return
        end
        local currentPos = thing:getPosition()
        if not currentPos or currentPos.x ~= fromPos.x or currentPos.y ~= fromPos.y or currentPos.z ~= fromPos.z then
            return
        end

        executeDrop(thing, toPos)
    end

    local function onCancel()
        closeWindow()
    end

    dropConfirmWindow = displayGeneralBox(tr('Drop Item'),
        tr('Esse item será permantentemente deletado.\n               Você deseja continuar? '), {{
            text = tr('Confirmar'),
            callback = onConfirm
        }, {
            text = tr('Cancelar'),
            callback = onCancel
        }}, nil, onCancel)
end

function UIGameMap.create()
    local gameMap = UIGameMap.internalCreate()
    gameMap:setKeepAspectRatio(true)
    gameMap:setVisibleDimension({
        width = 15,
        height = 11
    })
    gameMap:setDrawLights(true)
    return gameMap
end

function UIGameMap:onDragEnter(mousePos)
    local tile = self:getTile(mousePos)
    if not tile then
        return false
    end

    local thing = tile:getTopMoveThing()
    if not thing then
        return false
    end

    -- Push mechanic removed: block dragging creatures from the map
    if thing:isCreature() then
        return false
    end

    -- Push mechanic removed: block dragging non-pickupable items (crates, barrels, furniture, etc.)
    if thing:isItem() and not thing:isPickupable() then
        return false
    end

    if thing:isItem() and not thing:isNotMoveable() then
        UIDragIcon:display(thing)
    end

    self.currentDragThing = thing

    g_mouse.pushCursor('target')
    self.allowNextRelease = false
    return true
end

function UIGameMap:onDragLeave(droppedWidget, mousePos)
    self.currentDragThing = nil
    self.hoveredWho = nil
    g_mouse.popCursor('target')
    UIDragIcon:hide()
    return true
end

function UIGameMap:onDrop(widget, mousePos)
    if not self:canAcceptDrop(widget, mousePos) then
        return false
    end

    local tile = self:getTile(mousePos)
    if not tile then
        return false
    end

    local thing = widget.currentDragThing
    if not thing then
        return false
    end

    -- Push mechanic removed: block dropping creatures
    if thing:isCreature() then
        return false
    end

    -- Push mechanic removed: block dropping non-pickupable items (map objects)
    if thing:isItem() and not thing:isPickupable() then
        return false
    end

    local thingPos = thing:getPosition()
    if not thingPos then
        return false
    end

    local thingTile = thing:getTile()
    if thingPos.x ~= 65535 and not thingTile then
        return false
    end

    local toPos = tile:getPosition()
    if thingPos.x == toPos.x and thingPos.y == toPos.y and thingPos.z == toPos.z then
        return false
    end

    -- Item vindo do inventário/container (x == 65535) é deletado ao cair no chão:
    -- pede confirmação. Itens que já estão no mapa seguem o fluxo normal.
    if thingPos.x == 65535 then
        confirmDropItem(thing, thingPos, toPos)
    else
        executeDrop(thing, toPos)
    end

    UIDragIcon:hide()
    return true
end

function UIGameMap:onMousePress()
    if not self:isDragging() then
        self.allowNextRelease = true
    end
end

function UIGameMap:onMouseMove()
    return false
end

function UIGameMap:onMouseRelease(mousePosition, mouseButton)
    if not self.allowNextRelease then
        return true
    end

    local autoWalkPos = self:getPosition(mousePosition)

    -- happens when clicking outside of map boundaries
    if not autoWalkPos then
        return false
    end

    local localPlayerPos = g_game.getLocalPlayer():getPosition()
    if autoWalkPos.z ~= localPlayerPos.z then
        local dz = autoWalkPos.z - localPlayerPos.z
        autoWalkPos.x = autoWalkPos.x + dz
        autoWalkPos.y = autoWalkPos.y + dz
        autoWalkPos.z = localPlayerPos.z
    end

    local lookThing
    local useThing
    local creatureThing
    local multiUseThing
    local attackCreature

    local tile = self:getTile(mousePosition)
    if tile then
        lookThing = tile:getTopLookThing()
        useThing = tile:getTopUseThing()
        creatureThing = tile:getTopCreature()
    end

    local autoWalkTile = g_map.getTile(autoWalkPos)
    if autoWalkTile then
        attackCreature = autoWalkTile:getTopCreature()
    end

    local ret = modules.game_interface.processMouseAction(mousePosition, mouseButton, autoWalkPos, lookThing, useThing,
        creatureThing, attackCreature)
    if ret then
        self.allowNextRelease = false
    end

    return ret
end

function UIGameMap:canAcceptDrop(widget, mousePos)
    if not widget or not widget.currentDragThing then
        return false
    end

    local children = rootWidget:recursiveGetChildrenByPos(mousePos)
    for i = 1, #children do
        local child = children[i]
        if child == self then
            return true
        elseif not child:isPhantom() then
            return false
        end
    end

    error('Widget ' .. self:getId() .. ' not in drop list.')
    return false
end
