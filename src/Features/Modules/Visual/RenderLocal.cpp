#include "RenderLocal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ranges>
#include <unordered_map>

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MemUtils.hpp>
#include <Utils/StringUtils.hpp>
#include <Features/Modules/Visual/Interface.hpp>

static ImVec2 rlCalcSize(ImFont* font, float fontSize, const std::string& text)
{
    if (!font || text.empty()) return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text.c_str());
}

static ImVec2 rlCalcSize(ImFont* font, float fontSize, const char* text)
{
    if (!font || !text || text[0] == '\0') return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);
}

static float rlExpApproach(float current, float target, float delta, float speed)
{
    if (delta <= 0.f) return current;
    const float t = 1.f - std::exp(-speed * delta);
    return current + (target - current) * t;
}

static float rlEaseInOut(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static float rlEaseOutCubic(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

static float rlEaseOutBack(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float inv = t - 1.f;
    return 1.f + c3 * inv * inv * inv + c1 * inv * inv;
}

static void rlSpringTo(float& value, float& velocity, float target, float delta, float stiffness, float damping)
{
    if (delta <= 0.f) return;
    const float accel = (target - value) * stiffness;
    velocity += accel * delta;
    velocity *= std::exp(-damping * delta);
    value += velocity * delta;
}

static void rlAddTextShadowed(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor color, const char* text)
{
    if (!drawList || !font || !text || text[0] == '\0') return;
    const float t = static_cast<float>(ImGui::GetTime());

    const float wave = 0.5f + 0.5f * std::sin(t * 2.5f + pos.x * 0.060f);
    const float wave2 = 0.5f + 0.5f * std::sin(t * 3.2f + pos.x * 0.034f + 1.2f);
    const float brighten = 0.86f + 0.14f * wave;
    const float whiteMix = 0.10f + 0.34f * wave2;

    ImVec4 v = color.Value;
    v.x = std::clamp(v.x * brighten, 0.f, 1.f);
    v.y = std::clamp(v.y * brighten, 0.f, 1.f);
    v.z = std::clamp(v.z * brighten, 0.f, 1.f);
    v.x = std::clamp(v.x + (1.f - v.x) * whiteMix, 0.f, 1.f);
    v.y = std::clamp(v.y + (1.f - v.y) * whiteMix, 0.f, 1.f);
    v.z = std::clamp(v.z + (1.f - v.z) * whiteMix, 0.f, 1.f);

    drawList->AddText(font, fontSize, pos, ImColor(v), text);
}

static void rlAddTextShadowedNoShimmer(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor color, const char* text)
{
    if (!drawList || !font || !text || text[0] == '\0') return;
    drawList->AddText(font, fontSize, pos, color, text);
}

static bool rlTryGetMcCodeAt(const std::string& s, size_t i, size_t& prefixLen)
{
    if (i >= s.size()) return false;

    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xA7)
    {
        prefixLen = 1;
        return true;
    }

    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7)
    {
        prefixLen = 2;
        return true;
    }

    return false;
}

static std::string rlStripMcColorCodes(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();)
    {
        size_t prefixLen = 0;
        if (rlTryGetMcCodeAt(s, i, prefixLen))
        {
            size_t j = i + prefixLen;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j < s.size())
            {
                i = j + 1;
                continue;
            }
        }

        out.push_back(s[i]);
        i++;
    }

    return out;
}

static ImColor rlMcColorFromCode(char code)
{
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(code))))
    {
    case '0': return ImColor(0.f, 0.f, 0.f, 1.f);
    case '1': return ImColor(0.f, 0.f, 0.66f, 1.f);
    case '2': return ImColor(0.f, 0.66f, 0.f, 1.f);
    case '3': return ImColor(0.f, 0.66f, 0.66f, 1.f);
    case '4': return ImColor(0.66f, 0.f, 0.f, 1.f);
    case '5': return ImColor(0.66f, 0.f, 0.66f, 1.f);
    case '6': return ImColor(1.f, 0.66f, 0.f, 1.f);
    case '7': return ImColor(0.66f, 0.66f, 0.66f, 1.f);
    case '8': return ImColor(0.33f, 0.33f, 0.33f, 1.f);
    case '9': return ImColor(0.33f, 0.33f, 1.f, 1.f);
    case 'a': return ImColor(0.33f, 1.f, 0.33f, 1.f);
    case 'b': return ImColor(0.33f, 1.f, 1.f, 1.f);
    case 'c': return ImColor(1.f, 0.33f, 0.33f, 1.f);
    case 'd': return ImColor(1.f, 0.33f, 1.f, 1.f);
    case 'e': return ImColor(1.f, 1.f, 0.33f, 1.f);
    case 'f': return ImColor(1.f, 1.f, 1.f, 1.f);
    default: return ImColor(1.f, 1.f, 1.f, 1.f);
    }
}

