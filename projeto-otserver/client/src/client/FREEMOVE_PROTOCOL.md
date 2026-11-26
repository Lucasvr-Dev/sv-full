# Protocolo Free Movement (Fase 3)

Fonte da verdade: `freemoveprotocol.h`, idêntico em `sev-c/src/` e `cli-c/src/client/`.
Todos os campos são **little-endian**. O servidor é autoritativo: o cliente envia só **direção**, nunca posição.

## Opcodes

| Direção | Opcode | Por quê este número |
|---|---|---|
| Cliente → Servidor | `0xEA` | Livre na tabela de entrada do servidor e na do cliente Redemption |
| Servidor → Cliente | `0x4E` | Livre nas duas tabelas de saída |

Um opcode por direção, com **sub-tipo** no 2º byte. Outro sub-tipo desconhecido é descartado.

## Handshake

1. Ao entrar no jogo o cliente envia `HELLO`.
2. Se `freeMovement = true` no `config.lua` **e** a `CONTRACT_VERSION` bate, o servidor marca o jogador como compatível e responde `CONFIG`.
3. O cliente valida `unitsPerTile` e `cellPixels` contra suas constantes. Se divergirem, o recurso fica desligado.
4. Sem `CONFIG`, nada muda: servidor e cliente continuam no modo clássico por tile.

O servidor **só envia `STATE` a quem fez HELLO**, então clientes antigos nunca recebem um opcode que não entendem.

## Cliente → Servidor (`0xEA`)

### `HELLO` (sub-tipo `0x00`, 3 bytes)
| Campo | Tipo | Notas |
|---|---|---|
| opcode | u8 | `0xEA` |
| sub-tipo | u8 | `0x00` |
| version | u8 | `CONTRACT_VERSION` (hoje 1) |

### `INPUT` (sub-tipo `0x01`, 4 bytes)
| Campo | Tipo | Notas |
|---|---|---|
| opcode | u8 | `0xEA` |
| sub-tipo | u8 | `0x01` |
| seq | u8 | Contador do cliente, 1..255, nunca 0. O servidor devolve o último processado em `ackSeq` |
| dir | u8 | bits 0-1 = `dirX+1`, bits 2-3 = `dirY+1`. Valor 3 em qualquer campo, ou bits 4-7 ligados = pacote inválido |

**Frequência (cliente):** no máximo 1 pacote a cada 50 ms (teto de 20/s) em mudança de direção, incluindo parar. Com direção ativa, reenvia a cada 250 ms (keepalive). Parado, não envia nada.

## Servidor → Cliente (`0x4E`)

### `CONFIG` (sub-tipo `0x00`, 8 bytes)
| Campo | Tipo | Notas |
|---|---|---|
| opcode, sub-tipo | u8, u8 | `0x4E`, `0x00` |
| version | u8 | `CONTRACT_VERSION` |
| unitsPerTile | u16 | 2048 |
| cellPixels | u8 | 8 (células da AoE circular) |
| halfExtentUnits | u16 | Meia-largura padrão da caixa física (640 = 10 px) |

### `STATE` (sub-tipo `0x01`, 19 bytes)
| Campo | Tipo | Unidade / notas |
|---|---|---|
| opcode, sub-tipo | u8, u8 | `0x4E`, `0x01` |
| creatureId | u32 | Nunca 0 |
| tileX, tileY | u16, u16 | Tile que contém o **centro** da criatura |
| z | u8 | 0..15 |
| offX, offY | u8, u8 | Posição dentro do tile em **1/8 px** (0..255, 128 = centro) |
| velX, velY | i16, i16 | **px/s** (para o cliente extrapolar entre pacotes) |
| flags | u8 | bit0 `TELEPORT` (o cliente deve snapar), bit1 `LOCKED` (stun/root), bit2 `BLOCKED` (encostou) |
| ackSeq | u8 | Último `INPUT` processado para essa criatura |

**Precisão:** o servidor guarda 1/64 px; na rede vai 1/8 px (erro máximo < 1/8 px). O centro do tile é exato.

**Quando o servidor envia:** a quem vê a criatura, e só se (a) velocidade ou flags mudaram, ou houve input/teleporte; (b) a cada 200 ms enquanto se move; (c) a cada 1 s se está parada **fora** do centro do tile (para quem acabou de chegar). Parada no centro não precisa de estado.
Com isso, uma criatura se movendo gera ~5 pacotes/s por espectador, em vez de 20.

## Validação e segurança

- **Cliente → servidor:** direção inválida e pacote truncado são descartados em silêncio. Limite por jogador de 20 inputs/s; **parar nunca é descartado**. Sem `HELLO` válido, `INPUT` é ignorado. Jogador morto ou removido é ignorado.
- **Velocidade nunca vem do cliente.** Vem de `Creature::getStepDuration()` (atributo `speed` e velocidade do piso), com teto de 40 tiles/s.
- **Stun/root:** o tick aplica `Creature::isMovementBlocked()`, igual ao `Game::playerMove` legado.
- **Sem keepalive por 1 s** o corpo para sozinho (cliente travado).
- O servidor desconecta quem passa de `maxPacketsPerSecond` (25, `connection.cpp`). O cliente respeita isso com o teto de 20/s.
- **Servidor → cliente:** o cliente rejeita `STATE` com `z > 15`, flags desconhecidas, velocidade > 64 tiles/s ou `creatureId` 0.

## Limitações conhecidas desta fase

- O cliente **recebe e guarda** o `STATE` (`Creature::getPhysicalSnapshot`), mas ainda não renderiza nem envia input real: isso é a Fase 4.
- A física anda em ticks de 50 ms; um input processado entre ticks só tem efeito no tick seguinte.
- Quem não tem suporte vê a criatura se mover tile a tile, quando o centro cruza a borda (o envio legado continua).
- Sem predição nem reconciliação no cliente (Fase 4).

## Como habilitar

No `config.lua` do servidor: `freeMovement = true` (padrão `false`; a chave está documentada em `config.lua.dist`).
