-- Vocation collection: fragments (virtual), permanent unlock and switching between
-- independent per-vocation progressions. Persistence lives in `player_vocation_collection`;
-- the actual snapshot/restore of level, skills, etc. is done in C++ (player:switchVocationProfile).

VocationCollection = {
	FRAGMENTS_REQUIRED = 100,
	OPCODE = 210,
	START_LEVEL = 8,
	-- base vocations that take part in the collection (ids from data/XML/vocations.xml)
	BASE = {
		{id = 4, name = "Knight", description = "Combate corpo a corpo e defesa."},
		{id = 3, name = "Paladin", description = "Combate a distancia."},
		{id = 1, name = "Sorcerer", description = "Magias de ataque (fogo, energia, morte)."},
		{id = 2, name = "Druid", description = "Magias de cura e gelo/terra."},
	},
}

local VC = VocationCollection

function VC.getEntry(baseId)
	for _, entry in ipairs(VC.BASE) do
		if entry.id == baseId then
			return entry
		end
	end
	return nil
end

local function baseIdOf(player)
	return player:getVocation():getBase():getId()
end

-- Creates missing rows and marks the character's current vocation as owned.
function VC.ensureRows(player)
	local guid = player:getGuid()
	for _, entry in ipairs(VC.BASE) do
		db.query(string.format("INSERT IGNORE INTO `player_vocation_collection` (`player_id`, `base_vocation`) VALUES (%d, %d)", guid, entry.id))
	end
	local current = baseIdOf(player)
	if VC.getEntry(current) then
		db.query(string.format("UPDATE `player_vocation_collection` SET `unlocked` = 1 WHERE `player_id` = %d AND `base_vocation` = %d", guid, current))
	end
end

-- Returns {[baseId] = {fragments, unlocked, hasProfile, level}}
function VC.getState(player)
	VC.ensureRows(player)
	local state = {}
	local res = db.storeQuery(string.format("SELECT `base_vocation`, `fragments`, `unlocked`, `has_profile`, `level` FROM `player_vocation_collection` WHERE `player_id` = %d", player:getGuid()))
	if res then
		repeat
			state[result.getNumber(res, "base_vocation")] = {
				fragments = result.getNumber(res, "fragments"),
				unlocked = result.getNumber(res, "unlocked") == 1,
				hasProfile = result.getNumber(res, "has_profile") == 1,
				level = result.getNumber(res, "level"),
			}
		until not result.next(res)
		result.free(res)
	end
	return state
end

function VC.addFragments(player, baseId, amount)
	if not VC.getEntry(baseId) or amount < 1 then
		return false
	end
	VC.ensureRows(player)
	db.query(string.format("UPDATE `player_vocation_collection` SET `fragments` = `fragments` + %d WHERE `player_id` = %d AND `base_vocation` = %d", amount, player:getGuid(), baseId))
	return true
end

-- Permanent unlock. Fragments are NOT consumed, so the 100/100 progress stays visible.
function VC.unlock(player, baseId)
	local state = VC.getState(player)[baseId]
	if not state then
		return false, "Vocation invalida."
	end
	if state.unlocked then
		return false, "Esta vocation ja esta desbloqueada."
	end
	if state.fragments < VC.FRAGMENTS_REQUIRED then
		return false, string.format("Fragmentos insuficientes (%d/%d).", state.fragments, VC.FRAGMENTS_REQUIRED)
	end
	db.query(string.format("UPDATE `player_vocation_collection` SET `unlocked` = 1 WHERE `player_id` = %d AND `base_vocation` = %d", player:getGuid(), baseId))
	return true
end

function VC.switch(player, baseId)
	if not VC.getEntry(baseId) then
		return false, "Vocation invalida."
	end
	if baseIdOf(player) == baseId then
		return false, "Voce ja esta usando esta vocation."
	end
	if not VC.getState(player)[baseId].unlocked then
		return false, "Vocation ainda nao desbloqueada."
	end
	if player:isPzLocked() or player:getCondition(CONDITION_INFIGHT) then
		return false, "Voce nao pode trocar de vocation em combate."
	end
	if not player:switchVocationProfile(baseId) then
		return false, "Nao foi possivel trocar de vocation."
	end
	return true
end

function VC.buildPayload(player)
	local state = VC.getState(player)
	local current = baseIdOf(player)
	local list = {}
	for _, entry in ipairs(VC.BASE) do
		local s = state[entry.id]
		local active = entry.id == current
		local level = VC.START_LEVEL
		if active then
			level = player:getLevel()
		elseif s.hasProfile then
			level = s.level
		end
		list[#list + 1] = {
			id = entry.id,
			name = entry.name,
			description = entry.description,
			clientId = Vocation(entry.id):getClientId(),
			fragments = math.min(s.fragments, VC.FRAGMENTS_REQUIRED),
			required = VC.FRAGMENTS_REQUIRED,
			unlocked = s.unlocked,
			active = active,
			level = level,
		}
	end
	return {vocations = list, current = current, playerName = player:getName()}
end

function VC.sendList(player)
	player:sendExtendedOpcode(VC.OPCODE, json.encode({action = "list", data = VC.buildPayload(player)}))
end

function VC.sendMessage(player, text, ok)
	player:sendExtendedOpcode(VC.OPCODE, json.encode({action = "msg", data = {text = text, ok = ok and true or false}}))
end

function VC.onOpcode(player, buffer)
	local ok, req = pcall(json.decode, buffer)
	if not ok or type(req) ~= "table" then
		return
	end
	local baseId = tonumber(req.vocation)
	if req.action == "fetch" then
		-- just send the list
	elseif req.action == "unlock" then
		local done, err = VC.unlock(player, baseId)
		VC.sendMessage(player, done and "Vocation desbloqueada!" or err, done)
	elseif req.action == "select" then
		local done, err = VC.switch(player, baseId)
		VC.sendMessage(player, done and "Vocation trocada!" or err, done)
	else
		return
	end
	VC.sendList(player)
end
