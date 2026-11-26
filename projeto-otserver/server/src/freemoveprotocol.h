// Free Movement - Fase 3: contrato do protocolo de rede.
//
// IDÊNTICO no servidor (sev-c/src/freemoveprotocol.h) e no cliente
// (cli-c/src/client/freemoveprotocol.h). Sem dependências dos projetos: a
// (de)serialização é feita por templates sobre adaptadores mínimos.
//
// Um opcode por direção, com sub-tipo no segundo byte:
//
//   Cliente -> Servidor   opcode 0xEA
//     0x00 HELLO   [u8 version]
//     0x01 INPUT   [u8 seq][u8 dir]            dir: bits 0-1 = dirX+1, bits 2-3 = dirY+1
//
//   Servidor -> Cliente   opcode 0x4E
//     0x00 CONFIG  [u8 version][u16 unitsPerTile][u8 cellPixels][u16 halfExtentUnits]
//     0x01 STATE   [u32 creatureId][u16 tileX][u16 tileY][u8 z][u8 offX][u8 offY]
//                  [i16 velX][i16 velY][u8 flags][u8 ackSeq]
//
//   offX/offY: posição dentro do tile em 1/8 px (0..255; 128 = centro do tile).
//   velX/velY: pixels por segundo (a posição em si é sempre a do servidor).
//   ackSeq:    seq do último INPUT processado para essa criatura (reconciliação).
//
// O servidor é a fonte da verdade: o cliente só envia DIREÇÃO, nunca posição.
// Todos os campos são little-endian, como o resto do protocolo Tibia.

#ifndef FREEMOVEMENT_FREEMOVEPROTOCOL_H
#define FREEMOVEMENT_FREEMOVEPROTOCOL_H

#include <cstddef>
#include <cstdint>

#include "physicalposition.h"

namespace FreeMoveProto {

constexpr uint8_t CONTRACT_VERSION = 1;

constexpr uint8_t CLIENT_OPCODE = 0xEA; // cliente -> servidor
constexpr uint8_t SERVER_OPCODE = 0x4E; // servidor -> cliente

enum ClientSub : uint8_t {
	C_HELLO = 0x00,
	C_INPUT = 0x01,
};

enum ServerSub : uint8_t {
	S_CONFIG = 0x00,
	S_STATE = 0x01,
};

enum StateFlags : uint8_t {
	FLAG_TELEPORT = 1 << 0, // o cliente deve "snapar" (teleporte, escada, ressincronização)
	FLAG_LOCKED = 1 << 1,   // stun/root: não se move
	FLAG_BLOCKED = 1 << 2,  // encostou em algo neste tick
	FLAGS_MASK = FLAG_TELEPORT | FLAG_LOCKED | FLAG_BLOCKED,
};

// Limites de sanidade usados na validação (rejeita lixo e pacotes forjados).
constexpr int32_t MAX_VELOCITY_PX_PER_SEC = Physics::TILE_PIXELS * 64; // 64 tiles/s, folga sobre o teto de 40
constexpr uint8_t MAX_FLOOR = 15;

struct Hello {
	uint8_t version = 0;
};

struct Input {
	uint8_t seq = 0;
	int8_t dirX = 0; // -1, 0, 1
	int8_t dirY = 0;
};

struct Config {
	uint8_t version = CONTRACT_VERSION;
	uint16_t unitsPerTile = static_cast<uint16_t>(Physics::UNITS_PER_TILE);
	uint8_t cellPixels = static_cast<uint8_t>(Physics::CELL_PIXELS);
	uint16_t halfExtentUnits = 0;
};

struct State {
	uint32_t creatureId = 0;
	uint16_t tileX = 0;
	uint16_t tileY = 0;
	uint8_t z = 0;
	uint8_t offX = 128; // 1/8 px
	uint8_t offY = 128;
	int16_t velX = 0;   // px/s
	int16_t velY = 0;
	uint8_t flags = 0;
	uint8_t ackSeq = 0;

