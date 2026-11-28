-- Configuracao do sistema de estrelas dos personagens (vocation collection).
-- Edite SOMENTE este arquivo para mudar custos e bonus. Depois de editar, use /reload globals
-- (ou reinicie o servidor). A logica fica em vocation_collection.lua e nao precisa ser alterada.
--
-- Todo personagem comeca com 1 estrela. Cada entrada de LEVELS descreve a evolucao PARA aquela estrela:
--   fragments = fragmentos consumidos para chegar nessa estrela (saem do estoque do personagem)
--   bonus     = percentual ADICIONAL concedido ao chegar nela (aditivo, sempre calculado sobre os
--               atributos originais da vocation; aceita decimais, ex.: 2.5)
--
-- Bonus acumulado = soma dos bonus das estrelas ja alcancadas. Valores iniciais: 3 + 3 + 3 + 11 = 20%.
-- Os niveis precisam ser sequenciais a partir de 2; o numero maximo de estrelas e o maior nivel definido.
--
-- Atributos afetados (fixos no servidor, em player.h): Magic Level, Distance, Sword, Axe, Club e Shield.

VocationStarsConfig = {
	LEVELS = {
		[2] = {fragments = 50, bonus = 3},
		[3] = {fragments = 100, bonus = 3},
		[4] = {fragments = 150, bonus = 3},
		[5] = {fragments = 200, bonus = 11}, -- 4 -> 5 (+4%) e bonus adicional da 5a estrela (+7%)
	},
}

-- Validacao: configuracao quebrada nao pode corromper fragmentos nem atributos.
do
	local maxStars = 1
	for stars in pairs(VocationStarsConfig.LEVELS) do
		if type(stars) == "number" and stars > maxStars then
			maxStars = stars
		end
	end
	local valid = true
	for stars = 2, maxStars do
		local level = VocationStarsConfig.LEVELS[stars]
		if type(level) ~= "table"
			or type(level.fragments) ~= "number" or level.fragments < 1 or level.fragments % 1 ~= 0
			or type(level.bonus) ~= "number" or level.bonus < 0 then
			print(string.format("[VocationStarsConfig] Configuracao invalida para a estrela %d (fragments inteiro >= 1 e bonus >= 0). Sistema de estrelas desativado.", stars))
			valid = false
		end
	end
	VocationStarsConfig.MAX_STARS = valid and maxStars or 1
end
