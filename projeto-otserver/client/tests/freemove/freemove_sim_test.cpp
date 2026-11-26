// Simulação ponta a ponta do Free Movement (Fase 4): física REAL do servidor + política REAL de envio
// de STATE + rede com latência/jitter + suavizador do cliente a 60 FPS. Mede o que o cliente
// desenharia contra a posição verdadeira do servidor. Precisa dos dois repositórios lado a lado.
// A partir da raiz do cli-c:
//   g++ -std=c++17 -Wall -Wextra -Werror -Isrc/client -I../sev-c/src tests/freemove/freemove_sim_test.cpp ../sev-c/src/physicsbody.cpp -o freemove_sim_test
#include "freemovepolicy.h"
#include "freemoverender.h"
#include "physicsbody.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <set>
#include <utility>
#include <vector>

using namespace Physics;

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

struct World : ICollisionWorld {
	std::set<std::pair<int32_t, int32_t>> walls;
	bool isTileBlocked(int32_t x, int32_t y, uint8_t) override { return walls.count({x, y}) != 0; }
	CrossResult onCenterCross(int32_t, int32_t, TileRef&) override { return CrossResult::MOVED; }
};

struct InputEvent { int64_t t; int dx, dy; };
struct Metrics { double maxErr = 0, meanErr = 0, maxStep = 0, restErr = 0; int packets = 0; int frames = 0; };

static double worldPx(int32_t units) { return units / 64.0; }

// Percentil simples
static Metrics simulate(int64_t latency, int64_t jitter, const std::vector<InputEvent>& script, int64_t endTime, uint32_t seed,
                        double* tauOverride = nullptr)
{
	(void)tauOverride;
	World world;
	world.walls.insert({103, 100}); // parede a leste

	PhysicsBody body;
	body.setPosition(PhysicalPosition(tileToUnits(100, 1024), tileToUnits(100, 1024), 7));
	body.setMaxSpeed(3 * UNITS_PER_TILE); // 3 tiles/s = 96 px/s
	FreeMove::StateSendPolicy policy;
	FreeMove::RenderSmoother smoother;

	struct InFlight { int64_t arrival; FreeMoveProto::State state; };
	std::deque<InFlight> network;
	int64_t lastArrival = 0;
	uint32_t rng = seed;
	auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };

	struct Truth { int64_t t; double x, y; };
	std::vector<Truth> truth;
	truth.push_back({0, worldPx(body.getPosition().x), worldPx(body.getPosition().y)});

	size_t nextInput = 0;
	bool forced = false;
	Metrics m;
	double prevX = 0, prevY = 0; bool havePrev = false;
	double sumErr = 0;

	for (int64_t t = 1; t <= endTime; ++t) {
		while (nextInput < script.size() && script[nextInput].t == t) {
			body.setInput(script[nextInput].dx, script[nextInput].dy);
			forced = true;
			++nextInput;
		}

		if (t % 50 == 0) { // tick do servidor
			const MoveResult r = body.update(50, world);
			uint8_t flags = r == MoveResult::BLOCKED ? FreeMoveProto::FLAG_BLOCKED : 0;
			FreeMoveProto::State s = FreeMoveProto::makeState(1, body.getPosition(), body.getVelocityX(), body.getVelocityY(), flags, 0);
			if (policy.shouldSend(t, s, forced)) {
				int64_t arr = t + latency + (jitter ? (int64_t)(rnd() % (2 * jitter + 1)) - jitter : 0);
				arr = std::max(arr, lastArrival); // TCP: ordem preservada
				lastArrival = arr;
				network.push_back({arr, s});
				++m.packets;
			}
			forced = false;
			truth.push_back({t, worldPx(body.getPosition().x), worldPx(body.getPosition().y)});
		}

		while (!network.empty() && network.front().arrival <= t) {
			const FreeMoveProto::State& s = network.front().state;
			const PhysicalPosition p = FreeMoveProto::toPhysicalPosition(s);
			smoother.onState(worldPx(p.x), worldPx(p.y), s.velX, s.velY, s.flags, t);
			network.pop_front();
		}

		if (t % 16 == 0 && smoother.hasState()) { // frame do cliente (60 FPS)
			const FreeMove::Vec2 r = smoother.position(t);
			// verdade no instante (t - latência): é o melhor que se pode mostrar sem predição
			const int64_t tau = std::max<int64_t>(0, t - latency);
			double tx = truth.front().x, ty = truth.front().y;
			for (size_t i = 1; i < truth.size(); ++i) {
				if (truth[i].t >= tau) {
					const double span = static_cast<double>(truth[i].t - truth[i - 1].t);
					const double f = span > 0 ? (tau - truth[i - 1].t) / span : 1.0;
					tx = truth[i - 1].x + (truth[i].x - truth[i - 1].x) * f;
					ty = truth[i - 1].y + (truth[i].y - truth[i - 1].y) * f;
					break;
				}
				tx = truth[i].x; ty = truth[i].y;
			}
			const double err = std::hypot(r.x - tx, r.y - ty);
			m.maxErr = std::max(m.maxErr, err);
			sumErr += err;
			++m.frames;
			if (havePrev) m.maxStep = std::max(m.maxStep, std::hypot(r.x - prevX, r.y - prevY));
			prevX = r.x; prevY = r.y; havePrev = true;
		}
	}
	m.meanErr = m.frames ? sumErr / m.frames : 0;
	const FreeMove::Vec2 fin = smoother.position(endTime + 5000);
	m.restErr = std::hypot(fin.x - worldPx(body.getPosition().x), fin.y - worldPx(body.getPosition().y));
	return m;
}

