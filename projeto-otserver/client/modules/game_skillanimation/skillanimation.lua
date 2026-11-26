-- Sistema 1 (animação de skill): o servidor deixa o corpo do jogador "invisível"
-- mandando um outfit vazio. O client, por conta própria, desenha nesse caso o
-- efeito nº 13 (a bola azul). Este módulo esconde o corpo inteiro (incluindo
-- esse efeito) enquanto o outfit estiver vazio, usando um AttachedEffect com
-- hideOwner e opacidade 0. Quando o outfit volta ao normal, o efeito é removido.
--
-- Vale para QUALQUER jogador com outfit vazio (inclusive os que o jogador local vê).

local HIDE_EFFECT_ID = 59001 -- id livre (game_attachedeffects usa 1..11)
local HIDE_EFFECT_THING = 1  -- efeito qualquer; fica com opacidade 0 e nunca aparece

local registered = false

local function ensureRegistered()
    if registered then
        return true
    end

    local effect = g_attachedEffects.registerByThing(HIDE_EFFECT_ID, 'skillanimation_hide', HIDE_EFFECT_THING, ThingCategoryEffect)
    if not effect then
        g_logger.error('[game_skillanimation] nao foi possivel registrar o efeito de esconder corpo')
        return false
    end

    effect:setHideOwner(true)
    effect:setOpacity(0)
    effect:setPermanent(true)

    registered = true
    return true
end

local function refresh(creature)
    if not creature or not creature:isPlayer() then
        return
    end

    local attached = creature:getAttachedEffectById(HIDE_EFFECT_ID)

    if creature:isInvisible() then
        if not attached and ensureRegistered() then
            local effect = g_attachedEffects.getById(HIDE_EFFECT_ID)
            if effect then
                creature:attachEffect(effect)
            end
        end
    elseif attached then
        creature:detachEffectById(HIDE_EFFECT_ID)
    end
end

local function onOutfitChange(creature, outfit, oldOutfit)
    refresh(creature)
end

local function onAppear(creature)
    refresh(creature)
end

function init()
    connect(Creature, { onOutfitChange = onOutfitChange, onAppear = onAppear })
    connect(LocalPlayer, { onOutfitChange = onOutfitChange, onAppear = onAppear })
end

function terminate()
    disconnect(Creature, { onOutfitChange = onOutfitChange, onAppear = onAppear })
    disconnect(LocalPlayer, { onOutfitChange = onOutfitChange, onAppear = onAppear })
end
