-- Direção cardinal de `from` até `to`.
function getDirectionTo(from, to)
	local dx = to.x - from.x
	local dy = to.y - from.y

	if math.abs(dx) > math.abs(dy) then
		return dx > 0 and DIRECTION_EAST or DIRECTION_WEST
	end

	return dy > 0 and DIRECTION_SOUTH or DIRECTION_NORTH
end

-- Vira a criatura para o alvo.
function faceTarget(creature)
	local target = creature:getTarget()
	if not target then
		return false
	end

	local from = creature:getPosition()
	local to = target:getPosition()
	local currentDirection = creature:getDirection()

	local dx = to.x - from.x
	local dy = to.y - from.y

	-- Alvo na diagonal.
	if dx ~= 0 and dy ~= 0 then
		-- Corrige a orientação horizontal.
		if dx < 0 and currentDirection == DIRECTION_EAST then
			creature:setDirection(DIRECTION_WEST)
		elseif dx > 0 and currentDirection == DIRECTION_WEST then
			creature:setDirection(DIRECTION_EAST)
		end

		-- Corrige a orientação vertical.
		if dy < 0 and currentDirection == DIRECTION_SOUTH then
			creature:setDirection(DIRECTION_NORTH)
		elseif dy > 0 and currentDirection == DIRECTION_NORTH then
			creature:setDirection(DIRECTION_SOUTH)
		end

		return true
	end

	-- Demais posições: comportamento cardinal normal.
	local needed = getDirectionTo(from, to)

	if currentDirection ~= needed then
		creature:setDirection(needed)
	end

	return true
end