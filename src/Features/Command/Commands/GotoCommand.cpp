#include "GotoCommand.hpp"
#include <Features/Modules/Movement/Baritone.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>

void GotoCommand::execute(const std::vector<std::string>& args)
{
    if (args.size() < 3)
    {
        ChatUtils::displayClientMessage(getUsage());
        return;
    }

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    glm::vec3 currentPos = *player->getPos();
    glm::vec3 targetPos;

    try {
        if (args[1] == "~") targetPos.x = currentPos.x;
        else targetPos.x = std::stof(args[1]);

        targetPos.y = currentPos.y;

        if (args[2] == "~") targetPos.z = currentPos.z;
        else targetPos.z = std::stof(args[2]);
    }
    catch (...) {
        ChatUtils::displayClientMessage("Invalid coordinates!");
        return;
    }

    auto module = gFeatureManager->mModuleManager->getModule<Baritone>();
    if (module)
    {
        module->setTarget(targetPos);
    }
    else
    {
        ChatUtils::displayClientMessage("Baritone module not found!");
    }
}

std::vector<std::string> GotoCommand::getAliases() const
{
    return { "path", "baritone" };
}

std::string GotoCommand::getDescription() const
{
    return "Pathfinds to coordinates (X Z)";
}

std::string GotoCommand::getUsage() const
{
    return "Usage: .goto <x> <z> (NOT Y)";
}