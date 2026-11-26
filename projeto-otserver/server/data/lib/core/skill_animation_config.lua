-- =============================================================================
--  SISTEMA 1 — ANIMAÇÃO DE SKILL: CONFIGURAÇÃO E TABELA DE ANIMAÇÕES
--  Este é o arquivo que você edita no dia a dia. O motor fica em
--  data/lib/core/skill_animation.lua (não precisa mexer nele).
--  Depois de editar: /reload libs (ou reinicie o servidor). Erros de
--  digitação na tabela aparecem no console como "[SkillAnimation] ...".
-- =============================================================================
SkillAnimationConfig = {
    -- Nome registrado em data/monster/monsters.xml para a "skin de animação".
    actorName = "Animation Actor",

    -- Condition de outfit vazio (invisibilidade do corpo real).
    -- Usa um conditionId de SLOT, que o servidor NUNCA grava no banco
    -- (Condition::isPersistent só persiste DEFAULT/COMBAT). Por isso um crash
    -- não consegue deixar ninguém invisível no próximo login.
    conditionId = CONDITIONID_AMMO,
    outfitSubId = 9101, -- identifica a condition desta lib (não reutilize)
    invisibleSubId = 9102,

    -- Rede de segurança: a condition de outfit dura (duration + margem) e se
    -- apaga sozinha mesmo que todo o Lua falhe. O lock de ação dura exatamente
    -- "duration" e também expira sozinho (timestamp em memória no C++).
    safetyMarginMs = 400,

    -- Com que frequência o Actor confere se o jogador mudou de tile (teleport).
    syncIntervalMs = 250,

    -- Monstros: false = continuam atacando o corpo invisível (padrão pedido).
    --           true  = aplica também CONDITION_INVISIBLE, então monstros
    --                   perdem o alvo (e jogadores comuns veem o corpo vazio).
    monstersLoseTarget = false,

    -- Imunidade durante a animação (dano que não seja cura).
    -- Padrão: o corpo pode ser atingido por tudo (PvE e PvP).
    immuneToMonsters = false,
    immuneToPlayers = false,

    -- Mensagem quando alguém tenta usar spell/item/andar durante a animação é
    -- tratada no C++ (RETURNVALUE_YOUAREEXHAUSTED); aqui só o log de depuração.
    debug = false
}

-- =============================================================================
--  QUANDO SAI O DANO (hit) — definido NA SKILL, não aqui
--
--  As animações abaixo são compartilhadas por várias skills (ex.: "skill_ms500").
--  Por isso elas NÃO trazem hits: cada skill diz, no próprio script, em que
--  momento (em ms desde o início da animação) o dano sai:
--
--    function onCastSpell(creature, variant)
--        return playSkillAnimation(creature, "skill_ms500", variant,
--            function(player, var, hitNumber, totalHits)
--                combat:execute(player, var)
--            end,
--            { hitAt = { 200 } })          -- 1 hit aos 200 ms
--    end
--
--    { hitAt = { 100, 300 } }   -- 2 hits (100 ms e 300 ms)
--    { hitSteps = { 2, 3 } }    -- alternativa: pelos índices dos passos
--
--  Se a skill não informar nada, valem as flags hit = true da animação (se
--  existirem). Se nada definir hit algum, sai 1 hit na metade da duração e o
--  console avisa. O mesmo onHit roda em cada hit; hitNumber/totalHits dizem
--  qual é (1º de 2, etc.).
-- =============================================================================

-- =============================================================================
--  FUNÇÃO AUXILIAR: makeAnimation
--
--  Gera os passos sozinha a partir de uma lista de outfits, para você não
--  precisar escrever cada passo à mão.
--
--  makeAnimation{
--      outfits  = { outfitA, outfitB, ..., "self" },  -- frames, em ordem
--      interval = 400,       -- ms entre um frame e o próximo (padrão 200)
--      hits     = { [2] = true, [3] = true },  -- OPCIONAL: hit padrão da animação
--                            -- (a skill pode sobrescrever com hitAt)
--      effect   = CONST_ME_*,  -- opcional: efeito em todos os frames
--                              -- (padrão CONST_ME_NONE, sem efeito)
--      duration = 2500,      -- opcional: duração total = tempo de stun.
--                            -- Se omitido: (frames - 1) * interval + tail
--      tail     = 300,       -- opcional: tempo extra após o último frame
--                            -- (só vale quando duration não é informado)
--  }
--
--  Regra: duration precisa ser >= ao delay do último frame, senão o frame
--  nunca roda (o validate() avisa no console).
-- =============================================================================

