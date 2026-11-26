function onSay(player, words, param)
    if not player:getGroup():getAccess() then
        return true
    end

    local split = param:split(",")
    local targetName = split[1] and split[1]:trim()
    local amount = split[2] and tonumber(split[2]:trim())

    if not targetName or not amount or amount < 1 then
        player:sendCancelMessage("Uso: /addlevel nome, quantidade")
        return false
    end

    local target = Player(targetName)
    if not target then
        player:sendCancelMessage("Jogador não encontrado ou offline.")
        return false
    end

    local newLevel = target:getLevel() + amount
    local expToAdd = Game.getExperienceForLevel(newLevel) - target:getExperience()
    target:addExperience(expToAdd, false)

    player:sendTextMessage(MESSAGE_INFO_DESCR,
        string.format("Você adicionou %d level(s) para %s. Novo level: %d.",
            amount, target:getName(), target:getLevel()))
    target:sendTextMessage(MESSAGE_INFO_DESCR,
        string.format("Você recebeu %d level(s)! Novo level: %d.",
            amount, target:getLevel()))
    return false
end