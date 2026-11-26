// Free Movement - Fase 1: posição física em ponto fixo.
//
// Este arquivo é IDÊNTICO no servidor (sev-c/src/physicalposition.h) e no
// cliente (cli-c/src/client/physicalposition.h). Não adicione dependências
// de nenhum dos dois projetos: a conversão para/de Position usa templates.
//
// Escala:  1 tile = 32 px = 2048 unidades   (1 px = 64 unidades)
// Mapa máximo: 65536 tiles * 2048 = 2^27 unidades, cabe em int32_t.
// No servidor só se usa aritmética inteira (determinismo, sem drift).
// float existe apenas para renderização no cliente.

#ifndef FREEMOVEMENT_PHYSICALPOSITION_H
#define FREEMOVEMENT_PHYSICALPOSITION_H

#include <cstdint>

namespace Physics {

constexpr int32_t TILE_PIXELS = 32;
constexpr int32_t UNITS_PER_PIXEL = 64;
constexpr int32_t UNITS_PER_TILE = TILE_PIXELS * UNITS_PER_PIXEL; // 2048
constexpr int32_t TILE_SHIFT = 11;                                // 2^11 = 2048
constexpr int32_t TILE_MASK = UNITS_PER_TILE - 1;
constexpr int32_t TILE_CENTER = UNITS_PER_TILE / 2;               // 1024

// Células usadas pela AoE circular (como no vídeo do MVC): 8 px = 512 unidades,
// ou seja, 4 x 4 células por tile. Configurável aqui.
constexpr int32_t CELL_PIXELS = 8;
constexpr int32_t UNITS_PER_CELL = CELL_PIXELS * UNITS_PER_PIXEL;  // 512
constexpr int32_t CELLS_PER_TILE = UNITS_PER_TILE / UNITS_PER_CELL; // 4
constexpr int32_t CELL_SHIFT = 9;                                   // 2^9 = 512
static_assert((1 << CELL_SHIFT) == UNITS_PER_CELL, "CELL_SHIFT deve casar com UNITS_PER_CELL");
static_assert((1 << TILE_SHIFT) == UNITS_PER_TILE, "TILE_SHIFT deve casar com UNITS_PER_TILE");

// Rede: o offset dentro do tile (0..2047) é enviado em 1 byte por eixo,
// com precisão de 1/8 px (8 unidades). O servidor continua sendo a verdade.
constexpr int32_t NET_OFFSET_SHIFT = 3;
constexpr int32_t NET_OFFSET_STEP = 1 << NET_OFFSET_SHIFT; // 8 unidades

constexpr int32_t MAX_TILE_COORD = 65535;
constexpr int32_t MAX_WORLD_UNITS = (MAX_TILE_COORD + 1) * UNITS_PER_TILE - 1;

// Conversões escalares (divisão/resto com piso, corretas inclusive p/ negativos).
constexpr int32_t unitsToTile(int32_t units) { return units >> TILE_SHIFT; }
constexpr int32_t unitsToOffset(int32_t units) { return units & TILE_MASK; }
constexpr int32_t tileToUnits(int32_t tile, int32_t offset = 0) { return tile * UNITS_PER_TILE + offset; } // multiplicação: definida p/ tiles negativos
constexpr int32_t unitsToPixelsFloor(int32_t units) { return units >> 6; } // /64
constexpr float unitsToPixelsF(int32_t units) { return static_cast<float>(units) / UNITS_PER_PIXEL; }
constexpr int32_t pixelsToUnits(int32_t px) { return px * UNITS_PER_PIXEL; }
constexpr int32_t unitsToCell(int32_t units) { return units >> CELL_SHIFT; }

constexpr uint8_t offsetToNet(int32_t offset) { return static_cast<uint8_t>(offset >> NET_OFFSET_SHIFT); }
constexpr int32_t netToOffset(uint8_t q) { return static_cast<int32_t>(q) << NET_OFFSET_SHIFT; }

struct PixelF {
	float x = 0.f;
	float y = 0.f;
};

struct PhysicalPosition {
	int32_t x = 0; // unidades de mundo (1 tile = 2048)
	int32_t y = 0;
	uint8_t z = 0;

