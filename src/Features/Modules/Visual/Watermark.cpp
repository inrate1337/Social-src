#include "Watermark.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <Features/Modules/Visual/Interface.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>

static bool wmIsRightAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopRight || anchor == HudElement::Anchor::MiddleRight || anchor == HudElement::Anchor::BottomRight;
}

static bool wmIsMiddleXAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopMiddle || anchor == HudElement::Anchor::Middle || anchor == HudElement::Anchor::BottomMiddle;
}

static bool wmIsBottomAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::BottomLeft || anchor == HudElement::Anchor::BottomMiddle || anchor == HudElement::Anchor::BottomRight;
}

static ImVec2 wmGetElementTopLeft(const HudElement* element, ImVec2 size)
{
    ImVec2 pos = element->getPos();

    if (element->mCentered)
    {
        pos.x -= size.x * 0.5f;
        pos.y -= size.y * 0.5f;
        return pos;
    }

    if (wmIsRightAnchored(element->mAnchor)) pos.x -= size.x;
    else if (wmIsMiddleXAnchored(element->mAnchor)) pos.x -= size.x * 0.5f;

    if (wmIsBottomAnchored(element->mAnchor)) pos.y -= size.y;

    return pos;
}

static float wmEaseOutCubic(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

static float wmEaseOutBack(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float inv = t - 1.f;
    return 1.f + c3 * inv * inv * inv + c1 * inv * inv;
}

void Watermark::onEnable()
{
    mElement->mVisible = true;
}

void Watermark::onDisable()
{
    mElement->mVisible = false;
}

void Watermark::onPingUpdateEvent(PingUpdateEvent& event)
{
    mPing = event.mPing;
}

void Watermark::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!ci->getLocalPlayer()) return;
    static float anim = 0.f;
    const bool sampleMode = mElement && mElement->mSampleMode;
    const bool wantVisible = mEnabled || sampleMode;
    anim = MathUtils::lerp(anim, wantVisible ? 1.f : 0.f, ImGui::GetIO().DeltaTime * 7.f);
    anim = MathUtils::clamp(anim, 0.f, 1.f);

    if (anim < 0.01f) return;

    if (!mElement) return;

    if (mStyle.mValue == Style::SevenDays)
    {
        static std::string filePath = "seven_days.png";
        static ID3D11ShaderResourceView* texture;
        static bool loaded = false;
        static int width, height;
        if (!loaded)
        {
            D3DHook::loadTextureFromEmbeddedResource(filePath.c_str(), &texture, &width, &height);
            loaded = true;
            width /= 10;
            height /= 10;
        }

        const ImVec2 size = ImVec2(static_cast<float>(width), static_cast<float>(height));
        mElement->mSize = { size.x, size.y };
        const ImVec2 topLeftTarget = wmGetElementTopLeft(mElement.get(), size);
        ImVec2 renderPosition = {
            MathUtils::lerp(-200.f, topLeftTarget.x, anim),
            MathUtils::lerp(-200.f, topLeftTarget.y, anim)
        };

        ImGui::GetBackgroundDrawList()->AddImage(texture, renderPosition, ImVec2(renderPosition.x + width, renderPosition.y + height));
        return;
    }

    if (!gFeatureManager || !gFeatureManager->mModuleManager) return;

    FontHelper::pushPrefFont();

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    auto* interfaceModule = gFeatureManager && gFeatureManager->mModuleManager ? gFeatureManager->mModuleManager->getModule<Interface>() : nullptr;
    const bool useHudBlur = interfaceModule && interfaceModule->mHudBlur.mValue;
    const float blurStrength = useHudBlur ? interfaceModule->mHudBlurStrength.mValue : 0.f;

    const float dt = ImGui::GetIO().DeltaTime;
    const float textFontSize = 18.f;
    const float paddingX = 12.f;
    const float paddingY = 8.f;
    const float blockGap = 8.f;
    const float logoBoxSize = 36.f;
    const float rounding = 8.f;
    const float borderThickness = 1.6f;

    if (mLastStatUpdate == 0 || NOW - mLastStatUpdate >= 3000)
    {
        mFpsDisplay = static_cast<int>(ImGui::GetIO().Framerate + 0.5f);
        mPingDisplay = mPing < 0 ? 0 : mPing;
        mLastStatUpdate = NOW;
    }

#ifdef __PRIVATE_BUILD__
    const char* branchName = "NIGHTLY";
#else
    const char* branchName = "Free";
