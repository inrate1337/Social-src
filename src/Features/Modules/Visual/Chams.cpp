#include "Chams.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>

#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>

#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>
#include <Utils/MemUtils.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <mutex>
#include <vector>

void Chams::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Chams::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &Chams::onRenderEvent>(this);
}

void Chams::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Chams::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &Chams::onRenderEvent>(this);
}

void Chams::onBoneRenderEvent(BoneRenderEvent& event) {}

static void drawGlowLine(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, const ImColor& color, float thickness, float glowStrength)
{
    const float baseAlpha = color.Value.w;
    const float outerSteps = std::clamp(glowStrength, 0.f, 16.f);
    const int steps = static_cast<int>(outerSteps);

    drawList->AddLine(a, b, ImColor(0.f, 0.f, 0.f, baseAlpha * 0.4f), thickness + 1.25f);

    for (int i = steps; i >= 1; --i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(std::max(1, steps));
        const float alpha = baseAlpha * (0.18f * t);
        drawList->AddLine(a, b, ImColor(color.Value.x, color.Value.y, color.Value.z, alpha), thickness + (t * 3.0f));
    }

    drawList->AddLine(a, b, color, thickness);
}

static bool tryDrawSegment(ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b, const ImColor& color, float thickness, float glowStrength)
{
    ImVec2 sa, sb;
    if (!RenderUtils::worldToScreen(a, sa)) return false;
    if (!RenderUtils::worldToScreen(b, sb)) return false;
    const float dx = sb.x - sa.x;
    const float dy = sb.y - sa.y;
    const float len = std::sqrt((dx * dx) + (dy * dy));
    const float scale = std::clamp(len / 80.0f, 0.35f, 1.0f);
    drawGlowLine(drawList, sa, sb, color, thickness * scale, glowStrength * scale);
    return true;
}

static float getActorBodyYawDeg(Actor* actor)
{
    if (!actor) return 0.f;
    if (auto bodyRot = actor->getMobBodyRotationComponent()) return bodyRot->yBodyRot;
    if (auto rot = actor->getActorRotationComponent()) return rot->mYaw;
    return 0.f;
}

struct LimbAnimState
{
    glm::vec3 mLastPos{};
    glm::vec2 mLastMoveDir{};
    float mPhase = 0.f;
    float mAmount = 0.f;
    float mSpeed = 0.f;
    float mDirSign = 1.f;
    float mDirTarget = 1.f;
    uint64_t mLastTimeMs = 0;
    bool mInit = false;
};

struct CachedSegment
{
    int64_t runtimeId;
    glm::vec3 relA;
    glm::vec3 relB;
    ImColor color;
};

static std::mutex gChamsCacheMutex;
static std::vector<CachedSegment> gChamsSegments;

