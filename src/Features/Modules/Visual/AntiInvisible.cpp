#include "AntiInvisible.hpp"
#include <Features/FeatureManager.hpp>
#include <Features/Events/ActorRenderEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/ActorFlags.hpp>

void AntiInvisible::onEnable()
{
    if (!gFeatureManager || !gFeatureManager->mDispatcher) return;
    gFeatureManager->mDispatcher->listen<ActorRenderEvent, &AntiInvisible::onActorRenderEvent>(this);
}

void AntiInvisible::onDisable()
{
    if (!gFeatureManager || !gFeatureManager->mDispatcher) return;
    gFeatureManager->mDispatcher->deafen<ActorRenderEvent, &AntiInvisible::onActorRenderEvent>(this);
}

void AntiInvisible::onActorRenderEvent(ActorRenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto localPlayer = ci->getLocalPlayer();
    if (!localPlayer) return;

    auto actor = event.mEntity;
    if (!actor) return;

    if (actor == localPlayer) return;
    if (true && !actor->isPlayer()) return; 
    if (actor->getStatusFlag(ActorFlags::Invisible))
    {
        if (actor->isPlayer() && gFriendManager && gFriendManager->isFriend(actor) && !mShowFriends.mValue) {
            return;
        }
        actor->setStatusFlag(ActorFlags::Invisible, false);
    }
}
