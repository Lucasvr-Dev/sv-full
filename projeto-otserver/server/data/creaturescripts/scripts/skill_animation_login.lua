function onLogin(player)
	-- defesa extra: nunca começar a sessão preso/invisível
	removeLeftoverInvisibility(player)

	player:registerEvent("SkillAnimationDeath")
	if SkillAnimationConfig.immuneToMonsters or SkillAnimationConfig.immuneToPlayers then
		player:registerEvent("SkillAnimationImmunity")
	end
	return true
end
