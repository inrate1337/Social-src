#include "KeyBinds.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_map>

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/Keyboard.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/StringUtils.hpp>
#include <Features/Modules/Visual/Interface.hpp>

static ImVec2 kbCalcSize(ImFont* font, float fontSize, const std::string& text)
{
    if (!font || text.empty()) return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text.c_str());
}

static ImVec2 kbCalcSize(ImFont* font, float fontSize, const char* text)
{
    if (!font || !text || text[0] == '\0') return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);
}

static const char* categoryIcon(ModuleCategory category)
{
    switch (category)
    {
    case ModuleCategory::Combat: return "A";
    case ModuleCategory::Movement: return "c";
    case ModuleCategory::Player: return "b";
    case ModuleCategory::Visual: return "v";
    case ModuleCategory::Misc: return "e";
    default: return "A";
    }
}

static float kbExpApproach(float current, float target, float delta, float speed)
{
    if (delta <= 0.f) return current;
    const float t = 1.f - std::exp(-speed * delta);
    return current + (target - current) * t;
}

static float kbEaseInOut(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static float kbEaseOutCubic(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

static float kbEaseOutBack(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float inv = t - 1.f;
    return 1.f + c3 * inv * inv * inv + c1 * inv * inv;
}

static void kbSpringTo(float& value, float& velocity, float target, float delta, float stiffness, float damping)
{
    if (delta <= 0.f) return;
    const float accel = (target - value) * stiffness;
    velocity += accel * delta;
    velocity *= std::exp(-damping * delta);
    value += velocity * delta;
}

static void kbAddTextShadowed(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor color, const char* text)
{
    if (!drawList || !font || !text || text[0] == '\0') return;
    drawList->AddText(font, fontSize, pos, color, text);
}

static bool kbIsRightAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopRight || anchor == HudElement::Anchor::MiddleRight || anchor == HudElement::Anchor::BottomRight;
}

static bool kbIsMiddleXAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopMiddle || anchor == HudElement::Anchor::Middle || anchor == HudElement::Anchor::BottomMiddle;
}

static bool kbIsBottomAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::BottomLeft || anchor == HudElement::Anchor::BottomMiddle || anchor == HudElement::Anchor::BottomRight;
}

void KeyBinds::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &KeyBinds::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &KeyBinds::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = true;
}

void KeyBinds::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &KeyBinds::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &KeyBinds::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = false;

    std::scoped_lock lock(mMutex);
    mBinds.clear();
}

void KeyBinds::onBaseTickEvent(BaseTickEvent& event)
{
    if (!mEnabled) return;
    if (!gFeatureManager || !gFeatureManager->mModuleManager) return;

    std::vector<BindEntry> entries;
    for (const auto& mod : gFeatureManager->mModuleManager->getModules())
    {
        if (!mod) continue;
        if (!mod->mEnabled) continue;
        if (mod->mKey == 0) continue;

        BindEntry e;
        e.moduleName = mod->getName();
        e.key = mod->mKey;
        e.keyName = Keyboard::getKey(mod->mKey);
        e.category = mod->mCategory;
        entries.emplace_back(std::move(e));
    }

    std::ranges::sort(entries, [](const BindEntry& a, const BindEntry& b) {
        return a.moduleName < b.moduleName;
    });

    {
        std::scoped_lock lock(mMutex);
        mBinds.swap(entries);
    }
}

