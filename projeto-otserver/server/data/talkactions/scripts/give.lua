local MAX_STACKABLE = 10000
local MAX_NON_STACKABLE = 100

function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	local usage = "Uso: /give nomeDoPlayer, itemId ou nome, quantidade"
	if param == "" then
		player:sendCancelMessage(usage)
		return false
	end

	local split = param:split(",")
	if #split < 2 then
		player:sendCancelMessage(usage)
		return false
	end

	local targetName = split[1]:trim()
	local itemInput = split[2]:trim()
	local count = math.floor(tonumber(split[3]) or 1)

	local itemType = ItemType(tonumber(itemInput) or itemInput)
	if itemType:getId() == 0 then
		player:sendCancelMessage("Item nao encontrado: " .. itemInput)
		return false
	end

	local stackable = itemType:isStackable()
	local maxCount = stackable and MAX_STACKABLE or MAX_NON_STACKABLE
	if count < 1 or count > maxCount then
		player:sendCancelMessage("A quantidade deve ser entre 1 e " .. maxCount .. " para este item.")
		return false
	end

	local itemId = itemType:getId()
	local itemName = itemType:getName()

	-- ONLINE
	local target = Player(targetName)
	if target then
		local item = target:addItem(itemId, count)
		if not item then
			local inbox = target:getInbox()
			if not inbox or not inbox:addItem(itemId, count, INDEX_WHEREEVER, FLAG_NOLIMIT) then
				player:sendCancelMessage("Nao foi possivel entregar o item.")
				return false
			end
		end

		target:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Voce recebeu %dx %s de %s.", count, itemName, player:getName()))
		target:getPosition():sendMagicEffect(CONST_ME_GIFT_WRAPS)
		player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Voce deu %dx %s para %s.", count, itemName, target:getName()))
		return false
	end

	-- OFFLINE
	local resultId = db.storeQuery("SELECT `id`, `name` FROM `players` WHERE `name` = " .. db.escapeString(targetName))
	if not resultId then
		player:sendCancelMessage("Jogador nao existe: " .. targetName)
		return false
	end

	local playerId = result.getNumber(resultId, "id")
	local realName = result.getString(resultId, "name")
	result.free(resultId)

	local nextSid = 100
	local sidResult = db.storeQuery("SELECT MAX(`sid`) AS `maxsid` FROM `player_inboxitems` WHERE `player_id` = " .. playerId)
	if sidResult then
		nextSid = math.max(nextSid, result.getNumber(sidResult, "maxsid"))
		result.free(sidResult)
	end

	local emptyAttr = db.escapeString("")
	local rows = {}
	local remaining = count
	while remaining > 0 do
		local amount = 1
		if stackable then
			amount = math.min(remaining, 100)
		end
		nextSid = nextSid + 1
		rows[#rows + 1] = "(" .. playerId .. ", 0, " .. nextSid .. ", " .. itemId .. ", " .. amount .. ", " .. emptyAttr .. ")"
		remaining = remaining - amount
	end

	local query = "INSERT INTO `player_inboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES " .. table.concat(rows, ", ")
	if not db.query(query) then
		player:sendCancelMessage("Erro ao salvar o item no banco de dados.")
		return false
	end

	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("%s esta offline. %dx %s enviados para o inbox.", realName, count, itemName))
	return false
end