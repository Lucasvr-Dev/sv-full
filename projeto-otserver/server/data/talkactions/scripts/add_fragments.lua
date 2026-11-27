-- /addfragments vocation, amount            (adds to yourself)
-- /addfragments playerName, vocation, amount
-- vocation: Knight, Paladin, Sorcerer, Druid (or base id 1-4). Test command, GM only.
function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	local split = param:split(",")
	local target = player
	if #split >= 3 then
		target = Player(split[1]:trim())
		table.remove(split, 1)
		if not target then
			player:sendCancelMessage("Jogador nao encontrado ou offline.")
			return false
		end
	end

	local vocText = split[1] and split[1]:trim() or ""
	local amount = split[2] and tonumber(split[2]:trim())
	local baseId = tonumber(vocText)
	if not baseId then
		for _, entry in ipairs(VocationCollection.BASE) do
			if entry.name:lower() == vocText:lower() then
				baseId = entry.id
			end
		end
	end

	if not baseId or not VocationCollection.getEntry(baseId) or not amount or amount < 1 then
		player:sendCancelMessage("Uso: /addfragments [jogador,] vocation, quantidade  (Knight, Paladin, Sorcerer, Druid)")
		return false
	end

	VocationCollection.addFragments(target, baseId, amount)
	local name = VocationCollection.getEntry(baseId).name
	local frags = VocationCollection.getState(target)[baseId].fragments
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("+%d fragmentos de %s para %s (total %d/%d).", amount, name, target:getName(), frags, VocationCollection.FRAGMENTS_REQUIRED))
	if target ~= player then
		target:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Voce recebeu %d fragmentos de %s.", amount, name))
	end
	VocationCollection.sendList(target)
	return false
end