#endif

    auto* textFont = ImGui::GetFont();
    ImFont* energyFont = nullptr;
    {
        auto it = FontHelper::Fonts.find("essence.ttf");
        if (it != FontHelper::Fonts.end() && it->second) energyFont = it->second;
    }
    if (!energyFont) energyFont = textFont;

    const std::string branchText = branchName;
    const std::string fpsText = std::to_string(mFpsDisplay) + "fps";
    const std::string pingText = std::to_string(static_cast<long long>(mPingDisplay)) + "ms";
    const std::string sepPad = "    ";
    const std::string dot = "·";
    const std::string space = " ";

    const std::string iconPrivate = "b";
    const std::string iconFps = "m";
    const std::string iconPing = "l";
    const std::string iconCoords = "j";

    std::string coordsText = "N/A";
    if (auto ci = ClientInstance::get())
    {
        if (auto player = ci->getLocalPlayer())
        {
            if (auto pos = player->getPos())
            {
                std::ostringstream oss;
                oss.setf(std::ios::fixed, std::ios::floatfield);
                oss << std::setprecision(1) << pos->x << " " << pos->y << " " << pos->z;
                coordsText = oss.str();
            }
        }
    }

    auto calcSize = [&](ImFont* font, const std::string& s) -> ImVec2 {
        if (!font || s.empty()) return ImVec2(0.f, 0.f);
        return font->CalcTextSizeA(textFontSize, FLT_MAX, 0.0f, s.c_str(), nullptr);
    };

    const ImVec2 szSepPad = calcSize(textFont, sepPad);
    const ImVec2 szDot = calcSize(textFont, dot);
    const ImVec2 szSpace = calcSize(textFont, space);
    const float sepW = (szSepPad.x * 2.f) + szDot.x;

    static float segAnim[3]{};
    static float coordsAnim = 0.f;
    {
        const bool targets[3] = {mShowName.mValue, mShowFps.mValue, mShowPing.mValue};
        for (int i = 0; i < 3; i++)
        {
            const float speed = targets[i] ? 22.f : 34.f;
            segAnim[i] = MathUtils::lerp(segAnim[i], targets[i] ? 1.f : 0.f, dt * speed);
            segAnim[i] = MathUtils::clamp(segAnim[i], 0.f, 1.f);
        }
        {
            const float speed = mShowCoords.mValue ? 22.f : 34.f;
            coordsAnim = MathUtils::lerp(coordsAnim, mShowCoords.mValue ? 1.f : 0.f, dt * speed);
        }
        coordsAnim = MathUtils::clamp(coordsAnim, 0.f, 1.f);
    }

    const float tSeg[3] = {
        wmEaseOutCubic(segAnim[0]),
        wmEaseOutCubic(segAnim[1]),
        wmEaseOutCubic(segAnim[2])
    };
    const float tCoords = wmEaseOutCubic(coordsAnim);

    float fullTextWidth = 0.f;
    float fullTextHeight = 0.f;
    {
        ImVec2 iconSizes[3] = {
            calcSize(energyFont, iconPrivate),
            calcSize(energyFont, iconFps),
            calcSize(energyFont, iconPing)
        };
        ImVec2 textSizes[3] = {
            calcSize(textFont, branchText),
            calcSize(textFont, fpsText),
            calcSize(textFont, pingText)
        };

        for (int i = 0; i < 3; i++)
        {
            if (tSeg[i] <= 0.001f) continue;
            const float segW = iconSizes[i].x + szSpace.x + textSizes[i].x;
            fullTextWidth += segW * tSeg[i];
            fullTextHeight = std::max(fullTextHeight, std::max(iconSizes[i].y, textSizes[i].y));
        }

        for (int i = 0; i < 3; i++)
        {
            if (tSeg[i] <= 0.001f) continue;
            int next = -1;
            for (int j = i + 1; j < 3; j++)
            {
                if (tSeg[j] > 0.001f)
                {
                    next = j;
                    break;
                }
            }
            if (next != -1) fullTextWidth += sepW * std::min(tSeg[i], tSeg[next]);
        }
    }

    const float infoRowT = std::max(tSeg[0], std::max(tSeg[1], tSeg[2]));
    const bool showInfoRow = infoRowT > 0.001f;
    const float infoPadX = paddingX * infoRowT;
    const float infoBlockWidth = fullTextWidth + infoPadX * 2.f;
    const float infoBlockHeight = logoBoxSize;

    const float topRowWidth = logoBoxSize + (showInfoRow ? ((blockGap * infoRowT) + infoBlockWidth) : 0.f);
    const float topRowHeight = logoBoxSize;

    const ImVec2 szIconCoords = calcSize(energyFont, iconCoords);
    const ImVec2 szCoords = calcSize(textFont, coordsText);
    const float coordsTextWidth = szIconCoords.x + szSpace.x + szCoords.x;
    const float coordsTextHeight = std::max(szIconCoords.y, szCoords.y);
    const float coordsWidth = coordsTextWidth + paddingX * 2.f;
    const float coordsPaddingY = 6.f;
    const float coordsHeight = coordsTextHeight + coordsPaddingY * 2.f;

    const bool showCoordsRow = tCoords > 0.001f;

    const float elementWidthTarget = std::max(topRowWidth, (showCoordsRow ? coordsWidth : 0.f));
    const float elementHeightTarget = topRowHeight + (showCoordsRow ? (blockGap + coordsHeight) : 0.f);

    static bool elementSizeInit = false;
    static float elementWidthSmoothed = 0.f;
    static float elementHeightSmoothed = 0.f;
    if (!elementSizeInit)
    {
        elementWidthSmoothed = elementWidthTarget;
        elementHeightSmoothed = elementHeightTarget;
        elementSizeInit = true;
    }
    elementWidthSmoothed = MathUtils::lerp(elementWidthSmoothed, elementWidthTarget, dt * 14.f);
    elementHeightSmoothed = MathUtils::lerp(elementHeightSmoothed, elementHeightTarget, dt * 14.f);

    const float elementWidth = elementWidthSmoothed;
    const float elementHeight = elementHeightSmoothed;

    mElement->mSize = { elementWidth, elementHeight };

    const bool inEditor = HudEditor::gInstance && HudEditor::gInstance->mEnabled;
    const ImVec2 elementTopLeftTarget = wmGetElementTopLeft(mElement.get(), ImVec2(elementWidth, elementHeight));
    static ImVec2 elementTopLeftTargetSmoothed = elementTopLeftTarget;
    elementTopLeftTargetSmoothed.x = MathUtils::lerp(elementTopLeftTargetSmoothed.x, elementTopLeftTarget.x, dt * 14.f);
    elementTopLeftTargetSmoothed.y = MathUtils::lerp(elementTopLeftTargetSmoothed.y, elementTopLeftTarget.y, dt * 14.f);
    const ImVec2 elementTopLeft = {
        MathUtils::lerp(-200.f, elementTopLeftTargetSmoothed.x, anim),
        MathUtils::lerp(-200.f, elementTopLeftTargetSmoothed.y, anim)
    };
    const ImVec2 elementMax = ImVec2(elementTopLeft.x + elementWidth, elementTopLeft.y + elementHeight);

    static float hoverAnim = 0.f;
    const bool hovered = inEditor && ImRenderUtils::isMouseOver(ImVec4(elementTopLeft.x, elementTopLeft.y, elementMax.x, elementMax.y));
    hoverAnim = MathUtils::lerp(hoverAnim, hovered ? 1.f : 0.f, ImGui::GetIO().DeltaTime * 12.f);
    hoverAnim = MathUtils::clamp(hoverAnim, 0.f, 1.f);

    ImColor backgroundColor = ColorUtils::getUiCardColor(1.0f);
    ImColor borderColor = ColorUtils::getUiBorderColor(16.0f / 255.0f);
    ImColor backgroundHover = (ColorUtils::getUiTheme() == ColorUtils::UiTheme::Light) ? ImColor(240, 240, 246, 255) : ImColor(26, 25, 32, 255);
    ImColor borderHover = (ColorUtils::getUiTheme() == ColorUtils::UiTheme::Light) ? ImColor(130, 130, 150, 64) : ImColor(255, 255, 255, 42);
    backgroundColor.Value = ImLerp(backgroundColor.Value, backgroundHover.Value, hoverAnim);
    borderColor.Value = ImLerp(borderColor.Value, borderHover.Value, hoverAnim);
    backgroundColor.Value.w *= anim;
    borderColor.Value.w *= anim;

    const ImVec2 topRowMinTarget = ImVec2(elementTopLeftTargetSmoothed.x + (elementWidth - topRowWidth) * 0.5f, elementTopLeftTargetSmoothed.y);
    ImVec2 topRowMin = {
        MathUtils::lerp(-200.f, topRowMinTarget.x, anim),
        MathUtils::lerp(-200.f, topRowMinTarget.y, anim)
    };

    const ImVec2 logoMin = topRowMin;
    const ImVec2 logoMax = ImVec2(topRowMin.x + logoBoxSize, topRowMin.y + logoBoxSize);

    {
        ImColor logoBgCol = backgroundColor;
        ImColor logoBorderCol = borderColor;
        const float logoShadowA = 170.f * anim;

        const float bgScale = 0.94f + 0.06f * wmEaseOutCubic(anim);
        ImVec2 logoCenter = ImVec2((logoMin.x + logoMax.x) * 0.5f, (logoMin.y + logoMax.y) * 0.5f);
        ImVec2 logoDrawMin = ImVec2(logoCenter.x + (logoMin.x - logoCenter.x) * bgScale, logoCenter.y + (logoMin.y - logoCenter.y) * bgScale);
        ImVec2 logoDrawMax = ImVec2(logoCenter.x + (logoMax.x - logoCenter.x) * bgScale, logoCenter.y + (logoMax.y - logoCenter.y) * bgScale);
        if (useHudBlur)
        {
            ImRenderUtils::addBlurAlpha(ImVec4(logoDrawMin.x, logoDrawMin.y, logoDrawMax.x, logoDrawMax.y), blurStrength, logoBgCol.Value.w, rounding, drawList, true);
            logoBgCol.Value.w *= 0.85f;
        }
        drawList->AddRectFilled(logoDrawMin, logoDrawMax, logoBgCol, rounding);
        drawList->AddShadowRect(logoDrawMin, logoDrawMax, IM_COL32(0, 0, 0, static_cast<int>(logoShadowA)), 55.0f, ImVec2(0.f, 0.f), 0, rounding);
        drawList->AddRect(logoDrawMin, logoDrawMax, logoBorderCol, rounding, 0, borderThickness);
    }

    ImVec2 infoMin{};
    ImVec2 infoMax{};
    if (showInfoRow)
    {
        ImColor infoBgCol = backgroundColor;
        ImColor infoBorderCol = borderColor;
        infoBgCol.Value.w *= infoRowT;
        infoBorderCol.Value.w *= infoRowT;
        const float infoShadowA = 170.f * anim * infoRowT;

        infoMin = ImVec2(logoMax.x + (blockGap * infoRowT), topRowMin.y);
        infoMax = ImVec2(infoMin.x + infoBlockWidth, infoMin.y + infoBlockHeight);

        const float bgScale = 0.94f + 0.06f * wmEaseOutCubic(anim);
        ImVec2 infoCenter = ImVec2((infoMin.x + infoMax.x) * 0.5f, (infoMin.y + infoMax.y) * 0.5f);
        ImVec2 infoDrawMin = ImVec2(infoCenter.x + (infoMin.x - infoCenter.x) * bgScale, infoCenter.y + (infoMin.y - infoCenter.y) * bgScale);
        ImVec2 infoDrawMax = ImVec2(infoCenter.x + (infoMax.x - infoCenter.x) * bgScale, infoCenter.y + (infoMax.y - infoCenter.y) * bgScale);
        if (useHudBlur)
        {
            ImRenderUtils::addBlurAlpha(ImVec4(infoDrawMin.x, infoDrawMin.y, infoDrawMax.x, infoDrawMax.y), blurStrength, infoBgCol.Value.w, rounding, drawList, true);
            infoBgCol.Value.w *= 0.85f;
        }
        drawList->AddRectFilled(infoDrawMin, infoDrawMax, infoBgCol, rounding);
        {
            ImColor glow = ColorUtils::getGuiAccentColor(0);
            glow.Value.w = 0.40f * anim * infoRowT;
            const float glowBaseH = infoBlockHeight * 0.5f;
            const float glowWidth = (infoMax.x - infoMin.x) * 0.5f;
            const float glowX = infoMin.x + ((infoMax.x - infoMin.x) - glowWidth) * 0.5f;
            const float glowLift = glowBaseH * 1.f;
            const ImVec2 glowMin = ImVec2(glowX, infoMin.y - glowLift);
            const ImVec2 glowMax = ImVec2(glowX + glowWidth, infoMin.y + glowBaseH - glowLift);
            drawList->PushClipRect(infoDrawMin, infoDrawMax, true);
            drawList->AddShadowRect(glowMin, glowMax, glow, 45.0f, ImVec2(0.f, 0.f), 0, rounding);
            drawList->PopClipRect();
        }
        drawList->AddShadowRect(infoDrawMin, infoDrawMax, IM_COL32(0, 0, 0, static_cast<int>(infoShadowA)), 55.0f, ImVec2(0.f, 0.f), 0, rounding);
        drawList->AddRect(infoDrawMin, infoDrawMax, infoBorderCol, rounding, 0, borderThickness);
    }

    const float infoTabHeight = 3.f;

    ImColor tabColor = ColorUtils::getGuiAccentColor(0);
    tabColor.Value.w = 1.f;

    {
        static std::string filePath = "notch.png"; //me replace this logo coming soon
        static ID3D11ShaderResourceView* texture = nullptr;
        static bool loaded = false;
        static int texW = 0;
        static int texH = 0;
        if (!loaded)
        {
            D3DHook::loadTextureFromEmbeddedResource(filePath.c_str(), &texture, &texW, &texH);
            loaded = true;
        }

        const float iconPad = 6.f;
        ImVec2 iconMin = ImVec2(logoMin.x + iconPad, logoMin.y + iconPad);
        ImVec2 iconMax = ImVec2(logoMax.x - iconPad, logoMax.y - iconPad);

        float iconW = iconMax.x - iconMin.x;
        float iconH = iconMax.y - iconMin.y;
        float iconSize = (iconW < iconH) ? iconW : iconH;
        iconMin = ImVec2(logoMin.x + (logoBoxSize - iconSize) * 0.5f, logoMin.y + (logoBoxSize - iconSize) * 0.5f);
        iconMax = ImVec2(iconMin.x + iconSize, iconMin.y + iconSize);

        ImVec2 iconCenter = ImVec2(iconMin.x + iconSize * 0.5f, iconMin.y + iconSize * 0.5f);
        const float iconScale = 0.90f + 0.10f * wmEaseOutCubic(anim);
        iconMin = ImVec2(iconCenter.x + (iconMin.x - iconCenter.x) * iconScale, iconCenter.y + (iconMin.y - iconCenter.y) * iconScale);
        iconMax = ImVec2(iconCenter.x + (iconMax.x - iconCenter.x) * iconScale, iconCenter.y + (iconMax.y - iconCenter.y) * iconScale);
        iconSize *= iconScale;

        const float logoNudgeX = 1.0f;
        const float logoNudgeY = -1.0f;
        iconMin.x += logoNudgeX;
        iconMax.x += logoNudgeX;
        iconMin.y += logoNudgeY;
        iconMax.y += logoNudgeY;
        iconCenter.x += logoNudgeX;
        iconCenter.y += logoNudgeY;

        ImColor glowColor = ColorUtils::getGuiAccentColor(0);
        drawList->AddShadowCircle(iconCenter, iconSize * 0.24f, ImColor(glowColor.Value.x, glowColor.Value.y, glowColor.Value.z, anim), 40, ImVec2(0.f, 0.f), 0, 64);

        if (texture)
        {
            ImColor tint = ColorUtils::getGuiAccentColor(0);
            tint.Value.w = anim;
            drawList->AddImage(texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), ImGui::ColorConvertFloat4ToU32(tint.Value));
        }
    }

    if (showInfoRow)
    {
        const float bgScale = 0.94f + 0.06f * wmEaseOutCubic(anim);
        ImVec2 infoCenter = ImVec2((infoMin.x + infoMax.x) * 0.5f, (infoMin.y + infoMax.y) * 0.5f);
        ImVec2 infoDrawMin = ImVec2(infoCenter.x + (infoMin.x - infoCenter.x) * bgScale, infoCenter.y + (infoMin.y - infoCenter.y) * bgScale);
        ImVec2 infoDrawMax = ImVec2(infoCenter.x + (infoMax.x - infoCenter.x) * bgScale, infoCenter.y + (infoMax.y - infoCenter.y) * bgScale);

        float textY = infoDrawMin.y + ((infoDrawMax.y - infoDrawMin.y) - fullTextHeight) * 0.5f;
        ImVec2 textPos = ImVec2(infoDrawMin.x + infoPadX, textY);

        const std::string* icons[3] = {&iconPrivate, &iconFps, &iconPing};
        const std::string* texts[3] = {&branchText, &fpsText, &pingText};

        ImColor accentBase = ColorUtils::getGuiAccentColor(0);
        ImColor textBase = ColorUtils::getUiTextColor(1.0f);
        ImColor dotBase = ColorUtils::getUiTextDimColor(1.0f);
        accentBase.Value.w *= anim;
        textBase.Value.w *= anim;
        dotBase.Value.w *= anim;

        auto drawChunk = [&](ImFont* font, const std::string& s, ImColor col, float factor) {
            if (s.empty()) return;
            factor = std::clamp(factor, 0.f, 1.f);
            col.Value.w *= factor;
            if (col.Value.w > 0.0001f) drawList->AddText(font ? font : textFont, textFontSize, textPos, col, s.c_str());
            textPos.x += calcSize(font ? font : textFont, s).x * factor;
        };

        auto drawNightlyGradient = [&](const std::string& s, float factor) {
            if (s.empty()) return;
            factor = std::clamp(factor, 0.f, 1.f);
            if (factor <= 0.0001f) return;

            const float totalW = calcSize(textFont, s).x;
            if (totalW <= 0.0001f) return;

            const float alpha = anim * factor;
            if (alpha <= 0.0001f) return;

            const float t = static_cast<float>(ImGui::GetTime());
            ImColor accent = ColorUtils::getGuiAccentColor(0);
            ImVec4 accentV = accent.Value;
            accentV.w = alpha;
            const ImVec4 white = ImVec4(1.0f, 1.0f, 1.0f, alpha);

            float x = textPos.x;
            for (char c : s)
            {
                char buf[2] = { c, 0 };
                const float cw = calcSize(textFont, buf).x;
                const float cx = (x - textPos.x) + (cw * 0.5f);
                float u = (cx / totalW) + (t * 0.65f);
                u = u - std::floor(u);
                const float wave = 0.5f + 0.5f * std::sin(u * 6.2831853f);
                const float w = wave * wave * (3.0f - 2.0f * wave);
                const float mix = w * 0.55f;

                ImVec4 col = {
                    accentV.x + (white.x - accentV.x) * mix,
                    accentV.y + (white.y - accentV.y) * mix,
                    accentV.z + (white.z - accentV.z) * mix,
                    alpha
                };

                drawList->AddText(textFont, textFontSize, ImVec2(x, textPos.y), ImColor(col), buf);
                x += cw;
            }

            textPos.x += totalW * factor;
        };

        drawList->PushClipRect(infoDrawMin, infoDrawMax, true);
        int prev = -1;
        for (int i = 0; i < 3; i++)
        {
            if (tSeg[i] <= 0.001f) continue;
            if (prev != -1)
            {
                const float sepT = std::min(tSeg[prev], tSeg[i]);
                drawChunk(textFont, sepPad, textBase, sepT);
                drawChunk(textFont, dot, dotBase, sepT);
                drawChunk(textFont, sepPad, textBase, sepT);
            }
            drawChunk(energyFont, *icons[i], accentBase, tSeg[i]);
            drawChunk(textFont, space, textBase, tSeg[i]);
            if (i == 0 && (branchText == "NIGHTLY" || branchText == "Nightly" || branchText == "nightly"))
            {
                drawNightlyGradient(*texts[i], tSeg[i]);
            }
            else
            {
                drawChunk(textFont, *texts[i], textBase, tSeg[i]);
            }
            prev = i;
        }
        drawList->PopClipRect();
    }

    if (showCoordsRow)
    {
        ImColor coordsBgCol = backgroundColor;
        ImColor coordsBorderCol = borderColor;
        coordsBgCol.Value.w *= tCoords;
        coordsBorderCol.Value.w *= tCoords;
        const float coordsShadowA = 170.f * anim * tCoords;

        const ImVec2 coordsMinTarget = ImVec2(
            elementTopLeftTarget.x + (elementWidth - coordsWidth) * 0.5f,
            elementTopLeftTarget.y + topRowHeight + blockGap
        );
        ImVec2 coordsMin = {
            MathUtils::lerp(-200.f, coordsMinTarget.x, anim),
            MathUtils::lerp(-200.f, coordsMinTarget.y, anim)
        };

        const ImVec2 coordsMax = ImVec2(coordsMin.x + coordsWidth, coordsMin.y + coordsHeight);

        const float bgScale = 0.94f + 0.06f * wmEaseOutCubic(anim);
        ImVec2 coordsCenter = ImVec2((coordsMin.x + coordsMax.x) * 0.5f, (coordsMin.y + coordsMax.y) * 0.5f);
        ImVec2 coordsDrawMin = ImVec2(coordsCenter.x + (coordsMin.x - coordsCenter.x) * bgScale, coordsCenter.y + (coordsMin.y - coordsCenter.y) * bgScale);
        ImVec2 coordsDrawMax = ImVec2(coordsCenter.x + (coordsMax.x - coordsCenter.x) * bgScale, coordsCenter.y + (coordsMax.y - coordsCenter.y) * bgScale);
        if (useHudBlur)
        {
            ImRenderUtils::addBlurAlpha(ImVec4(coordsDrawMin.x, coordsDrawMin.y, coordsDrawMax.x, coordsDrawMax.y), blurStrength, coordsBgCol.Value.w, rounding, drawList, true);
            coordsBgCol.Value.w *= 0.85f;
        }
        drawList->AddRectFilled(coordsDrawMin, coordsDrawMax, coordsBgCol, rounding);
        {
            ImColor glow = ColorUtils::getGuiAccentColor(0);
            glow.Value.w = 0.40f * anim * tCoords;
            const float glowBaseH = coordsHeight * 0.5f;
            const float glowWidth = (coordsMax.x - coordsMin.x) * 0.5f;
            const float glowX = coordsMin.x + ((coordsMax.x - coordsMin.x) - glowWidth) * 0.5f;
            const float glowLift = glowBaseH * 1.f;
            const ImVec2 glowMin = ImVec2(glowX, coordsMin.y - glowLift);
            const ImVec2 glowMax = ImVec2(glowX + glowWidth, coordsMin.y + glowBaseH - glowLift);
            drawList->PushClipRect(coordsDrawMin, coordsDrawMax, true);
            drawList->AddShadowRect(glowMin, glowMax, glow, 45.0f, ImVec2(0.f, 0.f), 0, rounding);
            drawList->PopClipRect();
        }
        drawList->AddShadowRect(coordsDrawMin, coordsDrawMax, IM_COL32(0, 0, 0, static_cast<int>(coordsShadowA)), 55.0f, ImVec2(0.f, 0.f), 0, rounding);
        drawList->AddRect(coordsDrawMin, coordsDrawMax, coordsBorderCol, rounding, 0, borderThickness);

        const float coordsTabHeight = infoTabHeight;
        const float drawW = coordsDrawMax.x - coordsDrawMin.x;
        const float drawH = coordsDrawMax.y - coordsDrawMin.y;
        const float coordsTabWidth = std::min(std::max(56.f, coordsWidth * 0.45f), std::max(0.f, drawW - 12.f));
        if (coordsTabWidth > 10.f && drawH > coordsTabHeight + 4.f)
        {
            ImColor tab = tabColor;
            tab.Value.w *= anim * tCoords;
            const ImVec2 coordsTabMin = ImVec2(
                coordsDrawMin.x + (drawW - coordsTabWidth) * 0.5f,
                coordsDrawMax.y - coordsTabHeight
            );
            const ImVec2 coordsTabMax = ImVec2(coordsTabMin.x + coordsTabWidth, coordsTabMin.y + coordsTabHeight);
            drawList->AddRectFilled(coordsTabMin, coordsTabMax, tab, rounding, ImDrawFlags_RoundCornersTop);
        }

        float textY = coordsDrawMin.y + (drawH - coordsTextHeight) * 0.5f;
        ImVec2 textPos = ImVec2(coordsDrawMin.x + paddingX, textY);

        ImColor accent = ColorUtils::getGuiAccentColor(0);
        ImColor textColor = ColorUtils::getUiTextColor(1.0f);
        accent.Value.w *= anim * tCoords;
        textColor.Value.w *= anim * tCoords;

        auto drawSeg = [&](ImFont* font, const std::string& s, const ImColor& col) {
            if (s.empty()) return;
            drawList->AddText(font ? font : textFont, textFontSize, textPos, col, s.c_str());
            textPos.x += calcSize(font ? font : textFont, s).x;
        };

        drawSeg(energyFont, iconCoords, accent);
        drawSeg(textFont, space, textColor);
        drawSeg(textFont, coordsText, textColor);
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
    menuAnim = MathUtils::lerp(menuAnim, menuTarget ? 1.f : 0.f, dt * menuSpeed);
    menuAnim = MathUtils::clamp(menuAnim, 0.f, 1.f);

    struct Item { const char* label; BoolSetting* s; };
    Item items[] = {
        {"Название", &mShowName},
        {"ФПС", &mShowFps},
        {"Пинг", &mShowPing},
        {"Координаты", &mShowCoords}
    };

    static float toggleAnim[4]{};
    for (int i = 0; i < 4; i++)
    {
        const float target = items[i].s->mValue ? 1.f : 0.f;
        const float speed = target > 0.5f ? 9.f : 14.f;
        toggleAnim[i] = MathUtils::lerp(toggleAnim[i], target, dt * speed);
        toggleAnim[i] = MathUtils::clamp(toggleAnim[i], 0.f, 1.f);
    }

    const float pad = 12.f;
    const float rowH = textFontSize + 12.f;
    const float indicatorR = 3.2f;
    const float indicatorW = indicatorR * 2.f;
    const float iconGap = 6.f;
    float maxIconW = 0.f;
    float maxLabelW = 0.f;
    for (const auto& it : items)
    {
        maxLabelW = std::max(maxLabelW, calcSize(textFont, it.label).x);
    }
    const std::string* menuIcons[4] = {&iconPrivate, &iconFps, &iconPing, &iconCoords};
    for (int i = 0; i < 4; i++)
    {
        maxIconW = std::max(maxIconW, calcSize(energyFont ? energyFont : textFont, *menuIcons[i]).x);
    }
    const float indicatorShift = indicatorW + 7.f + maxIconW + iconGap;
    const float menuW = pad * 2.f + maxLabelW + indicatorShift + 6.f;
    const float menuH = pad * 2.f + rowH * 4.f;

    const float anchorY = elementTopLeft.y + topRowHeight + (blockGap + coordsHeight) * tCoords;

    ImVec2 menuMin = ImVec2(display.x * 0.5f - menuW * 0.5f, anchorY + 10.f);
    if (menuMin.y + menuH > display.y - 6.f) menuMin.y = anchorY - menuH - 10.f;
    menuMin.x = std::clamp(menuMin.x, 6.f, display.x - menuW - 6.f);
    menuMin.y = std::clamp(menuMin.y, 6.f, display.y - menuH - 6.f);
    ImVec2 menuMax = ImVec2(menuMin.x + menuW, menuMin.y + menuH);

    const float menuA = wmEaseOutCubic(menuAnim);
    const float menuPop = std::clamp(wmEaseOutBack(menuAnim), 0.f, 1.15f);
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

        for (int i = 0; i < 4; i++)
        {
            ImVec2 rowMin = ImVec2(drawMin.x + pad * s, drawMin.y + pad * s + rowH * i * s);
            ImVec2 rowMax = ImVec2(drawMax.x - pad * s, rowMin.y + rowH * s);
            const bool rowHovered = inEditor && menuHovered && ImRenderUtils::isMouseOver(ImVec4(rowMin.x, rowMin.y, rowMax.x, rowMax.y));

            if (inEditor && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && rowHovered)
            {
                items[i].s->mValue = !items[i].s->mValue;
            }

            const float t = wmEaseOutCubic(toggleAnim[i]);
            if (t > 0.001f)
            {
                ImColor circleCol = menuAccent;
                circleCol.Value.w *= a * t;
                const float r = indicatorR * (0.75f + 0.25f * t) * s;
                const ImVec2 c = ImVec2(rowMin.x + 3.f + indicatorR * s, rowMin.y + (rowH * s) * 0.5f);
                drawList->AddCircleFilled(c, r, circleCol, 18);
            }

            const float fontScaled = textFontSize * s;
            const float baseShift = indicatorW + 7.f;
            const float dotW = calcSize(textFont, dot).x;
            const float textShift = (baseShift + dotW + iconGap + maxIconW + iconGap) * t * s;
            if (t > 0.001f)
            {
                ImVec2 dotP = ImVec2(rowMin.x + 2.f + baseShift * t * s, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(textFont, fontScaled, dotP, menuDot, dot.c_str());
                ImColor iconCol = menuAccent;
                iconCol.Value.w *= a * (0.25f + 0.75f * t);
                ImVec2 iconP = ImVec2(rowMin.x + 2.f + baseShift * t * s + dotW + iconGap, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(energyFont ? energyFont : textFont, fontScaled, iconP, iconCol, menuIcons[i]->c_str());
            }
            ImVec2 textP = ImVec2(rowMin.x + 2.f + textShift, rowMin.y + (rowH * s - fontScaled) * 0.5f);
            drawList->AddText(textFont, fontScaled, textP, menuText, items[i].label);
        }
    }

    FontHelper::popPrefFont();
}
