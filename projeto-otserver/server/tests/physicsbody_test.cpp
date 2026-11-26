// Testes da Fase 2 (PhysicsBody). Compilar a partir da raiz do repositório:
//   g++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/physicsbody_test.cpp src/physicsbody.cpp -o physicsbody_test
#include "physicsbody.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <utility>

using namespace Physics;

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// Mundo falso: grade de tiles bloqueados + política de travessia configurável.
struct FakeWorld : ICollisionWorld {
	std::set<std::pair<int32_t, int32_t>> walls;
	std::function<CrossResult(int32_t, int32_t, TileRef&)> onCross;
	int crossCalls = 0;
	int terrainQueries = 0;

	bool isTileBlocked(int32_t x, int32_t y, uint8_t) override { ++terrainQueries; return walls.count({x, y}) != 0; }
	CrossResult onCenterCross(int32_t dx, int32_t dy, TileRef& r) override {
		++crossCalls;
		return onCross ? onCross(dx, dy, r) : CrossResult::MOVED;
	}
	void wall(int32_t x, int32_t y) { walls.insert({x, y}); }
};

static PhysicsBody makeBody(int32_t tileX, int32_t tileY, int32_t offX, int32_t offY, int32_t speed)
{
	PhysicsBody b;
	b.setPosition(PhysicalPosition(tileToUnits(tileX, offX), tileToUnits(tileY, offY), 7));
	b.setMaxSpeed(speed);
	return b;
}

// Invariante: a caixa não pode sobrepor um tile bloqueado que não sobrepunha antes.
static bool overlaps(const PhysicsBody& b, int32_t tx, int32_t ty)
{
	const PhysicalPosition& p = b.getPosition();
	const int64_t x0 = p.x - b.getHalfExtentX(), x1 = p.x + b.getHalfExtentX();
	const int64_t y0 = p.y - b.getHalfExtentY(), y1 = p.y + b.getHalfExtentY();
	const int64_t tx0 = (int64_t)tx * UNITS_PER_TILE, ty0 = (int64_t)ty * UNITS_PER_TILE;
	return x0 < tx0 + UNITS_PER_TILE && x1 > tx0 && y0 < ty0 + UNITS_PER_TILE && y1 > ty0;
}

