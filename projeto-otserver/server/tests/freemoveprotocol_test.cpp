// Testes da Fase 3 (protocolo + políticas). Compilar a partir da raiz do repositório:
//   g++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/freemoveprotocol_test.cpp -o freemoveprotocol_test
#include "freemovepolicy.h"
#include "freemoveprotocol.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace FreeMoveProto;

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// Buffer little-endian com o mesmo contrato dos adaptadores reais.
struct Buf {
	std::vector<uint8_t> data;
	size_t pos = 0;
	bool good = true;
	// Writer
	void u8(uint8_t v) { data.push_back(v); }
	void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
	void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
	void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
	// Reader
	uint8_t rd8() { if (pos + 1 > data.size()) { good = false; return 0; } return data[pos++]; }
	bool ok() const { return good; }
};
struct Rd {
	Buf& b;
	uint8_t u8() { return b.rd8(); }
	uint16_t u16() { uint16_t lo = b.rd8(); uint16_t hi = b.rd8(); return static_cast<uint16_t>(lo | (hi << 8)); }
	uint32_t u32() { uint32_t lo = u16(); uint32_t hi = u16(); return lo | (hi << 16); }
	int16_t i16() { return static_cast<int16_t>(u16()); }
	bool ok() const { return b.ok(); }
};

