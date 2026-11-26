-- Spell de teste do Sistema 1.
-- A animação "skill_ms1500" é compartilhada; os hits (quando sai o dano) são
-- definidos AQUI, por skill, em ms desde o início da animação.
local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_NONE)
combat:setArea(createCombatArea(AREA_SQUARE1X1))

function onGetFormulaValues(player, level, maglevel)
	local min = (level / 5) + (maglevel * 1.2) + 10
	local max = (level / 5) + (maglevel * 2.0) + 20
	return -min, -max
end

combat:setCallback(CALLBACK_PARAM_LEVELMAGICVALUE, "onGetFormulaValues")

function onCastSpell(creature, variant)
	-- false = animação não iniciou: o servidor não cobra mana nem cooldown
	return playSkillAnimation(creature, "skill_ms1500", variant,
		function(player, var, hitNumber, totalHits)
			combat:execute(player, var)
		end,
		{ hitAt = { 400, 800 } }) -- 2 hits: aos 400 ms e aos 800 ms
end
