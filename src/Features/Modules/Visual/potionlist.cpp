#include "potionlist.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <magic_enum.hpp>

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Network/Packets/MobEffectPacket.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/NotifyUtils.hpp>
#include <Utils/StringUtils.hpp>
#include <Features/Modules/Visual/Interface.hpp>

static ImVec2 plCalcSize(ImFont* font, float fontSize, const std::string& text)
{
    if (!font || text.empty()) return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text.c_str());
}

static ImVec2 plCalcSize(ImFont* font, float fontSize, const char* text)
{
    if (!font || !text || text[0] == '\0') return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);
}

static float plExpApproach(float current, float target, float delta, float speed)
{
    if (delta <= 0.f) return current;
    const float t = 1.f - std::exp(-speed * delta);
    return current + (target - current) * t;
}

static float plEaseInOut(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static float plEaseOutCubic(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

static float plEaseOutBack(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float inv = t - 1.f;
    return 1.f + c3 * inv * inv * inv + c1 * inv * inv;
}

static void plSpringTo(float& value, float& velocity, float target, float delta, float stiffness, float damping)
{
    if (delta <= 0.f) return;
    const float accel = (target - value) * stiffness;
    velocity += accel * delta;
    velocity *= std::exp(-damping * delta);
    value += velocity * delta;
}

static void plAddTextShadowed(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor color, const char* text)
{
    if (!drawList || !font || !text || text[0] == '\0') return;
    drawList->AddText(font, fontSize, pos, color, text);
}

static bool plIsRightAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopRight || anchor == HudElement::Anchor::MiddleRight || anchor == HudElement::Anchor::BottomRight;
}

static bool plIsMiddleXAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopMiddle || anchor == HudElement::Anchor::Middle || anchor == HudElement::Anchor::BottomMiddle;
}

static bool plIsBottomAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::BottomLeft || anchor == HudElement::Anchor::BottomMiddle || anchor == HudElement::Anchor::BottomRight;
}

static ID3D11ShaderResourceView* plPotionTex()
{
    static ID3D11ShaderResourceView* tex = nullptr;
    static int texW = 0;
    static int texH = 0;
    static bool loaded = false;
    if (!loaded)
    {
        D3DHook::loadTextureFromEmbeddedResource("potion.png", &tex, &texW, &texH);
        loaded = true;
    }
    return tex;
}

static std::string plSnakeCase(std::string_view in)
{
    std::string out;
    out.reserve(in.size() + 8);
    char prev = '\0';
    for (const char c : in)
    {
        const bool isUpper = (c >= 'A' && c <= 'Z');
        const bool prevLower = (prev >= 'a' && prev <= 'z');
        const bool prevDigit = (prev >= '0' && prev <= '9');
        if (!out.empty() && isUpper && (prevLower || prevDigit)) out.push_back('_');
        if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else out.push_back(static_cast<char>(c >= ' ' ? c : '_'));
        prev = c;
    }
    return out;
}

static std::string plEffectResourceKey(unsigned int effectId)
{
    const auto type = static_cast<EffectType>(effectId);
    if (type == EffectType::Empty) return {};
    if (type == EffectType::VillageHero) return "hero_of_the_village";
    return plSnakeCase(magic_enum::enum_name(type));
}

static ID3D11ShaderResourceView* plEffectTex(unsigned int effectId)
{
    struct CacheEntry
    {
        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0;
        int h = 0;
        bool tried = false;
    };

    static std::unordered_map<unsigned int, CacheEntry> cache;
    auto& e = cache[effectId];
    if (e.tried) return e.srv ? e.srv : plPotionTex();
    e.tried = true;

    const std::string key = plEffectResourceKey(effectId);
    if (!key.empty())
    {
        if (D3DHook::loadTextureFromEmbeddedResource(key.c_str(), &e.srv, &e.w, &e.h))
        {
            return e.srv;
        }
    }
    e.srv = nullptr;
    return plPotionTex();
}

static std::string plRoman(int value)
{
    if (value <= 0) return "I";
    const int lvl = value + 1;
    switch (lvl)
    {
    case 1: return "I";
    case 2: return "II";
    case 3: return "III";
    case 4: return "IV";
    case 5: return "V";
    case 6: return "VI";
    case 7: return "VII";
    case 8: return "VIII";
    case 9: return "IX";
    case 10: return "X";
    default: return std::to_string(lvl);
    }
}

static std::string plFormatDuration(int ticks)
{
    const int totalSec = std::max(0, ticks) / 20;
    const int minutes = totalSec / 60;
    const int seconds = totalSec % 60;
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
    return std::string(buf);
}

