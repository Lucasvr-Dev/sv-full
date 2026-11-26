local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
-- combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_NONE)
-- combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_NONE)
combat:setParameter(COMBAT_PARAM_BLOCKARMOR, true)
combat:setParameter(COMBAT_PARAM_USECHARGES, true)

function onGetFormulaValues(player, skill, attack, factor)
	local min = (player:getLevel() / 5) + (skill * attack * 0.02) + 100
	local max = (player:getLevel() / 5) + (skill * attack * 0.04) + 200
	return -min, -max
end

combat:setCallback(CALLBACK_PARAM_SKILLVALUE, "onGetFormulaValues")

local MINHA_ANIM = {
	grid = {w = 3, h = 3},
	[DIRECTION_SOUTH] = {effect = 362, col = 4, row = 2, grid = {w = 5, h = 5}},
	[DIRECTION_NORTH] = {effect = 361, col = 4, row = 5, grid = {w = 5, h = 5}},
	[DIRECTION_EAST]  = {effect = 359, col = 2, row = 4, grid = {w = 5, h = 5}},
	[DIRECTION_WEST]  = {effect = 360, col = 5, row = 4, grid = {w = 5, h = 5}},
}

function onCastSpell(creature, variant)
	faceTarget(creature)
	GiantSprite.play(creature, MINHA_ANIM)

	return playSkillAnimation(creature, "skill_ms1000", variant, function(player, var)
		return combat:execute(creature, variant)
	end,
		{ hitAt = { 300, 900 } }) -- 2 hits: aos 400 ms e aos 800 ms
end