local function makeAnimation(opts)
    local steps = {}
    local interval = opts.interval or 200
    for i, outfit in ipairs(opts.outfits) do
        steps[#steps + 1] = {
            delay = (i - 1) * interval,
            outfit = outfit,
            effect = opts.effect or CONST_ME_NONE,
            hit = (opts.hits and opts.hits[i]) or false
        }
    end
    return {
        duration = opts.duration or
            ((#opts.outfits - 1) * interval + (opts.tail or 300)),
        steps = steps
    }
end

-- =============================================================================
--  TABELA DE ANIMAÇÕES
--
--  Cada skill chama a animação pelo nome: playSkillAnimation(creature, "nome", ...)
--  Duas formas de declarar (podem ser misturadas na mesma tabela):
--
--  A) Com makeAnimation (recomendado, mais curto): veja acima.
--
--  B) À mão, quando cada passo precisa de um tempo diferente:
--
--  SkillAnimations["nome"] = {
--      duration = <ms>,         -- duração total = duração do stun
--      steps = {
--          {
--              delay   = <ms>,                 -- 0..duration, a partir do início
--              outfit  = "self" | { lookType=, lookHead=, lookBody=, lookLegs=,
--                                   lookFeet=, lookAddons=, lookTypeEx=, lookMount= },
--              direction = "self" | DIRECTION_*,  -- opcional
--              effect  = CONST_ME_*,           -- opcional, sai na posição do Actor
--              hit     = true,                 -- opcional: hit padrão neste passo
--          },
--          ...
--      },
--  }
--
--  "self" no outfit = o outfit que o jogador tinha ao iniciar a skill.
--  O 1º passo com delay = 0 roda na hora; os demais, via agendador (mín. ~50ms).
--  Os outfits abaixo são PLACEHOLDERS (outfits padrão do client 10.98).
-- =============================================================================

-- Outfits reutilizáveis (troque pelos seus)
local giro1 = {
    lookType = 130,
    lookHead = 78,
    lookBody = 68,
    lookLegs = 58,
    lookFeet = 76
}
local giro2 = {
    lookType = 134,
    lookHead = 94,
    lookBody = 79,
    lookLegs = 114,
    lookFeet = 114
}
local giro3 = {
    lookType = 131,
    lookHead = 77,
    lookBody = 94,
    lookLegs = 94,
    lookFeet = 77
}

SkillAnimations = {
    -- Mesma animação de antes (1500 ms), agora gerada pela função.
    ["skill_ms500"] = makeAnimation {
        interval = 100,
        duration = 500,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms600"] = makeAnimation {
        interval = 100,
        duration = 600,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms700"] = makeAnimation {
        interval = 100,
        duration = 700,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms800"] = makeAnimation {
        interval = 100,
        duration = 800,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms900"] = makeAnimation {
        interval = 100,
        duration = 900,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1000"] = makeAnimation {
        interval = 100,
        duration = 1000,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1100"] = makeAnimation {
        interval = 100,
        duration = 1100,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1200"] = makeAnimation {
        interval = 100,
        duration = 1200,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1300"] = makeAnimation {
        interval = 100,
        duration = 1300,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1400"] = makeAnimation {
        interval = 100,
        duration = 1400,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1500"] = makeAnimation {
        interval = 100,
        duration = 1500,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1600"] = makeAnimation {
        interval = 100,
        duration = 1600,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1700"] = makeAnimation {
        interval = 100,
        duration = 1700,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1800"] = makeAnimation {
        interval = 100,
        duration = 1800,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms1900"] = makeAnimation {
        interval = 100,
        duration = 1900,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2000"] = makeAnimation {
        interval = 100,
        duration = 2000,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2100"] = makeAnimation {
        interval = 100,
        duration = 2100,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2200"] = makeAnimation {
        interval = 100,
        duration = 2200,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2300"] = makeAnimation {
        interval = 100,
        duration = 2300,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2400"] = makeAnimation {
        interval = 100,
        duration = 2400,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2500"] = makeAnimation {
        interval = 100,
        duration = 2500,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2600"] = makeAnimation {
        interval = 100,
        duration = 2600,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2700"] = makeAnimation {
        interval = 100,
        duration = 2700,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2800"] = makeAnimation {
        interval = 100,
        duration = 2800,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms2900"] = makeAnimation {
        interval = 100,
        duration = 2900,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    },
    ["skill_ms3000"] = makeAnimation {
        interval = 100,
        duration = 3000,
        outfits = {giro1, giro2, giro3, "self"},
        hits = {[2] = true, [3] = true}
    }
}