static std::string plPrettifyEffectName(std::string_view in)
{
    std::string out;
    out.reserve(in.size() + 8);
    char prev = '\0';
    for (const char c : in)
    {
        const bool isUpper = (c >= 'A' && c <= 'Z');
        const bool prevLower = (prev >= 'a' && prev <= 'z');
        const bool prevDigit = (prev >= '0' && prev <= '9');
        if (out.size() > 0 && isUpper && (prevLower || prevDigit)) out.push_back(' ');
        out.push_back(c);
        prev = c;
    }
    return out;
}

void PotionList::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &PotionList::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &PotionList::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &PotionList::onPacketInEvent>(this);
    if (mElement) mElement->mVisible = true;

    {
        std::scoped_lock lock(mMutex);
        mEffects.clear();
    }
    mLastPlayerPtr = 0;
    mLastRuntimeId = 0;
    mHadPlayer = false;
}

void PotionList::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &PotionList::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &PotionList::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &PotionList::onPacketInEvent>(this);
    if (mElement) mElement->mVisible = false;

    std::scoped_lock lock(mMutex);
    mEffects.clear();
    mLastPlayerPtr = 0;
    mLastRuntimeId = 0;
    mHadPlayer = false;
}

void PotionList::onPacketInEvent(PacketInEvent& event)
{
    if (!mEnabled) return;
    if (!event.mPacket) return;
    if (event.mPacket->getId() != PacketID::MobEffect) return;

    auto ci = ClientInstance::get();
    if (!ci) return;
    auto player = ci->getLocalPlayer();
    if (!player)
    {
        if (mHadPlayer)
        {
            std::scoped_lock lock(mMutex);
            mEffects.clear();
            mLastPlayerPtr = 0;
            mLastRuntimeId = 0;
            mHadPlayer = false;
        }
        return;
    }

    const uintptr_t playerPtr = reinterpret_cast<uintptr_t>(player);
    const uint64_t runtimeId = player->getRuntimeID();
    if (mHadPlayer && (playerPtr != mLastPlayerPtr || (mLastRuntimeId != 0 && runtimeId != mLastRuntimeId)))
    {
        std::scoped_lock lock(mMutex);
        mEffects.clear();
    }
    mHadPlayer = true;
    mLastPlayerPtr = playerPtr;
    mLastRuntimeId = runtimeId;

    auto mep = event.getPacket<MobEffectPacket>();
    if (!mep) return;
    if (mep->mRuntimeId != player->getRuntimeID()) return;

    const unsigned int effectId = static_cast<unsigned int>(mep->mEffectId);

    if (mep->mEventId == MobEffectPacket::Event::Remove)
    {
        std::scoped_lock lock(mMutex);
        auto it = mEffects.find(effectId);
        if (it != mEffects.end())
        {
            if (!it->second.notifiedEnded)
            {
                NotifyUtils::notify("Effect " + it->second.name + " ended", 5.0f, Notification::Type::Error);
            }
            mEffects.erase(it);
        }
        return;
    }

    if (mep->mEventId != MobEffectPacket::Event::Add && mep->mEventId != MobEffectPacket::Event::Update) return;

    PotionList::EffectEntry entry;
    entry.id = effectId;
    entry.name = plPrettifyEffectName(mep->getEffectName());
    entry.amplifier = mep->mEffectAmplifier;
    entry.durationTicks = std::max(0, mep->mEffectDurationTicks);
    entry.showParticles = mep->mShowParticles;

    std::scoped_lock lock(mMutex);
    if (mEffects.size() > 64)
    {
        mEffects.clear();
    }
    if (auto it = mEffects.find(effectId); it != mEffects.end())
    {
        const bool refreshed = entry.durationTicks > it->second.durationTicks + 20;
        entry.notifiedTenSeconds = refreshed ? false : it->second.notifiedTenSeconds;
        entry.notifiedEnded = refreshed ? false : it->second.notifiedEnded;
    }
    mEffects[effectId] = std::move(entry);
}