int main()
{
	const std::vector<InputEvent> script = {
		{200, 1, 0}, {1700, 0, 0},      // leste 1,5 s: bate na parede (tile 103) e fica encostado
		{2500, 0, 1}, {3400, 0, 0},     // sul
		{3600, -1, -1}, {4700, 0, 0},   // diagonal noroeste
		{4900, -1, 0}, {5000, 1, 0}, {5100, -1, 0}, {5150, 0, 1}, {5400, 0, 0}, // troca rápida de direção
	};
	const int64_t end = 6500;

	struct Net { const char* name; int64_t lat; int64_t jit; double maxErrLimit; double stepLimit; };
	const Net nets[] = {
		{"boa   (40 ms, jitter 15)", 40, 15, 0, 0},
		{"ruim (150 ms, jitter 40)", 150, 40, 0, 0},
		{"LAN    (5 ms, jitter 2)", 5, 2, 0, 0},
	};

	Metrics results[3];
	for (int i = 0; i < 3; ++i) {
		results[i] = simulate(nets[i].lat, nets[i].jit, script, end, 12345u + i);
		std::printf("%-26s erro máx %5.2f px | médio %4.2f px | maior passo/frame %4.2f px | repouso %.3f px | %d pacotes em %.1f s (%.1f/s)\n",
		            nets[i].name, results[i].maxErr, results[i].meanErr, results[i].maxStep, results[i].restErr,
		            results[i].packets, end / 1000.0, results[i].packets / (end / 1000.0));
	}

	// Referências: velocidade 96 px/s => 1,6 px por frame de 16 ms; tick do servidor de 50 ms => 4,8 px.
	const double nominalStep = 96.0 * 0.016;
	for (int i = 0; i < 3; ++i) {
		CHECK(results[i].restErr < 0.5);                 // sempre termina exatamente onde o servidor parou
		CHECK(results[i].maxStep < nominalStep * 2.5);   // sem saltos visíveis: no pior caso < 4 px por frame
		CHECK(results[i].maxErr < 12.0);                 // nunca mais de ~1/3 de tile distante do servidor (defasado)
		CHECK(results[i].meanErr < 4.0);
		CHECK(results[i].packets / (end / 1000.0) < 14.0); // banda: bem abaixo dos 20 pacotes/s do tick
	}
	CHECK(results[2].maxErr <= results[1].maxErr + 1e-9); // melhor rede nunca é pior que a ruim

	std::printf("%d verificacoes, %d falhas\n", total, fails);
	return fails ? 1 : 0;
}
