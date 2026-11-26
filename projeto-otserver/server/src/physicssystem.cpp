// Free Movement - Fase 2: ver physicssystem.h.

#include "otpch.h"

#include "physicssystem.h"

#include "creature.h"
#include "game.h"
#include "player.h"
#include "scheduler.h"
#include "tile.h"

extern Game g_game;

PhysicsSystem g_physics;

namespace {

using Physics::CrossResult;
using Physics::TileRef;

// Colisão inicial por tile: reaproveita as regras que o walking legado já usa
// (Tile::queryAdd e Game::internalMoveCreature), sem duplicá-las.
// A Fase 5 troca/complementa isto por AABB de itens e criaturas.
class TileCollisionWorld final : public Physics::ICollisionWorld
{
public:
	explicit TileCollisionWorld(Creature* creature) : creature(creature) {}

	bool isTileBlocked(int32_t tileX, int32_t tileY, uint8_t z) override
	{
		const Tile* tile = g_game.map.getTile(static_cast<uint16_t>(tileX), static_cast<uint16_t>(tileY), z);
		if (!tile) {
			return true;
		}
		// Só o terreno e itens bloqueiam a caixa; criaturas são tratadas na travessia do
		// centro (regra legada de uma criatura por tile) até a Fase 5.
		return tile->queryAdd(0, *creature, 1, FLAG_IGNOREBLOCKCREATURE) != RETURNVALUE_NOERROR;
	}

	CrossResult onCenterCross(int32_t dx, int32_t dy, TileRef& relocated) override
	{
		Direction dir;
		if (dx > 0) {
			dir = DIRECTION_EAST;
		} else if (dx < 0) {
			dir = DIRECTION_WEST;
		} else if (dy > 0) {
			dir = DIRECTION_SOUTH;
		} else {
			dir = DIRECTION_NORTH;
		}

		const Position before = creature->getPosition();
		const Position expected(before.x + dx, before.y + dy, before.z);

		// internalMoveCreature faz queryAdd, escadas/teleporte, Map::moveCreature e
		// dispara onCreatureMove/onStepIn para os spectators, igual ao walking legado.
		if (g_game.internalMoveCreature(creature, dir) != RETURNVALUE_NOERROR || creature->isRemoved()) {
			return CrossResult::BLOCKED;
		}

		const Position& after = creature->getPosition();
		if (after == expected) {
			return CrossResult::MOVED;
		}

		relocated.x = after.x;
		relocated.y = after.y;
		relocated.z = after.z;
		return CrossResult::RELOCATED;
	}

private:
	Creature* creature;
};

} // namespace

bool PhysicsSystem::enable(Creature* creature)
{
	if (!creature || creature->isRemoved() || isEnabled(creature->getID())) {
		return false;
	}

	Entry& entry = entries[creature->getID()];
	entry.body.setPosition(Physics::PhysicalPosition::fromTile(creature->getPosition()));
	pendingDisable.erase(creature->getID());
	if (!scheduled) {
		lastTickTime = OTSYS_TIME(); // primeiro tick mede o tempo a partir de agora
	}
	ensureScheduled();
	return true;
}

void PhysicsSystem::disable(uint32_t creatureId)
{
	if (inTick) {
		pendingDisable.insert(creatureId); // não destrói a entrada no meio do update
		return;
	}
	entries.erase(creatureId);
}

Physics::PhysicsBody* PhysicsSystem::getBody(uint32_t creatureId)
{
	auto it = entries.find(creatureId);
	return it == entries.end() ? nullptr : &it->second.body;
}

bool PhysicsSystem::handleInput(Player* player, uint8_t seq, int32_t dirX, int32_t dirY)
{
	if (!player || player->isRemoved()) {
		return false;
	}

	if (!isEnabled(player->getID()) && !enable(player)) {
		return false;
	}

	Entry& entry = entries[player->getID()];
	const int64_t now = OTSYS_TIME();

	// Limite de taxa: parar nunca é descartado (senão um flood poderia impedir o jogador de parar).
	const bool isStop = dirX == 0 && dirY == 0;
	if (!isStop && !entry.bucket.tryTake(now)) {
		return false;
	}

	entry.ackSeq = seq;
	entry.lastInputTime = now;
	entry.forceSend = true; // o dono precisa do ack e dos demais da nova velocidade
	entry.body.setInput(dirX, dirY);
	pendingDisable.erase(player->getID());
	return true;
}