static void rlDrawMcColoredText(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor defaultColor, const std::string& text)
{
    if (!drawList || !font || text.empty() || defaultColor.Value.w <= 0.f) return;

    const float t = static_cast<float>(ImGui::GetTime());

    ImColor current = defaultColor;
    std::string segment;
    segment.reserve(text.size());

    auto flush = [&]() {
        if (segment.empty()) return;
        const float wave = 0.5f + 0.5f * std::sin(t * 2.5f + pos.x * 0.060f);
        const float wave2 = 0.5f + 0.5f * std::sin(t * 3.2f + pos.x * 0.034f + 1.2f);
        const float brighten = 0.86f + 0.14f * wave;
        const float whiteMix = 0.10f + 0.34f * wave2;

        ImVec4 v = current.Value;
        v.x = std::clamp(v.x * brighten, 0.f, 1.f);
        v.y = std::clamp(v.y * brighten, 0.f, 1.f);
        v.z = std::clamp(v.z * brighten, 0.f, 1.f);
        v.x = std::clamp(v.x + (1.f - v.x) * whiteMix, 0.f, 1.f);
        v.y = std::clamp(v.y + (1.f - v.y) * whiteMix, 0.f, 1.f);
        v.z = std::clamp(v.z + (1.f - v.z) * whiteMix, 0.f, 1.f);

        drawList->AddText(font, fontSize, pos, ImColor(v), segment.c_str());
        pos.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, segment.c_str()).x;
        segment.clear();
    };

    for (size_t i = 0; i < text.size();)
    {
        size_t prefixLen = 0;
        if (rlTryGetMcCodeAt(text, i, prefixLen))
        {
            size_t j = i + prefixLen;
            while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) j++;
            if (j < text.size())
            {
                flush();
                char code = text[j];
                if (static_cast<char>(std::tolower(static_cast<unsigned char>(code))) == 'r')
                {
                    current = defaultColor;
                }
                else
                {
                    ImColor mapped = rlMcColorFromCode(code);
                    mapped.Value.w = defaultColor.Value.w;
                    current = mapped;
                }
                i = j + 1;
                continue;
            }
        }

        segment.push_back(text[i]);
        i++;
    }

    flush();
}

static void rlDrawMcColoredTextShadowed(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor defaultColor, const std::string& text)
{
    if (!drawList || !font || text.empty() || defaultColor.Value.w <= 0.f) return;

    rlDrawMcColoredText(drawList, font, fontSize, pos, defaultColor, text);
}

static void rlDrawMcColoredTextShadowedClipped(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor defaultColor, const std::string& text, float maxWidth, float clipHeight, ImColor fadeToColor)
{
    if (!drawList || !font || text.empty() || defaultColor.Value.w <= 0.f) return;
    if (maxWidth <= 0.f || clipHeight <= 0.f) return;

    const std::string plain = rlStripMcColorCodes(text);
    const float plainWidth = plain.empty() ? 0.f : font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, plain.c_str()).x;
    const bool isClipped = plainWidth > maxWidth + 0.5f;

    const ImVec2 clipMin = pos;
    const ImVec2 clipMax = ImVec2(pos.x + maxWidth, pos.y + clipHeight);

    drawList->PushClipRect(clipMin, clipMax, true);
    rlDrawMcColoredTextShadowed(drawList, font, fontSize, pos, defaultColor, text);
    drawList->PopClipRect();

    if (!isClipped) return;

    const float fadeWidth = std::min(22.f, std::max(10.f, maxWidth * 0.28f));
    const ImVec2 fadeMin = ImVec2(pos.x + maxWidth - fadeWidth, pos.y - 1.f);
    const ImVec2 fadeMax = ImVec2(pos.x + maxWidth, pos.y + clipHeight + 1.f);

    ImColor left = fadeToColor;
    left.Value.w = 0.f;
    drawList->AddRectFilledMultiColor(
        fadeMin,
        fadeMax,
        left,
        fadeToColor,
        fadeToColor,
        left
    );
}

