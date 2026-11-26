GiantSprite = {}

GiantSprite.list = {}

local DIRECTIONS = {DIRECTION_NORTH, DIRECTION_EAST, DIRECTION_SOUTH, DIRECTION_WEST}

local function resolve(source, overrides)
  local base = type(source) == "string" and GiantSprite.list[source] or source
  if type(base) ~= "table" then
    print("[GiantSprite] animação inválida: " .. tostring(source))
    return nil
  end

  local cfg = {grid = base.grid}
  for _, dir in ipairs(DIRECTIONS) do
    local b = base[dir]
    if b then
      cfg[dir] = {effect = b.effect, col = b.col, row = b.row, grid = b.grid}
    end
  end

  if overrides then
    if overrides.grid then cfg.grid = overrides.grid end
    for _, dir in ipairs(DIRECTIONS) do
      local d = cfg[dir]
      if d then
        if overrides.effect then d.effect = overrides.effect end
        if overrides.effects and overrides.effects[dir] then d.effect = overrides.effects[dir] end
        if overrides[dir] then
          for k, v in pairs(overrides[dir]) do d[k] = v end
        end
      end
    end
  end

  return cfg
end

function GiantSprite.playAt(position, direction, source, overrides)
  local cfg = resolve(source, overrides)
  if not cfg then return false end

  local d = cfg[direction]
  local g = d and (d.grid or cfg.grid)
  if not d or not g then
    print("[GiantSprite] sem config/grid para a direção " .. tostring(direction))
    return false
  end

  local dx = g.w - d.col
  local dy = g.h - d.row
  Position(position.x + dx, position.y + dy, position.z):sendMagicEffect(d.effect)
  return true
end

function GiantSprite.play(creature, source, overrides)
  return GiantSprite.playAt(creature:getPosition(), creature:getDirection(), source, overrides)
end