void Chams::onBaseTickEvent(BaseTickEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto localPlayer = event.mActor;
    if (!localPlayer) return;

    auto actors = ActorUtils::getActorList(true, true);
    static std::unordered_map<int64_t, LimbAnimState> limbAnim;
    std::vector<CachedSegment> newSegments;
    newSegments.reserve(actors.size() * 16);
    const uint64_t nowMs = NOW;

    for (auto actor : actors)
    {
        if (!actor) continue;
        if (!ActorUtils::isActorValid(actor)) continue;
        if (!actor->isPlayer()) continue;

        if (actor == localPlayer)
        {
            if (!mRenderLocal.mValue) continue;
            if (ci->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) continue;
        }

        float dist = localPlayer->distanceTo(actor);
        if (dist < 0.f) continue;
        if (mDistanceLimited.mValue && dist > mDistance.mValue) continue;

        ImColor color = mUseInterfaceColor.mValue ? ColorUtils::getGuiAccentColor(0) : mColor.getAsImColor();
        if (gFriendManager && gFriendManager->isFriend(actor))
        {
            if (!mShowFriends.mValue) continue;
            color = ImColor(0.0f, 1.0f, 0.0f, color.Value.w);
        }

        auto shapeComp = actor->getAABBShapeComponent();
        if (!shapeComp) continue;

        auto renderPosComp = actor->getRenderPositionComponent();
        if (!renderPosComp) continue;

        glm::vec3 basePos = renderPosComp->mPosition;
        basePos -= PLAYER_HEIGHT_VEC;

        if (actor == localPlayer)
        {
            basePos = RenderUtils::transform.mPlayerPos - PLAYER_HEIGHT_VEC;
        }

        const int64_t rid = actor->getRuntimeID();
        if (rid < 0) continue;

        const float hitboxWidth = std::max(0.01f, shapeComp->mWidth);
        const float hitboxHeight = std::max(0.01f, shapeComp->mHeight);

        const glm::vec3 min = basePos - glm::vec3(hitboxWidth * 0.5f, 0.f, hitboxWidth * 0.5f);
        const glm::vec3 max = basePos + glm::vec3(hitboxWidth * 0.5f, hitboxHeight, hitboxWidth * 0.5f);

        const float w = std::max(0.01f, max.x - min.x);
        const float h = std::max(0.01f, max.y - min.y);
        const float cx = basePos.x;
        const float cz = basePos.z;

        const float yFoot = min.y;
        const float yKnee = min.y + h * 0.26f;
        const float yPelvis = min.y + h * 0.48f;
        const float yChest = min.y + h * 0.72f;
        const float yNeck = min.y + h * 0.84f;
        const float yHead = min.y + h * 0.95f;

        const float xShoulder = w * 0.35f;
        const float xHand = w * 0.55f;
        const float xHip = w * 0.18f;

        const float yawDeg = getActorBodyYawDeg(actor);
        const float yawRad = glm::radians(yawDeg + 90.f);
        const glm::vec3 forward = { std::cos(yawRad), 0.f, std::sin(yawRad) };
        const glm::vec3 right = { -forward.z, 0.f, forward.x };

        float limbSwing = 0.f;
        float limbAmount = 0.f;
        if (auto walkAnim = actor->getWalkAnimationComponent())
        {
            limbSwing = walkAnim->mSwing;
            limbAmount = std::clamp(walkAnim->mSwingAmount, 0.f, 1.0f);
        }

        auto& st = limbAnim[rid];
        if (!st.mInit)
        {
            st.mInit = true;
            st.mLastPos = basePos;
            st.mLastMoveDir = {};
            st.mPhase = 0.f;
            st.mAmount = 0.f;
            st.mSpeed = 0.f;
            st.mDirSign = 1.f;
            st.mDirTarget = 1.f;
            st.mLastTimeMs = nowMs;
        }

        const float dt = std::clamp((nowMs - st.mLastTimeMs) / 1000.f, 1.0f / 240.0f, 0.2f);
        st.mLastTimeMs = nowMs;

        const glm::vec3 delta = basePos - st.mLastPos;
        st.mLastPos = basePos;
        const glm::vec2 delta2D = { delta.x, delta.z };
        const float dist2D = glm::length(delta2D);
        const float rawSpeed = dist2D / dt;
        const float spdLerp = std::clamp(dt * 12.0f, 0.f, 1.0f);
        st.mSpeed = std::lerp(st.mSpeed, rawSpeed, spdLerp);
        st.mAmount = std::clamp(st.mSpeed / 4.3f, 0.f, 1.0f);

        if (dist2D > 0.0005f)
        {
            st.mLastMoveDir = delta2D / dist2D;
        }

        const float moveDot = (st.mLastMoveDir.x * forward.x) + (st.mLastMoveDir.y * forward.z);
        if (moveDot > 0.35f) st.mDirTarget = 1.f;
        else if (moveDot < -0.35f) st.mDirTarget = -1.f;
        st.mDirSign = std::lerp(st.mDirSign, st.mDirTarget, std::clamp(dt * 10.0f, 0.f, 1.0f));

        const float phaseSpeed = 9.0f;
        st.mPhase += st.mSpeed * dt * phaseSpeed;

        if (limbAmount <= 0.001f)
        {
            limbSwing = st.mPhase;
            limbAmount = st.mAmount;
        }
        else
        {
            limbAmount = std::max(limbAmount, st.mAmount);
        }

        constexpr float pi = 3.14159265358979323846f;
        const float legStride = w * 0.42f * limbAmount;
        const float kneeStride = legStride * 0.6f;
        const float armStride = w * 0.35f * limbAmount;
        const float footLift = h * 0.095f * limbAmount;
        const float handLift = h * 0.06f * limbAmount;

        const float lLegSin = std::sin(limbSwing);
        const float rLegSin = std::sin(limbSwing + pi);
        const float lLegCos = std::cos(limbSwing);
        const float rLegCos = std::cos(limbSwing + pi);

        const float lArmSin = rLegSin;
        const float rArmSin = lLegSin;
        const float lArmCos = rLegCos;
        const float rArmCos = lLegCos;

        const glm::vec3 head = { cx, yHead, cz };
        const glm::vec3 neck = { cx, yNeck, cz };
        const glm::vec3 chest = { cx, yChest, cz };
        const glm::vec3 pelvis = { cx, yPelvis, cz };

        const glm::vec3 lShoulder = chest - (right * xShoulder);
        const glm::vec3 rShoulder = chest + (right * xShoulder);
        const glm::vec3 handsCenter = { cx, min.y + h * 0.56f, cz };
        const glm::vec3 lHandBase = handsCenter - (right * xHand);
        const glm::vec3 rHandBase = handsCenter + (right * xHand);
        const glm::vec3 lHand = lHandBase + (forward * (lArmSin * armStride * st.mDirSign)) + glm::vec3(0.f, std::max(0.f, -lArmCos) * handLift, 0.f);
        const glm::vec3 rHand = rHandBase + (forward * (rArmSin * armStride * st.mDirSign)) + glm::vec3(0.f, std::max(0.f, -rArmCos) * handLift, 0.f);

        const glm::vec3 lHip = pelvis - (right * xHip);
        const glm::vec3 rHip = pelvis + (right * xHip);
        const glm::vec3 lKneeBase = { lHip.x, yKnee, lHip.z };
        const glm::vec3 rKneeBase = { rHip.x, yKnee, rHip.z };
        const glm::vec3 lFootBase = { lHip.x, yFoot, lHip.z };
        const glm::vec3 rFootBase = { rHip.x, yFoot, rHip.z };
        const glm::vec3 lKnee = lKneeBase + (forward * (lLegSin * kneeStride * st.mDirSign));
        const glm::vec3 rKnee = rKneeBase + (forward * (rLegSin * kneeStride * st.mDirSign));
        const glm::vec3 lFoot = lFootBase + (forward * (lLegSin * legStride * st.mDirSign)) + glm::vec3(0.f, std::max(0.f, -lLegCos) * footLift, 0.f);
        const glm::vec3 rFoot = rFootBase + (forward * (rLegSin * legStride * st.mDirSign)) + glm::vec3(0.f, std::max(0.f, -rLegCos) * footLift, 0.f);

        newSegments.push_back({ rid, head - basePos, neck - basePos, color });
        newSegments.push_back({ rid, neck - basePos, chest - basePos, color });
        newSegments.push_back({ rid, chest - basePos, pelvis - basePos, color });

        newSegments.push_back({ rid, chest - basePos, lShoulder - basePos, color });
        newSegments.push_back({ rid, lShoulder - basePos, lHand - basePos, color });
        newSegments.push_back({ rid, chest - basePos, rShoulder - basePos, color });
        newSegments.push_back({ rid, rShoulder - basePos, rHand - basePos, color });

        newSegments.push_back({ rid, pelvis - basePos, lKnee - basePos, color });
        newSegments.push_back({ rid, lKnee - basePos, lFoot - basePos, color });
        newSegments.push_back({ rid, pelvis - basePos, rKnee - basePos, color });
        newSegments.push_back({ rid, rKnee - basePos, rFoot - basePos, color });
    }

    for (auto it = limbAnim.begin(); it != limbAnim.end();)
    {
        if (nowMs - it->second.mLastTimeMs > 10000) it = limbAnim.erase(it);
        else ++it;
    }

    {
        std::lock_guard<std::mutex> lock(gChamsCacheMutex);
        gChamsSegments.swap(newSegments);
    }
}

