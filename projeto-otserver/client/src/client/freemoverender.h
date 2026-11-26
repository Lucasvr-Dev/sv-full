// Free Movement - Fase 4 (somente cliente): posição de RENDER de uma criatura a partir dos
// STATEs do servidor. Lógica pura (sem Creature/Map/relógio do engine): recebe o tempo em ms.
//
// Técnica: "extrapolação + offset de correção que decai".
//   alvo(t)   = posição do último STATE + velocidade * min(idade, MAX_EXTRAPOLATION_MS)
//   render(t) = alvo(t) + correção(t)
// Em regime não há atraso: a criatura é desenhada onde o servidor diz que ela está. Quando
// chega um STATE que discorda da extrapolação, o erro entra em 'correção' (o render continua
// contínuo, sem salto) e decai exponencialmente, em vez de "teleportar" a criatura.
// TELEPORT ou erro muito grande: snap imediato.
//
// Coordenadas em pixels de MUNDO absolutos (double: float32 perderia precisão em ~2M px).

#ifndef FREEMOVEMENT_FREEMOVERENDER_H
#define FREEMOVEMENT_FREEMOVERENDER_H

#include <cmath>
#include <cstdint>

#include "freemoveprotocol.h"

namespace FreeMove {

struct Vec2 {
	double x = 0.0;
	double y = 0.0;
};

class RenderSmoother
{
public:
	static constexpr int64_t MAX_EXTRAPOLATION_MS = 250; // o servidor reenvia a cada 200 ms enquanto se move
	static constexpr double SNAP_DISTANCE_PX = 64.0;     // 2 tiles: acima disso é snap, não suavização
	static constexpr double CORRECTION_TAU_MS = 90.0;    // constante de tempo do decaimento da correção
	static constexpr double CORRECTION_EPSILON_PX = 0.25;

	// Novo STATE. worldX/worldY em px absolutos (tile*32 + offset), vel em px/s, now em ms.
	// Retorna true se a criatura foi "snapada" (sem suavização).
	bool onState(double worldX, double worldY, double velX, double velY, uint8_t flags, int64_t now)
	{
		bool snapped = true;
		if (has) {
			decay(now);
			const Vec2 oldTarget = targetAt(now);
			const double dx = oldTarget.x - worldX;
			const double dy = oldTarget.y - worldY;
			const bool teleport = (flags & FreeMoveProto::FLAG_TELEPORT) != 0;
			if (!teleport && std::sqrt(dx * dx + dy * dy) <= SNAP_DISTANCE_PX) {
				// mantém o render contínuo: o que o render já mostra = novo alvo + correção maior
				corr.x += dx;
				corr.y += dy;
				snapped = false;
			}
		}
		if (snapped) {
			corr = Vec2{};
		}

		base = Vec2{worldX, worldY};
		vx = velX;
		vy = velY;
		receivedAt = now;
		lastDecay = now;
		has = true;
		return snapped;
	}

	// Posição de render em 'now'.
	Vec2 position(int64_t now)
	{
		if (!has) {
			return Vec2{};
		}
		decay(now);
		const Vec2 t = targetAt(now);
		return Vec2{t.x + corr.x, t.y + corr.y};
	}

	// true enquanto precisa de atualização por frame: movendo (dentro da janela de extrapolação)
	// ou com correção ainda visível. Quando false, a posição está estável e pode parar de agendar.
	bool isAnimating(int64_t now) const
	{
		if (!has) {
			return false;
		}
		if ((vx != 0 || vy != 0) && (now - receivedAt) < MAX_EXTRAPOLATION_MS) {
			return true;
		}
		const double dt = static_cast<double>(now - lastDecay);
		const double f = dt > 0.0 ? std::exp(-dt / CORRECTION_TAU_MS) : 1.0;
		return std::fabs(corr.x * f) > CORRECTION_EPSILON_PX || std::fabs(corr.y * f) > CORRECTION_EPSILON_PX;
	}

	bool isMoving() const { return has && (vx != 0 || vy != 0); }
	bool hasState() const { return has; }
	double velocityX() const { return vx; }
	double velocityY() const { return vy; }

	void reset() { *this = RenderSmoother{}; }

private:
	void decay(int64_t now)
	{
		const double dt = static_cast<double>(now - lastDecay);
		if (dt > 0.0) {
			const double f = std::exp(-dt / CORRECTION_TAU_MS);
			corr.x *= f;
			corr.y *= f;
			lastDecay = now;
		}
	}

	Vec2 targetAt(int64_t now) const
	{
		int64_t age = now - receivedAt;
		age = age < 0 ? 0 : (age > MAX_EXTRAPOLATION_MS ? MAX_EXTRAPOLATION_MS : age);
		return Vec2{base.x + vx * static_cast<double>(age) / 1000.0, base.y + vy * static_cast<double>(age) / 1000.0};
	}

	Vec2 base;
	Vec2 corr;
	double vx = 0.0;
	double vy = 0.0;
	int64_t receivedAt = 0;
	int64_t lastDecay = 0;
	bool has = false;
};

// Direção de 8 vias (como Otc::Direction) a partir da velocidade, sem depender do engine.
// 0 = Norte, 1 = Leste, 2 = Sul, 3 = Oeste, 4 = NE, 5 = SE, 6 = SW, 7 = NW; -1 = parado.
inline int directionFromVelocity(int16_t vx, int16_t vy)
{
	if (vx == 0 && vy == 0) {
		return -1;
	}
	// Abaixo de 1/3 da componente maior a outra é ruído da quantização: cardinal.
	const int ax = vx < 0 ? -vx : vx;
	const int ay = vy < 0 ? -vy : vy;
	const bool diag = ax * 3 > ay && ay * 3 > ax;
	if (diag) {
		if (vy < 0) {
			return vx > 0 ? 4 : 7;
		}
		return vx > 0 ? 5 : 6;
	}
	if (ax >= ay) {
		return vx > 0 ? 1 : 3;
	}
	return vy > 0 ? 2 : 0;
}

} // namespace FreeMove

#endif // FREEMOVEMENT_FREEMOVERENDER_H
