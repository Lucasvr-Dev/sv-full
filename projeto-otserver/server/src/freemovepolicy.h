// Free Movement - Fase 3 (somente servidor): políticas puras de rede.
//  - TokenBucket: limite de taxa dos INPUTs por jogador (defesa contra flood).
//  - StateSendPolicy: decide QUANDO enviar o STATE de uma criatura, para economizar banda.
// Sem dependências do engine, para poder ser testado isoladamente.

#ifndef FS_FREEMOVEPOLICY_H
#define FS_FREEMOVEPOLICY_H

#include <algorithm>
#include <cstdint>

#include "freemoveprotocol.h"

namespace FreeMove {

// Balde de fichas em milésimos (inteiro, sem float). 'perSecond' fichas por segundo, até 'capacity'.
class TokenBucket
{
public:
	TokenBucket(uint32_t capacity = 20, uint32_t perSecond = 20) : capacity(capacity), perSecond(perSecond), milli(static_cast<int64_t>(capacity) * 1000) {}

	// true se há ficha (e a consome). 'now' em ms, monotônico.
	bool tryTake(int64_t now)
	{
		if (lastTime != 0 && now > lastTime) {
			milli = std::min<int64_t>(static_cast<int64_t>(capacity) * 1000, milli + (now - lastTime) * perSecond);
		}
		lastTime = now;
		if (milli < 1000) {
			return false;
		}
		milli -= 1000;
		return true;
	}

private:
	uint32_t capacity;
	uint32_t perSecond;
	int64_t milli;
	int64_t lastTime = 0;
};

// Estratégia "posição + velocidade": o cliente extrapola entre pacotes, então só enviamos
//  (a) quando algo muda (velocidade, flags; TELEPORT é de envio único e não conta) ou o servidor força (input, teleporte);
//  (b) a cada MOVING_INTERVAL_MS enquanto se move (ressincroniza drift);
//  (c) a cada STATIONARY_INTERVAL_MS se estiver parado FORA do centro do tile, para quem
//      acabou de entrar no campo de visão (quem está no centro dispensa estado: vale o tile).
class StateSendPolicy
{
public:
	static constexpr int64_t MOVING_INTERVAL_MS = 200;
	static constexpr int64_t STATIONARY_INTERVAL_MS = 1000;

	bool shouldSend(int64_t now, const FreeMoveProto::State& s, bool forced)
	{
		const bool interesting = s.isMoving() || !s.isCentered() || s.flags != 0;

		bool send = false;
		if (!hasLast) {
			send = forced || interesting;
		} else if (forced || s.velX != last.velX || s.velY != last.velY || ((s.flags ^ last.flags) & ~FreeMoveProto::FLAG_TELEPORT) != 0) {
			send = true;
		} else if (s.isMoving()) {
			send = (now - lastTime) >= MOVING_INTERVAL_MS;
		} else if (!s.isCentered()) {
			send = (now - lastTime) >= STATIONARY_INTERVAL_MS;
		}

		if (send) {
			last = s;
			lastTime = now;
			hasLast = true;
		}
		return send;
	}

	void reset() { hasLast = false; }

private:
	FreeMoveProto::State last;
	int64_t lastTime = 0;
	bool hasLast = false;
};

} // namespace FreeMove

#endif // FS_FREEMOVEPOLICY_H
