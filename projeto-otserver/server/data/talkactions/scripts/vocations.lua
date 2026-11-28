-- !vocations              -> lists your collection (and refreshes the client window)
-- !changevocation name    -> switches to an already unlocked vocation
function onSay(player, words, param)
	if words:lower() == "!changevocation" then
		local wanted = param:trim():lower()
		for _, entry in ipairs(VocationCollection.BASE) do
			if entry.name:lower() == wanted then
				local ok, err = VocationCollection.switch(player, entry.id)
				player:sendTextMessage(MESSAGE_INFO_DESCR, ok and ("Agora voce e " .. entry.name .. ".") or err)
				VocationCollection.sendList(player)
				return false
			end
		end
		player:sendCancelMessage("Uso: !changevocation Knight|Paladin|Sorcerer|Druid")
		return false
	end

	local state = VocationCollection.getState(player)
	local lines = {}
	for _, entry in ipairs(VocationCollection.BASE) do
		local s = state[entry.id]
		if s.unlocked then
			lines[#lines + 1] = string.format("%s: desbloqueada, %d/%d estrelas (%d fragmentos)", entry.name, s.stars, VocationCollection.getMaxStars(), s.fragments)
		else
			lines[#lines + 1] = string.format("%s: bloqueada (%d/%d)", entry.name, math.min(s.fragments, VocationCollection.FRAGMENTS_REQUIRED), VocationCollection.FRAGMENTS_REQUIRED)
		end
	end
	player:sendTextMessage(MESSAGE_INFO_DESCR, table.concat(lines, "\n"))
	VocationCollection.sendList(player)
	return false
end
