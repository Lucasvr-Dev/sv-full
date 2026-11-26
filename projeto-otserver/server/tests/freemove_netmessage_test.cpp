// Verifica que o NetworkMessage REAL do servidor produz/consome exatamente os bytes do contrato.
// Compilar a partir da raiz do repositório (precisa dos headers do engine, não linka o servidor):
//   g++ -std=c++17 -Wall -Wextra -Isrc tests/freemove_netmessage_test.cpp -o freemove_netmessage_test
#include "otpch.h"

#include "networkmessage.h"
#include "freemoveprotocol.h"

#include <cstdio>
#include <vector>

using namespace FreeMoveProto;

// Mesmos adaptadores de protocolgame.cpp (mantidos em sincronia manualmente: são 10 linhas).
struct FreeMoveWriter {
	NetworkMessage& msg;
	void u8(uint8_t v) { msg.addByte(v); }
	void u16(uint16_t v) { msg.add<uint16_t>(v); }
	void u32(uint32_t v) { msg.add<uint32_t>(v); }
	void i16(int16_t v) { msg.add<int16_t>(v); }
};
struct FreeMoveReader {
	NetworkMessage& msg;
	uint8_t u8() { return msg.getByte(); }
	uint16_t u16() { return msg.get<uint16_t>(); }
	uint32_t u32() { return msg.get<uint32_t>(); }
	int16_t i16() { return msg.get<int16_t>(); }
	bool ok() const { return !msg.isOverrun(); }
};

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::vector<uint8_t> bytesOf(const NetworkMessage& m)
{
	const uint8_t* p = m.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION;
	return std::vector<uint8_t>(p, p + m.getLength());
}

int main()
{
	// STATE com valores que exercitam little-endian e sinal
	State s{0x01020304u, 0x0A0B, 0x0C0D, 7, 0x11, 0x22, -2, 300, FLAG_BLOCKED | FLAG_LOCKED, 0xFE};
	NetworkMessage m;
	FreeMoveWriter w{m};
	writeState(w, s);
	const std::vector<uint8_t> expected = {
		0x4E, 0x01,                   // opcode, sub-tipo
		0x04, 0x03, 0x02, 0x01,       // creatureId (LE)
		0x0B, 0x0A, 0x0D, 0x0C,       // tileX, tileY (LE)
		0x07, 0x11, 0x22,             // z, offX, offY
		0xFE, 0xFF, 0x2C, 0x01,       // velX=-2, velY=300 (LE, complemento de 2)
		0x06, 0xFE,                   // flags, ackSeq
	};
	CHECK(bytesOf(m) == expected);
	CHECK(bytesOf(m).size() == STATE_SIZE);

	// Leitura pelo NetworkMessage real: round-trip completo
	m.setBufferPosition(0);
	FreeMoveReader r{m};
	CHECK(r.u8() == SERVER_OPCODE && r.u8() == S_STATE);
	State out;
	CHECK(readState(r, out) && out == s);

	// INPUT e HELLO (o que o cliente envia), lidos como o servidor lê
	NetworkMessage mi;
	FreeMoveWriter wi{mi};
	writeInput(wi, Input{200, -1, 1});
	CHECK(bytesOf(mi) == (std::vector<uint8_t>{0xEA, 0x01, 200, packDir(-1, 1)}));
	mi.setBufferPosition(0);
	FreeMoveReader ri{mi};
	CHECK(ri.u8() == CLIENT_OPCODE && ri.u8() == C_INPUT);
	Input in;
	CHECK(readInput(ri, in) && in.seq == 200 && in.dirX == -1 && in.dirY == 1);

	NetworkMessage mh;
	FreeMoveWriter wh{mh};
	writeHello(wh);
	CHECK(bytesOf(mh) == (std::vector<uint8_t>{0xEA, 0x00, CONTRACT_VERSION}));

	// Pacote truncado no NetworkMessage real: overrun detectado, nunca aceito
	NetworkMessage mt;
	mt.addByte(0x01);
	mt.addByte(0x05);            // INPUT com só o seq, sem a direção
	mt.setBufferPosition(0);
	FreeMoveReader rt{mt};
	rt.u8();                     // sub-tipo
	Input it;
	CHECK(!readInput(rt, it));

	std::printf("%d verificacoes, %d falhas\n", total, fails);
	return fails ? 1 : 0;
}