static uint32_t rng = 987654321u;
static uint32_t next() { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main()
{
	// Constantes de contrato
	static_assert(CLIENT_OPCODE == 0xEA && SERVER_OPCODE == 0x4E, "opcodes mudaram: atualizar docs e cliente");

	// 1) Tamanhos exatos de cada pacote
	{
		Buf b; writeHello(b); CHECK(b.data.size() == HELLO_SIZE);
		Buf i; writeInput(i, Input{7, 1, -1}); CHECK(i.data.size() == INPUT_SIZE);
		Buf c; writeConfig(c, Config{}); CHECK(c.data.size() == CONFIG_SIZE);
		Buf s; writeState(s, State{}); CHECK(s.data.size() == STATE_SIZE);
	}

	// 2) Direção: as 9 combinações válidas fazem round-trip; as inválidas são rejeitadas
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			int8_t rx, ry;
			CHECK(unpackDir(packDir(dx, dy), rx, ry) && rx == dx && ry == dy);
		}
	}
	{
		int8_t x, y;
		int invalidAccepted = 0;
		for (int v = 0; v < 256; ++v) {
			bool valid = ((v & 3) <= 2) && (((v >> 2) & 3) <= 2) && (v & 0xF0) == 0;
			if (unpackDir(static_cast<uint8_t>(v), x, y) != valid) ++invalidAccepted;
		}
		CHECK(invalidAccepted == 0);
	}

	// 3) Round-trip de INPUT para todos os seq e direções
	{
		int bad = 0;
		for (int seq = 0; seq < 256; ++seq) {
			for (int dx = -1; dx <= 1; ++dx) {
				for (int dy = -1; dy <= 1; ++dy) {
					Buf b; writeInput(b, Input{static_cast<uint8_t>(seq), static_cast<int8_t>(dx), static_cast<int8_t>(dy)});
					CHECK(b.data[0] == CLIENT_OPCODE && b.data[1] == C_INPUT);
					b.pos = 2; Rd r{b}; Input out;
					if (!readInput(r, out) || out.seq != seq || out.dirX != dx || out.dirY != dy) ++bad;
				}
			}
		}
		CHECK(bad == 0);
	}

	// 4) STATE: round-trip com valores extremos e posição física via makeState
	{
		Physics::PhysicalPosition p(Physics::tileToUnits(3218, 1290), Physics::tileToUnits(3194, 2047), 7);
		State s = makeState(0x10000001u, p, 1448, -1448, FLAG_BLOCKED, 200);
		CHECK(s.tileX == 3218 && s.tileY == 3194 && s.z == 7);
		CHECK(s.offX == 1290 / 8 && s.offY == 255);
		CHECK(s.velX == 22 && s.velY == -22);
		Buf b; writeState(b, s);
		CHECK(b.data[0] == SERVER_OPCODE && b.data[1] == S_STATE);
		b.pos = 2; Rd r{b}; State out;
		CHECK(readState(r, out) && out == s);
		// erro de posição pela rede < 1/8 px
		const Physics::PhysicalPosition back = toPhysicalPosition(out);
		CHECK(std::abs(back.x - p.x) < Physics::NET_OFFSET_STEP && std::abs(back.y - p.y) < Physics::NET_OFFSET_STEP);
		CHECK(back.tileX() == 3218 && back.tileY() == 3194);
		// o centro do tile é exato na rede
		State c = makeState(1, Physics::PhysicalPosition(Physics::tileToUnits(5, 1024), Physics::tileToUnits(5, 1024), 7), 0, 0, 0, 0);
		CHECK(c.isCentered());
	}

	// 5) Validação do STATE: rejeita lixo
	{
		auto roundTrip = [](State s) { Buf b; writeState(b, s); b.pos = 2; Rd r{b}; State o; return readState(r, o); };
		State good; good.creatureId = 5; good.z = 7;
		CHECK(roundTrip(good));
		State s = good; s.creatureId = 0; CHECK(!roundTrip(s));
		s = good; s.z = 16; CHECK(!roundTrip(s));
		s = good; s.flags = 0x80; CHECK(!roundTrip(s));
		s = good; s.velX = 32767; CHECK(!roundTrip(s));
		s = good; s.velY = -32768; CHECK(!roundTrip(s));
		s = good; s.velX = static_cast<int16_t>(MAX_VELOCITY_PX_PER_SEC); CHECK(roundTrip(s));
	}

	// 6) Pacotes truncados (qualquer corte) nunca são aceitos
	{
		Buf full; writeState(full, State{42, 100, 100, 7, 128, 128, 5, -5, 0, 1});
		int accepted = 0;
		for (size_t cut = 2; cut < full.data.size(); ++cut) {
			Buf b; b.data.assign(full.data.begin(), full.data.begin() + cut); b.pos = 2;
			Rd r{b}; State o;
			if (readState(r, o)) ++accepted;
		}
		CHECK(accepted == 0);
		Buf inp; writeInput(inp, Input{1, 1, 0}); inp.data.pop_back(); inp.pos = 2;
		Rd ri{inp}; Input oi; CHECK(!readInput(ri, oi));
		Buf cfg; writeConfig(cfg, Config{}); cfg.data.resize(5); cfg.pos = 2;
		Rd rc{cfg}; Config oc; CHECK(!readConfig(rc, oc));
	}

	// 7) Fuzz: bytes aleatórios nunca quebram o parser, e tudo que passa é válido
	{
		int acceptedInputs = 0, violations = 0;
		for (int i = 0; i < 200000; ++i) {
			Buf b; size_t n = next() % 8;
			for (size_t k = 0; k < n; ++k) b.data.push_back(static_cast<uint8_t>(next()));
			Rd r{b}; Input in;
			if (readInput(r, in)) {
				++acceptedInputs;
				if (in.dirX < -1 || in.dirX > 1 || in.dirY < -1 || in.dirY > 1) ++violations;
			}
			Buf b2; for (size_t k = 0; k < 24; ++k) b2.data.push_back(static_cast<uint8_t>(next()));
			Rd r2{b2}; State st;
			if (readState(r2, st) && (st.z > MAX_FLOOR || st.creatureId == 0 || (st.flags & ~FLAGS_MASK))) ++violations;
		}
		CHECK(violations == 0);
		CHECK(acceptedInputs > 0); // o teste exercita o caminho de aceitação
	}

	// 8) Config: round-trip e rejeição de zeros
	{
		Config c; c.halfExtentUnits = 640;
		Buf b; writeConfig(b, c); b.pos = 2; Rd r{b}; Config o;
		CHECK(readConfig(r, o) && o.version == CONTRACT_VERSION && o.unitsPerTile == 2048 && o.cellPixels == 8 && o.halfExtentUnits == 640);
		Config z; z.unitsPerTile = 0;
		Buf bz; writeConfig(bz, z); bz.pos = 2; Rd rz{bz}; Config oz; CHECK(!readConfig(rz, oz));
	}

	// 9) TokenBucket: rajada permitida, depois limita; recarrega com o tempo
	{
		FreeMove::TokenBucket tb(20, 20);
		int64_t t = 1000;
		int okBurst = 0;
		for (int i = 0; i < 30; ++i) if (tb.tryTake(t)) ++okBurst;
		CHECK(okBurst == 20);                    // capacidade
		CHECK(!tb.tryTake(t + 10));               // 10 ms: ainda sem ficha cheia
		CHECK(tb.tryTake(t + 60));                // 60 ms * 20/s = 1.2 ficha
		int sustained = 0;                         // 10 s a 100 pacotes/s: só ~20/s passam
		for (int i = 0; i < 1000; ++i) if (tb.tryTake(t + 1000 + i * 10)) ++sustained;
		CHECK(sustained >= 200 && sustained <= 221); // 20/s * 10 s + no máximo 'capacity' fichas acumuladas
		FreeMove::TokenBucket idle(20, 20);
		idle.tryTake(1000);
		int afterIdle = 0;                         // ocioso por muito tempo: não passa de 'capacity'
		for (int i = 0; i < 40; ++i) if (idle.tryTake(1000000)) ++afterIdle;
		CHECK(afterIdle == 20);
	}

	// 10) StateSendPolicy
	{
		using FreeMove::StateSendPolicy;
		State centered; centered.creatureId = 1; centered.z = 7;
		State moving = centered; moving.velX = 32; moving.offX = 140;

		StateSendPolicy p;
		CHECK(!p.shouldSend(1000, centered, false));          // parado no centro, nunca enviado: nada a dizer
		CHECK(p.shouldSend(1000, centered, true));             // forçado (ex.: input processado): envia
		StateSendPolicy q;
		CHECK(q.shouldSend(1000, moving, false));              // começou a mover: envia
		CHECK(!q.shouldSend(1050, moving, false));             // mesma velocidade: espera
		CHECK(!q.shouldSend(1199, moving, false));
		CHECK(q.shouldSend(1200, moving, false));              // 200 ms: ressincroniza
		State turned = moving; turned.velX = -32;
		CHECK(q.shouldSend(1210, turned, false));              // mudou de velocidade: imediato
		State blocked = turned; blocked.flags = FLAG_BLOCKED;
		CHECK(q.shouldSend(1220, blocked, false));             // mudou flag: imediato
		State stopped = blocked; stopped.velX = 0; stopped.flags = 0; stopped.offX = 150;
		CHECK(q.shouldSend(1230, stopped, false));             // parou fora do centro: envia
		CHECK(!q.shouldSend(1500, stopped, false));
		CHECK(!q.shouldSend(2229, stopped, false));
		CHECK(q.shouldSend(2230, stopped, false));             // 1 s parado fora do centro: heartbeat p/ quem chegou agora
		State home = stopped; home.offX = 128; home.offY = 128;
		StateSendPolicy r;
		r.shouldSend(0, stopped, true);
		CHECK(r.shouldSend(10, home, true));                   // forçado sempre envia
		StateSendPolicy s2;
		s2.shouldSend(0, moving, false);
		State homeStop = centered;                              // voltou ao centro e parou: avisa uma vez
		CHECK(s2.shouldSend(10, homeStop, false));
		CHECK(!s2.shouldSend(5000, homeStop, false));          // e nunca mais (parado no centro dispensa estado)
		CHECK(!s2.shouldSend(60000, homeStop, false));
		// TELEPORT é de envio único: ao apagar no tick seguinte NÃO gera novo pacote
		StateSendPolicy tp;
		State tele = moving; tele.flags = FLAG_TELEPORT;
		CHECK(tp.shouldSend(0, tele, true));
		State after = moving; after.flags = 0;
		CHECK(!tp.shouldSend(50, after, false));
		// banda: 10 s movendo a 20 Hz de ticks => ~50 pacotes, não 200
		StateSendPolicy bw;
		int sent = 0;
		for (int t = 0; t < 200; ++t) if (bw.shouldSend(t * 50, moving, false)) ++sent;
		CHECK(sent >= 49 && sent <= 51);
	}

	std::printf("%d verificacoes, %d falhas\n", total, fails);
	return fails ? 1 : 0;
}