	constexpr PhysicalPosition() = default;
	constexpr PhysicalPosition(int32_t x, int32_t y, uint8_t z) : x(x), y(y), z(z) {}

	// --- Tile <-> mundo -----------------------------------------------------
	// Cria a partir de um tile + offset (padrão: centro do tile, comportamento legado).
	template <typename PositionT>
	static constexpr PhysicalPosition fromTile(const PositionT& pos, int32_t offsetX = TILE_CENTER, int32_t offsetY = TILE_CENTER)
	{
		return PhysicalPosition(tileToUnits(static_cast<int32_t>(pos.x), offsetX),
		                        tileToUnits(static_cast<int32_t>(pos.y), offsetY),
		                        static_cast<uint8_t>(pos.z));
	}

	// Tile que contém este ponto (é o "tile lógico" usado por mapa, itens, scripts).
	template <typename PositionT>
	constexpr PositionT toTile() const
	{
		return PositionT(static_cast<decltype(PositionT::x)>(tileX()),
		                 static_cast<decltype(PositionT::y)>(tileY()), z);
	}

	constexpr int32_t tileX() const { return unitsToTile(x); }
	constexpr int32_t tileY() const { return unitsToTile(y); }
	constexpr int32_t offsetX() const { return unitsToOffset(x); }
	constexpr int32_t offsetY() const { return unitsToOffset(y); }

	// Célula de 8 px (índice global) que contém este ponto.
	constexpr int32_t cellX() const { return unitsToCell(x); }
	constexpr int32_t cellY() const { return unitsToCell(y); }

	constexpr bool isInsideMap() const
	{
		return x >= 0 && y >= 0 && x <= MAX_WORLD_UNITS && y <= MAX_WORLD_UNITS;
	}

	constexpr bool isCenteredInTile() const { return offsetX() == TILE_CENTER && offsetY() == TILE_CENTER; }

	// Posição em pixels de mundo (para render / debug), nunca para lógica de servidor.
	constexpr PixelF toWorldPixels() const { return PixelF{unitsToPixelsF(x), unitsToPixelsF(y)}; }

	// Mundo -> tela. 'camera' é o ponto de mundo no centro da tela; 'scale' é o
	// fator de zoom (1.0 = 32 px por tile); screenCenter é o centro do viewport em px.
	constexpr PixelF toScreen(const PhysicalPosition& camera, float scale, PixelF screenCenter) const
	{
		return PixelF{screenCenter.x + unitsToPixelsF(x - camera.x) * scale,
		              screenCenter.y + unitsToPixelsF(y - camera.y) * scale};
	}

	// Tela -> mundo (inverso). Usado para mira sub-tile: mouse vira ponto físico.
	// O arredondamento é para o inteiro mais próximo, em unidades.
	static constexpr PhysicalPosition fromScreen(PixelF screen, const PhysicalPosition& camera, float scale, PixelF screenCenter)
	{
		const float dxPx = (screen.x - screenCenter.x) / scale;
		const float dyPx = (screen.y - screenCenter.y) / scale;
		return PhysicalPosition(camera.x + roundToInt(dxPx * UNITS_PER_PIXEL),
		                        camera.y + roundToInt(dyPx * UNITS_PER_PIXEL), camera.z);
	}

	constexpr bool operator==(const PhysicalPosition& o) const { return x == o.x && y == o.y && z == o.z; }
	constexpr bool operator!=(const PhysicalPosition& o) const { return !(*this == o); }

private:
	static constexpr int32_t roundToInt(float v) { return static_cast<int32_t>(v < 0.f ? v - 0.5f : v + 0.5f); }
};

// Distância ao quadrado entre dois pontos, em unidades^2 (int64 para não estourar).
// Evita sqrt no servidor; compare com raio^2.
constexpr int64_t distanceSquared(const PhysicalPosition& a, const PhysicalPosition& b)
{
	const int64_t dx = static_cast<int64_t>(a.x) - b.x;
	const int64_t dy = static_cast<int64_t>(a.y) - b.y;
	return dx * dx + dy * dy;
}

} // namespace Physics

#endif // FREEMOVEMENT_PHYSICALPOSITION_H
