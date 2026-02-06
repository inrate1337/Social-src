#include "ElytraTarget.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Modules/Combat/Aura.hpp>
#include <Features/Modules/Misc/Friends.hpp>

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/MinecraftSim.hpp>
#include <SDK/Minecraft/Actor/Components/ActorRotationComponent.hpp>
#include <SDK/Minecraft/Actor/Components/StateVectorComponent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MemUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

static glm::vec2 yawToXZ(float yawDeg, float len)
{
    float calcYaw = glm::radians(yawDeg + 90.f);
    return { std::cos(calcYaw) * len, std::sin(calcYaw) * len };
}

static bool etIsPtrReadable(void* p)
{
    return p && MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(p));
}

static bool etIsActorValidSafe(Actor* actor)
{
    if (!etIsPtrReadable(actor)) return false;
    bool valid = false;
    if (!TryCallWrapper([&]() { valid = actor->isValid(); })) return false;
    if (!valid) return false;
    bool destroying = false;
    TryCallWrapper([&]() { destroying = actor->isDestroying(); });
    if (destroying) return false;
    return true;
}

static bool etIsActorAliveSafe(Actor* actor)
{
    if (!etIsActorValidSafe(actor)) return false;
    bool dead = false;
    if (TryCallWrapper([&]() { dead = actor->isDead(); }) && dead) return false;
    float hp = 0.f;
    if (TryCallWrapper([&]() { hp = actor->getHealth(); }))
    {
        if (!std::isfinite(hp) || hp <= 0.f) return false;
    }
    return true;
}

static float etDistanceSafe(Actor* a, Actor* b, float fallback)
{
    if (!a || !b) return fallback;
    float out = fallback;
    if (!TryCallWrapper([&]() { out = a->distanceTo(b); })) return fallback;
    if (!std::isfinite(out)) return fallback;
    return out;
}

ElytraTarget::ElytraTarget() : ModuleBase<ElytraTarget>("ElytraTarget", "Stick to a target while gliding", ModuleCategory::Combat, 0, false)
{
    addSettings(
        &mUseAuraTarget,
        &mNoFriends,
        &mRange,
        &mFollowMode,
        &mFollowDistance,
        &mDeadzone,
        &mSpeed,
        &mVerticalSpeed,
        &mPrediction,
        &mRotate,
        &mOrbit,
        &mOrbitRadius,
        &mOrbitSpeed,
        &mOrbitStartDistance
    );

    mNames = {
        {Lowercase, "elytratarget"},
        {LowercaseSpaced, "elytra target"},
        {Normal, "ElytraTarget"},
        {NormalSpaced, "Elytra Target"}
    };

    VISIBILITY_CONDITION(mOrbitRadius, mOrbit.mValue);
    VISIBILITY_CONDITION(mOrbitSpeed, mOrbit.mValue);
    VISIBILITY_CONDITION(mOrbitStartDistance, mOrbit.mValue);
}

void ElytraTarget::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &ElytraTarget::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &ElytraTarget::onPacketOutEvent, nes::event_priority::LAST>(this);
    mTarget = nullptr;
    mHasTarget = false;
    mDesiredMove = { 0.f, 0.f };
    mDesiredDeltaY = 0.f;
    mOrbitAngleDeg = 0.f;
    mLastOrbitTick = 0;
}

void ElytraTarget::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &ElytraTarget::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &ElytraTarget::onPacketOutEvent>(this);
    mTarget = nullptr;
    mHasTarget = false;
    mDesiredMove = { 0.f, 0.f };
    mDesiredDeltaY = 0.f;
    mOrbitAngleDeg = 0.f;
    mLastOrbitTick = 0;
}

glm::vec3 ElytraTarget::getPredictedTargetPos(Actor* target) const
{
    if (!etIsActorValidSafe(target)) return {};
    StateVectorComponent* sv = nullptr;
    if (!TryCallWrapper([&]() { sv = target->getStateVectorComponent(); })) return {};
    if (!sv) return {};
    if (!etIsPtrReadable(sv)) return {};

    AABB box;
    if (!TryCallWrapper([&]() { box = target->getAABB(); })) return {};
    glm::vec3 center = (box.mMin + box.mMax) * 0.5f;
    float h = box.mMax.y - box.mMin.y;
    center.y = box.mMin.y + h * 0.70f;

    glm::vec3 delta = (sv->mPos - sv->mPosOld);
    return center + (delta * mPrediction.mValue);
}

