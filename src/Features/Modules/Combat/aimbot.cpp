#include "aimbot.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/LookInputEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/RenderEvent.hpp>

#include <Features/Modules/Misc/Friends.hpp>
#include <Features/Modules/Combat/Aura.hpp>

#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Actor/Components/CameraComponent.hpp>
#include <SDK/Minecraft/MinecraftSim.hpp>
#include <SDK/Minecraft/Actor/Components/ActorRotationComponent.hpp>
#include <SDK/Minecraft/Actor/Components/ActorHeadRotationComponent.hpp>
#include <SDK/Minecraft/Actor/Components/MobBodyRotationComponent.hpp>
#include <SDK/Minecraft/World/HitResult.hpp>

#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/NotifyUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>
#include <imgui.h>
#include <algorithm>
#include <cfloat>

namespace {
    // Clamp angle delta in shortest direction
    float shortestDelta(float current, float target) {
        float a = target - current;
        while (a > 180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }
}

void Aimbot::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Aimbot::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &Aimbot::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &Aimbot::onRenderEvent>(this);
    mTarget = nullptr;
    mHasTarget = false;
}

void Aimbot::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Aimbot::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &Aimbot::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &Aimbot::onRenderEvent>(this);
    mTarget = nullptr;
    mHasTarget = false;
}

glm::vec3 Aimbot::getAimPos(Actor* a) const
{
    if (!a) return {};
    AABB box = a->getAABB();
    glm::vec3 min = box.mMin;
    glm::vec3 max = box.mMax;
    glm::vec3 center = (min + max) * 0.5f;
    float h = max.y - min.y;
    center.y = min.y + h * 0.70f;
    if (auto sv = a->getStateVectorComponent())
    {
        float pt = 0.f;
        if (auto ci = ClientInstance::get())
            if (auto ms = ci->getMinecraftSim())
                if (auto gs = ms->getGameSim())
                    pt = gs->mDeltaTime;
        if (pt <= 0.f) pt = ImGui::GetIO().DeltaTime; // fallback
        glm::vec3 d = (sv->mPos - sv->mPosOld) * pt;
        center.x += d.x;
        center.z += d.z;
        // Do NOT modify center.y here PLS
    }

    return center;
}

Actor* Aimbot::acquireTarget(Actor* player)
{
    if (!player || !ActorUtils::isActorValid(player)) return nullptr;
    auto isValidTarget = [&](Actor* a) -> bool {
        if (!a || a == player) return false;
        if (!ActorUtils::isActorValid(a)) return false;
        if (!a->isPlayer() || a->getHealth() <= 0) return false;
        if (a->distanceTo(player) > mRange.mValue) return false;
        if (!mWalls.mValue && !player->canSee(a)) return false;
        if (mNoFriends.mValue && gFriendManager && gFriendManager->isFriend(a)) return false;
        return true;
    };

    auto aura = gFeatureManager && gFeatureManager->mModuleManager ? gFeatureManager->mModuleManager->getModule<Aura>() : nullptr;
    if (aura && aura->mEnabled && Aura::sHasTarget)
    {
        Actor* auraTarget = nullptr;
        if (Aura::sTargetRuntimeID != 0) auraTarget = ActorUtils::getActorFromRuntimeID(Aura::sTargetRuntimeID);
        if (!auraTarget) auraTarget = Aura::sTarget;
        if (isValidTarget(auraTarget)) return auraTarget;
    }

    if (mLock.mValue && isValidTarget(mTarget)) return mTarget;
    auto actors = ActorUtils::getActorList(false, true);
    Actor* best = nullptr;
    float bestScore = FLT_MAX;
    auto eye = *player->getPos();
    auto myRots = player->getActorRotationComponent();
    float myYaw = myRots ? myRots->mYaw : 0.f;
    float myPitch = myRots ? myRots->mPitch : 0.f;
    for (auto a : actors)
    {
        if (!isValidTarget(a)) continue;
        auto targetPos = getAimPos(a);
        glm::vec2 rots = MathUtils::getRots(eye, targetPos);
        // rots: pitch, yaw 
        float yawDelta = fabs(shortestDelta(myYaw, rots.y));
        float pitchDelta = fabs(shortestDelta(myPitch, rots.x));

        float angDelta = yawDelta + pitchDelta * 0.5f;
        bool withinFov = mAim360.mValue ? true : (angDelta <= mFov.mValue);
        if (withinFov)
        {
            float dist = glm::distance(eye, targetPos);
            float score = angDelta * 2.0f + dist * 0.25f;
            if (score < bestScore) { bestScore = score; best = a; }
        }
    }

    return best;
}

float Aimbot::angleDistance(float a, float b)
{
    float d = fabs(shortestDelta(a, b));
    return d;
}

void Aimbot::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor;
    if (!player || !ActorUtils::isActorValid(player)) { mTarget = nullptr; mHasTarget = false; return; }

    Actor* t = acquireTarget(player);
    mTarget = t;
    mHasTarget = (t && ActorUtils::isActorValid(t));

    if (mHasTarget)
    {
        mTargetAABB = mTarget->getAABB();
        glm::vec2 rots = MathUtils::getRots(*player->getPos(), getAimPos(mTarget));
        mDesiredRotsDeg = rots; // pitch, yaw
    }
}