	bool isCentered() const { return offX == 128 && offY == 128; }
	bool isMoving() const { return velX != 0 || velY != 0; }
	bool operator==(const State& o) const
	{
		return creatureId == o.creatureId && tileX == o.tileX && tileY == o.tileY && z == o.z && offX == o.offX &&
		       offY == o.offY && velX == o.velX && velY == o.velY && flags == o.flags && ackSeq == o.ackSeq;
	}
};

// Posição física (unidades de 1/64 px) <-> campos do pacote.
inline State makeState(uint32_t creatureId, const Physics::PhysicalPosition& p, int32_t velUnitsX, int32_t velUnitsY,
                       uint8_t flags, uint8_t ackSeq)
{
	State s;
	s.creatureId = creatureId;
	s.tileX = static_cast<uint16_t>(p.tileX());
	s.tileY = static_cast<uint16_t>(p.tileY());
	s.z = p.z;
	s.offX = Physics::offsetToNet(p.offsetX());
	s.offY = Physics::offsetToNet(p.offsetY());
	s.velX = static_cast<int16_t>(velUnitsX / Physics::UNITS_PER_PIXEL);
	s.velY = static_cast<int16_t>(velUnitsY / Physics::UNITS_PER_PIXEL);
	s.flags = static_cast<uint8_t>(flags & FLAGS_MASK);
	s.ackSeq = ackSeq;
	return s;
}

// Posição de mundo (unidades) representada por um STATE (precisão de 1/8 px).
inline Physics::PhysicalPosition toPhysicalPosition(const State& s)
{
	return Physics::PhysicalPosition(Physics::tileToUnits(s.tileX, Physics::netToOffset(s.offX)),
	                                 Physics::tileToUnits(s.tileY, Physics::netToOffset(s.offY)), s.z);
}

// --- direção empacotada ------------------------------------------------------
constexpr uint8_t packDir(int32_t dirX, int32_t dirY)
{
	return static_cast<uint8_t>(((dirX + 1) & 0x3) | (((dirY + 1) & 0x3) << 2));
}

inline bool unpackDir(uint8_t packed, int8_t& dirX, int8_t& dirY)
{
	const int32_t x = packed & 0x3;
	const int32_t y = (packed >> 2) & 0x3;
	if ((packed & 0xF0) != 0 || x > 2 || y > 2) {
		return false; // bits sobrando ou valor 3: pacote inválido
	}
	dirX = static_cast<int8_t>(x - 1);
	dirY = static_cast<int8_t>(y - 1);
	return true;
}

// --- serialização ---------------------------------------------------------------
// Writer precisa de: u8(uint8_t), u16(uint16_t), u32(uint32_t), i16(int16_t).
// Reader precisa de: u8(), u16(), u32(), i16() e bool ok() (false após ler além do fim).
// As funções read* assumem que opcode e sub-tipo JÁ foram consumidos.

template <class W>
void writeHello(W& w, uint8_t version = CONTRACT_VERSION)
{
	w.u8(CLIENT_OPCODE);
	w.u8(C_HELLO);
	w.u8(version);
}

template <class W>
void writeInput(W& w, const Input& in)
{
	w.u8(CLIENT_OPCODE);
	w.u8(C_INPUT);
	w.u8(in.seq);
	w.u8(packDir(in.dirX, in.dirY));
}

template <class W>
void writeConfig(W& w, const Config& c)
{
	w.u8(SERVER_OPCODE);
	w.u8(S_CONFIG);
	w.u8(c.version);
	w.u16(c.unitsPerTile);
	w.u8(c.cellPixels);
	w.u16(c.halfExtentUnits);
}

template <class W>
void writeState(W& w, const State& s)
{
	w.u8(SERVER_OPCODE);
	w.u8(S_STATE);
	w.u32(s.creatureId);
	w.u16(s.tileX);
	w.u16(s.tileY);
	w.u8(s.z);
	w.u8(s.offX);
	w.u8(s.offY);
	w.i16(s.velX);
	w.i16(s.velY);
	w.u8(s.flags);
	w.u8(s.ackSeq);
}

template <class R>
bool readHello(R& r, Hello& out)
{
	out.version = r.u8();
	return r.ok();
}

template <class R>
bool readInput(R& r, Input& out)
{
	out.seq = r.u8();
	const uint8_t packed = r.u8();
	return r.ok() && unpackDir(packed, out.dirX, out.dirY);
}

template <class R>
bool readConfig(R& r, Config& out)
{
	out.version = r.u8();
	out.unitsPerTile = r.u16();
	out.cellPixels = r.u8();
	out.halfExtentUnits = r.u16();
	return r.ok() && out.unitsPerTile != 0 && out.cellPixels != 0;
}

template <class R>
bool readState(R& r, State& out)
{
	out.creatureId = r.u32();
	out.tileX = r.u16();
	out.tileY = r.u16();
	out.z = r.u8();
	out.offX = r.u8();
	out.offY = r.u8();
	out.velX = r.i16();
	out.velY = r.i16();
	out.flags = r.u8();
	out.ackSeq = r.u8();
	if (!r.ok()) {
		return false;
	}
	const int32_t vx = out.velX < 0 ? -static_cast<int32_t>(out.velX) : out.velX;
	const int32_t vy = out.velY < 0 ? -static_cast<int32_t>(out.velY) : out.velY;
	return out.creatureId != 0 && out.z <= MAX_FLOOR && (out.flags & ~FLAGS_MASK) == 0 &&
	       vx <= MAX_VELOCITY_PX_PER_SEC && vy <= MAX_VELOCITY_PX_PER_SEC;
}

// Tamanhos de referência (bytes de payload, incluindo opcode e sub-tipo).
constexpr size_t HELLO_SIZE = 3;
constexpr size_t INPUT_SIZE = 4;
constexpr size_t CONFIG_SIZE = 8;
constexpr size_t STATE_SIZE = 19;

} // namespace FreeMoveProto

#endif // FREEMOVEMENT_FREEMOVEPROTOCOL_H
