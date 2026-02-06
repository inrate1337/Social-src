#include "TargetESP.hpp"
#include <Features/Events/RenderEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <Features/Modules/Combat/Aura.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/AABBShapeComponent.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MemUtils.hpp>

#include <glm/glm.hpp>

#include <Hook/Hooks/RenderHooks/D3DHook.hpp>

#include <array>
#include <vector>

//������� ������ gpt #gpt sila #power#little penis

static float finiteOr(float v, float fallback)
{
    return std::isfinite(v) ? v : fallback;
}

static bool targetEspIsPtrReadable(void* p)
{
    return p && MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(p));
}

static bool targetEspIsActorValidSafe(Actor* actor)
{
    if (!targetEspIsPtrReadable(actor)) return false;
    bool valid = false;
    if (!TryCallWrapper([&]() { valid = actor->isValid(); })) return false;
    if (!valid) return false;
    bool destroying = false;
    TryCallWrapper([&]() { destroying = actor->isDestroying(); });
    if (destroying) return false;
    return true;
}

static bool targetEspIsActorAliveSafe(Actor* actor)
{
    if (!targetEspIsActorValidSafe(actor)) return false;
    bool dead = false;
    if (TryCallWrapper([&]() { dead = actor->isDead(); }) && dead) return false;
    float hp = 0.f;
    if (TryCallWrapper([&]() { hp = actor->getHealth(); }))
    {
        if (!std::isfinite(hp) || hp <= 0.f) return false;
    }
    return true;
}