void Aimbot::onLookInputEvent(LookInputEvent& event)
{
    if (!mEnabled) return;
    auto ci = ClientInstance::get();
    auto player = ci ? ci->getLocalPlayer() : nullptr;
    if (!player || !ActorUtils::isActorValid(player)) return;
    if (!mHasTarget) return;
    auto desired = mDesiredRotsDeg;
    auto arc = event.mCameraDirectLookComponent;
    int camState = 0;
    if (auto opts = ClientInstance::get()->getOptions()) camState = opts->mThirdPerson->value; // 0 FP, 1 TP, 2 TP Front
    float curYawDeg = 0.f, curPitchDeg = 0.f;
    bool haveCur = false;
    CameraOrbitComponent* orbit = nullptr;

    if (camState != 0)
    {
        for (auto&& [id, cameraComponent] : player->mContext.mRegistry->view<CameraComponent>().each())
        {
            const auto mode = cameraComponent.getMode();
            if ((camState == 1 && mode == CameraMode::ThirdPerson) || (camState == 2 && mode == CameraMode::ThirdPersonFront))
            {
                orbit = player->mContext.mRegistry->try_get<CameraOrbitComponent>(id);
                if (orbit)
                {
                    glm::vec2 curRads = orbit->mRotRads;
                    curYawDeg = -glm::degrees(curRads.x) + 180.f;
                    curPitchDeg = glm::degrees(curRads.y);
                    haveCur = true;
                }
                break;
            }
        }
    }

    if (!haveCur && arc)
    {
        glm::vec2 curRads = arc->mRotRads;
        curYawDeg = -glm::degrees(curRads.x) + 180.f;
        curPitchDeg = -glm::degrees(curRads.y);
        haveCur = true;
    }

    if (!haveCur) return;

    float dt = ImGui::GetIO().DeltaTime > 0.f ? ImGui::GetIO().DeltaTime : 1.f / 60.f;
    bool maxSpeed = mAimSpeed.mValue >= (mAimSpeed.mMax - 0.001f);
    float newPitch = curPitchDeg;
    float newYaw = curYawDeg;
    if (maxSpeed)
    {
        newPitch = desired.x;
        newYaw = desired.y;
    }
    else
    {
        float spd = MathUtils::clamp(mSmooth.mValue, 0.0f, 1.0f);
        float lerpAmt = spd <= 0.001f ? 1.0f : MathUtils::clamp(dt * (6.0f + 24.0f * (1.0f - spd)), 0.0f, 1.0f);
        float targetPitch = curPitchDeg + shortestDelta(curPitchDeg, desired.x) * lerpAmt;
        float targetYaw   = curYawDeg   + shortestDelta(curYawDeg,   desired.y) * lerpAmt;
        float maxStep = MathUtils::clamp(mAimSpeed.mValue, 30.f, 3600.f) * dt;
        float stepPitch = shortestDelta(curPitchDeg, targetPitch);
        float stepYaw   = shortestDelta(curYawDeg,   targetYaw);
        stepPitch = std::clamp(stepPitch, -maxStep, maxStep);
        stepYaw   = std::clamp(stepYaw,   -maxStep, maxStep);
        newPitch = curPitchDeg + stepPitch;
        newYaw   = curYawDeg   + stepYaw;
        if (angleDistance(newPitch, desired.x) <= 0.05f && angleDistance(newYaw, desired.y) <= 0.05f)
        {
            newPitch = desired.x;
            newYaw = desired.y;
        }
    }
    newPitch = MathUtils::wrap(newPitch, -90.f, 90.f);
    newYaw   = MathUtils::wrap(newYaw, -180.f, 180.f);
    float visualYaw = newYaw;
    if (mBackview.mValue) {
        visualYaw = MathUtils::wrap(newYaw + 180.f, -180.f, 180.f);
    }
    if (camState == 2)
    {
        visualYaw = MathUtils::wrap(visualYaw + 180.f, -180.f, 180.f);
    }
    if (camState == 0 && arc)
    {
        arc->mRotRads = glm::radians(glm::vec2(180.f - visualYaw, -newPitch)); 
    }
    else if (orbit)
    {
        orbit->mRotRads = glm::radians(glm::vec2(180.f - visualYaw, newPitch));
    }
}

void Aimbot::onPacketOutEvent(PacketOutEvent& event)
{
    if (!mEnabled || !mHasTarget) return;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput)
    {
        auto paip = event.getPacket<PlayerAuthInputPacket>();
        paip->mRot = mDesiredRotsDeg; // pitch, yaw
        paip->mYHeadRot = mDesiredRotsDeg.y;
        paip->mRot.x = MathUtils::wrap(paip->mRot.x, -90, 90);
        paip->mRot.y = MathUtils::wrap(paip->mRot.y, -180, 180);
        paip->mYHeadRot = MathUtils::wrap(paip->mYHeadRot, -180, 180);
    }
    else if (event.mPacket->getId() == PacketID::MovePlayer)
    {
        auto pkt = event.getPacket<MovePlayerPacket>();
        pkt->mRot = mDesiredRotsDeg; // pitch, yaw
        pkt->mYHeadRot = mDesiredRotsDeg.y;
    }
}

void Aimbot::onRenderEvent(RenderEvent&)
{
}