void Chams::onRenderEvent(RenderEvent& event)
{
    if (!ImGui::GetCurrentContext()) return;
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto localPlayer = ci->getLocalPlayer();
    if (!localPlayer) return;

    std::vector<CachedSegment> segments;
    {
        std::lock_guard<std::mutex> lock(gChamsCacheMutex);
        if (gChamsSegments.empty()) return;
        segments = gChamsSegments;
    }

    auto drawList = ImGui::GetBackgroundDrawList();
    const float th = mThickness.mValue;
    const float gl = mGlowStrength.mValue;

    const int64_t localRid = localPlayer->getRuntimeID();
    std::unordered_map<int64_t, glm::vec3> basePosById;
    basePosById.reserve(std::min<size_t>(segments.size(), 2048));

    for (const auto& seg : segments)
    {
        glm::vec3 basePos{};
        auto it = basePosById.find(seg.runtimeId);
        if (it != basePosById.end())
        {
            basePos = it->second;
        }
        else
        {
            if (seg.runtimeId == localRid)
            {
                basePos = RenderUtils::transform.mPlayerPos - PLAYER_HEIGHT_VEC;
                basePosById.emplace(seg.runtimeId, basePos);
            }
            else
            {
                Actor* actor = ActorUtils::getActorFromRuntimeID(seg.runtimeId);
                if (!actor || !actor->isValid())
                    continue;

                if (auto renderPosComp = actor->getRenderPositionComponent())
                    basePos = renderPosComp->mPosition - PLAYER_HEIGHT_VEC;
                else if (auto pos = actor->getPos())
                    basePos = *pos - PLAYER_HEIGHT_VEC;
                else
                    continue;

                basePosById.emplace(seg.runtimeId, basePos);
            }
        }

        tryDrawSegment(drawList, basePos + seg.relA, basePos + seg.relB, seg.color, th, gl);
    }
}
