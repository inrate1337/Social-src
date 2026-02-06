//
// Created by vastrakai on 6/25/2024.
//

#include "BaseTickHook.hpp"

#include <SDK/Minecraft/Actor/Actor.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>

std::unique_ptr<Detour> BaseTickHook::mDetour = nullptr;

void BaseTickHook::onBaseTick(Actor* actor)
{
    auto oFunc = mDetour->getOriginal<&onBaseTick>();
    auto ci = ClientInstance::get();
    if (!ci) return oFunc(actor);
    auto lp = ci->getLocalPlayer();
    if (!lp || actor != lp) return oFunc(actor);

    mQueueMutex.lock();
    auto messages = mQueuedMessages;
    if (!messages.empty())
    {
        std::string messageStr = "";
        for (auto& message : messages) messageStr += message + "\n";
        if (auto guiData = ci->getGuiData()) guiData->displayClientMessage(messageStr);
        mQueuedMessages.clear();
    }
    mQueueMutex.unlock();

    if (auto supplies = actor->getSupplies())
    {
        supplies->mInHandSlot = supplies->mSelectedSlot;
    }

    for (auto& [mTime, mPacket, mBypassHook] : mQueuedPackets)
    {
        spdlog::trace("Sending packet with ID: {} [queued {}ms ago] [{}]", magic_enum::enum_name(mPacket->getId()), NOW - mTime, mBypassHook ? "bypassing hook" : "not bypassing hook");
        auto sender = ci->getPacketSender();
        if (!sender) continue;
        if (mBypassHook) sender->sendToServer(mPacket.get());
        else sender->send(mPacket.get());
    }
    mQueuedPackets.clear();

    static bool once = false;
    if (!once)
    {
        once = true;

        auto holder = nes::make_holder<BaseTickInitEvent>(actor);
        if (gFeatureManager && gFeatureManager->mDispatcher)
        {
            gFeatureManager->mDispatcher->trigger(holder);
        }
    }

    auto holder = nes::make_holder<BaseTickEvent>(actor);
    if (gFeatureManager && gFeatureManager->mDispatcher)
    {
        gFeatureManager->mDispatcher->trigger(holder);
    }

    return oFunc(actor);
}

void BaseTickHook::init()
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto lp = ci->getLocalPlayer();
    if (!lp) return;
    mDetour = std::make_unique<Detour>("Actor::baseTick", reinterpret_cast<void*>(lp->vtable[OffsetProvider::Actor_baseTick]), &BaseTickHook::onBaseTick);
    mDetour->enable();
}
