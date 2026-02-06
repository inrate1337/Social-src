//
// Created by vastrakai on 10/4/2024.
//

#include "Tracers.hpp"

#include <Features/Modules/Misc/Friends.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/Keyboard.hpp>
#include <Utils/MemUtils.hpp>

#include <algorithm>
#include <cmath>
#include <cfloat>

void Tracers::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &Tracers::onRenderEvent>(this);
    mSmoothedAngles.clear();
    mSmoothedRingRadius = mRingRadius.mValue;
}

void Tracers::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &Tracers::onRenderEvent>(this);
    mSmoothedAngles.clear();
    mSmoothedRingRadius = 0.0f;
}

static ImVec2 rotatePoint(const ImVec2& p, float c, float s)
{
    return ImVec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

static float normalizeAngle(float a)
{
    while (a > IM_PI) a -= IM_PI * 2.0f;
    while (a < -IM_PI) a += IM_PI * 2.0f;
    return a;
}

static bool drawRotatedGlyph(ImDrawList* drawList, ImFont* font, float fontSize, const ImVec2& centerPos, ImU32 col, ImWchar glyphChar, float angle)
{
    if (!drawList || !font) return false;
    const ImFontGlyph* glyph = font->FindGlyph(glyphChar);
    if (!glyph) return false;

    const float scale = fontSize / font->FontSize;

    const float x0 = glyph->X0 * scale;
    const float y0 = glyph->Y0 * scale;
    const float x1 = glyph->X1 * scale;
    const float y1 = glyph->Y1 * scale;

    const ImVec2 localCenter((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);

    const float c = std::cos(angle);
    const float s = std::sin(angle);

    const ImVec2 tl = rotatePoint(ImVec2(x0, y0) - localCenter, c, s) + centerPos;
    const ImVec2 tr = rotatePoint(ImVec2(x1, y0) - localCenter, c, s) + centerPos;
    const ImVec2 br = rotatePoint(ImVec2(x1, y1) - localCenter, c, s) + centerPos;
    const ImVec2 bl = rotatePoint(ImVec2(x0, y1) - localCenter, c, s) + centerPos;

    drawList->AddImageQuad(
        font->ContainerAtlas->TexID,
        tl, tr, br, bl,
        ImVec2(glyph->U0, glyph->V0),
        ImVec2(glyph->U1, glyph->V0),
        ImVec2(glyph->U1, glyph->V1),
        ImVec2(glyph->U0, glyph->V1),
        col
    );
    return true;
}

void Tracers::onRenderEvent(RenderEvent& event)
{
    if (!ClientInstance::get()->getLevelRenderer()) return;

    auto actors = ActorUtils::getActorList(false, true);
    auto localPlayer = ClientInstance::get()->getLocalPlayer();


    auto drawList = ImGui::GetBackgroundDrawList();

    ImFont* arrowFont = ImGui::GetFont();
    if (auto it = FontHelper::Fonts.find("essence.ttf"); it != FontHelper::Fonts.end() && it->second)
    {
        arrowFont = it->second;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 ringCenter(displaySize.x * 0.5f, displaySize.y * 0.5f);
    const float maxRadius = std::max(0.0f, std::min(displaySize.x, displaySize.y) * 0.5f - 10.0f);
    const float baseRadius = std::clamp(mRingRadius.mValue, 5.0f, maxRadius);
    const float fontSize = 18.0f;
    const ImWchar glyph = static_cast<ImWchar>('w');

    const float dt = ImGui::GetIO().DeltaTime;
    const float smoothSpeed = 8.0f;
    const float alpha = 1.0f - std::exp(-dt * smoothSpeed);
    std::vector<int64_t> seenIds;
    seenIds.reserve(actors.size());

    const bool isMoving = Keyboard::isUsingMoveKeys();
    const float targetRadius = std::clamp(isMoving ? (baseRadius + 25.0f) : baseRadius, 5.0f, maxRadius);
    if (mSmoothedRingRadius <= 0.0f) mSmoothedRingRadius = baseRadius;
    mSmoothedRingRadius = mSmoothedRingRadius + (targetRadius - mSmoothedRingRadius) * alpha;
    const float ringRadius = mSmoothedRingRadius;

    for (auto actor : actors)
    {
        if (!ActorUtils::isActorValid(actor)) continue;
        if (actor == localPlayer && ClientInstance::get()->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) continue;
        if (actor == localPlayer && !mRenderLocal.mValue) continue;
        AABBShapeComponent* shapeComp = nullptr;
        if (!TryCallWrapper([&]() { shapeComp = actor->getAABBShapeComponent(); })) continue;
        if (!shapeComp) continue;

        bool isPlayer = false;
        if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) continue;
        if (isPlayer)
        {
            if (gFriendManager->isFriend(actor))
            {
                if (!mShowFriends.mValue) continue;
            }
        }

        AABB aabb;
        if (!TryCallWrapper([&]() { aabb = actor->getAABB(); })) continue;
        auto points = MathUtils::getImBoxPoints(aabb);

        if (points.empty()) continue;

        float minX = points[0].x, minY = points[0].y;
        float maxX = points[0].x, maxY = points[0].y;
        for (const auto& p : points)
        {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }

        const ImVec2 targetCenter((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
        const ImVec2 delta(targetCenter.x - ringCenter.x, targetCenter.y - ringCenter.y);
        const float lenSq = delta.x * delta.x + delta.y * delta.y;
        if (lenSq < 4.0f) continue;

        const float targetAngle = std::atan2(delta.y, delta.x);
        int64_t id = 0;
        if (!TryCallWrapper([&]() { id = actor->getRuntimeID(); })) continue;
        seenIds.push_back(id);

        auto it = mSmoothedAngles.find(id);
        if (it == mSmoothedAngles.end())
        {
            it = mSmoothedAngles.emplace(id, targetAngle).first;
        }
        float& smoothed = it->second;

        const float diff = normalizeAngle(targetAngle - smoothed);
        smoothed = normalizeAngle(smoothed + diff * alpha);

        const ImVec2 dir(std::cos(smoothed), std::sin(smoothed));

        const ImVec2 arrowPos(ringCenter.x + dir.x * ringRadius, ringCenter.y + dir.y * ringRadius);
        const float angle = smoothed - (IM_PI * 0.5f);
        const bool isFriend = isPlayer && gFriendManager->isFriend(actor);
        const ImColor arrowColor = isFriend ? ImColor(0, 255, 0, 255) : ImColor(255, 255, 255, 255);

        const float glowSize1 = fontSize + 6.0f;
        const float glowSize2 = fontSize + 3.0f;
        const ImColor glowColor1 = ImColor(arrowColor.Value.x, arrowColor.Value.y, arrowColor.Value.z, 0.18f);
        const ImColor glowColor2 = ImColor(arrowColor.Value.x, arrowColor.Value.y, arrowColor.Value.z, 0.28f);

        drawRotatedGlyph(drawList, arrowFont, glowSize1, arrowPos, glowColor1, glyph, angle);
        drawRotatedGlyph(drawList, arrowFont, glowSize2, arrowPos, glowColor2, glyph, angle);
        drawRotatedGlyph(drawList, arrowFont, fontSize, arrowPos, arrowColor, glyph, angle);
    }

    if (!mSmoothedAngles.empty())
    {
        std::vector<int64_t> eraseIds;
        eraseIds.reserve(mSmoothedAngles.size());
        for (const auto& [id, _] : mSmoothedAngles)
        {
            if (std::find(seenIds.begin(), seenIds.end(), id) == seenIds.end())
            {
                eraseIds.push_back(id);
            }
        }
        for (const auto& id : eraseIds) mSmoothedAngles.erase(id);
    }
}
