// Testes da Fase 1. Compilar: g++ -std=c++17 -I<dir do header> physicalposition_test.cpp
#include "physicalposition.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <initializer_list>

using namespace Physics;
struct Pos { uint16_t x; uint16_t y; uint8_t z; constexpr Pos(uint16_t x, uint16_t y, uint8_t z) : x(x), y(y), z(z) {} }; // formato do servidor
struct PosI { int32_t x; int32_t y; uint8_t z; constexpr PosI(int32_t x, int32_t y, uint8_t z) : x(x), y(y), z(z) {} };  // formato do cliente

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
	// Escala
	static_assert(UNITS_PER_TILE == 2048, "");
	static_assert(UNITS_PER_CELL == 512 && CELLS_PER_TILE == 4, "");

	// Exemplo da explicação: tile 100 no meio => 205824
	auto c = PhysicalPosition::fromTile(Pos(100, 100, 7));
	CHECK(c.x == 205824 && c.y == 205824);
	CHECK(c.tileX() == 100 && c.offsetX() == 1024 && c.isCenteredInTile());
	CHECK(c.toTile<Pos>().x == 100 && c.toTile<Pos>().z == 7);
	CHECK(c.toTile<PosI>().y == 100);

	// 1 px à direita = +64 unidades e continua no mesmo tile
	PhysicalPosition m = c; m.x += 64;
	CHECK(m.x == 205888 && m.tileX() == 100 && !m.isCenteredInTile());

	// Round-trip lossless em TODAS as unidades de um tile (interno)
	for (int32_t off = 0; off < UNITS_PER_TILE; ++off) {
		int32_t u = tileToUnits(3218, off);
		CHECK(unitsToTile(u) == 3218 && unitsToOffset(u) == off);
	}

	// Bordas do tile e do mapa
	CHECK(PhysicalPosition(2047, 0, 7).tileX() == 0);
	CHECK(PhysicalPosition(2048, 0, 7).tileX() == 1);
	CHECK(PhysicalPosition(MAX_WORLD_UNITS, MAX_WORLD_UNITS, 7).isInsideMap());
	CHECK(PhysicalPosition(MAX_WORLD_UNITS, MAX_WORLD_UNITS, 7).tileX() == 65535);
	CHECK(!PhysicalPosition(MAX_WORLD_UNITS + 1, 0, 7).isInsideMap());
	CHECK(!PhysicalPosition(-1, 0, 7).isInsideMap());
	// Piso correto para negativos (tile -1, não 0)
	CHECK(unitsToTile(-1) == -1 && unitsToOffset(-1) == 2047);

	// Tile -> mundo -> tile para tiles de ambos os extremos
	for (int32_t t : {0, 1, 100, 32767, 32768, 65535}) {
		auto p = PhysicalPosition::fromTile(Pos((uint16_t)t, (uint16_t)t, 7), 0, 2047);
		CHECK(p.tileX() == t && p.offsetX() == 0 && p.offsetY() == 2047);
	}

	// Rede: erro máximo < 1/8 px (8 unidades) e nunca troca de tile
	int32_t maxErr = 0;
	for (int32_t off = 0; off < UNITS_PER_TILE; ++off) {
		int32_t back = netToOffset(offsetToNet(off));
		int32_t e = off - back;
		CHECK(e >= 0 && e < NET_OFFSET_STEP);
		CHECK(back >= 0 && back < UNITS_PER_TILE);
		if (e > maxErr) maxErr = e;
	}
	CHECK(maxErr == 7);

	// Células de 8 px
	CHECK(PhysicalPosition(0, 0, 7).cellX() == 0);
	CHECK(PhysicalPosition(511, 0, 7).cellX() == 0 && PhysicalPosition(512, 0, 7).cellX() == 1);
	CHECK(unitsToCell(2048) == 4);
	CHECK(unitsToCell(-1) == -1);

	// Pixels
	CHECK(pixelsToUnits(32) == UNITS_PER_TILE);
	CHECK(unitsToPixelsFloor(205888) == 3217);
	CHECK(std::fabs(unitsToPixelsF(32) - 0.5f) < 1e-6f);

	// Mundo <-> tela: round-trip em vários zooms e posições sub-tile
	PhysicalPosition cam = PhysicalPosition::fromTile(Pos(3218, 3194, 7), 600, 1800);
	PixelF center{240.f, 160.f};
	int worst = 0;
	for (float scale : {0.5f, 1.0f, 1.5f, 2.0f, 2.65f, 3.0f}) {
		for (int32_t dx = -3000; dx <= 3000; dx += 137) {
			for (int32_t dy = -3000; dy <= 3000; dy += 211) {
				PhysicalPosition p(cam.x + dx, cam.y + dy, 7);
				PixelF s = p.toScreen(cam, scale, center);
				PhysicalPosition b = PhysicalPosition::fromScreen(s, cam, scale, center);
				int e = std::abs(b.x - p.x); e = std::max(e, std::abs(b.y - p.y));
				if (e > worst) worst = e;
			}
		}
	}
	std::printf("pior erro mundo->tela->mundo: %d unidades (%.3f px)\n", worst, worst / 64.0);
	CHECK(worst <= 2); // ruído de float32, bem abaixo de 1/16 px

	// Distância
	CHECK(distanceSquared(PhysicalPosition(0, 0, 7), PhysicalPosition(3, 4, 7)) == 25);
	CHECK(distanceSquared(PhysicalPosition(0, 0, 7), PhysicalPosition(MAX_WORLD_UNITS, MAX_WORLD_UNITS, 7)) > 0);

	std::printf("%d verificacoes, %d falhas\n", total, fails);
	return fails ? 1 : 0;
}
