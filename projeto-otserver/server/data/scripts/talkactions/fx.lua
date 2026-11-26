local fx = TalkAction("/fx")

function fx.onSay(player, words, param)
	player:sendTextMessage(MESSAGE_INFO_DESCR, "fx recebido: '" .. tostring(param) .. "'")

	if not player:getGroup():getAccess() then
		player:sendCancelMessage("Sem acesso: seu personagem precisa ser GM/God.")
		return false
	end

	local id = tonumber(param)
	if not id or id < 0 or id > 65535 then
		player:sendCancelMessage("Uso: /fx <id de 0 a 65535>")
		return false
	end

	local pos = player:getPosition()
	pos:sendMagicEffect(id)

	local alvo = Position(pos.x + 3, pos.y, pos.z)
	pos:sendDistanceEffect(alvo, id)

	player:sendTextMessage(MESSAGE_INFO_DESCR, "fx " .. id .. " enviado.")
	return false
end

fx:separator(" ")
fx:register()
