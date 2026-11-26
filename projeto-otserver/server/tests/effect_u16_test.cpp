// Verifica que IDs de effect/missile de 16 bits sao serializados sem truncar e sem desalinhar o pacote.
// Usa o NetworkMessage REAL do servidor (header-only, nao linka o servidor).
// Compilar a partir da raiz do repositorio:
//   g++ -std=c++17 -Wall -Wextra -Isrc -I/ucrt64/include tests/effect_u16_test.cpp -o effect_u16_test -lws2_32 -lmswsock && ./effect_u16_test
#include "otpch.h"

#include "networkmessage.h"
#include "const.h"

#include <cstdio>
#include <type_traits>

static_assert(std::is_same<std::underlying_type<MagicEffectClasses>::type, uint16_t>::value, "MagicEffectClasses deve ser uint16_t");
static_assert(std::is_same<std::underlying_type<ShootType_t>::type, uint16_t>::value, "ShootType_t deve ser uint16_t");

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FALHOU: %s (linha %d)\n", #cond, __LINE__); ++failures; } } while (0)

// Mesmo layout de ProtocolGame::sendMagicEffect: 0x83, x(u16), y(u16), z(u8), effect(u16)
static void writeMagicEffect(NetworkMessage& msg, uint16_t x, uint16_t y, uint8_t z, uint16_t type) {
	msg.addByte(0x83);
	msg.add<uint16_t>(x);
	msg.add<uint16_t>(y);
	msg.addByte(z);
	msg.add<uint16_t>(type);
}

// Mesmo layout de ProtocolGame::sendDistanceShoot: 0x85, from(5), to(5), type(u16)
static void writeDistanceShoot(NetworkMessage& msg, uint16_t type) {
	msg.addByte(0x85);
	msg.add<uint16_t>(100); msg.add<uint16_t>(200); msg.addByte(7);
	msg.add<uint16_t>(101); msg.add<uint16_t>(201); msg.addByte(7);
	msg.add<uint16_t>(type);
}

int main() {
	const uint16_t ids[] = {0, 1, 255, 256, 1000, 65535};

	for (uint16_t id : ids) {
		// Effect + byte sentinela logo depois, para provar o alinhamento
		NetworkMessage msg;
		writeMagicEffect(msg, 32000, 31000, 7, id);
		msg.addByte(0xAB);
		msg.setBufferPosition(0);
		CHECK(msg.getByte() == 0x83);
		CHECK(msg.get<uint16_t>() == 32000);
		CHECK(msg.get<uint16_t>() == 31000);
		CHECK(msg.getByte() == 7);
		CHECK(msg.get<uint16_t>() == id);   // sem truncar
		CHECK(msg.getByte() == 0xAB);       // sem desalinhar
		CHECK(!msg.isOverrun());

		// Missile + sentinela
		NetworkMessage dmsg;
		writeDistanceShoot(dmsg, id);
		dmsg.addByte(0xCD);
		dmsg.setBufferPosition(0);
		CHECK(dmsg.getByte() == 0x85);
		dmsg.skipBytes(10);
		CHECK(dmsg.get<uint16_t>() == id);
		CHECK(dmsg.getByte() == 0xCD);
		CHECK(!dmsg.isOverrun());

		// Os enums guardam o valor completo (sem truncar para 8 bits)
		CHECK(static_cast<uint16_t>(static_cast<MagicEffectClasses>(id)) == id);
		CHECK(static_cast<uint16_t>(static_cast<ShootType_t>(id)) == id);
	}

	// Ordem de bytes: little-endian (igual ao resto do protocolo / getU16 do cliente)
	NetworkMessage le;
	writeMagicEffect(le, 0, 0, 0, 0x0102);
	const uint8_t* raw = le.getBuffer() + NetworkMessage::INITIAL_BUFFER_POSITION;
	CHECK(raw[6] == 0x02 && raw[7] == 0x01);

	// Efeitos legados (<256) continuam com o mesmo valor
	CHECK(static_cast<uint16_t>(CONST_ME_TELEPORT) == 11);

	if (failures == 0) {
		std::printf("effect_u16_test: OK\n");
		return 0;
	}
	return 1;
}
