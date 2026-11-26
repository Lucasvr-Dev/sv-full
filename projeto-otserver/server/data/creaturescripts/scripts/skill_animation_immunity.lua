function onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType, origin)
	if not creature:isPlayer() or not SkillAnimation.isActive(creature) then
		return primaryDamage, primaryType, secondaryDamage, secondaryType
	end
	if primaryType == COMBAT_HEALING then
		return primaryDamage, primaryType, secondaryDamage, secondaryType
	end

	local cfg = SkillAnimationConfig
	if attacker then
		if (attacker:isMonster() and cfg.immuneToMonsters) or (attacker:isPlayer() and cfg.immuneToPlayers) then
			return 0, primaryType, 0, secondaryType
		end
	end
	return primaryDamage, primaryType, secondaryDamage, secondaryType
end
