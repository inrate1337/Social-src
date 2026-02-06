//
// Created by vastrakai on 6/28/2024.
//

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Components/ActorOwnerComponent.hpp>
#include <SDK/Minecraft/Actor/Components/ActorTypeComponent.hpp>
#include <SDK/Minecraft/Actor/Components/RuntimeIDComponent.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <entity/registry.hpp>

#include "ActorUtils.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Modules/Misc/AntiBot.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/Packets/InventoryTransactionPacket.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <Utils/MemUtils.hpp>
#include <chrono>

static bool isPtrReadable(void* p)
{
    return p && MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(p));
}

static uint64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

static AntiBot* getAntiBotModule()
{
    if (!gFeatureManager || !gFeatureManager->mModuleManager) return nullptr;
    auto* mod = gFeatureManager->mModuleManager->getModule<AntiBot>();
    if (!mod) return nullptr;
    if (!MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(mod))) return nullptr;
    if (!mod->mEnabled) return nullptr;
    return mod;
}

static bool isBotSafe(AntiBot* mod, Actor* actor)
{
    if (!mod || !actor) return false;
    if (!MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(mod))) return false;
    if (!mod->mEnabled) return false;
    bool out = false;
    if (!TryCallWrapper([&]() { out = mod->isBot(actor); })) return false;
    return out;
}

static bool isActorValidSafe(Actor* actor)
{
    if (!isPtrReadable(actor)) return false;
    bool valid = false;
    if (!TryCallWrapper([&]() { valid = actor->isValid(); })) return false;
    if (!valid) return false;
    bool destroying = false;
    TryCallWrapper([&]() { destroying = actor->isDestroying(); });
    if (destroying) return false;
    return true;
}

std::vector<struct Actor *> ActorUtils::getActorList(bool playerOnly, bool excludeBots)
{
    auto ci = ClientInstance::get();
    if (!ci) return {};

    auto player = ci->getLocalPlayer();
    if (!player)
    {
        //spdlog::warn("ActorUtils::getActorList called when player is nullptr");
        return {};
    }

    auto* antibot = excludeBots ? getAntiBotModule() : nullptr;
    std::vector<struct Actor *> actors;

    try
    {
        const bool useEnttView = excludeBots && antibot && antibot->mEntitylistMode.mValue == AntiBot::EntitylistMode::EnttView;
        if (useEnttView)
        {
            if (!player->mContext.mRegistry) return {};
            auto view = player->mContext.mRegistry->view<ActorOwnerComponent>();
            for (auto&& [ent, moduleOwner] : view.each())
            {
                if (!player->mContext.mRegistry->valid(ent)) continue;
                Actor* actor = moduleOwner.mActor;
                if (!actor) continue;
                if (!isActorValidSafe(actor)) continue;

                if (antibot && isBotSafe(antibot, actor)) continue;

                if (playerOnly)
                {
                    bool isPlayer = false;
                    if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) continue;
                    if (!isPlayer) continue;
                }

                actors.push_back(actor);
            }
        }
        else
        {
            Level* level = nullptr;
            if (!TRY_CALL([&]() { level = player->getLevel(); })) return {};
            if (!level) return {};

            std::vector<Actor*> tactors;
            if (!TRY_CALL([&]() { tactors = level->getRuntimeActorList(); })) return {};
            for (auto actor : tactors)
            {
                if (!actor) continue;
                if (!isActorValidSafe(actor)) continue;

                if (antibot && isBotSafe(antibot, actor)) continue;

                if (playerOnly)
                {
                    bool isPlayer = false;
                    if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) continue;
                    if (!isPlayer) continue;
                }

                actors.push_back(actor);
            }
        }
    } catch (std::exception &e)
    {
        spdlog::error("Error in ActorUtils::getActorList: {}", e.what());
        return {};
    } catch (...)
    {
        spdlog::error("Error in ActorUtils::getActorList: Unknown error");
        return {};
    }


    return actors;
}