Actor* ElytraTarget::acquireTarget(Actor* player)
{
    if (!etIsActorValidSafe(player)) return nullptr;

    auto isValidTarget = [&](Actor* a) -> bool {
        if (!a || a == player) return false;
        if (!etIsActorAliveSafe(a)) return false;
        bool isPlayer = false;
        if (!TryCallWrapper([&]() { isPlayer = a->isPlayer(); })) return false;
        if (!isPlayer) return false;
        if (etDistanceSafe(a, player, FLT_MAX) > mRange.mValue) return false;
        if (mNoFriends.mValue && gFriendManager && etIsPtrReadable(gFriendManager))
        {
            bool isFriend = false;
            if (TryCallWrapper([&]() { isFriend = gFriendManager->isFriend(a); }) && isFriend) return false;
        }
        return true;
    };

    if (mUseAuraTarget.mValue && Aura::sHasTarget)
    {
        Actor* auraTarget = nullptr;
        if (Aura::sTargetRuntimeID != 0) auraTarget = ActorUtils::getActorFromRuntimeID(Aura::sTargetRuntimeID);
        if (!auraTarget) auraTarget = Aura::sTarget;
        if (isValidTarget(auraTarget)) return auraTarget;
    }

    Actor* best = nullptr;
    float bestDist = FLT_MAX;
    auto actors = ActorUtils::getActorList(false, true);
    for (auto a : actors)
    {
        if (!isValidTarget(a)) continue;
        float d = etDistanceSafe(a, player, FLT_MAX);
        if (d < bestDist)
        {
            bestDist = d;
            best = a;
        }
    }

    return best;
}

