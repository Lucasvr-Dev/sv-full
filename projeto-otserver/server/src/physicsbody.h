// Free Movement - Fase 2: corpo físico com movimento contínuo.
//
// Este núcleo NÃO depende de Creature/Tile/Game. Tudo que ele precisa saber do
// mundo vem por ICollisionWorld, o que permite testá-lo isoladamente e trocar a
// colisão por tile (Fase 2) por AABB de itens/criaturas (Fase 5) sem mexer aqui.
//
// Regras:
//  - aritmética somente inteira (determinismo, sem drift);
//  - o servidor define a velocidade; o "input" é só uma direção (-1, 0, 1);
//  - diagonal normalizada (mesma velocidade escalar que reta);
//  - o corpo é uma caixa (AABB) centrada na posição; o "tile lógico" é o tile
//    que contém o CENTRO, como o resto do TFS espera.

#ifndef FS_PHYSICSBODY_H
#define FS_PHYSICSBODY_H

#include <cstdint>

#include "physicalposition.h"

namespace Physics {

struct TileRef {
	int32_t x = 0;
	int32_t y = 0;
	uint8_t z = 0;
};

enum class CrossResult : uint8_t {
	BLOCKED,   // o engine recusou a travessia (criatura no tile, regra de script...)
	MOVED,     // o centro passou para o tile vizinho pedido
	RELOCATED, // o engine colocou a criatura em outro lugar (escada, teleporte)
};

enum class MoveResult : uint8_t {
	NONE,      // sem movimento (parado/travado)
	MOVED,     // moveu livremente
	BLOCKED,   // moveu, mas encostou em algo em pelo menos um eixo
	RELOCATED, // foi recolocado pelo engine (movimento encerrado neste tick)
};

class ICollisionWorld
{
public:
	virtual ~ICollisionWorld() = default;

	// true se o tile impede que a CAIXA o sobreponha (terreno, itens sólidos, sem chão).
	// Coordenadas fora do mapa nunca chegam aqui: o corpo já as trata como bloqueadas.
	virtual bool isTileBlocked(int32_t tileX, int32_t tileY, uint8_t z) = 0;

	// Chamado quando o CENTRO do corpo cruza para um tile adjacente.
	// (dx, dy) tem exatamente um componente diferente de zero (-1 ou +1).
	virtual CrossResult onCenterCross(int32_t dx, int32_t dy, TileRef& relocated) = 0;
};

class PhysicsBody
{
public:
	static constexpr int32_t DEFAULT_HALF_EXTENT = 10 * UNITS_PER_PIXEL;     // caixa de 20x20 px
	static constexpr int32_t MAX_SPEED = 40 * UNITS_PER_TILE;                 // teto: 40 tiles/s
	static constexpr int32_t MAX_STEP = 8 * UNITS_PER_PIXEL;                  // sub-passo: 8 px (< meio tile)
	static constexpr uint32_t MAX_DT_MS = 250;                                // proteção contra tick atrasado

	// Define a posição (teleporte/spawn). Zera velocidade e resíduos, mantém o input.
	void setPosition(const PhysicalPosition& p);
	const PhysicalPosition& getPosition() const { return pos; }

	void setHalfExtent(int32_t halfX, int32_t halfY);
	int32_t getHalfExtentX() const { return halfX; }
	int32_t getHalfExtentY() const { return halfY; }

	// Velocidade escalar máxima em unidades/segundo (o servidor calcula a partir do speed da criatura).
	void setMaxSpeed(int32_t unitsPerSecond) { maxSpeed = unitsPerSecond; }
	int32_t getMaxSpeed() const { return maxSpeed; }

	// Direção desejada, cada eixo em -1, 0 ou 1 (valores fora disso são limitados).
	void setInput(int32_t dirX, int32_t dirY);
	int32_t getInputX() const { return inputX; }
	int32_t getInputY() const { return inputY; }

	// Para na hora, na posição exata atual (sem snap para o centro do tile).
	void stop();

	// Travado (stun/root/paralisia): não se move, mas o input é preservado.
	void setLocked(bool value) { locked = value; }
	bool isLocked() const { return locked; }

	// Velocidade efetiva aplicada no último update (unidades/s), já considerando colisões.
	int32_t getVelocityX() const { return velX; }
	int32_t getVelocityY() const { return velY; }
	bool isMoving() const { return velX != 0 || velY != 0; }

	// Avança a simulação por dtMs milissegundos.
	MoveResult update(uint32_t dtMs, ICollisionWorld& world);

private:
	enum class AxisResult : uint8_t { OK, BLOCKED, RELOCATED };

	void computeDesiredVelocity(int32_t& vx, int32_t& vy) const;
	AxisResult moveAxis(bool horizontal, int32_t delta, ICollisionWorld& world);

	PhysicalPosition pos;
	int32_t halfX = DEFAULT_HALF_EXTENT;
	int32_t halfY = DEFAULT_HALF_EXTENT;
	int32_t maxSpeed = 0;
	int32_t inputX = 0;
	int32_t inputY = 0;
	int32_t velX = 0;
	int32_t velY = 0;
	int64_t remX = 0; // resíduo em unidades*ms (evita drift ao truncar)
	int64_t remY = 0;
	bool locked = false;
};

} // namespace Physics

#endif // FS_PHYSICSBODY_H
