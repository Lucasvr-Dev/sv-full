// Free Movement - Fase 3 (somente cliente): decide QUANDO enviar o INPUT de direção.
//
// O servidor desconecta quem passa de maxPacketsPerSecond (25 por padrão, média desde a
// conexão), então o cliente NUNCA envia um pacote por frame. Regras:
//  - uma MUDANÇA de direção (inclusive parar) é enviada na hora, no máximo 1 pacote a cada
//    MIN_INTERVAL_MS: teto rígido de 20 pacotes/s, mesmo com o jogador martelando as teclas.
//    Mudanças mais rápidas são coalescidas (vale a mais recente); uma parada logo após outro
//    envio espera, no máximo, MIN_INTERVAL_MS;
//  - enquanto houver direção ativa, reenvia a cada KEEPALIVE_MS (o servidor para o corpo
//    se ficar mais de 1 s sem input, por exemplo se o cliente travar).
//
// Sem dependências do engine: chamado a cada frame com o relógio em ms, sem alocar nada.

#ifndef FREEMOVEMENT_FREEMOVEINPUT_H
#define FREEMOVEMENT_FREEMOVEINPUT_H

#include <cstdint>

#include "freemoveprotocol.h"

namespace FreeMove {

class InputSender
{
public:
	static constexpr int64_t MIN_INTERVAL_MS = 50;
	static constexpr int64_t KEEPALIVE_MS = 250;

	// Chame a cada frame com a direção desejada agora. Retorna true quando 'out' deve ser enviado.
	bool poll(int64_t now, int32_t dirX, int32_t dirY, FreeMoveProto::Input& out)
	{
		dirX = dirX < -1 ? -1 : (dirX > 1 ? 1 : dirX);
		dirY = dirY < -1 ? -1 : (dirY > 1 ? 1 : dirY);

		const bool changed = dirX != lastX || dirY != lastY;
		const bool active = dirX != 0 || dirY != 0;

		bool send = false;
		if (changed) {
			send = !hasSent || (now - lastSendTime) >= MIN_INTERVAL_MS;
		} else if (active) {
			send = (now - lastSendTime) >= KEEPALIVE_MS;
		}

		if (!send) {
			return false;
		}

		lastX = dirX;
		lastY = dirY;
		lastSendTime = now;
		hasSent = true;
		out.seq = nextSeq;
		lastSeqSent = nextSeq;
		nextSeq = nextSeq == 255 ? 1 : static_cast<uint8_t>(nextSeq + 1); // pula o 0
		out.dirX = static_cast<int8_t>(dirX);
		out.dirY = static_cast<int8_t>(dirY);
		return true;
	}

	// Chame ao entrar/sair do jogo: o servidor começa sem input ativo.
	void reset()
	{
		lastX = lastY = 0;
		lastSendTime = 0;
		hasSent = false;
	}

	uint8_t lastSeq() const { return lastSeqSent; }

private:
	int32_t lastX = 0;
	int32_t lastY = 0;
	int64_t lastSendTime = 0;
	uint8_t nextSeq = 1; // 0 fica reservado como "nenhum input processado ainda"
	uint8_t lastSeqSent = 0;
	bool hasSent = false;
};

} // namespace FreeMove

#endif // FREEMOVEMENT_FREEMOVEINPUT_H
