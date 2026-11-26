// Free Movement - Fase 2: implementação do corpo físico (ver physicsbody.h).
// Sem dependências do engine: só C++ padrão e physicalposition.h.

#include "physicsbody.h"

#include <algorithm>

namespace Physics {

namespace {

// 1/sqrt(2) em ponto fixo 16 bits (46341 / 65536 = 0.707108): normaliza a diagonal.
constexpr int64_t DIAGONAL_FACTOR = 46341;
constexpr int32_t DIAGONAL_SHIFT = 16;

bool tileBlocked(ICollisionWorld& world, int32_t tx, int32_t ty, uint8_t z)
{
	// Limites do mapa e do formato de Position (uint16): fora disso é parede.
	if (tx < 0 || ty < 0 || tx > MAX_TILE_COORD || ty > MAX_TILE_COORD) {
		return true;
	}
	return world.isTileBlocked(tx, ty, z);
}

// Testa uma "coluna" de tiles: o tile 'along' no eixo do movimento, varrendo
// os tiles [crossMin, crossMax] no outro eixo (a largura da caixa).
bool lineBlocked(ICollisionWorld& world, bool horizontal, int32_t along, int32_t crossMin, int32_t crossMax, uint8_t z)
{
	for (int32_t c = crossMin; c <= crossMax; ++c) {
		const bool blocked = horizontal ? tileBlocked(world, along, c, z) : tileBlocked(world, c, along, z);
		if (blocked) {
			return true;
		}
	}
	return false;
}

} // namespace

void PhysicsBody::setPosition(const PhysicalPosition& p)
{
	pos = p;
	velX = velY = 0;
	remX = remY = 0;
}

void PhysicsBody::setHalfExtent(int32_t hx, int32_t hy)
{
	// Mantém a caixa menor que um tile: o sub-passo e a varredura dependem disso.
	halfX = std::clamp<int32_t>(hx, 1, TILE_CENTER - 1);
	halfY = std::clamp<int32_t>(hy, 1, TILE_CENTER - 1);
}

void PhysicsBody::setInput(int32_t dirX, int32_t dirY)
{
	inputX = std::clamp<int32_t>(dirX, -1, 1);
	inputY = std::clamp<int32_t>(dirY, -1, 1);
}

void PhysicsBody::stop()
{
	inputX = inputY = 0;
	velX = velY = 0;
	remX = remY = 0;
}

void PhysicsBody::computeDesiredVelocity(int32_t& vx, int32_t& vy) const
{
	vx = vy = 0;
	if (locked || maxSpeed <= 0 || (inputX == 0 && inputY == 0)) {
		return;
	}

	int64_t speed = std::min<int64_t>(maxSpeed, MAX_SPEED);
	if (inputX != 0 && inputY != 0) {
		speed = (speed * DIAGONAL_FACTOR) >> DIAGONAL_SHIFT;
	}
	vx = static_cast<int32_t>(inputX * speed);
	vy = static_cast<int32_t>(inputY * speed);
}

MoveResult PhysicsBody::update(uint32_t dtMs, ICollisionWorld& world)
{
	int32_t vx, vy;
	computeDesiredVelocity(vx, vy);
	if (vx == 0 && vy == 0) {
		velX = velY = 0;
		remX = remY = 0;
		return MoveResult::NONE;
	}

	dtMs = std::min(dtMs, MAX_DT_MS);
	if (dtMs == 0) {
		return MoveResult::NONE;
	}

	// Deslocamento em unidades inteiras; o resto fica acumulado para o próximo tick.
	remX += static_cast<int64_t>(vx) * dtMs;
	remY += static_cast<int64_t>(vy) * dtMs;
	int32_t dx = static_cast<int32_t>(remX / 1000);
	int32_t dy = static_cast<int32_t>(remY / 1000);
	remX -= static_cast<int64_t>(dx) * 1000;
	remY -= static_cast<int64_t>(dy) * 1000;

	bool blockedX = false;
	bool blockedY = false;

	// Sub-passos alternando os eixos: o corpo "desliza" ao encostar em um deles.
	while ((dx != 0 && !blockedX) || (dy != 0 && !blockedY)) {
		if (dx != 0 && !blockedX) {
			const int32_t step = std::clamp<int32_t>(dx, -MAX_STEP, MAX_STEP);
			dx -= step;
			const AxisResult r = moveAxis(true, step, world);
			if (r == AxisResult::RELOCATED) {
				return MoveResult::RELOCATED;
			}
			if (r == AxisResult::BLOCKED) {
				blockedX = true;
			}
		}
		if (dy != 0 && !blockedY) {
			const int32_t step = std::clamp<int32_t>(dy, -MAX_STEP, MAX_STEP);
			dy -= step;
			const AxisResult r = moveAxis(false, step, world);
			if (r == AxisResult::RELOCATED) {
				return MoveResult::RELOCATED;
			}
			if (r == AxisResult::BLOCKED) {
				blockedY = true;
			}
		}
	}

	velX = blockedX ? 0 : vx;
	velY = blockedY ? 0 : vy;
	if (blockedX) {
		remX = 0;
	}
	if (blockedY) {
		remY = 0;
	}
	return (blockedX || blockedY) ? MoveResult::BLOCKED : MoveResult::MOVED;
}

// Move o corpo 'delta' unidades (|delta| <= MAX_STEP) em um eixo.
// A caixa ocupa [c - half, c + half) em cada eixo (intervalo semiaberto, então
// encostar exatamente na borda de um tile NÃO conta como sobreposição).
PhysicsBody::AxisResult PhysicsBody::moveAxis(bool horizontal, int32_t delta, ICollisionWorld& world)
{
	if (delta == 0) {
		return AxisResult::OK;
	}

	int32_t& c = horizontal ? pos.x : pos.y;
	const int32_t half = horizontal ? halfX : halfY;
	const int32_t other = horizontal ? pos.y : pos.x;
	const int32_t otherHalf = horizontal ? halfY : halfX;

	const int32_t oldC = c;
	int32_t newC = oldC + delta;
	bool blocked = false;

	// Faixa de tiles ocupada pela caixa no eixo transversal.
	const int32_t crossMin = unitsToTile(other - otherHalf);
	const int32_t crossMax = unitsToTile(other + otherHalf - 1);

	// 1) Terreno: só os tiles NOVOS na direção do movimento são verificados.
	//    Tiles já sobrepostos são ignorados, o que evita ficar preso se a
	//    criatura nascer/for colocada encostada ou sobrepondo uma parede.
	if (delta > 0) {
		const int32_t oldMax = unitsToTile(oldC + half - 1);
		const int32_t newMax = unitsToTile(newC + half - 1);
		for (int32_t t = oldMax + 1; t <= newMax; ++t) {
			if (lineBlocked(world, horizontal, t, crossMin, crossMax, pos.z)) {
				newC = tileToUnits(t) - half; // borda da caixa encosta no início do tile t
				blocked = true;
				break;
			}
		}
	} else {
		const int32_t oldMin = unitsToTile(oldC - half);
		const int32_t newMin = unitsToTile(newC - half);
		for (int32_t t = oldMin - 1; t >= newMin; --t) {
			if (lineBlocked(world, horizontal, t, crossMin, crossMax, pos.z)) {
				newC = tileToUnits(t + 1) + half; // borda da caixa encosta no fim do tile t
				blocked = true;
				break;
			}
		}
	}
	c = newC;

	// 2) Tile lógico: se o CENTRO passou para outro tile, o engine decide.
	const int32_t oldTile = unitsToTile(oldC);
	const int32_t newTile = unitsToTile(c);
	if (newTile != oldTile) {
		const int32_t dirTile = newTile > oldTile ? 1 : -1;
		TileRef relocated;
		const CrossResult cr = world.onCenterCross(horizontal ? dirTile : 0, horizontal ? 0 : dirTile, relocated);
		if (cr == CrossResult::RELOCATED) {
			pos = PhysicalPosition::fromTile(relocated); // centro do tile de destino
			velX = velY = 0;
			remX = remY = 0;
			return AxisResult::RELOCATED;
		}
		if (cr == CrossResult::BLOCKED) {
			// Volta o centro para dentro do tile de origem, colado na borda.
			c = (dirTile > 0) ? tileToUnits(newTile) - 1 : tileToUnits(oldTile);
			blocked = true;
		}
	}

	return blocked ? AxisResult::BLOCKED : AxisResult::OK;
}

} // namespace Physics