std::vector<Actor*> ActorUtils::getActorsOfType(ActorType type)
{
    auto ci = ClientInstance::get();
    if (!ci) return {};

    auto player = ci->getLocalPlayer();
    if (!player)
    {
        //spdlog::warn("ActorUtils::getActorList called when player is nullptr");
        return {};
    }

    std::vector<Actor*> actors;

    try
    {
        if (!player->mContext.mRegistry) return {};
        for (auto &&[_, moduleOwner, typeComponent]: player->mContext.mRegistry->view<ActorOwnerComponent, ActorTypeComponent>().each())
        {
            if (!player->mContext.mRegistry->valid(_)) continue;

            if (!moduleOwner.mActor)
            {
                spdlog::debug("Found null actor pointer for entity!");
                continue;
            };
            // do NOT exclude bots as we are specifically requesting a type

            if (typeComponent.mType == type)
            {
                if (!isActorValidSafe(moduleOwner.mActor)) continue;
                actors.push_back(moduleOwner.mActor);
            }
        }
    } catch (std::exception &e)
    {
        spdlog::error("Error in ActorUtils::getActorsOfType: {}", e.what());
        return {};
    } catch (...)
    {
        spdlog::error("Error in ActorUtils::getActorsOfType: Unknown error");
        return {};
    }

    return actors;
}

bool ActorUtils::isBot(Actor* actor)
{
    if (!actor) return false;
    auto* antibot = getAntiBotModule();
    if (!antibot) return false;
    return isBotSafe(antibot, actor);
}

bool ActorUtils::isActorValid(Actor* actor)
{
    return isActorValidSafe(actor);
}

std::shared_ptr<InventoryTransactionPacket> ActorUtils::createAttackTransaction(Actor* actor, int slot)
{
    if (!isActorValidSafe(actor)) return nullptr;

    auto ci = ClientInstance::get();
    if (!ci) return nullptr;

    auto player = ci->getLocalPlayer();
    if (!player) return nullptr;
    glm::vec3* playerPos = nullptr;
    if (!TRY_CALL([&]() { playerPos = player->getPos(); })) return nullptr;
    if (!playerPos) return nullptr;

    PlayerInventory* supplies = nullptr;
    if (!TRY_CALL([&]() { supplies = player->getSupplies(); })) return nullptr;
    if (!isPtrReadable(supplies)) return nullptr;

    if (slot == -1) slot = supplies->mSelectedSlot;
    auto pkt = MinecraftPackets::createPacket<InventoryTransactionPacket>();

    auto cit = std::make_unique<ItemUseOnActorInventoryTransaction>();
    cit->mSlot = slot;

    Inventory* invContainer = nullptr;
    if (!TRY_CALL([&]() { invContainer = supplies->getContainer(); })) return nullptr;
    if (!isPtrReadable(invContainer)) return nullptr;

    ItemStack* inHand = nullptr;
    if (!TRY_CALL([&]() { inHand = invContainer->getItem(slot); })) return nullptr;
    if (!isPtrReadable(inHand)) return nullptr;
    cit->mItemInHand = NetworkItemStackDescriptor(*inHand);

    ActorUniqueIDComponent* unique = nullptr;
    if (!TRY_CALL([&]() { unique = actor->getActorUniqueIDComponent(); })) return nullptr;
    cit->mActorId = unique ? unique->mUniqueID : -1;
    cit->mActionType = ItemUseOnActorInventoryTransaction::ActionType::Attack;
    AABB aabb;
    if (!TRY_CALL([&]() { aabb = actor->getAABB(); })) return nullptr;
    cit->mClickPos = aabb.getClosestPoint(*playerPos);
    cit->mPlayerPos = *playerPos;

    pkt->mTransaction = std::move(cit);

    return pkt;
}

Actor* ActorUtils::getActorFromUniqueId(const int64_t uniqueId)
{
    auto ci = ClientInstance::get();
    if (!ci) return nullptr;

    auto player = ci->getLocalPlayer();
    if (!player) return nullptr;
    if (!player->mContext.mRegistry) return nullptr;

    for (auto &&[_, moduleOwner, ridc, uidc]: player->mContext.mRegistry->view<ActorOwnerComponent, RuntimeIDComponent, ActorUniqueIDComponent>().each())
    {
        if (uidc.mUniqueID == uniqueId && isActorValidSafe(moduleOwner.mActor)) return moduleOwner.mActor;
    }

    return nullptr;
}

Actor* ActorUtils::getActorFromRuntimeID(int64_t runtimeId)
{
    auto ci = ClientInstance::get();
    if (!ci) return nullptr;

    auto player = ci->getLocalPlayer();
    if (!player) return nullptr;
    if (!player->mContext.mRegistry) return nullptr;

    for (auto &&[_, moduleOwner, ridc]: player->mContext.mRegistry->view<ActorOwnerComponent, RuntimeIDComponent>().each())
    {
        if (ridc.mRuntimeID == runtimeId && isActorValidSafe(moduleOwner.mActor)) return moduleOwner.mActor;
    }

    return nullptr;
}
