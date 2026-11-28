-- Vocation collection: fragments (virtual), permanent unlock, star evolution and switching between
-- independent per-vocation progressions. Persistence lives in `player_vocation_collection`;
-- the actual snapshot/restore of level, skills, etc. is done in C++ (player:switchVocationProfile).
--
-- Fragments: the first FRAGMENTS_REQUIRED unlock the vocation and are CONSUMED. After that, every
-- fragment (and any excess already held) is spent exclusively on stars. Costs and bonuses of the stars
-- live in vocation_stars_config.lua. The star bonus is never written to skills: C++ derives it on read
-- from the percentage set by player:setVocationStarBonus (see VC.applyStarBonus).

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

-- Returns {[baseId] = {fragments, unlocked, stars, hasProfile, level}}
function VC.getState(player)
	VC.ensureRows(player)
	local state = {}
	local res = db.storeQuery(string.format("SELECT `base_vocation`, `fragments`, `unlocked`, `stars`, `has_profile`, `level` FROM `player_vocation_collection` WHERE `player_id` = %d", player:getGuid()))
	if res then
		repeat
			state[result.getNumber(res, "base_vocation")] = {
				fragments = result.getNumber(res, "fragments"),
				unlocked = result.getNumber(res, "unlocked") == 1,
				stars = math.max(1, math.min(result.getNumber(res, "stars"), VocationStarsConfig.MAX_STARS)),
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

-- Permanent unlock. Consumes FRAGMENTS_REQUIRED fragments; whatever is left is kept for stars.
-- The UPDATE is guarded (still locked, enough fragments), so a repeated or concurrent request can
-- neither unlock twice nor consume fragments twice.
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
	db.query(string.format("UPDATE `player_vocation_collection` SET `unlocked` = 1, `fragments` = `fragments` - %d WHERE `player_id` = %d AND `base_vocation` = %d AND `unlocked` = 0 AND `fragments` >= %d", VC.FRAGMENTS_REQUIRED, player:getGuid(), baseId, VC.FRAGMENTS_REQUIRED))
	if not VC.getState(player)[baseId].unlocked then
		return false, "Nao foi possivel desbloquear a vocation."
	end
	return true
end

-- Stars -----------------------------------------------------------------------------------------

function VC.getMaxStars()
	return VocationStarsConfig.MAX_STARS
end

-- Fragments needed to go from `stars` to `stars + 1` (nil when already at the maximum).
function VC.getStarCost(stars)
	if stars >= VC.getMaxStars() then
		return nil
	end
	return VocationStarsConfig.LEVELS[stars + 1].fragments
end

-- Bonus is handled in basis points (1% = 100) so sums and the value sent to C++ are exact integers.
local function toBasisPoints(percent)
	return math.floor(percent * 100 + 0.5)
end

-- Extra percentage (in basis points) granted by the next star, nil at the maximum.
function VC.getNextStarBonusBp(stars)
	if stars >= VC.getMaxStars() then
		return nil
	end
	return toBasisPoints(VocationStarsConfig.LEVELS[stars + 1].bonus)
end

-- Total bonus (in basis points) of a given amount of stars: additive sum of the stars reached.
function VC.getStarBonusBp(stars)
	local total = 0
	for star = 2, math.min(stars, VC.getMaxStars()) do
		total = total + toBasisPoints(VocationStarsConfig.LEVELS[star].bonus)
	end
	return total
end

-- Pushes the bonus of the ACTIVE vocation to the engine. Must run after login, evolve and switch.
-- The engine only keeps it in memory and applies it on top of the base skills when they are read.
function VC.applyStarBonus(player)
	local state = VC.getState(player)[baseIdOf(player)]
	player:setVocationStarBonus(state and VC.getStarBonusBp(state.stars) or 0)
end

local evolving = {}

-- Spends fragments to raise one vocation by one star. Everything is decided here, on the server.
-- The UPDATE re-checks stars and fragments, so a duplicated request cannot consume twice.
function VC.evolve(player, baseId)
	local guid = player:getGuid()
	if evolving[guid] then
		return false, "Evolucao em andamento."
	end
	evolving[guid] = true
	local ok, done, err = pcall(function()
		local state = VC.getState(player)[baseId]
		if not state then
			return false, "Vocation invalida."
		end
		if not state.unlocked then
			return false, "Desbloqueie a vocation antes de evoluir."
		end
		local cost = VC.getStarCost(state.stars)
		if not cost then
			return false, "Esta vocation ja alcancou o nivel maximo de evolucao."
		end
		if state.fragments < cost then
			return false, string.format("Fragmentos insuficientes (%d/%d).", state.fragments, cost)
		end
		db.query(string.format("UPDATE `player_vocation_collection` SET `stars` = `stars` + 1, `fragments` = `fragments` - %d WHERE `player_id` = %d AND `base_vocation` = %d AND `unlocked` = 1 AND `stars` = %d AND `fragments` >= %d", cost, guid, baseId, state.stars, cost))
		if VC.getState(player)[baseId].stars ~= state.stars + 1 then
			return false, "Nao foi possivel evoluir a vocation."
		end
		return true
	end)
	evolving[guid] = nil
	if not ok then
		print("[VocationCollection] evolve error: " .. tostring(done))
		return false, "Nao foi possivel evoluir a vocation."
	end
	if done and baseId == baseIdOf(player) then
		VC.applyStarBonus(player)
	end
	return done, err
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
	VC.applyStarBonus(player)
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
			-- locked: progress towards the unlock (capped); unlocked: fragments available for stars
			fragments = s.unlocked and s.fragments or math.min(s.fragments, VC.FRAGMENTS_REQUIRED),
			required = VC.FRAGMENTS_REQUIRED,
			unlocked = s.unlocked,
			stars = s.stars,
			maxStars = VC.getMaxStars(),
			nextCost = VC.getStarCost(s.stars),
			bonus = VC.getStarBonusBp(s.stars) / 100,
			nextBonus = VC.getNextStarBonusBp(s.stars) and VC.getNextStarBonusBp(s.stars) / 100 or nil,
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
	elseif req.action == "evolve" then
		local done, err = VC.evolve(player, baseId)
		VC.sendMessage(player, done and "Vocation evoluida!" or err, done)
	elseif req.action == "select" then
		local done, err = VC.switch(player, baseId)
		VC.sendMessage(player, done and "Vocation trocada!" or err, done)
	else
		return
	end
	VC.sendList(player)
end
