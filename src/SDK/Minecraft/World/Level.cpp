//
// Created by vastrakai on 7/10/2024.
//

#include "Level.hpp"

#include <libhat.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/ActorOwnerComponent.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>
#include <SDK/Minecraft/Actor/SyncedPlayerMovementSettings.hpp>

std::unordered_map<mce::UUID, PlayerListEntry>* Level::getPlayerList()
{
    static auto vIndex = OffsetProvider::Level_getPlayerList;
    return MemUtils::callVirtualFunc<std::unordered_map<mce::UUID, PlayerListEntry>*>(vIndex, this);
}

HitResult* Level::getHitResult()
{
    static auto vIndex = OffsetProvider::Level_getHitResult;
    return MemUtils::callVirtualFunc<HitResult*>(vIndex, this);
}

SyncedPlayerMovementSettings* Level::getPlayerMovementSettings()
{
    static auto vIndex = OffsetProvider::Level_getPlayerMovementSettings;
    return MemUtils::callVirtualFunc<SyncedPlayerMovementSettings*>(vIndex, this);
}

std::vector<Actor*> Level::getRuntimeActorList()
{
    std::vector<Actor*> actors;
    auto ci = ClientInstance::get();
    auto player = ci ? ci->getLocalPlayer() : nullptr;
    if (player && player->mContext.mRegistry)
    {
        bool ok = TRY_CALL([&]()
        {
            for (auto&& [_, owner] : player->mContext.mRegistry->view<ActorOwnerComponent>().each())
            {
                if (owner.mActor) actors.push_back(owner.mActor);
            }
        });
        if (ok && !actors.empty()) return actors;
        actors.clear();
    }
    static auto func = SigManager::Level_getRuntimeActorList;
    if (!func || !MemUtils::isValidPtr(func)) return actors;

    if (!TRY_CALL([&]() { MemUtils::callFastcall<void>(func, this, &actors); })) return {};
    return actors;
}

LevelData* Level::getLevelData()
{
    static auto vIndex = OffsetProvider::Level_getLevelData;
    return MemUtils::callVirtualFunc<LevelData*>(vIndex, this);
}

class BlockPalette* Level::getBlockPalette()
{
    static auto vIndex = OffsetProvider::Level_getBlockPalette;
    return MemUtils::callVirtualFunc<class BlockPalette*>(vIndex, this);
}