void PotionList::onBaseTickEvent(BaseTickEvent& event)
{
    if (!mEnabled) return;
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto player = ci->getLocalPlayer();
    if (!player)
    {
        if (mHadPlayer)
        {
            std::scoped_lock lock(mMutex);
            mEffects.clear();
            mLastPlayerPtr = 0;
            mLastRuntimeId = 0;
            mHadPlayer = false;
        }
        return;
    }

    const uintptr_t playerPtr = reinterpret_cast<uintptr_t>(player);
    const uint64_t runtimeId = player->getRuntimeID();
    if (mHadPlayer && (playerPtr != mLastPlayerPtr || (mLastRuntimeId != 0 && runtimeId != mLastRuntimeId)))
    {
        std::scoped_lock lock(mMutex);
        mEffects.clear();
    }
    mHadPlayer = true;
    mLastPlayerPtr = playerPtr;
    mLastRuntimeId = runtimeId;
    if (event.mActor != player) return;

    std::scoped_lock lock(mMutex);
    for (auto it = mEffects.begin(); it != mEffects.end();)
    {
        auto& e = it->second;
        if (e.durationTicks > 0) e.durationTicks -= 1;
        if (e.durationTicks > 0 && e.durationTicks <= 200 && !e.notifiedTenSeconds)
        {
            NotifyUtils::notify("Effect " + e.name + ": 10 seconds remaining", 5.0f, Notification::Type::Warning);
            e.notifiedTenSeconds = true;
        }
        if (e.durationTicks <= 0)
        {
            if (!e.notifiedEnded)
            {
                NotifyUtils::notify("Effect " + e.name + " ended", 5.0f, Notification::Type::Error);
                e.notifiedEnded = true;
            }
            it = mEffects.erase(it);
        }
        else ++it;
    }
}

