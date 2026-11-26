// Testes unitários do suavizador de render (Fase 4, cliente). A partir da raiz do cli-c:
//   g++ -std=c++17 -Wall -Wextra -Werror -Isrc/client tests/freemove/freemoverender_test.cpp -o freemoverender_test
#include "freemoverender.h"

#include <cmath>
#include <cstdio>

using namespace FreeMove;

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

int main()
{
	using FreeMoveProto::FLAG_TELEPORT;

	// 1) Primeiro estado: o render é exatamente a posição do servidor
	{
		RenderSmoother s;
		CHECK(!s.hasState());
		CHECK(s.onState(1000.0, 2000.0, 0, 0, 0, 100));
		Vec2 p = s.position(100);
		CHECK(near(p.x, 1000.0) && near(p.y, 2000.0));
		CHECK(near(s.position(5000).x, 1000.0)); // parado: não anda sozinho
	}

	// 2) Extrapolação em regime: pos + vel * idade
	{
		RenderSmoother s;
		s.onState(100.0, 100.0, 96, 0, 0, 0);
		CHECK(near(s.position(100).x, 100.0 + 9.6));
		CHECK(near(s.position(100).y, 100.0));
		CHECK(near(s.position(250).x, 100.0 + 24.0));
		// além da janela de extrapolação não continua andando (evita atravessar parede por pacote perdido)
		CHECK(near(s.position(1000).x, 100.0 + 24.0));
		CHECK(near(s.position(100000).x, 100.0 + 24.0));
	}

	// 3) Correção sem salto: o render é contínuo no instante em que o STATE chega
	{
		RenderSmoother s;
		s.onState(0.0, 0.0, 96, 0, 0, 0);
		const Vec2 before = s.position(200);              // 19.2
		const bool snapped = s.onState(24.2, 0.0, 96, 0, 0, 200); // servidor discorda: +5 px
		const Vec2 after = s.position(200);
		CHECK(!snapped);
		CHECK(near(before.x, after.x, 1e-9) && near(before.y, after.y, 1e-9));
		// a correção decai e o render converge para a trajetória nova
		const Vec2 later = s.position(200 + 90 * 8);
		const double target = 24.2 + 96 * (std::min<int64_t>(90 * 8, 250)) / 1000.0;
		CHECK(near(later.x, target, 0.05));
		CHECK(s.position(200 + 90).x > before.x); // andou
	}

	// 4) TELEPORT e erro grande: snap imediato, sem suavização
	{
		RenderSmoother s;
		s.onState(0.0, 0.0, 0, 0, 0, 0);
		CHECK(s.onState(10.0, 0.0, 0, 0, FLAG_TELEPORT, 10)); // flag
		CHECK(near(s.position(10).x, 10.0));
		CHECK(s.onState(500.0, 0.0, 0, 0, 0, 20));            // > 64 px
		CHECK(near(s.position(20).x, 500.0));
		CHECK(!s.onState(520.0, 0.0, 0, 0, 0, 30));           // 20 px: suaviza
	}

	// 5) Parar: a posição fica fixa, em qualquer ponto (sem snap para o centro)
	{
		RenderSmoother s;
		s.onState(0.0, 0.0, 96, 0, 0, 0);
		s.onState(13.37, 0.0, 0, 0, 0, 150);                  // parou em 13.37 px
		const Vec2 settled = s.position(150 + 90 * 10);
		CHECK(near(settled.x, 13.37, 0.01));
		CHECK(near(s.position(60000).x, 13.37, 1e-3)); // longe no futuro: exatamente onde o servidor parou
		CHECK(!s.isAnimating(150 + 90 * 10));
	}

	// 6) isAnimating
	{
		RenderSmoother s;
		CHECK(!s.isAnimating(0));
		s.onState(0.0, 0.0, 96, 0, 0, 0);
		CHECK(s.isAnimating(100));
		CHECK(!s.isAnimating(300));                            // fora da janela e sem correção
		RenderSmoother c;
		c.onState(0.0, 0.0, 0, 0, 0, 0);
		c.onState(0.0, 0.0, 0, 0, 0, 10);                       // sem erro: nada a animar
		CHECK(!c.isAnimating(10));
		RenderSmoother d;
		d.onState(0.0, 0.0, 0, 0, 0, 0);
		d.onState(8.0, 0.0, 0, 0, 0, 10);                       // salto de 8 px: correção
		CHECK(d.isAnimating(10));                               // correção visível: continua animando
		CHECK(!d.isAnimating(10 + 90 * 10));                    // depois de ~10 constantes de tempo: estável
	}

	// 7) Robustez: relógio andando para trás e valores extremos nunca geram NaN/inf
	{
		RenderSmoother s;
		s.onState(2.0e6, 2.0e6, 1000, -1000, 0, 5000);
		Vec2 p = s.position(4000); // antes do recebimento
		CHECK(std::isfinite(p.x) && std::isfinite(p.y));
		CHECK(near(p.x, 2.0e6) && near(p.y, 2.0e6));
		s.onState(2.0e6 + 3, 2.0e6, 32767, -32768, 0, 1);       // tempo voltou
		p = s.position(0);
		CHECK(std::isfinite(p.x) && std::isfinite(p.y));
	}

	// 8) Direção de 8 vias
	{
		CHECK(directionFromVelocity(0, 0) == -1);
		CHECK(directionFromVelocity(0, -96) == 0);   // N
		CHECK(directionFromVelocity(96, 0) == 1);    // E
		CHECK(directionFromVelocity(0, 96) == 2);    // S
		CHECK(directionFromVelocity(-96, 0) == 3);   // W
		CHECK(directionFromVelocity(68, -68) == 4);  // NE
		CHECK(directionFromVelocity(68, 68) == 5);   // SE
		CHECK(directionFromVelocity(-68, 68) == 6);  // SW
		CHECK(directionFromVelocity(-68, -68) == 7); // NW
		CHECK(directionFromVelocity(96, 10) == 1);   // quase horizontal: cardinal
		CHECK(directionFromVelocity(-5, 96) == 2);   // quase vertical: cardinal
	}

	std::printf("%d verificacoes, %d falhas\n", total, fails);
	return fails ? 1 : 0;
}
