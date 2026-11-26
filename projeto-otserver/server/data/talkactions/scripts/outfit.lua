function onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end
	local lookType = tonumber(param)
	if not lookType then
		player:sendCancelMessage("Use: /outfit ID")
		return false
	end
	local outfit = player:getOutfit()
	outfit.lookType = lookType
	player:setOutfit(outfit)
	return false
end