int main()
{
	const int32_t ONE_TILE_PER_SEC = UNITS_PER_TILE;

	// 1) Sem drift: mesma distância independente de como o tempo é fatiado.
	{
		FakeWorld w;
		auto a = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		auto b = a, c = a;
		a.setInput(1, 0); b.setInput(1, 0); c.setInput(1, 0);
		for (int i = 0; i < 20; ++i) a.update(50, w);
		for (int i = 0; i < 1000; ++i) b.update(1, w);
		const uint32_t pattern[] = {33, 17, 50, 100, 250, 1, 99, 200, 250};
		uint32_t sum = 0;
		for (uint32_t d : pattern) { c.update(d, w); sum += d; }
		CHECK(sum == 1000);
		CHECK(a.getPosition().x == tileToUnits(100, 1024) + 2048);
		CHECK(b.getPosition().x == a.getPosition().x);
		CHECK(c.getPosition().x == a.getPosition().x);
		CHECK(a.getPosition().y == tileToUnits(100, 1024));
	}

	// 2) Diagonal normalizada: velocidade escalar ~ igual à da reta.
	{
		FakeWorld w;
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 1);
		for (int i = 0; i < 20; ++i) b.update(50, w);
		const double dx = b.getPosition().x - tileToUnits(100, 1024);
		const double dy = b.getPosition().y - tileToUnits(100, 1024);
		const double len = std::sqrt(dx * dx + dy * dy);
		CHECK(std::fabs(len - 2048.0) / 2048.0 < 0.005);
		CHECK(dx == dy);
		// diagonal oposta e mista também normalizadas
		auto n = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		n.setInput(-1, 1);
		for (int i = 0; i < 20; ++i) n.update(50, w);
		CHECK(tileToUnits(100, 1024) - n.getPosition().x == (int32_t)dx);
	}

	// 3) Parar em posição intermediária: sem snap para o centro do tile.
	{
		FakeWorld w;
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 0);
		b.update(130, w); // 2048 * 130 / 1000 = 266.24 -> 266 unidades
		b.stop();
		CHECK(b.getPosition().offsetX() == 1024 + 266);
		CHECK(!b.getPosition().isCenteredInTile());
		for (int i = 0; i < 10; ++i) b.update(50, w); // parado: nada muda
		CHECK(b.getPosition().offsetX() == 1024 + 266);
		CHECK(!b.isMoving());
		CHECK(b.update(50, w) == MoveResult::NONE);
	}

	// 4) Parede: encosta exatamente e desliza no outro eixo.
	{
		FakeWorld w;
		w.wall(101, 100);
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		const int32_t hx = b.getHalfExtentX();
		b.setInput(1, 0);
		MoveResult last = MoveResult::NONE;
		for (int i = 0; i < 40; ++i) last = b.update(50, w);
		CHECK(last == MoveResult::BLOCKED);
		CHECK(b.getPosition().x == tileToUnits(101) - hx); // borda da caixa == início da parede
		CHECK(b.getPosition().tileX() == 100);
		CHECK(!overlaps(b, 101, 100));
		CHECK(b.getVelocityX() == 0);

		// diagonal contra a parede: X travado, Y segue (sliding)
		const int32_t y0 = b.getPosition().y;
		b.setInput(1, 1);
		b.update(500, w);
		CHECK(b.getPosition().x == tileToUnits(101) - hx);
		CHECK(b.getPosition().y > y0);
		CHECK(b.getVelocityX() == 0 && b.getVelocityY() > 0);
	}

	// 5) Caixa de 20 px passa em corredor de 1 tile (paredes em y=99 e y=101).
	{
		FakeWorld w;
		for (int x = 90; x <= 120; ++x) { w.wall(x, 99); w.wall(x, 101); }
		auto b = makeBody(95, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 0);
		MoveResult worst = MoveResult::MOVED;
		for (int i = 0; i < 60; ++i) { if (b.update(50, w) != MoveResult::MOVED) worst = MoveResult::BLOCKED; }
		CHECK(worst == MoveResult::MOVED);
		CHECK(b.getPosition().x == tileToUnits(95, 1024) + 3 * 2048);
	}

	// 6) Travessia de tile recusada pelo engine: fica colado na borda, sem atravessar.
	{
		FakeWorld w;
		w.onCross = [](int32_t dx, int32_t, TileRef&) { return dx > 0 ? CrossResult::BLOCKED : CrossResult::MOVED; };
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 0);
		MoveResult last = MoveResult::NONE;
		for (int i = 0; i < 40; ++i) last = b.update(50, w);
		CHECK(last == MoveResult::BLOCKED);
		CHECK(b.getPosition().tileX() == 100 && b.getPosition().offsetX() == 2047);
		// ao ir para o outro lado, a travessia é permitida
		b.setInput(-1, 0);
		for (int i = 0; i < 20; ++i) b.update(50, w); // 1 s = 1 tile
		CHECK(b.getPosition().tileX() == 99 && b.getPosition().offsetX() == 2047);
	}

	// 7) tick atrasado (250 ms a 20 tiles/s = 5 tiles) não atravessa parede.
	{
		FakeWorld w;
		w.wall(103, 100);
		auto b = makeBody(100, 100, 1024, 1024, 20 * UNITS_PER_TILE);
		b.setInput(1, 0);
		b.update(250, w);
		CHECK(b.getPosition().x == tileToUnits(103) - b.getHalfExtentX());
		CHECK(!overlaps(b, 103, 100));
		// dt gigante é limitado ao teto
		auto c = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		c.setInput(1, 0);
		FakeWorld empty;
		c.update(1000000, empty);
		CHECK(c.getPosition().x == tileToUnits(100, 1024) + 512); // 250 ms
	}

	// 8) Limites do mapa (tile -1 e tile 65536 são parede; o mundo nem é consultado).
	{
		FakeWorld w;
		auto b = makeBody(0, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(-1, 0);
		for (int i = 0; i < 40; ++i) b.update(50, w);
		CHECK(b.getPosition().x == b.getHalfExtentX());
		auto e = makeBody(MAX_TILE_COORD, 100, 1024, 1024, ONE_TILE_PER_SEC);
		e.setInput(1, 0);
		for (int i = 0; i < 80; ++i) e.update(50, w);
		CHECK(e.getPosition().x == MAX_WORLD_UNITS + 1 - e.getHalfExtentX());
		CHECK(e.getPosition().isInsideMap());
	}

	// 9) Relocação pelo engine (escada/teleporte): vai para o centro do destino.
	{
		FakeWorld w;
		w.onCross = [](int32_t, int32_t, TileRef& r) { r.x = 50; r.y = 60; r.z = 6; return CrossResult::RELOCATED; };
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 0);
		MoveResult res = MoveResult::NONE;
		for (int i = 0; i < 20 && res != MoveResult::RELOCATED; ++i) res = b.update(50, w);
		CHECK(res == MoveResult::RELOCATED);
		CHECK(b.getPosition().tileX() == 50 && b.getPosition().tileY() == 60 && b.getPosition().z == 6);
		CHECK(b.getPosition().isCenteredInTile());
		CHECK(!b.isMoving());
	}

	// 10) Anti-stuck: nascer sobrepondo uma parede não prende; sair é permitido.
	{
		FakeWorld w;
		w.wall(100, 100);
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 0);
		for (int i = 0; i < 40; ++i) b.update(50, w);
		CHECK(b.getPosition().x > tileToUnits(101)); // saiu para a direita
		// e uma parede adiante continua valendo
		FakeWorld w2;
		w2.wall(100, 100); w2.wall(102, 100);
		auto c = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		c.setInput(1, 0);
		for (int i = 0; i < 80; ++i) c.update(50, w2);
		CHECK(c.getPosition().x == tileToUnits(102) - c.getHalfExtentX());
	}

	// 11) Travado (stun/root) não move, mas preserva o input; destravar retoma.
	{
		FakeWorld w;
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 0);
		b.setLocked(true);
		for (int i = 0; i < 10; ++i) b.update(50, w);
		CHECK(b.getPosition().x == tileToUnits(100, 1024));
		b.setLocked(false);
		b.update(50, w);
		CHECK(b.getPosition().x > tileToUnits(100, 1024));
	}

	// 12) Teto de velocidade e input inválido.
	{
		FakeWorld w;
		auto b = makeBody(100, 100, 1024, 1024, 1000000000);
		b.setInput(7, -9); // limitado a (1, -1)
		CHECK(b.getInputX() == 1 && b.getInputY() == -1);
		b.setInput(1, 0);
		b.update(250, w);
		CHECK(b.getPosition().x - tileToUnits(100, 1024) <= PhysicsBody::MAX_SPEED / 4 + 1);
		auto z = makeBody(100, 100, 1024, 1024, -5);
		z.setInput(1, 0);
		CHECK(z.update(50, w) == MoveResult::NONE);
	}

	// 13) Canto: diagonal contra um único tile bloqueado nunca o sobrepõe (invariante a cada tick).
	{
		FakeWorld w;
		w.wall(101, 101);
		auto b = makeBody(100, 100, 1024, 1024, ONE_TILE_PER_SEC);
		b.setInput(1, 1);
		bool ok = true;
		for (int i = 0; i < 100; ++i) { b.update(33, w); if (overlaps(b, 101, 101)) ok = false; }
		CHECK(ok);
		// saiu do canto deslizando: andou nos dois eixos até encostar
		CHECK(b.getPosition().x > tileToUnits(100, 1024) || b.getPosition().y > tileToUnits(100, 1024));
	}

	// 14) Determinismo: mesma entrada + mesmos dt => estado idêntico, com obstáculos.
	{
		auto run = [](uint32_t seed) {
			FakeWorld w;
			for (int i = 0; i < 40; ++i) { w.wall(95 + (i * 7) % 15, 95 + (i * 11) % 15); }
			auto b = makeBody(100, 100, 300, 1700, 1500);
			uint32_t s = seed;
			for (int i = 0; i < 4000; ++i) {
				s = s * 1664525u + 1013904223u;
				b.setInput((int32_t)((s >> 8) % 3) - 1, (int32_t)((s >> 12) % 3) - 1);
				b.update(1 + (s >> 16) % 80, w);
			}
			return b.getPosition();
		};
		CHECK(run(1) == run(1));
		CHECK(run(2) == run(2));
	}

	// 15) Passeio aleatório com obstáculos: o invariante vale em todos os ticks.
	{
		FakeWorld w;
		for (int i = 0; i < 60; ++i) { w.wall(96 + (i * 5) % 9, 96 + (i * 13) % 9); }
		w.walls.erase({100, 100}); // garante que nasce em tile livre
		auto b = makeBody(100, 100, 1024, 1024, 3 * UNITS_PER_TILE);
		bool ok = true;
		uint32_t s = 12345;
		for (int i = 0; i < 20000; ++i) {
			s = s * 1664525u + 1013904223u;
			b.setInput((int32_t)((s >> 8) % 3) - 1, (int32_t)((s >> 12) % 3) - 1);
			b.update(1 + (s >> 16) % 120, w);
			for (const auto& t : w.walls) { if (overlaps(b, t.first, t.second)) { ok = false; } }
		}
		CHECK(ok);
	}

	std::printf("%d verificacoes, %d falhas\n", total, fails);
	return fails ? 1 : 0;
}
