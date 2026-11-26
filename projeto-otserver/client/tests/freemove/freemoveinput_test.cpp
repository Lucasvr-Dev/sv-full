// Testes do emissor de INPUT (Fase 3, cliente). Compilar a partir da raiz do cli-c:
//   g++ -std=c++17 -Wall -Wextra -Werror -Isrc/client tests/freemove/freemoveinput_test.cpp -o freemoveinput_test
#include "freemoveinput.h"

#include <cstdio>
#include <vector>

using FreeMove::InputSender;
using FreeMoveProto::Input;

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t rng = 42;
static uint32_t next() { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main()
{
	Input in;

	// 1) Ocioso: nunca envia nada
	{
		InputSender s; int n = 0;
		for (int t = 0; t < 10000; t += 16) if (s.poll(t, 0, 0, in)) ++n;
		CHECK(n == 0);
	}

	// 2) Apertar: envia na hora; mantido, só keepalive a cada 250 ms
	{
		InputSender s; int n = 0; int64_t first = -1, second = -1;
		for (int64_t t = 1000; t < 3000; t += 16) {
			if (s.poll(t, 1, 0, in)) { ++n; if (first < 0) first = t; else if (second < 0) second = t; CHECK(in.dirX == 1 && in.dirY == 0); }
		}
		CHECK(first == 1000);
		CHECK(second - first >= InputSender::KEEPALIVE_MS && second - first < InputSender::KEEPALIVE_MS + 16);
		CHECK(n >= 7 && n <= 9); // ~2 s / 250 ms
	}

	// 3) seq cresce e nunca é 0 (nem após dar a volta)
	{
		InputSender s; bool zero = false; int sent = 0; uint8_t prev = 0;
		for (int64_t t = 0; sent < 600; t += 60) {
			int dx = (sent % 2) ? 1 : -1;
			if (s.poll(t, dx, 0, in)) { ++sent; if (in.seq == 0) zero = true; if (sent > 1 && in.seq != (prev == 255 ? 1 : prev + 1)) zero = true; prev = in.seq; }
		}
		CHECK(!zero);
	}

	// 4) Coalescência: mudanças rápidas viram UM envio com a direção mais recente
	{
		InputSender s;
		CHECK(s.poll(1000, 1, 0, in));              // direita: envia
		CHECK(!s.poll(1010, 0, 1, in));             // baixo logo depois: espera
		CHECK(!s.poll(1020, -1, 0, in));            // esquerda: espera
		CHECK(!s.poll(1049, -1, 0, in));
		CHECK(s.poll(1050, -1, 0, in));             // passou 50 ms: envia a mais recente
		CHECK(in.dirX == -1 && in.dirY == 0);
	}

	// 5) Parar é enviado assim que o intervalo permite e para de enviar depois
	{
		InputSender s;
		s.poll(0, 1, 0, in);
		CHECK(s.poll(500, 0, 0, in) && in.dirX == 0 && in.dirY == 0);   // parar depois de muito tempo: na hora
		int n = 0;
		for (int64_t t = 516; t < 5000; t += 16) if (s.poll(t, 0, 0, in)) ++n;
		CHECK(n == 0);
		InputSender q;
		q.poll(0, 1, 0, in);                         // toque rápido: aperta e solta em 30 ms
		CHECK(!q.poll(30, 0, 0, in));
		CHECK(q.poll(50, 0, 0, in) && in.dirX == 0); // a parada sai no próximo intervalo permitido
	}

	// 6) Teto de banda: martelando teclas a 60 FPS por 60 s, nenhuma janela de 1 s passa de 20 pacotes
	{
		InputSender s;
		std::vector<int64_t> sentAt;
		for (int64_t t = 0; t < 60000; t += 16) {
			int dx = (int)(next() % 3) - 1, dy = (int)(next() % 3) - 1;
			if (s.poll(t, dx, dy, in)) sentAt.push_back(t);
		}
		int worstWindow = 0;
		for (size_t i = 0; i < sentAt.size(); ++i) {
			int inWindow = 0;
			for (size_t j = i; j < sentAt.size() && sentAt[j] < sentAt[i] + 1000; ++j) ++inWindow;
			if (inWindow > worstWindow) worstWindow = inWindow;
		}
		CHECK(sentAt.size() > 100);          // o teste realmente exercita o envio
		CHECK(worstWindow <= 20);
	}

	// 7) Reset (novo login): o primeiro input sai na hora de novo
	{
		InputSender s; s.poll(1000, 1, 0, in); s.reset();
		CHECK(s.poll(1001, 1, 0, in));
	}

	// 8) Valores fora do intervalo são limitados a -1..1
	{
		InputSender s;
		CHECK(s.poll(0, 9, -9, in) && in.dirX == 1 && in.dirY == -1);
	}

	std::printf("%d verificacoes, %d falhas\n", total, fails);
	return fails ? 1 : 0;
}