void PotionList::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!mElement) return;

    auto player = ci->getLocalPlayer();
    if (!player)
    {
        if (mHadPlayer)
        {
            std::scoped_lock lock(mMutex);
            mEffects.clear();
            mLastPlayerPtr = 0;
            mLastRuntimeId = 0;
            mHadPlayer = false;
        }
    }
    else
    {
        const uintptr_t playerPtr = reinterpret_cast<uintptr_t>(player);
        const uint64_t runtimeId = player->getRuntimeID();
        if (mHadPlayer && (playerPtr != mLastPlayerPtr || (mLastRuntimeId != 0 && runtimeId != mLastRuntimeId)))
        {
            std::scoped_lock lock(mMutex);
            mEffects.clear();
        }
        mHadPlayer = true;
        mLastPlayerPtr = playerPtr;
        mLastRuntimeId = runtimeId;
    }

    if (ci->getScreenName() != "hud_screen" && !mElement->mSampleMode) return;

    const float delta = ImGui::GetIO().DeltaTime;

    std::vector<EffectEntry> renderList;
    {
        std::scoped_lock lock(mMutex);
        renderList.reserve(mEffects.size());
        for (const auto& [id, entry] : mEffects) renderList.push_back(entry);
    }

    if (renderList.empty() && mElement->mSampleMode)
    {
        renderList = {
            {static_cast<unsigned int>(EffectType::Speed), "Speed", 1, 20 * 48, true},
            {static_cast<unsigned int>(EffectType::Strength), "Strength", 0, 20 * 22, true},
            {static_cast<unsigned int>(EffectType::Regeneration), "Regeneration", 1, 20 * 9, true}
        };
    }

    std::ranges::sort(renderList, [](const EffectEntry& a, const EffectEntry& b) {
        return a.name < b.name;
    });

    static float showAnim = 0.f;
    static float showVel = 0.f;
    const bool wantShow = true;
    plSpringTo(showAnim, showVel, wantShow ? 1.f : 0.f, delta, 130.f, 30.f);
    showAnim = MathUtils::clamp(showAnim, 0.f, 1.f);
    const float alphaAnim = plEaseOutCubic(showAnim);
    const float popAnimRaw = plEaseOutBack(showAnim);
    const float popAnim = std::clamp(popAnimRaw, 0.f, 1.2f);

    FontHelper::pushPrefFont(false, true);

    auto* textFont = ImGui::GetFont();
    ImFont* headerIconFont = textFont;
    if (auto it = FontHelper::Fonts.find("essence.ttf"); it != FontHelper::Fonts.end() && it->second)
    {
        headerIconFont = it->second;
    }

    const float fontSize = 18.f;
    const float paddingX = 10.f;
    const float paddingY = 8.f;
    const float rounding = 8.f;
    const float lineGap = 4.f;
    const float headerGap = 12.f;
    const float dotRadius = 2.9f;
    const float dotGapX = 7.f;
    const float rightGapX = 12.f;
    const float iconGap = 5.f;

    const std::string title = "PotionList";
    const std::string icon = "h";

    ImColor bg = ColorUtils::getUiCardColor(1.0f);
    ImColor border = ColorUtils::getUiBorderColor(16.0f / 255.0f);
    ImColor textColor = ColorUtils::getUiTextColor(1.0f);
    ImColor subTextColor = ColorUtils::getUiTextDimColor(1.0f);
    ImColor accent = ColorUtils::getGuiAccentColor(0);

    ImVec2 iconSz = plCalcSize(headerIconFont, fontSize, icon);
    ImVec2 titleSz = plCalcSize(textFont, fontSize, title);
    float headerHeight = std::max(iconSz.y, titleSz.y);
    float headerWidth = titleSz.x + 6.f + iconSz.x;

    float lineHeight = plCalcSize(textFont, fontSize, "A").y;

    struct ItemAnim
    {
        float value = 0.f;
        float velocity = 0.f;
    };

    static std::unordered_map<unsigned int, ItemAnim> itemAnims;
    std::unordered_map<unsigned int, EffectEntry> byId;
    byId.reserve(renderList.size());
    for (const auto& entry : renderList) byId[entry.id] = entry;

    for (const auto& [id, entry] : byId)
    {
        if (!itemAnims.contains(id)) itemAnims.emplace(id, ItemAnim{});
    }

    for (auto& [id, anim] : itemAnims)
    {
        const bool present = byId.contains(id);
        const float target = present ? 1.f : 0.f;
        plSpringTo(anim.value, anim.velocity, target, delta, 170.f, 38.f);
        anim.value = std::clamp(anim.value, 0.f, 1.f);
        if (present && anim.value > 0.995f) anim.value = 1.f;
        if (!present && anim.value < 0.005f) anim.value = 0.f;
    }

    std::vector<unsigned int> drawIds;
    drawIds.reserve(byId.size());
    for (const auto& [id, entry] : byId)
    {
        if (itemAnims[id].value > 0.001f) drawIds.push_back(id);
    }

    std::ranges::sort(drawIds, [&](unsigned int a, unsigned int b) {
        return byId[a].name < byId[b].name;
    });

    for (auto it = itemAnims.begin(); it != itemAnims.end();)
    {
        if (it->second.value <= 0.001f && !byId.contains(it->first)) it = itemAnims.erase(it);
        else ++it;
    }

    float maxLineWidth = headerWidth;
    for (unsigned int id : drawIds)
    {
        const auto& entry = byId[id];
        std::string left = entry.name;
        if (mShowLevel.mValue && entry.amplifier >= 0)
        {
            left += " ";
            left += plRoman(entry.amplifier);
        }
        const std::string right = mShowTime.mValue ? plFormatDuration(entry.durationTicks) : std::string();

        float w = 0.f;
        w += dotRadius * 2.f;
        w += dotGapX;
        w += plCalcSize(textFont, fontSize, left).x;

        const bool hasIcon = mShowIcons.mValue && plEffectTex(entry.id);
        const bool hasTime = !right.empty();
        const bool hasRightStuff = hasIcon || hasTime;
        if (hasRightStuff) w += rightGapX;
        if (hasTime) w += plCalcSize(textFont, fontSize, right).x;
        if (hasIcon)
        {
            const float iconSize = lineHeight * 0.82f;
            w += iconGap + iconSize;
        }
        maxLineWidth = std::max(maxLineWidth, w);
    }

    float contentHeight = 0.f;
    for (size_t i = 0; i < drawIds.size(); i++)
    {
        const float rawA = itemAnims[drawIds[i]].value;
        const float listStagger = 0.028f;
        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = plEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));
        const float aLayout = plEaseInOut(rawA) * listGate;

        contentHeight += lineHeight * aLayout;
        if (i + 1 < drawIds.size()) contentHeight += lineGap * aLayout;
    }

    const float targetWidth = maxLineWidth + paddingX * 2.f + 18.f;
    const float targetHeight = paddingY * 2.f + headerHeight + (drawIds.empty() ? 0.f : headerGap + contentHeight);

    static float panelWidth = 0.f;
    static float panelHeight = 0.f;
    panelWidth = plExpApproach(panelWidth, targetWidth, delta, 30.f);
    panelHeight = plExpApproach(panelHeight, targetHeight, delta, 30.f);
    if (std::abs(panelWidth - targetWidth) < 0.25f) panelWidth = targetWidth;
    if (std::abs(panelHeight - targetHeight) < 0.25f) panelHeight = targetHeight;

    const float panelScale = 0.92f + 0.08f * popAnim;

    mElement->mSize = {panelWidth, panelHeight};

    ImVec2 pos = mElement->getPos();
    if (plIsRightAnchored(mElement->mAnchor)) pos.x -= panelWidth;
    else if (plIsMiddleXAnchored(mElement->mAnchor)) pos.x -= panelWidth * 0.5f;

    if (plIsBottomAnchored(mElement->mAnchor)) pos.y -= panelHeight;

    if (mElement->mCentered)
    {
        pos.x -= panelWidth * 0.5f;
        pos.y -= panelHeight * 0.5f;
    }

    ImVec2 min = pos;
    ImVec2 max = {pos.x + panelWidth, pos.y + panelHeight};

    ImVec2 pivot = min;
    if (plIsRightAnchored(mElement->mAnchor)) pivot.x = max.x;
    else if (plIsMiddleXAnchored(mElement->mAnchor)) pivot.x = (min.x + max.x) * 0.5f;
    if (plIsBottomAnchored(mElement->mAnchor)) pivot.y = max.y;
    else pivot.y = min.y;

    ImVec2 scaledMin = {pivot.x + (min.x - pivot.x) * panelScale, pivot.y + (min.y - pivot.y) * panelScale};
    ImVec2 scaledMax = {pivot.x + (max.x - pivot.x) * panelScale, pivot.y + (max.y - pivot.y) * panelScale};
    const float scaledW = scaledMax.x - scaledMin.x;
    const float scaledH = scaledMax.y - scaledMin.y;

    const bool inEditor = HudEditor::gInstance && HudEditor::gInstance->mEnabled;
    const bool hovered = inEditor && ImRenderUtils::isMouseOver(ImVec4(scaledMin.x, scaledMin.y, scaledMax.x, scaledMax.y));
    static float hoverAnim = 0.f;
    hoverAnim = MathUtils::lerp(hoverAnim, hovered ? 1.f : 0.f, delta * 12.f);
    hoverAnim = MathUtils::clamp(hoverAnim, 0.f, 1.f);

    ImColor bgHover = (ColorUtils::getUiTheme() == ColorUtils::UiTheme::Light) ? ImColor(240, 240, 246, 255) : ImColor(26, 25, 32, 255);
    ImColor borderHover = (ColorUtils::getUiTheme() == ColorUtils::UiTheme::Light) ? ImColor(130, 130, 150, 64) : ImColor(255, 255, 255, 42);
    bg.Value = ImLerp(bg.Value, bgHover.Value, hoverAnim);
    border.Value = ImLerp(border.Value, borderHover.Value, hoverAnim);

    bg.Value.w *= alphaAnim;
    border.Value.w *= alphaAnim;
    textColor.Value.w *= alphaAnim;
    subTextColor.Value.w *= alphaAnim;
    accent.Value.w *= alphaAnim;

    auto* drawList = ImGui::GetBackgroundDrawList();
    auto* interfaceModule = gFeatureManager && gFeatureManager->mModuleManager ? gFeatureManager->mModuleManager->getModule<Interface>() : nullptr;
    const bool useHudBlur = interfaceModule && interfaceModule->mHudBlur.mValue;
    const float blurStrength = useHudBlur ? interfaceModule->mHudBlurStrength.mValue : 0.f;
    if (useHudBlur)
    {
        ImRenderUtils::addBlurAlpha(ImVec4(scaledMin.x, scaledMin.y, scaledMax.x, scaledMax.y), blurStrength, bg.Value.w, rounding, drawList, true);
        bg.Value.w *= 0.85f;
    }
    {
        ImColor shadow = IM_COL32(0, 0, 0, 255);
        shadow.Value.w = 0.60f * alphaAnim;
        drawList->AddShadowRect(scaledMin, scaledMax, shadow, 24.0f, ImVec2(0.f, 3.f), 0, rounding);
    }
    drawList->AddRectFilled(scaledMin, scaledMax, bg, rounding);
    drawList->AddRect(scaledMin, scaledMax, border, rounding, 0, 1.4f);
    if (hoverAnim > 0.001f)
    {
        ImColor outline = borderHover;
        outline.Value.w *= alphaAnim * hoverAnim;
        drawList->AddRect(scaledMin, scaledMax, outline, rounding, 0, 2.0f);
    }
    {
        ImColor glow = accent;
        glow.Value.w = 0.40f * alphaAnim;

        const float glowWidth = scaledW * 0.5f;
        const float glowHeight = scaledH * 0.5f;
        const float glowX = scaledMin.x + (scaledW - glowWidth) * 0.5f;
        const float glowLift = glowHeight * 1.f;
        const float glowY = scaledMin.y - glowLift;

        drawList->PushClipRect(scaledMin, scaledMax, true);
        drawList->AddShadowRect(ImVec2(glowX, glowY), ImVec2(glowX + glowWidth, glowY + glowHeight), glow, 45.0f, ImVec2(0.f, 0.f), 0, rounding);
        drawList->PopClipRect();
    }
    {
        ImColor tabColor = accent;
        tabColor.Value.w = 1.f * alphaAnim;

        const float tabHeight = 3.f;
        const float tabWidth = std::max(56.f, scaledW * 0.45f);
        const ImVec2 tabMin = ImVec2(
            scaledMin.x + (scaledW - tabWidth) * 0.5f,
            scaledMax.y - tabHeight
        );
        const ImVec2 tabMax = ImVec2(tabMin.x + tabWidth, tabMin.y + tabHeight);

        drawList->PushClipRect(scaledMin, scaledMax, true);
        drawList->AddRectFilled(tabMin, tabMax, tabColor, rounding, ImDrawFlags_RoundCornersTop);
        drawList->PopClipRect();
    }

    const float headerNudgeY = 1.5f;
    const float showOvershoot = std::max(0.f, popAnim - 1.f);
    const float headerSlideY = (1.f - std::min(popAnim, 1.f)) * -10.f + showOvershoot * 6.f;
    ImVec2 headerPos = {scaledMin.x + paddingX, scaledMin.y + paddingY + headerNudgeY + headerSlideY};
    plAddTextShadowed(drawList, textFont, fontSize, headerPos, textColor, title.c_str());
    plAddTextShadowed(drawList, headerIconFont, fontSize, {scaledMax.x - paddingX - iconSz.x, headerPos.y}, accent, icon.c_str());

    float y = headerPos.y + headerHeight + headerGap;
    for (size_t i = 0; i < drawIds.size(); i++)
    {
        const unsigned int id = drawIds[i];
        const auto& entry = byId[id];
        const float rawA = itemAnims[id].value;
        if (rawA <= 0.001f) continue;

        const float listStagger = 0.028f;
        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = plEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));

        const float aAlpha = std::clamp(plEaseOutCubic(rawA) * listGate, 0.f, 1.f);
        const float aMoveRaw = plEaseOutBack(rawA) * listGate;
        const float aMove = std::clamp(aMoveRaw, 0.f, 1.2f);
        const float overshoot = std::max(0.f, aMove - 1.f);
        const float aLayout = plEaseInOut(rawA) * listGate;

        ImColor nameColor = subTextColor;
        nameColor.Value.w *= aAlpha;

        ImColor rightColor = accent;
        rightColor.Value.w *= aAlpha;
        if (entry.durationTicks > 0 && entry.durationTicks <= 200)
        {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
            ImColor alert = ImColor(234, 78, 78, 255);
            alert.Value.w = nameColor.Value.w;
            const float blend = 0.35f + 0.65f * pulse;
            nameColor.Value = ImLerp(nameColor.Value, alert.Value, blend);
            rightColor.Value = ImLerp(rightColor.Value, alert.Value, blend);
        }

        const float slideT = std::min(aMove, 1.f);
        const float slideX = (1.f - slideT) * -8.f + overshoot * 5.f;
        const float slideY = (1.f - slideT) * -5.f + overshoot * 3.f;
        ImVec2 p = {scaledMin.x + paddingX + slideX, y + slideY};

        ImColor dotColor = accent;
        dotColor.Value.w = nameColor.Value.w;
        const float dotR = dotRadius * (0.85f + 0.15f * std::min(aMove, 1.f));
        drawList->AddCircleFilled(ImVec2(p.x + dotR, p.y + lineHeight * 0.55f), dotR, dotColor, 16);
        p.x += dotR * 2.f + dotGapX;

        std::string left = entry.name;
        if (mShowLevel.mValue && entry.amplifier >= 0)
        {
            left += " ";
            left += plRoman(entry.amplifier);
        }
        plAddTextShadowed(drawList, textFont, fontSize, p, nameColor, left.c_str());
        const float leftW = plCalcSize(textFont, fontSize, left).x;

        const std::string right = mShowTime.mValue ? plFormatDuration(entry.durationTicks) : std::string();
        const float rightW = plCalcSize(textFont, fontSize, right).x;
        const float iconSize = lineHeight * 0.82f;
        float rightEdge = (scaledMax.x - paddingX) + slideX;
        float iconX = rightEdge;
        if (mShowIcons.mValue)
        {
            if (auto* tex = plEffectTex(entry.id))
            {
                iconX = rightEdge - iconSize;
                const ImVec2 iconMin = ImVec2(iconX, p.y + (lineHeight - iconSize) * 0.5f);
                const ImVec2 iconMax = ImVec2(iconX + iconSize, iconMin.y + iconSize);
                drawList->AddImage(tex, iconMin, iconMax);
                rightEdge = iconX - iconGap;
            }
        }

        if (!right.empty())
        {
            float rightX = (rightEdge - rightW);
            const float minRightX = p.x + leftW + rightGapX;
            if (rightX < minRightX) rightX = minRightX;
            plAddTextShadowed(drawList, textFont, fontSize, ImVec2(rightX, p.y), rightColor, right.c_str());
        }

        y += lineHeight * aLayout;
        if (i + 1 < drawIds.size()) y += lineGap * aLayout;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    static bool menuTarget = false;
    static float menuAnim = 0.f;

    if (!inEditor) menuTarget = false;

    if (inEditor && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        menuTarget = !menuTarget;
    }

    const float menuSpeed = menuTarget ? 14.f : 16.f;
    menuAnim = MathUtils::lerp(menuAnim, menuTarget ? 1.f : 0.f, delta * menuSpeed);
    menuAnim = MathUtils::clamp(menuAnim, 0.f, 1.f);

    struct Item { const char* label; BoolSetting* s; };
    Item items[] = {
        {"Иконки", &mShowIcons},
        {"Уровень", &mShowLevel},
        {"Время", &mShowTime}
    };

    static float toggleAnim[3]{};
    for (int i = 0; i < 3; i++)
    {
        const float target = items[i].s->mValue ? 1.f : 0.f;
        const float speed = target > 0.5f ? 9.f : 14.f;
        toggleAnim[i] = MathUtils::lerp(toggleAnim[i], target, delta * speed);
        toggleAnim[i] = MathUtils::clamp(toggleAnim[i], 0.f, 1.f);
    }

    const std::string dot = "·";
    const std::string iconA = "h";
    const std::string iconB = "b";
    const std::string iconC = "l";

    ImFont* energyFont = nullptr;
    {
        auto it = FontHelper::Fonts.find("essence.ttf");
        if (it != FontHelper::Fonts.end() && it->second) energyFont = it->second;
    }
    if (!energyFont) energyFont = textFont;

    const float pad = 12.f;
    const float rowH = fontSize + 12.f;
    const float indicatorR = 3.2f;
    const float indicatorW = indicatorR * 2.f;
    const float menuIconGap = 6.f;
    float maxIconW = 0.f;
    float maxLabelW = 0.f;
    for (const auto& it : items)
    {
        maxLabelW = std::max(maxLabelW, plCalcSize(textFont, fontSize, it.label).x);
    }
    const std::string* menuIcons[3] = {&iconA, &iconB, &iconC};
    for (int i = 0; i < 3; i++)
    {
        maxIconW = std::max(maxIconW, plCalcSize(energyFont ? energyFont : textFont, fontSize, *menuIcons[i]).x);
    }
    const float indicatorShift = indicatorW + 7.f + maxIconW + menuIconGap;
    const float menuTextPad = std::max(4.f, fontSize * 0.35f);
    const float menuW = pad * 2.f + maxLabelW + indicatorShift + menuTextPad;
    const float menuH = pad * 2.f + rowH * 3.f;

    const float anchorY = scaledMax.y + 10.f;
    const float centerX = (scaledMin.x + scaledW * 0.5f);
    ImVec2 menuMinTarget = ImVec2(centerX - menuW * 0.5f, anchorY);
    if (menuMinTarget.y + menuH > display.y - 6.f) menuMinTarget.y = scaledMin.y - menuH - 10.f;
    menuMinTarget.x = std::clamp(menuMinTarget.x, 6.f, display.x - menuW - 6.f);
    menuMinTarget.y = std::clamp(menuMinTarget.y, 6.f, display.y - menuH - 6.f);

    static ImVec2 menuMinSmoothed = menuMinTarget;
    static bool menuPrevTarget = false;
    if (!menuPrevTarget && menuTarget) menuMinSmoothed = menuMinTarget;
    menuPrevTarget = menuTarget;

    menuMinSmoothed.x = MathUtils::lerp(menuMinSmoothed.x, menuMinTarget.x, delta * 14.f);
    menuMinSmoothed.y = MathUtils::lerp(menuMinSmoothed.y, menuMinTarget.y, delta * 14.f);
    ImVec2 menuMin = menuMinSmoothed;
    ImVec2 menuMax = ImVec2(menuMin.x + menuW, menuMin.y + menuH);

    const float menuA = plEaseOutCubic(menuAnim);
    const float menuPopRaw = plEaseOutBack(menuAnim);
    const float menuPop = std::clamp(menuPopRaw, 0.f, 1.15f);
    const float menuScale = 0.92f + 0.08f * menuPop;
    const ImVec2 menuCenter = ImVec2((menuMin.x + menuMax.x) * 0.5f, (menuMin.y + menuMax.y) * 0.5f);
    const ImVec2 drawMin = ImVec2(menuCenter.x + (menuMin.x - menuCenter.x) * menuScale, menuCenter.y + (menuMin.y - menuCenter.y) * menuScale);
    const ImVec2 drawMax = ImVec2(menuCenter.x + (menuMax.x - menuCenter.x) * menuScale, menuCenter.y + (menuMax.y - menuCenter.y) * menuScale);
    const bool menuHovered = ImRenderUtils::isMouseOver(ImVec4(drawMin.x, drawMin.y, drawMax.x, drawMax.y));
    if (inEditor && menuTarget)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !menuHovered && !hovered) menuTarget = false;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !menuHovered && !hovered) menuTarget = false;
    }

    if (menuAnim > 0.01f)
    {
        const float a = menuA;
        const float s = std::clamp(menuScale, 0.85f, 1.2f);

        ImColor menuBg = ColorUtils::getUiCardColor(1.0f);
        ImColor menuBorder = ColorUtils::getUiBorderColor(25.0f / 255.0f);
        ImColor menuText = ColorUtils::getUiTextColor(1.0f);
        ImColor menuDot = ColorUtils::getUiTextDimColor(1.0f);
        ImColor menuAccent = ColorUtils::getGuiAccentColor(0);
        menuBg.Value.w *= a;
        menuBorder.Value.w *= a;
        menuText.Value.w *= a;
        menuDot.Value.w *= a;
        menuAccent.Value.w *= a;

        drawList->AddShadowRect(drawMin, drawMax, IM_COL32(0, 0, 0, static_cast<int>(170.f * a)), 30.0f, ImVec2(0.f, 3.f), 0, 7.f);
        drawList->AddRectFilled(drawMin, drawMax, menuBg, 7.f);
        {
            ImColor glow = menuAccent;
            glow.Value.w = 0.40f * a;
            const float w = drawMax.x - drawMin.x;
            const float h = drawMax.y - drawMin.y;
            const float glowBaseH = std::min(std::max(10.f, rowH * s * 0.45f), h * 0.22f);
            const float glowWidth = w * 0.55f;
            const float glowX = drawMin.x + (w - glowWidth) * 0.5f;
            const float glowLift = glowBaseH * 0.95f;
            const ImVec2 glowMin = ImVec2(glowX, drawMin.y - glowLift);
            const ImVec2 glowMax = ImVec2(glowX + glowWidth, drawMin.y + glowBaseH - glowLift);
            drawList->PushClipRect(drawMin, drawMax, true);
            drawList->AddShadowRect(glowMin, glowMax, glow, 45.0f, ImVec2(0.f, 0.f), 0, 7.f);
            drawList->PopClipRect();
        }
        drawList->AddRect(drawMin, drawMax, menuBorder, 7.f, 0, 1.2f);
        {
            ImColor tab = menuAccent;
            tab.Value.w *= a;
            const float w = drawMax.x - drawMin.x;
            const float tabH = 3.f;
            const float tabW = w / 2.5f;
            const float tabX = drawMin.x + (w - tabW) * 0.5f;
            const ImVec2 tabMin = ImVec2(tabX, drawMax.y - tabH);
            const ImVec2 tabMax = ImVec2(tabX + tabW, drawMax.y);
            drawList->AddRectFilled(tabMin, tabMax, tab, 7.f, ImDrawFlags_RoundCornersTop);
        }

        for (int i = 0; i < 3; i++)
        {
            ImVec2 rowMin = ImVec2(drawMin.x + pad * s, drawMin.y + pad * s + rowH * i * s);
            ImVec2 rowMax = ImVec2(drawMax.x - pad * s, rowMin.y + rowH * s);
            const bool rowHovered = inEditor && menuHovered && ImRenderUtils::isMouseOver(ImVec4(rowMin.x, rowMin.y, rowMax.x, rowMax.y));
            if (inEditor && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && rowHovered)
            {
                items[i].s->mValue = !items[i].s->mValue;
            }

            const float t = plEaseOutCubic(toggleAnim[i]);
            if (t > 0.001f)
            {
                ImColor circleCol = menuAccent;
                circleCol.Value.w *= a * t;
                const float r = indicatorR * (0.75f + 0.25f * t) * s;
                const ImVec2 c = ImVec2(rowMin.x + 3.f + indicatorR * s, rowMin.y + (rowH * s) * 0.5f);
                drawList->AddCircleFilled(c, r, circleCol, 18);
            }

            const float fontScaled = fontSize * s;
            const float baseShift = indicatorW + 7.f;
            const float dotW = plCalcSize(textFont, fontSize, dot).x;
            const float textShift = (baseShift + dotW + menuIconGap + maxIconW + menuIconGap) * t * s;
            if (t > 0.001f)
            {
                ImVec2 dotP = ImVec2(rowMin.x + 2.f + baseShift * t * s, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(textFont, fontScaled, dotP, menuDot, dot.c_str());
                ImColor iconCol = menuAccent;
                iconCol.Value.w *= a * (0.25f + 0.75f * t);
                ImVec2 iconP = ImVec2(rowMin.x + 2.f + baseShift * t * s + dotW + menuIconGap, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(energyFont ? energyFont : textFont, fontScaled, iconP, iconCol, menuIcons[i]->c_str());
            }
            ImVec2 textP = ImVec2(rowMin.x + 2.f + textShift, rowMin.y + (rowH * s - fontScaled) * 0.5f);
            drawList->AddText(textFont, fontScaled, textP, menuText, items[i].label);
        }
    }

    FontHelper::popPrefFont();
}
