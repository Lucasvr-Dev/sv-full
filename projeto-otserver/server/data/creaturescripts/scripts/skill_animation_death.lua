function onDeath(player, corpse, killer, mostDamageKiller, lastHitUnjustified, mostDamageUnjustified)
	SkillAnimation.cancel(player, "death")
	return true
end