void ElytraTarget::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor;
    if (!etIsActorValidSafe(player))
    {
        mTarget = nullptr;
        mHasTarget = false;
        return;
    }

    bool gliding = false;
    if (!TryCallWrapper([&]() { gliding = player->getStatusFlag(ActorFlags::Gliding); }) || !gliding)
    {
        mTarget = nullptr;
        mHasTarget = false;
        return;
    }

    Actor* target = acquireTarget(player);
    mTarget = target;
    mHasTarget = etIsActorAliveSafe(target);
    if (!mHasTarget) return;

    glm::vec3* playerPosPtr = nullptr;
    if (!TryCallWrapper([&]() { playerPosPtr = player->getPos(); })) return;
    if (!playerPosPtr || !etIsPtrReadable(playerPosPtr)) return;
    glm::vec3 playerPos = *playerPosPtr;
    glm::vec3 targetPos = getPredictedTargetPos(target);

    mDesiredRotsDeg = MathUtils::getRots(playerPos, targetPos);

    glm::vec3 goal = targetPos;
    if (mFollowMode.mValue == FollowMode::Behind || mFollowMode.mValue == FollowMode::Ahead)
    {
        ActorRotationComponent* rot = nullptr;
        if (TryCallWrapper([&]() { rot = target->getActorRotationComponent(); }) && rot && etIsPtrReadable(rot))
        {
            float yaw = rot->mYaw;
            if (mFollowMode.mValue == FollowMode::Behind) yaw += 180.f;
            glm::vec2 off = yawToXZ(yaw, mFollowDistance.mValue);
            goal.x += off.x;
            goal.z += off.y;
        }
    }
    else
    {
        glm::vec3 toTarget = targetPos - playerPos;
        float dist = glm::length(toTarget);
        if (dist > 0.001f)
        {
            goal = targetPos - (toTarget / dist) * mFollowDistance.mValue;
        }
    }

    bool orbitActive = false;
    if (mOrbit.mValue)
    {
        float distToTarget = glm::length(targetPos - playerPos);
        if (distToTarget <= mOrbitStartDistance.mValue)
        {
            orbitActive = true;
            uint64_t now = NOW;
            if (mLastOrbitTick == 0) mLastOrbitTick = now;
            float dt = static_cast<float>(now - mLastOrbitTick) / 1000.0f;
            mLastOrbitTick = now;
            mOrbitAngleDeg += mOrbitSpeed.mValue * dt;
            if (mOrbitAngleDeg >= 360.f) mOrbitAngleDeg -= 360.f;
            if (mOrbitAngleDeg < 0.f) mOrbitAngleDeg += 360.f;

            float rad = glm::radians(mOrbitAngleDeg);
            float r = mOrbitRadius.mValue;
            goal.x = targetPos.x + std::cos(rad) * r;
            goal.z = targetPos.z + std::sin(rad) * r;
        }
    }

    glm::vec3 toGoal = goal - playerPos;
    float distToGoal = glm::length(toGoal);
    mDesiredDeltaY = toGoal.y;
    if (!orbitActive && distToGoal <= mDeadzone.mValue)
    {
        StateVectorComponent* playerSv = nullptr;
        if (TryCallWrapper([&]() { playerSv = player->getStateVectorComponent(); }) && playerSv && etIsPtrReadable(playerSv))
        {
            playerSv->mVelocity = { 0.f, 0.f, 0.f };
        }
        mDesiredMove = { 0.f, 0.f };
        return;
    }

    glm::vec3 planar = { toGoal.x, 0.f, toGoal.z };
    float planarDist = glm::length(planar);
    if (planarDist > 0.001f)
    {
        glm::vec3 planarDir = planar / planarDist;
        float desiredYaw = std::atan2(planarDir.z, planarDir.x);
        desiredYaw = glm::degrees(desiredYaw) - 90.f;
        desiredYaw = MathUtils::wrap(desiredYaw, -180.f, 180.f);

        ActorRotationComponent* playerRot = nullptr;
        if (!TryCallWrapper([&]() { playerRot = player->getActorRotationComponent(); })) playerRot = nullptr;
    float yawBase = mRotate.mValue ? mDesiredRotsDeg.y : (playerRot && etIsPtrReadable(playerRot) ? playerRot->mYaw : mDesiredRotsDeg.y);
        float deltaYaw = MathUtils::wrap(desiredYaw - yawBase, -180.f, 180.f);
        float r = glm::radians(deltaYaw);
        float strafe = std::sin(r);
        float forward = std::cos(r);
        mDesiredMove = { std::clamp(strafe, -1.f, 1.f), std::clamp(forward, -1.f, 1.f) };
    }
    else
    {
        mDesiredMove = { 0.f, 0.f };
    }

    glm::vec3 vel = { 0.f, 0.f, 0.f };
    float slowRadius = std::max(mDeadzone.mValue * 3.f, 0.5f);
    float t = std::clamp((distToGoal - mDeadzone.mValue) / slowRadius, 0.f, 1.f);
    float distBoost = std::clamp(distToGoal / 18.f, 0.75f, 4.0f);
    float baseSpeed = (mSpeed.mValue / 10.f) * distBoost * (0.35f + 0.65f * t);

    if (planarDist > 0.001f)
    {
        glm::vec3 planarDir = planar / planarDist;
        vel.x = planarDir.x * baseSpeed;
        vel.z = planarDir.z * baseSpeed;
    }

    float vyMax = (mVerticalSpeed.mValue / 10.f) * std::clamp(distToGoal / 18.f, 0.75f, 3.0f);
    float dy = toGoal.y;
    vel.y = std::clamp(dy, -1.f, 1.f) * vyMax;

    StateVectorComponent* playerSv = nullptr;
    if (TryCallWrapper([&]() { playerSv = player->getStateVectorComponent(); }) && playerSv && etIsPtrReadable(playerSv))
    {
        playerSv->mVelocity = vel;
    }
}

void ElytraTarget::onPacketOutEvent(PacketOutEvent& event)
{
    if (!mHasTarget || !etIsActorAliveSafe(mTarget))
    {
        mHasTarget = false;
        mTarget = nullptr;
        return;
    }
    if (!event.mPacket || !etIsPtrReadable(event.mPacket)) return;
    PacketID id{};
    if (!TryCallWrapper([&]() { id = event.mPacket->getId(); })) return;
    if (id != PacketID::PlayerAuthInput) return;

    auto ci = ClientInstance::get();
    auto player = ci ? ci->getLocalPlayer() : nullptr;
    if (!etIsActorValidSafe(player)) return;
    bool gliding = false;
    if (!TryCallWrapper([&]() { gliding = player->getStatusFlag(ActorFlags::Gliding); }) || !gliding) return;

    auto paip = event.getPacket<PlayerAuthInputPacket>();
    if (!paip) return;
    if (mRotate.mValue)
    {
        paip->mRot = mDesiredRotsDeg;
        paip->mYHeadRot = mDesiredRotsDeg.y;
        paip->mRot.x = MathUtils::wrap(paip->mRot.x, -90.f, 90.f);
        paip->mRot.y = MathUtils::wrap(paip->mRot.y, -180.f, 180.f);
        paip->mYHeadRot = MathUtils::wrap(paip->mYHeadRot, -180.f, 180.f);
    }

}
