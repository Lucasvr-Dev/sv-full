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
		lines[#lines + 1] = string.format("%s: %s (%d/%d)", entry.name, s.unlocked and "desbloqueada" or "bloqueada", math.min(s.fragments, VocationCollection.FRAGMENTS_REQUIRED), VocationCollection.FRAGMENTS_REQUIRED)
	end
	player:sendTextMessage(MESSAGE_INFO_DESCR, table.concat(lines, "\n"))
	VocationCollection.sendList(player)
	return false
end