static std::string rlFirstLine(std::string s)
{
    const size_t n = s.find('\n');
    if (n != std::string::npos) s.resize(n);
    return s;
}

static bool rlIsRightAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopRight || anchor == HudElement::Anchor::MiddleRight || anchor == HudElement::Anchor::BottomRight;
}

static bool rlIsMiddleXAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopMiddle || anchor == HudElement::Anchor::Middle || anchor == HudElement::Anchor::BottomMiddle;
}

static bool rlIsBottomAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::BottomLeft || anchor == HudElement::Anchor::BottomMiddle || anchor == HudElement::Anchor::BottomRight;
}

void RenderLocal::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &RenderLocal::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &RenderLocal::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = true;
}

void RenderLocal::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &RenderLocal::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &RenderLocal::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = false;

    std::scoped_lock lock(mMutex);
    mPlayers.clear();
}

void RenderLocal::onBaseTickEvent(BaseTickEvent& event)
{
    if (!mEnabled) return;

    auto ci = ClientInstance::get();
    if (!ci) return;
    auto player = ci->getLocalPlayer();
    if (!player) return;

    auto actors = ActorUtils::getActorList(true, true);

    std::unordered_map<std::string, PlayerEntry> bestByLower;
    bestByLower.reserve(actors.size());

    for (auto actor : actors)
    {
        if (!actor) continue;
        if (actor == player) continue;
        if (!ActorUtils::isActorValid(actor)) continue;
        bool isPlayer = false;
        if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) continue;
        if (!isPlayer) continue;

        std::string name;
        TryCallWrapper([&]() { name = rlFirstLine(actor->getNameTag()); });
        if (name.empty()) TryCallWrapper([&]() { name = actor->getRawName(); });
        if (name.empty()) continue;

        std::string key = StringUtils::toLower(rlStripMcColorCodes(name));
        bestByLower[key] = PlayerEntry{ std::move(name) };
    }

    std::vector<PlayerEntry> entries;
    entries.reserve(bestByLower.size());
    for (auto& [_, v] : bestByLower) entries.emplace_back(std::move(v));

    std::ranges::sort(entries, [](const PlayerEntry& a, const PlayerEntry& b) {
        return rlStripMcColorCodes(a.name) < rlStripMcColorCodes(b.name);
    });

    constexpr size_t kMaxEntries = 64;
    if (entries.size() > kMaxEntries)
    {
        entries.resize(kMaxEntries);
    }

    {
        std::scoped_lock lock(mMutex);
        mPlayers.swap(entries);
    }
}