void PhysicsSystem::ensureScheduled()
{
	if (scheduled) {
		return;
	}
	scheduled = true;
	g_scheduler.addEvent(createSchedulerTask(TICK_INTERVAL_MS, std::bind(&PhysicsSystem::tick, this)));
}

void PhysicsSystem::broadcastState(Creature* creature, const FreeMoveProto::State& state)
{
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, creature->getPosition(), true, true);
	for (Creature* spectator : spectators) {
		Player* spectatorPlayer = spectator->getPlayer();
		if (spectatorPlayer && spectatorPlayer->supportsFreeMove()) {
			spectatorPlayer->sendFreeMoveState(state);
		}
	}
}

void PhysicsSystem::tick()
{
	scheduled = false;

	const int64_t now = OTSYS_TIME();
	const int64_t elapsed = now - lastTickTime;
	lastTickTime = now;
	const uint32_t dt = static_cast<uint32_t>(std::clamp<int64_t>(elapsed, 1, Physics::PhysicsBody::MAX_DT_MS));

	// Copia os ids: scripts disparados pelo movimento podem mexer em 'entries'.
	std::vector<uint32_t> ids;
	ids.reserve(entries.size());
	for (const auto& kv : entries) {
		ids.push_back(kv.first);
	}

	inTick = true;
	for (uint32_t id : ids) {
		auto it = entries.find(id);
		if (it == entries.end() || pendingDisable.count(id)) {
			continue;
		}

		Creature* creature = g_game.getCreatureByID(id);
		if (!creature || creature->isRemoved()) {
			pendingDisable.insert(id);
			continue;
		}

		Entry& entry = it->second;
		Physics::PhysicsBody& body = entry.body;
		uint8_t flags = entry.pendingFlags;
		entry.pendingFlags = 0;

		// Se algo moveu a criatura por fora (teleporte de script, walking legado, respawn),
		// a física ressincroniza no centro do novo tile e o cliente deve "snapar".
		if (body.getPosition().toTile<Position>() != creature->getPosition()) {
			body.setPosition(Physics::PhysicalPosition::fromTile(creature->getPosition()));
			flags |= FreeMoveProto::FLAG_TELEPORT;
			entry.forceSend = true;
		}

		// Cliente travado/desconectado sem fechar o socket: sem keepalive o corpo para.
		if ((body.getInputX() != 0 || body.getInputY() != 0) && now - entry.lastInputTime > INPUT_TIMEOUT_MS) {
			body.setInput(0, 0);
			entry.forceSend = true;
		}

		// Mesma fonte de velocidade do walking legado (atributo speed + velocidade do piso):
		// um tile inteiro por getStepDuration() milissegundos.
		const int64_t stepDuration = creature->getStepDuration();
		body.setMaxSpeed(stepDuration > 0 ? static_cast<int32_t>(Physics::UNITS_PER_TILE * 1000 / stepDuration) : 0);

		// Mesmas travas do walking legado (Game::playerMove): stun/root/ação travada.
		// Cobre players, monstros e NPCs. O corpo fica parado e o STATE leva FLAG_LOCKED.
		body.setLocked(creature->isMovementBlocked());

		TileCollisionWorld world(creature);
		const Physics::MoveResult result = body.update(dt, world);

		if (result == Physics::MoveResult::RELOCATED) {
			flags |= FreeMoveProto::FLAG_TELEPORT; // escada/teleporte: o cliente snapa
			entry.forceSend = true;
		} else if (result == Physics::MoveResult::BLOCKED) {
			flags |= FreeMoveProto::FLAG_BLOCKED;
		}
		if (body.isLocked()) {
			flags |= FreeMoveProto::FLAG_LOCKED;
		}

		const FreeMoveProto::State state = FreeMoveProto::makeState(id, body.getPosition(), body.getVelocityX(),
		                                                           body.getVelocityY(), flags, entry.ackSeq);
		if (entry.policy.shouldSend(now, state, entry.forceSend)) {
			broadcastState(creature, state);
		}
		entry.forceSend = false;
	}
	inTick = false;

	for (uint32_t id : pendingDisable) {
		entries.erase(id);
	}
	pendingDisable.clear();

	if (!entries.empty()) {
		ensureScheduled();
	}
}
