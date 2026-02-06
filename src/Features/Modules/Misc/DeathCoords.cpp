#include "DeathCoords.hpp"
#include <SDK/Minecraft/ClientInstance.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>

void DeathCoords::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &DeathCoords::onTick>(this);
    wDead = false;
}

void DeathCoords::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &DeathCoords::onTick>(this);
    wDead = false;
}

void DeathCoords::onTick(BaseTickEvent& event) {
    auto ci = ClientInstance::get();
    if (!ci) return;

    auto player = ci->getLocalPlayer();
    if (!player) return;

    bool isDead = player->isDead() || player->getHealth() <= 0.01f;
    if (!isDead) {
        auto pos = player->getPos();
        if (pos)
            LAP = *pos;
    }

    if (isDead && !wDead) {
        ChatUtils::displayClientMessage(
            "Death coords: X: " + std::to_string((int)LAP.x) +
            " Y: " + std::to_string((int)LAP.y) +
            " Z: " + std::to_string((int)LAP.z)
        );
    }
    wDead = isDead;
}