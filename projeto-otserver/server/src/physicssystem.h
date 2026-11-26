// Free Movement: ligação entre PhysicsBody e o engine (Creature/Game/Map) e rede.
//
// Nenhuma criatura entra neste sistema até alguém chamar enable() ou chegar um INPUT de
// um jogador cujo cliente fez HELLO (com config freeMovement = true). Com o recurso
// desligado o comportamento do servidor não muda.

#ifndef FS_PHYSICSSYSTEM_H
#define FS_PHYSICSSYSTEM_H

#include <unordered_map>
#include <unordered_set>

#include "freemovepolicy.h"
#include "physicsbody.h"

class Creature;
class Player;

class PhysicsSystem
{
public:
	// Intervalo do tick de física. 50 ms = mesma granularidade do walking legado.
	static constexpr uint32_t TICK_INTERVAL_MS = 50;
	// Sem INPUT novo por este tempo o corpo para sozinho (cliente travado/desligado).
	// O cliente reenvia o input ativo a cada ~250 ms como keepalive.
	static constexpr int64_t INPUT_TIMEOUT_MS = 1000;

	// Coloca a criatura sob controle da física contínua, a partir do centro do tile atual.
	// Retorna false se a criatura for nula, removida ou já estiver ativa.
	bool enable(Creature* creature);

	// Tira a criatura da física. Seguro de chamar durante o tick (a remoção é adiada).
	void disable(uint32_t creatureId);

	bool isEnabled(uint32_t creatureId) const { return entries.find(creatureId) != entries.end(); }
	Physics::PhysicsBody* getBody(uint32_t creatureId);

	// INPUT de um jogador (já validado pelo protocolo: direções em -1..1).
	// Ativa a física do jogador se ainda não estiver ativa. Retorna false se descartado
	// (limite de taxa). Parar (0,0) nunca é descartado.
	bool handleInput(Player* player, uint8_t seq, int32_t dirX, int32_t dirY);

	// Avança todos os corpos e envia os STATEs necessários. Chamado pelo scheduler.
	void tick();

private:
	struct Entry {
		Physics::PhysicsBody body;
		FreeMove::TokenBucket bucket{20, 20}; // 20 inputs/s, rajada de 20 (o limite global é 25 pacotes/s)
		FreeMove::StateSendPolicy policy;
		int64_t lastInputTime = 0;
		uint8_t ackSeq = 0;
		bool forceSend = false;
		uint8_t pendingFlags = 0;
	};

	void ensureScheduled();
	void broadcastState(Creature* creature, const FreeMoveProto::State& state);

	std::unordered_map<uint32_t, Entry> entries;
	std::unordered_set<uint32_t> pendingDisable;
	int64_t lastTickTime = 0;
	bool scheduled = false;
	bool inTick = false;
};

extern PhysicsSystem g_physics;

#endif // FS_PHYSICSSYSTEM_H
