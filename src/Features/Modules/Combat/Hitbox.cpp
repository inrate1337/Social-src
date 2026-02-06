#include "Hitbox.hpp"
#include <Features/Events/BaseTickEvent.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <Features/Modules/Misc/AntiBot.hpp>
#include <Features/Modules/Misc/Friends.hpp>

void Hitbox::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Hitbox::onBaseTickEvent>(this);
}

void Hitbox::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Hitbox::onBaseTickEvent>(this);

    auto actors = ActorUtils::getActorList(true, false);
    auto localPlayer = ClientInstance::get()->getLocalPlayer();

    if (!localPlayer)
        return;

    auto lpShape = localPlayer->getAABBShapeComponent();
    if (!lpShape)
        return;

    for (auto actor : actors) {
        if (actor == localPlayer)
            continue;
        if (mNoFriends.mValue && gFriendManager && gFriendManager->isFriend(actor))
            continue;
        auto shape = actor->getAABBShapeComponent();
        if (!shape)
            continue;

        shape->mHeight = lpShape->mHeight;
        shape->mWidth = lpShape->mWidth;
    }
}

void Hitbox::onBaseTickEvent(class BaseTickEvent& event) {
    auto actors = ActorUtils::getActorList(true, false);
    auto localPlayer = ClientInstance::get()->getLocalPlayer();

    if (!localPlayer)
        return;

    for (auto actor : actors) {
        if (actor == localPlayer)
            continue;
        if (mNoFriends.mValue && gFriendManager && gFriendManager->isFriend(actor))
            continue;
        auto shape = actor->getAABBShapeComponent();
        if (!shape)
            continue;

        shape->mHeight = mHeight.mValue;
        shape->mWidth = mWidth.mValue;
    }
}