void KeyBinds::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!mElement) return;
    if (ci->getScreenName() != "hud_screen" && !mElement->mSampleMode) return;

    const float delta = ImGui::GetIO().DeltaTime;

    std::vector<BindEntry> renderList;
    {
        std::scoped_lock lock(mMutex);
        renderList = mBinds;
    }

    if (renderList.empty() && mElement->mSampleMode)
    {
        renderList = {
            {"Fly", 'F', "F", ModuleCategory::Movement},
            {"ClickGui", VK_TAB, "TAB", ModuleCategory::Visual},
            {"Scaffold", 'V', "V", ModuleCategory::Player}
        };
    }

    static float showAnim = 0.f;
    static float showVel = 0.f;
    const bool wantShow = true;
    kbSpringTo(showAnim, showVel, wantShow ? 1.f : 0.f, delta, 130.f, 30.f);
    showAnim = MathUtils::clamp(showAnim, 0.f, 1.f);
    const float alphaAnim = kbEaseOutCubic(showAnim);
    const float popAnimRaw = kbEaseOutBack(showAnim);
    const float popAnim = std::clamp(popAnimRaw, 0.f, 1.2f);

    FontHelper::pushPrefFont(false, true);

    auto* textFont = ImGui::GetFont();
    ImFont* categoryIconFont = textFont;
    if (auto it = FontHelper::Fonts.find("essence.ttf"); it != FontHelper::Fonts.end() && it->second)
    {
        categoryIconFont = it->second;
    }
    ImFont* combatCategoryIconFont = categoryIconFont;
    if (auto it = FontHelper::Fonts.find("clickgui.ttf"); it != FontHelper::Fonts.end() && it->second)
    {
        combatCategoryIconFont = it->second;
    }

    ImFont* headerIconFont = textFont;
    if (auto it = FontHelper::Fonts.find("essence.ttf"); it != FontHelper::Fonts.end() && it->second)
    {
        headerIconFont = it->second;
    }

    const float fontSize = 18.f;
    const float categoryIconFontSize = fontSize * 0.7f;
    const float paddingX = 10.f;
    const float paddingY = 8.f;
    const float rounding = 8.f;
    const float lineGap = 4.f;
    const float headerGap = 12.f;
    const float dotRadius = 2.9f;
    const float dotGapX = 7.f;
    const float keyGapX = 12.f;
    const float categoryGapX = 4.f;

    const std::string title = "KeyBinds";
    const std::string icon = "i";

    ImColor bg = ColorUtils::getUiCardColor(1.0f);
    ImColor border = ColorUtils::getUiBorderColor(16.0f / 255.0f);
    ImColor textColor = ColorUtils::getUiTextColor(1.0f);
    ImColor subTextColor = ColorUtils::getUiTextDimColor(230.0f / 255.0f);
    ImColor accent = ColorUtils::getGuiAccentColor(0);
    const bool showCategoryIcons = mShowCategoryIcons.mValue;

    ImVec2 iconSz = kbCalcSize(headerIconFont, fontSize, icon);
    ImVec2 titleSz = kbCalcSize(textFont, fontSize, title);
    float headerHeight = std::max(iconSz.y, titleSz.y);
    float headerWidth = titleSz.x + 6.f + iconSz.x;

    float lineHeight = kbCalcSize(textFont, fontSize, "A").y;

    struct ItemAnim
    {
        float value = 0.f;
        float velocity = 0.f;
    };

    static std::unordered_map<std::string, ItemAnim> itemAnims;
    std::unordered_map<std::string, BindEntry> byLower;
    byLower.reserve(renderList.size());
    for (const auto& entry : renderList)
    {
        byLower[StringUtils::toLower(entry.moduleName)] = entry;
    }

    for (auto& [key, entry] : byLower)
    {
        if (!itemAnims.contains(key)) itemAnims.emplace(key, ItemAnim{});
    }

    for (auto& [key, anim] : itemAnims)
    {
        const bool present = byLower.contains(key);
        const float target = present ? 1.f : 0.f;
        kbSpringTo(anim.value, anim.velocity, target, delta, 170.f, 38.f);
        anim.value = std::clamp(anim.value, 0.f, 1.f);
        if (present && anim.value > 0.995f) anim.value = 1.f;
        if (!present && anim.value < 0.005f) anim.value = 0.f;
    }

    std::vector<std::pair<std::string, BindEntry>> drawItems;
    drawItems.reserve(byLower.size());
    for (auto& [key, entry] : byLower)
    {
        if (itemAnims[key].value > 0.001f) drawItems.emplace_back(key, entry);
    }

    std::ranges::sort(drawItems, [](const auto& a, const auto& b) {
        return a.second.moduleName < b.second.moduleName;
    });

    for (auto it = itemAnims.begin(); it != itemAnims.end();)
    {
        if (it->second.value <= 0.001f && !byLower.contains(it->first)) it = itemAnims.erase(it);
        else ++it;
    }

    float maxLineWidth = headerWidth;
    for (const auto& [key, entry] : drawItems)
    {
        float w = 0.f;
        w += dotRadius * 2.f;
        w += dotGapX;
        w += kbCalcSize(textFont, fontSize, entry.moduleName).x;
        w += keyGapX;
        w += kbCalcSize(textFont, fontSize, entry.keyName).x;
        if (showCategoryIcons)
        {
            ImFont* catFont = (entry.category == ModuleCategory::Combat) ? combatCategoryIconFont : categoryIconFont;
            const char* catIcon = categoryIcon(entry.category);
            w += categoryGapX;
            w += kbCalcSize(catFont, categoryIconFontSize, catIcon).x;
        }
        maxLineWidth = std::max(maxLineWidth, w);
    }

    float contentHeight = 0.f;
    for (size_t i = 0; i < drawItems.size(); i++)
    {
        const float rawA = itemAnims[drawItems[i].first].value;
        const float listStagger = 0.028f;
        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = kbEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));
        const float aLayout = kbEaseInOut(rawA) * listGate;

        contentHeight += lineHeight * aLayout;
        if (i + 1 < drawItems.size()) contentHeight += lineGap * aLayout;
    }

    const float targetWidth = maxLineWidth + paddingX * 2.f + 18.f;
    const float targetHeight = paddingY * 2.f + headerHeight + (drawItems.empty() ? 0.f : headerGap + contentHeight);

    static float panelWidth = 0.f;
    static float panelHeight = 0.f;
    panelWidth = kbExpApproach(panelWidth, targetWidth, delta, 30.f);
    panelHeight = kbExpApproach(panelHeight, targetHeight, delta, 30.f);
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
    if (kbIsRightAnchored(mElement->mAnchor)) pos.x -= panelWidth;
    else if (kbIsMiddleXAnchored(mElement->mAnchor)) pos.x -= panelWidth * 0.5f;

    if (kbIsBottomAnchored(mElement->mAnchor)) pos.y -= panelHeight;

    if (mElement->mCentered)
    {
        pos.x -= panelWidth * 0.5f;
        pos.y -= panelHeight * 0.5f;
    }

    ImVec2 min = pos;
    ImVec2 max = {pos.x + panelWidth, pos.y + panelHeight};

    ImVec2 pivot = min;
    if (kbIsRightAnchored(mElement->mAnchor)) pivot.x = max.x;
    else if (kbIsMiddleXAnchored(mElement->mAnchor)) pivot.x = (min.x + max.x) * 0.5f;
    if (kbIsBottomAnchored(mElement->mAnchor)) pivot.y = max.y;
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
        ImColor outline = ColorUtils::getUiBorderColor(42.0f / 255.0f);
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
    kbAddTextShadowed(drawList, textFont, fontSize, headerPos, textColor, title.c_str());
    kbAddTextShadowed(drawList, headerIconFont, fontSize, {scaledMax.x - paddingX - iconSz.x, headerPos.y}, accent, icon.c_str());

    float y = headerPos.y + headerHeight + headerGap;
    for (size_t i = 0; i < drawItems.size(); i++)
    {
        const auto& entry = drawItems[i].second;
        const float rawA = itemAnims[drawItems[i].first].value;
        if (rawA <= 0.001f) continue;

        const float listStagger = 0.028f;
        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = kbEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));

        const float aAlpha = std::clamp(kbEaseOutCubic(rawA) * listGate, 0.f, 1.f);
        const float aMoveRaw = kbEaseOutBack(rawA) * listGate;
        const float aMove = std::clamp(aMoveRaw, 0.f, 1.2f);
        const float overshoot = std::max(0.f, aMove - 1.f);
        const float aLayout = kbEaseInOut(rawA) * listGate;

        ImColor nameColor = subTextColor;
        nameColor.Value.w *= aAlpha;

        ImColor keyColor = accent;
        keyColor.Value.w *= aAlpha;

        const float slideT = std::min(aMove, 1.f);
        const float slideX = (1.f - slideT) * -8.f + overshoot * 5.f;
        const float slideY = (1.f - slideT) * -5.f + overshoot * 3.f;
        ImVec2 p = {scaledMin.x + paddingX + slideX, y + slideY};

        ImColor dotColor = accent;
        dotColor.Value.w = nameColor.Value.w;
        const float dotR = dotRadius * (0.85f + 0.15f * std::min(aMove, 1.f));
        drawList->AddCircleFilled(ImVec2(p.x + dotR, p.y + lineHeight * 0.55f), dotR, dotColor, 16);
        p.x += dotR * 2.f + dotGapX;

        kbAddTextShadowed(drawList, textFont, fontSize, p, nameColor, entry.moduleName.c_str());
        const float nameW = kbCalcSize(textFont, fontSize, entry.moduleName).x;

        const float keyW = kbCalcSize(textFont, fontSize, entry.keyName).x;
        const char* catIcon = categoryIcon(entry.category);
        ImFont* catFont = (entry.category == ModuleCategory::Combat) ? combatCategoryIconFont : categoryIconFont;
        const float catW = showCategoryIcons ? kbCalcSize(catFont, categoryIconFontSize, catIcon).x : 0.f;
        const float totalRightW = showCategoryIcons ? (keyW + categoryGapX + catW) : keyW;

        float keyX = (scaledMax.x - paddingX - totalRightW) + slideX;
        const float minKeyX = p.x + nameW + keyGapX;
        if (keyX < minKeyX) keyX = minKeyX;

        kbAddTextShadowed(drawList, textFont, fontSize, ImVec2(keyX, p.y), keyColor, entry.keyName.c_str());
        if (showCategoryIcons)
        {
            ImColor catColor = accent;
            catColor.Value.w *= 0.65f * aAlpha;
            const float catH = kbCalcSize(catFont, categoryIconFontSize, catIcon).y;
            const float catY = p.y + (lineHeight - catH) * 0.5f;
            kbAddTextShadowed(drawList, catFont, categoryIconFontSize, ImVec2(keyX + keyW + categoryGapX, catY), catColor, catIcon);
        }

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
        {"Иконки категорий", &mShowCategoryIcons}
    };

    static float toggleAnim[1]{};
    for (int i = 0; i < 1; i++)
    {
        const float target = items[i].s->mValue ? 1.f : 0.f;
        const float speed = target > 0.5f ? 9.f : 14.f;
        toggleAnim[i] = MathUtils::lerp(toggleAnim[i], target, delta * speed);
        toggleAnim[i] = MathUtils::clamp(toggleAnim[i], 0.f, 1.f);
    }

    const std::string dot = "·";
    const std::string iconCats = "i";

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
        maxLabelW = std::max(maxLabelW, kbCalcSize(textFont, fontSize, it.label).x);
    }
    const std::string* menuIcons[1] = {&iconCats};
    for (int i = 0; i < 1; i++)
    {
        maxIconW = std::max(maxIconW, kbCalcSize(energyFont ? energyFont : textFont, fontSize, *menuIcons[i]).x);
    }
    const float indicatorShift = indicatorW + 7.f + maxIconW + menuIconGap;
    const float menuTextPad = std::max(4.f, fontSize * 0.35f);
    const float menuW = pad * 2.f + maxLabelW + indicatorShift + menuTextPad;
    const float menuH = pad * 2.f + rowH * 1.f;

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

    const float menuA = kbEaseOutCubic(menuAnim);
    const float menuPopRaw = kbEaseOutBack(menuAnim);
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

        for (int i = 0; i < 1; i++)
        {
            ImVec2 rowMin = ImVec2(drawMin.x + pad * s, drawMin.y + pad * s + rowH * i * s);
            ImVec2 rowMax = ImVec2(drawMax.x - pad * s, rowMin.y + rowH * s);
            const bool rowHovered = inEditor && menuHovered && ImRenderUtils::isMouseOver(ImVec4(rowMin.x, rowMin.y, rowMax.x, rowMax.y));
            if (inEditor && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && rowHovered)
            {
                items[i].s->mValue = !items[i].s->mValue;
            }

            const float t = kbEaseOutCubic(toggleAnim[i]);
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
            const float dotW = kbCalcSize(textFont, fontSize, dot).x;
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
