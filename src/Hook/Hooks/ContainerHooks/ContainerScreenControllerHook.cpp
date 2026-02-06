//
// Created by vastrakai on 7/5/2024.
//

#include "ContainerScreenControllerHook.hpp"

#include <memory>
#include <string>
#include <Features/Events/ContainerScreenTickEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>

std::unique_ptr<Detour> ContainerScreenControllerHook::mDetour;

uint32_t ContainerScreenControllerHook::onContainerTick(class ContainerScreenController *csc)
{
    auto original = mDetour->getOriginal<&ContainerScreenControllerHook::onContainerTick>();

    auto ci = ClientInstance::get();
    if (!ci) return original(csc);

    auto player = ci->getLocalPlayer();
    if (!player) return original(csc);

    auto model = player->getContainerManagerModel();
    if (!model) return original(csc);

    const std::string screenName = ci->getScreenName();
    if (screenName == "hud_screen" || screenName == "pause_screen" || screenName == "chat_screen")
        return original(csc);

    ContainerType type = model->mContainerType;
    if (type == ContainerType::None || type == ContainerType::Hud)
        return original(csc);

    if (gFeatureManager && gFeatureManager->mDispatcher)
    {
        auto holder = nes::make_holder<ContainerScreenTickEvent>(csc);
        gFeatureManager->mDispatcher->trigger(holder);
    }

    return original(csc);
}

void ContainerScreenControllerHook::init()
{
    auto func = SigManager::ContainerScreenController_tick;
    mDetour = std::make_unique<Detour>("ContainerScreenController::tick", reinterpret_cast<void*>(func), &ContainerScreenControllerHook::onContainerTick);
}
