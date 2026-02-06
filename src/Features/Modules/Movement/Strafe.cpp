#include "Strafe.hpp"
#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <cmath>


void Strafe::onEnable() {
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Strafe::onBaseTickEvent>(this);
}

void Strafe::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Strafe::onBaseTickEvent>(this);
}

void Strafe::onBaseTickEvent(BaseTickEvent& event) {
    auto player = event.mActor;
    if (!player) return;
    auto input = player->getMoveInputComponent();
    if (!input) return;
    auto stateVec = player->getStateVectorComponent();
    if (!stateVec) return;
    auto rotComponent = player->getActorRotationComponent();
    if (!rotComponent) return;
    if (player->isOnGround()) return;
    if (!input->mForward && !input->mBackward && !input->mLeft && !input->mRight) return;
    float yaw = rotComponent->mYaw;
    float speed = mSpeed.mValue;
    float rad = glm::radians(yaw);
    glm::vec2 dir(0, 0);

    if (input->mForward) {
        dir.x += -sinf(rad);
        dir.y += cosf(rad);
    }
    if (input->mBackward) {
        dir.x += sinf(rad);
        dir.y += -cosf(rad);
    }
    if (input->mLeft) {
        dir.x += cosf(rad);
        dir.y += sinf(rad);
    }
    if (input->mRight) {
        dir.x += -cosf(rad);
        dir.y += -sinf(rad);
    }
    if (glm::length(dir) > 0.01f) {
        dir = glm::normalize(dir);
        stateVec->mVelocity.x = dir.x * speed;
        stateVec->mVelocity.z = dir.y * speed;
    }
}
