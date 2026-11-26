-- Testa a lógica de teclas do modules/game_walk/walk.lua com o engine simulado (stubs).
-- A partir da raiz do cli-c:  luajit tests/freemove/walk_lua_test.lua
North, East, South, West, NorthEast, SouthEast, SouthWest, NorthWest = 0, 1, 2, 3, 4, 5, 6, 7
KeyboardNoModifier = 0

local fails, total = 0, 0
local function check(cond, msg) total = total + 1; if not cond then fails = fails + 1; print("FALHOU: " .. msg) end end

local state = {}
local panel = { setAutoRepeatDelay = function() end }
local started = false
local function reset(freeMove)
    state = { freeMove = freeMove, dirCalls = {}, walkCalls = 0, eventCalls = 0, keyDown = {}, keyUp = {}, keyPress = {}, turnCalls = 0 }
    -- O módulo guarda as teclas seguradas em estado de arquivo. Entre cenários, limpa pelo caminho real
    -- (perda de foco -> stopSmartWalk), como aconteceria no jogo.
    if started then
        panel.onFocusChange()
        state.dirCalls = {}
    end
end

modules = {
    client_options = { getOption = function() return false end },
    game_interface = { getRootPanel = function() return panel end },
    game_joystick = { addOnJoystickMoveListener = function() end },
}
Controller = { new = function() return { registerEvents = function() end } end }
g_clock = { millis = function() return 100000 end }
g_settings = { getNumber = function() return 0 end }
g_keyboard = {
    bindKeyDown = function(key, cb) state.keyDown[key] = cb end,
    bindKeyUp = function(key, cb) state.keyUp[key] = cb end,
    bindKeyPress = function(key, cb) state.keyPress[key] = cb end,
    unbindKeyDown = function() end, unbindKeyUp = function() end, unbindKeyPress = function() end,
    setKeyDelay = function() end, getModifiers = function() return KeyboardNoModifier end,
}
g_game = {
    isFreeMoveEnabled = function() return state.freeMove end,
    setFreeMoveDirection = function(dx, dy) table.insert(state.dirCalls, { dx, dy }) end,
    walk = function() state.walkCalls = state.walkCalls + 1 end,
    turn = function() state.turnCalls = state.turnCalls + 1 end,
    getLocalPlayer = function() return {
        isWalking = function() return false end, getDirection = function() return North end,
        lockWalk = function() end, isDead = function() return false end,
    } end,
    isOfficialTibia = function() return true end, enableFeature = function() end, disableFeature = function() end,
}
function addEvent(fn) state.eventCalls = state.eventCalls + 1; return {} end
function scheduleEvent(fn) state.eventCalls = state.eventCalls + 1; return {} end
function removeEvent() end
function table.removevalue(t, v) for i = #t, 1, -1 do if t[i] == v then table.remove(t, i); return true end end return false end

dofile("modules/game_walk/walk.lua")
reset(true)
WalkController:onGameStart() -- registra panel.onFocusChange
started = true

local function last() return state.dirCalls[#state.dirCalls] end
local function bind(key, dir) bindWalkKey(key, dir) end

-- 1) Segurar e soltar teclas: soma de vetores
reset(true)
bind("Right", East); bind("Down", South)
state.keyDown["Right"]();  check(last()[1] == 1 and last()[2] == 0, "Right -> (1,0)")
state.keyDown["Down"]();   check(last()[1] == 1 and last()[2] == 1, "Right+Down -> (1,1) diagonal")
state.keyUp["Right"]();    check(last()[1] == 0 and last()[2] == 1, "solta Right -> (0,1)")
state.keyUp["Down"]();     check(last()[1] == 0 and last()[2] == 0, "solta tudo -> (0,0) parar")

-- 2) Direções opostas se anulam
reset(true)
bind("Left", West); bind("Right", East)
state.keyDown["Left"](); state.keyDown["Right"]()
check(last()[1] == 0 and last()[2] == 0, "Left+Right se anulam")
state.keyUp["Left"]();   check(last()[1] == 1 and last()[2] == 0, "solta Left, segue Right")

-- 3) Numpad diagonal
reset(true)
bind("Numpad9", NorthEast)
state.keyDown["Numpad9"](); check(last()[1] == 1 and last()[2] == -1, "Numpad9 -> (1,-1)")
state.keyUp["Numpad9"]();   check(last()[1] == 0 and last()[2] == 0, "solta Numpad9 -> parar")

-- 4) Três teclas nunca passam de -1..1
reset(true)
bind("Up", North); bind("Numpad8", North); bind("Numpad7", NorthWest)
state.keyDown["Up"](); state.keyDown["Numpad8"](); state.keyDown["Numpad7"]()
check(last()[1] == -1 and last()[2] == -1, "soma limitada a -1..1 (NW)")

-- 5) Repetição de tecla (smartWalk) em modo livre NÃO usa walk legado nem agenda eventos
reset(true)
bind("Up", North)
state.keyDown["Up"](); state.keyPress["Up"](); state.keyPress["Up"]()
check(state.walkCalls == 0, "modo livre: g_game.walk nunca é chamado")
check(state.eventCalls == 0, "modo livre: smartWalk não agenda eventos de passo")

-- 6) Modo clássico intocado: não chama setFreeMoveDirection e continua agendando o passo
reset(false)
bind("Up", North)
state.keyDown["Up"](); state.keyPress["Up"]()
check(#state.dirCalls == 0, "modo clássico: nenhuma chamada de direção livre")
check(state.eventCalls >= 1, "modo clássico: smartWalk ainda agenda o passo")

-- 7) Perder o foco para o movimento (evita personagem correndo sozinho)
reset(true)
bind("Right", East)
WalkController:onGameStart()
state.keyDown["Right"](); check(last()[1] == 1, "andando antes de perder foco")
panel.onFocusChange()
check(last()[1] == 0 and last()[2] == 0, "perder o foco envia parar")

-- 8) Virar com Ctrl+seta em modo livre só vira: não entra na lista de teclas seguradas
reset(true)
bindTurnKey("Ctrl+Up", North)
state.keyDown["Ctrl+Up"]()
check(state.turnCalls == 1, "turn envia g_game.turn")
check(#state.dirCalls == 0, "turn em modo livre não altera a direção de movimento")

print(string.format("%d verificacoes, %d falhas", total, fails))
os.exit(fails == 0 and 0 or 1)