void RenderLocal::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!mElement) return;
    if (ci->getScreenName() != "hud_screen" && !mElement->mSampleMode) return;

    const float delta = ImGui::GetIO().DeltaTime;

    std::vector<PlayerEntry> renderList;
    {
        std::scoped_lock lock(mMutex);
        renderList = mPlayers;
    }

    if (renderList.empty() && mElement->mSampleMode)
    {
        renderList = {
            {"Steve"},
            {"Alex"},
            {"Notch"}
        };
    }

    static float showAnim = 0.f;
    static float showVel = 0.f;
    const bool wantShow = true;
    rlSpringTo(showAnim, showVel, wantShow ? 1.f : 0.f, delta, 130.f, 30.f);
    showAnim = MathUtils::clamp(showAnim, 0.f, 1.f);
    if (wantShow && showAnim > 0.995f) showAnim = 1.f;
    const float alphaAnim = rlEaseOutCubic(showAnim);
    const float popAnimRaw = rlEaseOutBack(showAnim);
    const float popAnim = std::clamp(popAnimRaw, 0.f, 1.2f);

    FontHelper::pushPrefFont(false, true);

    auto* textFont = ImGui::GetFont();
    ImFont* iconFont = textFont;
    if (auto it = FontHelper::Fonts.find("energy"); it != FontHelper::Fonts.end() && it->second)
    {
        iconFont = it->second;
    }

    const float fontSize = 18.f;
    const float paddingX = 10.f;
    const float paddingY = 8.f;
    const float rounding = 8.f;
    const float lineGap = 4.f;
    const float dotRadius = 2.9f;
    const float dotGapX = 7.f;
    const float maxNameWidth = std::clamp(ImGui::GetIO().DisplaySize.x * 0.22f, 140.f, 280.f);

    const std::string title = "Players";
    const std::string icon = "d";

    const bool showIcon = mShowIcon.mValue;
    const bool showDots = mShowDots.mValue;
    const float headerGap = 12.f;

    ImColor bg = ColorUtils::getUiCardColor(1.0f);
    ImColor border = ColorUtils::getUiBorderColor(25.0f / 255.0f);
    ImColor textColor = ColorUtils::getUiTextColor(1.0f);
    ImColor subTextColor = ColorUtils::getUiTextDimColor(1.0f);
    ImColor accent = ColorUtils::getGuiAccentColor(0);

    ImVec2 iconSz = showIcon ? rlCalcSize(iconFont, fontSize, icon) : ImVec2(0.f, 0.f);
    ImVec2 titleSz = rlCalcSize(textFont, fontSize, title);
    float headerHeight = std::max(iconSz.y, titleSz.y);
    float headerWidth = titleSz.x + (showIcon ? 6.f + iconSz.x : 0.f);

    float lineHeight = rlCalcSize(textFont, fontSize, "Ag").y + 1.0f;

    struct ItemAnim
    {
        float value = 0.f;
        float velocity = 0.f;
        uint64_t lastSeenMs = 0;
    };

    static std::unordered_map<std::string, ItemAnim> itemAnims;
    std::unordered_map<std::string, PlayerEntry> byLower;
    byLower.reserve(renderList.size());
    for (const auto& entry : renderList)
    {
        byLower[StringUtils::toLower(rlStripMcColorCodes(entry.name))] = entry;
    }

    const uint64_t now = NOW;

    for (auto& [key, entry] : byLower)
    {
        if (!itemAnims.contains(key)) itemAnims.emplace(key, ItemAnim{});
        itemAnims[key].lastSeenMs = now;
    }

    for (auto& [key, anim] : itemAnims)
    {
        const bool present = byLower.contains(key);
        const float target = present ? 1.f : 0.f;
        rlSpringTo(anim.value, anim.velocity, target, delta, 170.f, 38.f);
        anim.value = std::clamp(anim.value, 0.f, 1.f);
        if (present && anim.value > 0.995f) anim.value = 1.f;
        if (!present && anim.value < 0.005f) anim.value = 0.f;
    }

    std::vector<std::pair<std::string, PlayerEntry>> drawItems;
    drawItems.reserve(byLower.size());
    for (auto& [key, entry] : byLower)
    {
        if (itemAnims[key].value > 0.001f) drawItems.emplace_back(key, entry);
    }

    std::ranges::sort(drawItems, [](const auto& a, const auto& b) {
        return rlStripMcColorCodes(a.second.name) < rlStripMcColorCodes(b.second.name);
    });

    for (auto it = itemAnims.begin(); it != itemAnims.end();)
    {
        if (!byLower.contains(it->first))
        {
            const bool fullyHidden = it->second.value <= 0.001f;
            const bool stale = (now >= it->second.lastSeenMs) ? (now - it->second.lastSeenMs > 2000) : true;
            const bool almostHidden = it->second.value <= 0.02f;
            if (fullyHidden || (stale && almostHidden))
            {
                it = itemAnims.erase(it);
                continue;
            }
        }
        ++it;
    }

    constexpr size_t kMaxAnimEntries = 256;
    if (itemAnims.size() > kMaxAnimEntries)
    {
        std::vector<std::pair<std::string, uint64_t>> age;
        age.reserve(itemAnims.size());
        for (auto& [k, v] : itemAnims) age.emplace_back(k, v.lastSeenMs);
        std::ranges::sort(age, [](const auto& a, const auto& b) { return a.second < b.second; });
        const size_t extra = itemAnims.size() - kMaxAnimEntries;
        for (size_t i = 0; i < extra && i < age.size(); i++)
        {
            itemAnims.erase(age[i].first);
        }
    }

    float maxLineWidth = headerWidth;
    for (const auto& [key, entry] : drawItems)
    {
        float w = 0.f;
        if (showDots)
        {
            w += dotRadius * 2.f;
            w += dotGapX;
        }
        w += std::min(rlCalcSize(textFont, fontSize, rlStripMcColorCodes(entry.name)).x, maxNameWidth);
        maxLineWidth = std::max(maxLineWidth, w);
    }

    const float listStaggerBase = 0.028f;
    const float gateWindow = 0.18f;
    const float listStagger = std::min(
        listStaggerBase,
        (1.f - gateWindow) / static_cast<float>(std::max<size_t>(1, drawItems.size() > 0 ? (drawItems.size() - 1) : 0))
    );

    float contentHeight = 0.f;
    for (size_t i = 0; i < drawItems.size(); i++)
    {
        const float rawA = itemAnims[drawItems[i].first].value;
        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = rlEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));
        const float aLayout = rlEaseInOut(rawA) * listGate;

        contentHeight += lineHeight * aLayout;
        if (i + 1 < drawItems.size()) contentHeight += lineGap * aLayout;
    }

    const float targetWidth = maxLineWidth + paddingX * 2.f + 18.f;
    const float targetHeight = paddingY * 2.f + headerHeight + (drawItems.empty() ? 0.f : headerGap + contentHeight) + 2.f;

    static float panelWidth = 0.f;
    static float panelHeight = 0.f;
    panelWidth = rlExpApproach(panelWidth, targetWidth, delta, 30.f);
    panelHeight = rlExpApproach(panelHeight, targetHeight, delta, 30.f);
    if (std::abs(panelWidth - targetWidth) < 0.25f) panelWidth = targetWidth;
    if (std::abs(panelHeight - targetHeight) < 0.25f) panelHeight = targetHeight;

    const float panelScale = 0.92f + 0.08f * popAnim;

    if (!wantShow && showAnim < 0.01f && drawItems.empty())
    {
        mElement->mSize = {0.f, 0.f};
        FontHelper::popPrefFont();
        return;
    }

    mElement->mSize = {panelWidth, panelHeight};

    ImVec2 pos = mElement->getPos();
    if (rlIsRightAnchored(mElement->mAnchor)) pos.x -= panelWidth;
    else if (rlIsMiddleXAnchored(mElement->mAnchor)) pos.x -= panelWidth * 0.5f;

    if (rlIsBottomAnchored(mElement->mAnchor)) pos.y -= panelHeight;

    if (mElement->mCentered)
    {
        pos.x -= panelWidth * 0.5f;
        pos.y -= panelHeight * 0.5f;
    }

    ImVec2 min = pos;
    ImVec2 max = {pos.x + panelWidth, pos.y + panelHeight};

    ImVec2 pivot = min;
    if (rlIsRightAnchored(mElement->mAnchor)) pivot.x = max.x;
    else if (rlIsMiddleXAnchored(mElement->mAnchor)) pivot.x = (min.x + max.x) * 0.5f;
    if (rlIsBottomAnchored(mElement->mAnchor)) pivot.y = max.y;
    else pivot.y = min.y;

    ImVec2 scaledMin = {pivot.x + (min.x - pivot.x) * panelScale, pivot.y + (min.y - pivot.y) * panelScale};
    ImVec2 scaledMax = {pivot.x + (max.x - pivot.x) * panelScale, pivot.y + (max.y - pivot.y) * panelScale};
    const float scaledW = scaledMax.x - scaledMin.x;
    const float scaledH = scaledMax.y - scaledMin.y;

    bg.Value.w *= alphaAnim;
    border.Value.w *= alphaAnim;
    textColor.Value.w *= alphaAnim;
    subTextColor.Value.w *= alphaAnim;
    accent.Value.w *= alphaAnim;

    const bool inEditor = HudEditor::gInstance && HudEditor::gInstance->mEnabled;
    const bool hovered = inEditor && ImRenderUtils::isMouseOver(ImVec4(scaledMin.x, scaledMin.y, scaledMax.x, scaledMax.y));

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
    rlAddTextShadowed(drawList, textFont, fontSize, headerPos, textColor, title.c_str());
    if (showIcon)
    {
        rlAddTextShadowedNoShimmer(drawList, iconFont, fontSize, {scaledMax.x - paddingX - iconSz.x, headerPos.y}, accent, icon.c_str());
    }

    float y = headerPos.y + headerHeight + headerGap;
    for (size_t i = 0; i < drawItems.size(); i++)
    {
        const auto& entry = drawItems[i].second;
        const float rawA = itemAnims[drawItems[i].first].value;
        if (rawA <= 0.001f) continue;

        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = rlEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));

        const float aAlpha = std::clamp(rlEaseOutCubic(rawA) * listGate, 0.f, 1.f);
        const float aMoveRaw = rlEaseOutBack(rawA) * listGate;
        const float aMove = std::clamp(aMoveRaw, 0.f, 1.2f);
        const float overshoot = std::max(0.f, aMove - 1.f);
        const float aLayout = rlEaseInOut(rawA) * listGate;

        ImColor nameColor = subTextColor;
        nameColor.Value.w *= aAlpha;

        const float slideT = std::min(aMove, 1.f);
        const float slideX = (1.f - slideT) * -8.f + overshoot * 5.f;
        const float slideY = (1.f - slideT) * -5.f + overshoot * 3.f;
        ImVec2 p = {scaledMin.x + paddingX + slideX, y + slideY};

        if (showDots)
        {
            ImColor dotColor = accent;
            dotColor.Value.w = nameColor.Value.w;
            const float dotR = dotRadius * (0.85f + 0.15f * std::min(aMove, 1.f));
            drawList->AddCircleFilled(ImVec2(p.x + dotR, p.y + lineHeight * 0.55f), dotR, dotColor, 16);
            p.x += dotR * 2.f + dotGapX;
        }

        const float available = scaledMax.x - paddingX - p.x;
        const float clipWidth = std::clamp(std::min(maxNameWidth, available), 0.f, maxNameWidth);
        rlDrawMcColoredTextShadowedClipped(drawList, textFont, fontSize, p, nameColor, entry.name, clipWidth, lineHeight, bg);

        y += lineHeight * aLayout;
        if (i + 1 < drawItems.size()) y += lineGap * aLayout;
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
        {"Иконка", &mShowIcon},
        {"Точки", &mShowDots}
    };

    static float toggleAnim[2]{};
    for (int i = 0; i < 2; i++)
    {
        const float target = items[i].s->mValue ? 1.f : 0.f;
        const float speed = target > 0.5f ? 9.f : 14.f;
        toggleAnim[i] = MathUtils::lerp(toggleAnim[i], target, delta * speed);
        toggleAnim[i] = MathUtils::clamp(toggleAnim[i], 0.f, 1.f);
    }

    const std::string dot = "·";
    const std::string iconA = "d";
    const std::string iconB = "j";

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
        maxLabelW = std::max(maxLabelW, rlCalcSize(textFont, fontSize, it.label).x);
    }
    const std::string* menuIcons[2] = {&iconA, &iconB};
    for (int i = 0; i < 2; i++)
    {
        maxIconW = std::max(maxIconW, rlCalcSize(energyFont ? energyFont : textFont, fontSize, *menuIcons[i]).x);
    }
    const float indicatorShift = indicatorW + 7.f + maxIconW + menuIconGap;
    const float menuTextPad = std::max(4.f, fontSize * 0.35f);
    const float menuW = pad * 2.f + maxLabelW + indicatorShift + menuTextPad;
    const float menuH = pad * 2.f + rowH * 2.f;

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

    const float menuA = rlEaseOutCubic(menuAnim);
    const float menuPopRaw = rlEaseOutBack(menuAnim);
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

        for (int i = 0; i < 2; i++)
        {
            ImVec2 rowMin = ImVec2(drawMin.x + pad * s, drawMin.y + pad * s + rowH * i * s);
            ImVec2 rowMax = ImVec2(drawMax.x - pad * s, rowMin.y + rowH * s);
            const bool rowHovered = inEditor && menuHovered && ImRenderUtils::isMouseOver(ImVec4(rowMin.x, rowMin.y, rowMax.x, rowMax.y));
            if (inEditor && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && rowHovered)
            {
                items[i].s->mValue = !items[i].s->mValue;
            }

            const float t = rlEaseOutCubic(toggleAnim[i]);
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
            const float dotW = rlCalcSize(textFont, fontSize, dot).x;
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