static bool isFinite3(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static glm::vec3 safeNormalize(const glm::vec3& v)
{
    if (!isFinite3(v)) return {};
    const float len2 = (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
    if (!std::isfinite(len2) || len2 <= 1e-12f) return {};
    const float inv = 1.0f / std::sqrt(len2);
    if (!std::isfinite(inv)) return {};
    return {v.x * inv, v.y * inv, v.z * inv};
}

static ImColor lerpColor(ImColor a, ImColor b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    ImVec4 av = a.Value;
    ImVec4 bv = b.Value;
    return ImColor(
        av.x + (bv.x - av.x) * t,
        av.y + (bv.y - av.y) * t,
        av.z + (bv.z - av.z) * t,
        av.w + (bv.w - av.w) * t
    );
}

static glm::vec3 rotateY(const glm::vec3& v, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

static glm::vec3 rotateX(const glm::vec3& v, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

static ImColor mulAlpha(ImColor c, float a)
{
    ImVec4 v = c.Value;
    v.w = std::clamp(v.w * a, 0.f, 1.f);
    return ImColor(v);
}

static ImColor scaleRgb(ImColor c, float s)
{
    ImVec4 v = c.Value;
    v.x = std::clamp(v.x * s, 0.f, 1.f);
    v.y = std::clamp(v.y * s, 0.f, 1.f);
    v.z = std::clamp(v.z * s, 0.f, 1.f);
    return ImColor(v);
}

struct FiboCache
{
    std::vector<glm::vec3> dirs;
    std::vector<float> cosTheta;
    std::vector<float> sinTheta;
};

static const FiboCache& getFiboCache(int count)
{
    static std::array<FiboCache, 21> caches{};
    static std::array<bool, 21> ready{};

    count = std::clamp(count, 1, 20);
    if (!ready[count])
    {
        auto& c = caches[count];
        c.dirs.resize(count);
        c.cosTheta.resize(count);
        c.sinTheta.resize(count);

        constexpr float golden = 2.39996322972865332f;
        const float invCount = 1.0f / static_cast<float>(count);

        for (int i = 0; i < count; i++)
        {
            const float idx = static_cast<float>(i);
            const float u = (idx + 0.5f) * invCount;
            const float phi = std::acos(1.f - 2.f * u);
            const float theta = golden * idx;

            const float sPhi = std::sin(phi);
            const float cPhi = std::cos(phi);
            const float cT = std::cos(theta);
            const float sT = std::sin(theta);

            c.dirs[i] = glm::vec3(cT * sPhi, cPhi, sT * sPhi);
            c.cosTheta[i] = cT;
            c.sinTheta[i] = sT;
        }

        ready[count] = true;
    }

    return caches[count];
}

static void addShadowCircleCached(ImDrawList* drawList, const ImVec2& obj_center, float obj_radius, ImU32 shadow_col, float shadow_thickness, const ImVec2& shadow_offset, ImDrawFlags flags, int num_segments)
{
    if (!drawList) return;
    if (num_segments <= 0 || num_segments == 12)
    {
        drawList->AddShadowCircle(obj_center, obj_radius, shadow_col, shadow_thickness, shadow_offset, flags, num_segments);
        return;
    }

    num_segments = std::clamp(num_segments, 3, 64);

    static std::array<std::vector<ImVec2>, 65> unitCache{};
    static std::array<bool, 65> unitReady{};

    if (!unitReady[num_segments])
    {
        auto& v = unitCache[num_segments];
        v.resize(num_segments);

        constexpr float twoPi = 6.28318530717958647692f;
        const float step = twoPi / static_cast<float>(num_segments);
        for (int i = 0; i < num_segments; i++)
        {
            const float a = step * static_cast<float>(i);
            v[i] = ImVec2(std::cos(a), std::sin(a));
        }

        unitReady[num_segments] = true;
    }

    const auto& unit = unitCache[num_segments];
    ImVec2 points[64];
    for (int i = 0; i < num_segments; i++)
    {
        points[i] = ImVec2(
            obj_center.x + unit[i].x * obj_radius,
            obj_center.y + unit[i].y * obj_radius
        );
    }

    drawList->AddShadowConvexPoly(points, num_segments, shadow_col, shadow_thickness, shadow_offset, flags);
}

struct CrystalFace
{
    std::array<int, 3> idx;
    float depth = 0.f;
    float shade = 1.f;
    float grad = 0.5f;
};

static void drawCrystal(ImDrawList* drawList, const glm::vec3& center, float size, float yaw, float tilt, ImColor top, ImColor bottom, bool filled, float alpha)
{
    if (!isFinite3(center)) return;
    if (!std::isfinite(size) || size <= 0.0001f) return;
    if (!std::isfinite(yaw) || !std::isfinite(tilt)) return;
    if (!std::isfinite(alpha)) return;
    const float halfH = size * 0.70f;
    const float halfW = size * 0.55f;

    const float sTilt = std::sin(tilt);
    const float cTilt = std::cos(tilt);
    const float sYaw = std::sin(yaw);
    const float cYaw = std::cos(yaw);

    std::array<glm::vec3, 6> verts = {
        glm::vec3(0.f, +halfH, 0.f),
        glm::vec3(0.f, -halfH, 0.f),
        glm::vec3(+halfW, 0.f, 0.f),
        glm::vec3(-halfW, 0.f, 0.f),
        glm::vec3(0.f, 0.f, +halfW),
        glm::vec3(0.f, 0.f, -halfW),
    };

    for (auto& v : verts)
    {
        glm::vec3 r = v;
        {
            const float y = r.y * cTilt - r.z * sTilt;
            const float z = r.y * sTilt + r.z * cTilt;
            r.y = y;
            r.z = z;
        }
        {
            const float x = r.x * cYaw - r.z * sYaw;
            const float z = r.x * sYaw + r.z * cYaw;
            r.x = x;
            r.z = z;
        }
        v = r;
        v += center;
    }

    std::array<CrystalFace, 8> faces = {
        CrystalFace{{0, 2, 4}},
        CrystalFace{{0, 4, 3}},
        CrystalFace{{0, 3, 5}},
        CrystalFace{{0, 5, 2}},
        CrystalFace{{1, 4, 2}},
        CrystalFace{{1, 3, 4}},
        CrystalFace{{1, 5, 3}},
        CrystalFace{{1, 2, 5}},
    };

    const glm::vec3 viewPos = RenderUtils::transform.mOrigin;
    for (auto& f : faces)
    {
        const glm::vec3 a = verts[f.idx[0]];
        const glm::vec3 b = verts[f.idx[1]];
        const glm::vec3 c = verts[f.idx[2]];
        const glm::vec3 center3 = (a + b + c) * (1.f / 3.f);
        const glm::vec3 d = center3 - viewPos;
        f.depth = (d.x * d.x) + (d.y * d.y) + (d.z * d.z);
        if (!std::isfinite(f.depth)) f.depth = -1e30f;

        const glm::vec3 ab = b - a;
        const glm::vec3 ac = c - a;
        const glm::vec3 n = safeNormalize(glm::cross(ab, ac));
        const glm::vec3 toCam = safeNormalize(viewPos - center3);
        float ndot = (n.x * toCam.x) + (n.y * toCam.y) + (n.z * toCam.z);
        if (!std::isfinite(ndot)) ndot = 0.f;
        ndot = std::clamp(ndot, -1.f, 1.f);
        f.shade = 0.55f + 0.45f * (0.5f + 0.5f * ndot);
        if (!std::isfinite(f.shade)) f.shade = 1.f;

        const float yAvg = (a.y + b.y + c.y) * (1.f / 3.f);
        f.grad = std::clamp((yAvg - (center.y - halfH)) / (halfH * 2.f), 0.f, 1.f);
        if (!std::isfinite(f.grad)) f.grad = 0.5f;
    }

    std::ranges::sort(faces, [](const CrystalFace& a, const CrystalFace& b) {
        return a.depth > b.depth;
    });

    std::array<ImVec2, 6> proj{};
    std::array<bool, 6> ok{};
    for (int i = 0; i < static_cast<int>(verts.size()); i++)
    {
        if (!isFinite3(verts[i])) {
            ok[i] = false;
            proj[i] = {};
        }
        else {
            ok[i] = RenderUtils::worldToScreen(verts[i], proj[i]);
        }
    }

    ImColor line = lerpColor(bottom, top, 0.60f);
    line.Value.w = alpha;

    ImVec2 center2d;
    if (RenderUtils::worldToScreen(center, center2d) && ok[0] && ok[1])
    {
        const float dx = proj[0].x - proj[1].x;
        const float dy = proj[0].y - proj[1].y;
        const float r = std::max(6.f, std::sqrt((dx * dx) + (dy * dy)) * 0.45f);
        const ImVec2 off = ImVec2(0.f, 0.f);

        ImColor glowTheme = line;
        glowTheme.Value.w = 0.48f * alpha;
        const int segOuter = std::clamp(static_cast<int>(std::lround(r * 1.10f)), 12, 64);
        addShadowCircleCached(drawList, center2d, r * 1.10f, glowTheme, 38.0f, off, 0, segOuter);

        ImColor glowWhite = ImColor(1.f, 1.f, 1.f, 0.40f * alpha);
        const int segInner = std::clamp(static_cast<int>(std::lround(r * 0.85f)), 12, 64);
        addShadowCircleCached(drawList, center2d, r * 0.85f, glowWhite, 26.0f, off, 0, segInner);
    }

    if (filled)
    {
        for (const auto& f : faces)
        {
            if (!ok[f.idx[0]] || !ok[f.idx[1]] || !ok[f.idx[2]]) continue;
            const ImVec2 tri[3] = {proj[f.idx[0]], proj[f.idx[1]], proj[f.idx[2]]};
            ImColor col = lerpColor(bottom, top, f.grad);
            col = scaleRgb(col, 0.82f + 0.18f * f.shade);
            col.Value.w = alpha;
            drawList->AddConvexPolyFilled(tri, 3, col);
        }
    }

    const float thickness = 2.2f;

    auto addEdge = [&](int a, int b)
    {
        if (!ok[a] || !ok[b]) return;
        drawList->AddLine(proj[a], proj[b], line, thickness);
    };

    addEdge(0, 2);
    addEdge(0, 3);
    addEdge(0, 4);
    addEdge(0, 5);
    addEdge(1, 2);
    addEdge(1, 3);
    addEdge(1, 4);
    addEdge(1, 5);
    addEdge(2, 4);
    addEdge(2, 5);
    addEdge(3, 4);
    addEdge(3, 5);
}

void TargetESP::onEnable()
{
    mVisAnim = 0.f;
    mHasLast = false;
    gFeatureManager->mDispatcher->listen<RenderEvent, &TargetESP::onRenderEvent>(this);
}

void TargetESP::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &TargetESP::onRenderEvent>(this);
    mHasLast = false;
    mVisAnim = 0.f;
    mHitAnim = 0.f;
}

void TargetESP::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!ci->getLevelRenderer()) return;
    if (!ci->getLocalPlayer()) return;

    bool hasLive = false;
    glm::vec3 livePos = {};
    glm::vec3 liveMin = {};
    glm::vec3 liveMax = {};
    float liveHeight = 0.f;
    float liveWidth = 0.f;
    int liveHurtTime = 0;
    Actor* auraTarget = nullptr;
    if (Aura::sTargetRuntimeID != 0) auraTarget = ActorUtils::getActorFromRuntimeID(Aura::sTargetRuntimeID);
    if (!auraTarget) auraTarget = Aura::sTarget;
    if (Aura::sHasTarget && targetEspIsActorAliveSafe(auraTarget))
    {
        AABBShapeComponent* shape = nullptr;
        glm::vec3* posPtr = nullptr;
        RenderPositionComponent* renderPosComp = nullptr;
        MobHurtTimeComponent* hurt = nullptr;
        TryCallWrapper([&]() { shape = auraTarget->getAABBShapeComponent(); });
        TryCallWrapper([&]() { posPtr = auraTarget->getPos(); });
        TryCallWrapper([&]() { renderPosComp = auraTarget->getRenderPositionComponent(); });
        TryCallWrapper([&]() { hurt = auraTarget->getMobHurtTimeComponent(); });
        if (!targetEspIsPtrReadable(shape)) shape = nullptr;
        if (!targetEspIsPtrReadable(posPtr)) posPtr = nullptr;
        if (!targetEspIsPtrReadable(renderPosComp)) renderPosComp = nullptr;
        if (!targetEspIsPtrReadable(hurt)) hurt = nullptr;
        if (shape && (renderPosComp || posPtr))
        {
            hasLive = true;
            glm::vec3 basePos = renderPosComp ? renderPosComp->mPosition : *posPtr;
            bool isPlayer = false;
            if (TryCallWrapper([&]() { isPlayer = auraTarget->isPlayer(); }) && isPlayer) basePos -= PLAYER_HEIGHT_VEC;
            livePos = basePos;
            liveHeight = shape->mHeight;
            liveWidth = shape->mWidth;
            liveMin = basePos - glm::vec3(liveWidth * 0.5f, 0.0f, liveWidth * 0.5f);
            liveMax = basePos + glm::vec3(liveWidth * 0.5f, liveHeight, liveWidth * 0.5f);
            if (hurt) liveHurtTime = hurt->mHurtTime;
            mHasLast = true;
            mLastPos = livePos;
            mLastAabbMin = liveMin;
            mLastAabbMax = liveMax;
            mLastHeight = liveHeight;
            mLastWidth = liveWidth;
        }
    }

    const float dt = finiteOr(ImGui::GetIO().DeltaTime, 0.0f);
    const float desiredAnim = hasLive ? 1.f : 0.f;
    mVisAnim = MathUtils::animate(desiredAnim, mVisAnim, dt * 8.0f);
    float eased = std::clamp(mVisAnim, 0.f, 1.f);
    eased = eased * eased * (3.f - 2.f * eased);

    if (!hasLive && eased <= 0.01f)
    {
        mHasLast = false;
        return;
    }

    if (hasLive)
    {
        if (liveHurtTime > mHurtPeak) mHurtPeak = liveHurtTime;
        if (liveHurtTime <= 0) mHurtPeak = 0;
        mLastHurtTime = liveHurtTime;
    }
    else
    {
        mLastHurtTime = 0;
        mHurtPeak = 0;
    }
    float hitTarget = 0.0f;
    if (hasLive && mHurtPeak > 0)
    {
        hitTarget = (float)liveHurtTime / (float)mHurtPeak;
    }
    mHitAnim = MathUtils::animate(hitTarget, mHitAnim, dt * 8.0f);
    float hitT = std::clamp(mHitAnim, 0.0f, 1.0f);
    hitT = hitT * hitT * (3.0f - 2.0f * hitT);

    glm::vec3 pos = {};
    float height = 0.f;
    float width = 0.f;
    glm::vec3 aabbMin = {};
    glm::vec3 aabbMax = {};

    if (hasLive)
    {
        pos = livePos;
        height = liveHeight;
        width = liveWidth;
        aabbMin = liveMin;
        aabbMax = liveMax;
    }
    else
    {
        if (!mHasLast) return;
        pos = mLastPos;
        height = mLastHeight;
        width = mLastWidth;
        aabbMin = mLastAabbMin;
        aabbMax = mLastAabbMax;
    }

    if (!isFinite3(pos)) return;
    height = finiteOr(height, 0.f);
    width = finiteOr(width, 0.f);
    if (height < 0.f) height = 0.f;
    if (width < 0.f) width = 0.f;
    if (!isFinite3(aabbMin)) aabbMin = {};
    if (!isFinite3(aabbMax)) aabbMax = {};

    const float t = finiteOr(static_cast<float>(ImGui::GetTime()), 0.f);
    const int mode = std::clamp(mMode.mValue, 0, 1);
    const float countF = std::clamp(finiteOr(mCrystalCount.mValue, 6.0f), 1.0f, 20.0f);
    const int count = std::clamp((int)std::round(countF), 1, 20);

    height = (std::max)(0.1f, height);
    width = (std::max)(0.1f, width);
    const float radiusSetting = std::clamp(finiteOr(mRadius.mValue, 0.9f), 0.1f, 3.0f);
    const float radius = (std::max)(0.1f, radiusSetting * (0.65f + 0.55f * width));

    glm::vec3 baseCenter = pos;
    baseCenter.y += height * 1.10f;
    baseCenter.y -= height * 0.90f;

    ImColor accent = ColorUtils::getGuiAccentColor(0.f);
    accent.Value.w = 1.f;
    ImColor top = lerpColor(accent, IM_COL32(255, 255, 255, 255), 0.78f);
    ImColor bottom = lerpColor(accent, IM_COL32(255, 255, 255, 255), 0.18f);
    const float pulse = 0.86f + 0.14f * (0.5f + 0.5f * std::sin(t * 2.25f));
    top = scaleRgb(top, pulse);
    bottom = scaleRgb(bottom, pulse * 0.98f);
    top.Value.w = 1.f;
    bottom.Value.w = 1.f;

    auto* drawList = ImGui::GetBackgroundDrawList();

    const float radiusMul = 1.0f + (1.0f - eased) * 0.35f;
    const float ySpread = 1.0f + (1.0f - eased) * 0.12f;

    if (mode == 1)
    {
        static ID3D11ShaderResourceView* notchTex = nullptr;
        static int notchW = 0;
        static int notchH = 0;
        static bool notchLoaded = false;

        if (!notchLoaded)
        {
            D3DHook::loadTextureFromEmbeddedResource("nur.png", &notchTex, &notchW, &notchH);
            notchLoaded = true;
        }

        ID3D11ShaderResourceView* tex = notchTex;
        int texW = notchW;
        int texH = notchH;
        if (!tex || texW <= 0 || texH <= 0) return;

        glm::vec3 min3 = aabbMin;
        glm::vec3 max3 = aabbMax;
        if (min3 == glm::vec3{} && max3 == glm::vec3{})
        {
            min3 = glm::vec3(pos.x - (width * 0.5f), pos.y, pos.z - (width * 0.5f));
            max3 = glm::vec3(pos.x + (width * 0.5f), pos.y + height, pos.z + (width * 0.5f));
        }
        const float aabbH = (std::max)(0.1f, max3.y - min3.y);

        glm::vec3 center3 = (min3 + max3) * 0.5f;
        center3.y = min3.y + aabbH * 0.5f;

        ImVec2 center2d{};
        if (!RenderUtils::worldToScreen(center3, center2d)) return;

        ImVec2 top2d{};
        ImVec2 bottom2d{};
        glm::vec3 top3 = center3;
        glm::vec3 bottom3 = center3;
        top3.y = max3.y;
        bottom3.y = min3.y;
        if (!RenderUtils::worldToScreen(top3, top2d)) return;
        if (!RenderUtils::worldToScreen(bottom3, bottom2d)) return;
        float entityPxH = std::abs(bottom2d.y - top2d.y);
        entityPxH = (std::clamp)(entityPxH, 12.0f, ImGui::GetIO().DisplaySize.y * 4.0f);

        float appear = 0.75f + 0.25f * eased;
        float imageScale = std::clamp(finiteOr(mImageScale.mValue, 1.0f), 0.01f, 10.0f);
        float imageSpin = std::clamp(finiteOr(mImageSpin.mValue, 2.0f), 0.0f, 50.0f);
        float reverseDeg = std::clamp(finiteOr(mImageReverseDegrees.mValue, 0.0f), 0.0f, 10000.0f);
        float scaleMul = imageScale * appear;
        if (mImagePulse.mValue)
        {
            float p = 0.5f + 0.5f * std::sin(t * 4.0f);
            float easedP = p * p * (3.0f - 2.0f * p);
            scaleMul *= (0.92f + 0.16f * easedP);
        }
        if (mImageHitEffect.mValue)
        {
            scaleMul *= (1.0f - 0.22f * hitT);
        }

        float baseH = entityPxH * 0.36f * scaleMul;
        baseH = (std::max)(10.0f, baseH);
        float aspect = (float)texW / (float)texH;
        float baseW = baseH * aspect;

        float ang = 0.0f;
        if (reverseDeg > 0.0f)
        {
            float maxDeg = (std::max)(0.0f, reverseDeg);
            float deg = maxDeg * std::sin(t * imageSpin);
            ang = deg * (3.14159265358979323846f / 180.0f);
        }
        else
        {
            ang = t * imageSpin;
        }
        if (!std::isfinite(ang)) ang = 0.0f;
        const float cs = std::cos(ang);
        const float sn = std::sin(ang);
        const float hx = baseW * 0.5f;
        const float hy = baseH * 0.5f;

        auto rot = [&](float x, float y) -> ImVec2
        {
            return ImVec2(
                center2d.x + (x * cs - y * sn),
                center2d.y + (x * sn + y * cs)
            );
        };

        ImVec2 p1 = rot(-hx, -hy);
        ImVec2 p2 = rot(+hx, -hy);
        ImVec2 p3 = rot(+hx, +hy);
        ImVec2 p4 = rot(-hx, +hy);

        ImColor col = IM_COL32(255, 255, 255, 255);
        if (mImageHitEffect.mValue)
        {
            col = lerpColor(IM_COL32(255, 255, 255, 255), IM_COL32(255, 64, 64, 255), hitT);
        }
        int a = (int)(255.0f * std::clamp(eased, 0.0f, 1.0f));
        col.Value.w = (float)a / 255.0f;
        drawList->AddImageQuad(
            tex,
            p1, p2, p3, p4,
            ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
            ImGui::ColorConvertFloat4ToU32(col.Value)
        );
        return;
    }

    const auto& fib = getFiboCache(count);
    const float spinSpeed = std::clamp(finiteOr(mSpinSpeed.mValue, 1.25f), 0.0f, 6.0f);
    const float orbit = t * spinSpeed * 0.85f;
    const float sizeSetting = std::clamp(finiteOr(mSize.mValue, 0.55f), 0.15f, 2.0f);

    for (int i = 0; i < count; i++)
    {
        const float idx = static_cast<float>(i);
        glm::vec3 dir = fib.dirs[i];
        dir = rotateY(dir, orbit + idx * 0.25f);
        dir = rotateX(dir, 0.35f * std::sin(t * 0.9f + idx * 0.7f));

        glm::vec3 sideDir = {fib.cosTheta[i], dir.y, fib.sinTheta[i]};
        const float sideLen = std::sqrt(sideDir.x * sideDir.x + sideDir.y * sideDir.y + sideDir.z * sideDir.z);
        if (sideLen > 0.0001f) sideDir *= (1.f / sideLen);
        dir = sideDir * (1.f - eased) + dir * eased;

        glm::vec3 scatter = {
            std::sin(idx * 12.9898f) * 0.75f,
            std::sin(idx * 78.2330f) * 0.65f,
            std::sin(idx * 37.7190f) * 0.75f
        };
        const float scLen = std::sqrt(scatter.x * scatter.x + scatter.y * scatter.y + scatter.z * scatter.z);
        if (scLen > 0.0001f) scatter *= (1.f / scLen);

        const float radialJitter = 0.72f + 0.28f * (0.5f + 0.5f * std::sin(idx * 4.13f));
        glm::vec3 offset = {
            dir.x * (radius * radialJitter * radiusMul),
            dir.y * (height * 0.70f * ySpread),
            dir.z * (radius * radialJitter * radiusMul)
        };
        offset += scatter * ((1.f - eased) * radius * 0.35f);
        offset.y += scatter.y * ((1.f - eased) * height * 0.22f);

        const float yBob = 0.10f * std::sin(t * 3.1f + idx * 1.7f);
        glm::vec3 c = baseCenter + offset;
        c.y += yBob;

        const float s = sizeSetting * (0.90f + 0.10f * std::sin(t * 2.6f + idx)) * (0.75f + 0.25f * eased);
        const float yaw = orbit * 1.6f + idx * 0.9f;
        const float tilt = 0.55f * std::sin(t * 1.25f + idx * 0.8f);

        drawCrystal(drawList, c, s, yaw, tilt, top, bottom, mFilled.mValue, eased);
    }
}
