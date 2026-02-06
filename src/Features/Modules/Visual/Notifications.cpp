#include "Notifications.hpp"

#include <Features/Events/ConnectionRequestEvent.hpp>
#include <Features/Events/NotifyEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include "ClickGui.hpp"
#include <Features/Modules/Visual/Interface.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <algorithm>

namespace {
    float clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }

    float smoothstep(float t)
    {
        t = clamp01(t);
        return t * t * (3.0f - 2.0f * t);
    }

    float easeOutQuad(float t)
    {
        t = clamp01(t);
        float inv = 1.0f - t;
        return 1.0f - inv * inv;
    }

    float easeInQuad(float t)
    {
        t = clamp01(t);
        return t * t;
    }

    float easeOutExpo(float t)
    {
        t = clamp01(t);
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return 1.0f - std::pow(2.0f, -10.0f * t);
    }
}

void Notifications::onEnable()
{
    gFeatureManager->mDispatcher->listen<NotifyEvent, &Notifications::onNotifyEvent>(this);
    gFeatureManager->mDispatcher->listen<ModuleStateChangeEvent, &Notifications::onModuleStateChange>(this);
    gFeatureManager->mDispatcher->listen<ConnectionRequestEvent, &Notifications::onConnectionRequestEvent>(this);
}

void Notifications::onDisable()
{
    gFeatureManager->mDispatcher->deafen<NotifyEvent, &Notifications::onNotifyEvent>(this);
    gFeatureManager->mDispatcher->deafen<ModuleStateChangeEvent, &Notifications::onModuleStateChange>(this);
    gFeatureManager->mDispatcher->deafen<ConnectionRequestEvent, &Notifications::onConnectionRequestEvent>(this);
    mNotifications.clear();
}

void Notifications::onRenderEvent(RenderEvent& event)
{
    FontHelper::pushPrefFont();

    float dt = ImGui::GetIO().DeltaTime;
    for (auto& n : mNotifications)
    {
        n.mTimeShown += dt;
        n.mIsTimeUp = n.mTimeShown >= n.mDuration;
    }

    std::erase_if(mNotifications, [](const Notification& n) {
        return n.mIsTimeUp && n.mTimeShown > n.mDuration + 1.0f;
    });

    if (mLimitNotifications.mValue)
    {
        const size_t maxCount = static_cast<size_t>(std::max(1, mMaxNotifications.as<int>()));
        if (mNotifications.size() > maxCount)
        {
            mNotifications.erase(mNotifications.begin(), mNotifications.end() - maxCount);
        }
    }

    if (!mEnabled)
    {
        ImGui::PopFont();
        return;
    }

    auto ci = ClientInstance::get();
    if (ci)
    {
        const std::string screenName = ci->getScreenName();
        if (screenName != "hud_screen" && screenName != "no_screen")
        {
            ImGui::PopFont();
            return;
        }
    }

    auto clickGui = gFeatureManager->mModuleManager->getModule<ClickGui>();
    if (clickGui && clickGui->mEnabled)
    {
        ImGui::PopFont();
        return;
    }

    const ImVec2 scr = ImGui::GetIO().DisplaySize;
    auto dl = ImGui::GetBackgroundDrawList();
    auto* interfaceModule = gFeatureManager && gFeatureManager->mModuleManager ? gFeatureManager->mModuleManager->getModule<Interface>() : nullptr;
    const bool useHudBlur = interfaceModule && interfaceModule->mHudBlur.mValue;
    const float blurStrength = useHudBlur ? interfaceModule->mHudBlurStrength.mValue : 0.f;

    float yOff = 0.0f;
    const float spacing = 8.0f;
    const float baseY = (scr.y * 0.5f) + 80.0f;

    auto* textFont = ImGui::GetFont();
    ImFont* iconFont = textFont;
    if (auto it = FontHelper::Fonts.find("icons3.ttf"); it != FontHelper::Fonts.end() && it->second)
    {
        iconFont = it->second;
    }

    const ImColor bgBase = ColorUtils::getUiCardColor(1.0f);
    const ImColor borderBase = ColorUtils::getUiBorderColor(16.0f / 255.0f);
    const ImColor textBase = ColorUtils::getUiTextColor(1.0f);
    ImColor accentBase = ColorUtils::getGuiAccentColor(0);
    accentBase.Value.w = 1.f;

    for (auto it = mNotifications.rbegin(); it != mNotifications.rend(); ++it)
    {
        auto& n = *it;

        const float fadeInSec = 0.18f;
        const float fadeOutSec = 0.34f;

        const float tInRaw = clamp01(n.mTimeShown / fadeInSec);
        const float tIn = easeOutExpo(tInRaw);

        float tOut = 1.0f;
        float tOutRaw = 0.0f;
        if (n.mIsTimeUp)
        {
            tOutRaw = clamp01((n.mTimeShown - n.mDuration) / fadeOutSec);
            tOut = 1.0f - easeInQuad(tOutRaw);
        }

        const float tFade = tIn * tOut;
        const float stackT = tFade;

        if (tFade < 0.03f && n.mIsTimeUp) continue;

        const float textFontSize = 18.f;
        const float iconFontSize = 22.f;
        const float paddingX = 12.f;
        const float iconBoxSize = 34.f;
        const float blockGap = 8.f;
        const float rounding = 8.f;
        const float borderThickness = 1.6f;
        const float slidePx = 36.f;

        ImVec2 msgSz = textFont ? textFont->CalcTextSizeA(textFontSize, FLT_MAX, 0.0f, n.mMessage.c_str(), nullptr) : ImVec2(0.f, 0.f);
        float textBoxW = msgSz.x + paddingX * 2.f;
        if (textBoxW < 150.f) textBoxW = 150.f;

        const float baseW = iconBoxSize + blockGap + textBoxW;
        const float baseH = iconBoxSize;

        const float w = baseW;
        const float h = baseH;

        const float targetY = baseY + yOff;
        const float centerX = scr.x * 0.5f;
        const float centerY = targetY + (baseH * 0.5f);
        const float x = centerX - (w * 0.5f);
        const float slideInT = easeOutExpo(tInRaw);
        const float slideOutT = n.mIsTimeUp ? easeInQuad(tOutRaw) : 0.0f;
        const float y = (centerY - (h * 0.5f)) + (1.f - slideInT) * slidePx + slideOutT * (slidePx * 0.85f);

        yOff += (baseH + spacing) * stackT;

        ImColor bg = bgBase;
        ImColor border = borderBase;
        ImColor fg = textBase;
        ImColor accent = accentBase;
        const bool isStaff = n.mMessage.rfind("Staff ", 0) == 0;
        const bool isEffect = n.mMessage.rfind("Effect ", 0) == 0;
        if (isEffect)
        {
            if (n.mType == Notification::Type::Error || n.mMessage.find(" ended") != std::string::npos)
            {
                accent = ImColor(234, 78, 78, 255);
            }
            else
            {
                accent = ImColor(245, 202, 64, 255);
            }
        }

        bg.Value.w *= tFade;
        border.Value.w *= tFade;
        fg.Value.w *= tFade;
        accent.Value.w *= tFade;

        const float r = rounding;
        const float bt = borderThickness;
        const float glowA = 0.40f * tFade;

        const ImVec2 iconMin = ImVec2(x, y);
        const ImVec2 iconMax = ImVec2(x + iconBoxSize, y + h);
        const ImVec2 textMin = ImVec2(iconMax.x + blockGap, y);
        const ImVec2 textMax = ImVec2(x + w, y + h);

        if (useHudBlur)
        {
            ImRenderUtils::addBlurAlpha(ImVec4(iconMin.x, iconMin.y, textMax.x, textMax.y), blurStrength, bg.Value.w, r, dl, true);
            bg.Value.w *= 0.85f;
        }
        {
            ImColor shadow = IM_COL32(0, 0, 0, 255);
            shadow.Value.w = 0.60f * tFade;
            dl->AddShadowRect(iconMin, textMax, shadow, 24.0f, ImVec2(0.f, 3.f), 0, r);
        }

        dl->AddRectFilled(iconMin, iconMax, bg, r);
        dl->AddRectFilled(textMin, textMax, bg, r);

        {
            ImColor glow = accent;
            glow.Value.w = glowA;
            const float glowBaseH = h * 0.5f;
            const float glowWidth = (textMax.x - textMin.x) * 0.5f;
            const float glowX = textMin.x + ((textMax.x - textMin.x) - glowWidth) * 0.5f;
            const float glowLift = glowBaseH * 1.f;
            const ImVec2 glowMin = ImVec2(glowX, textMin.y - glowLift);
            const ImVec2 glowMax = ImVec2(glowX + glowWidth, textMin.y + glowBaseH - glowLift);
            dl->PushClipRect(textMin, textMax, true);
            dl->AddShadowRect(glowMin, glowMax, glow, 45.0f, ImVec2(0.f, 0.f), 0, r);
            dl->PopClipRect();
        }

        dl->AddRect(iconMin, iconMax, border, r, 0, bt);
        dl->AddRect(textMin, textMax, border, r, 0, bt);

        std::string icon = "i";
        if (isStaff || isEffect) icon = "M";
        else if (n.mMessage.find("Connecting to server") != std::string::npos) icon = "Q";
        else if (
            n.mMessage.find(" was enabled") != std::string::npos ||
            (n.mMessage.size() >= 8 && n.mMessage.rfind(" Enabled") != std::string::npos)
        ) icon = "K";
        else if (
            n.mMessage.find(" was disabled") != std::string::npos ||
            (n.mMessage.size() >= 9 && n.mMessage.rfind(" Disabled") != std::string::npos)
        ) icon = "J";
        else
        {
            if (n.mType == Notification::Type::Warning) icon = "K";
            else if (n.mType == Notification::Type::Error) icon = "K";
        }

        ImVec2 iconSz = iconFont ? iconFont->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, icon.c_str(), nullptr) : ImVec2(0.f, 0.f);
        const float iconSlackY = std::max(0.0f, (iconMax.y - iconMin.y) - iconSz.y);
        const float iconExtraDown = iconSlackY * 0.35f;
        ImVec2 iconPos = ImVec2(
            iconMin.x + (iconMax.x - iconMin.x - iconSz.x) * 0.5f,
            iconMin.y + iconSlackY * 0.5f + iconExtraDown
        );
        {
            ImVec2 iconCenter = ImVec2((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
            dl->AddShadowCircle(iconCenter, iconBoxSize * 0.14f, ImColor(accent.Value.x, accent.Value.y, accent.Value.z, tFade), 32, ImVec2(0.f, 0.f), 0, 64);
        }
        dl->AddText(iconFont ? iconFont : textFont, iconFontSize, iconPos, accent, icon.c_str());

        ImVec2 textPos = ImVec2(textMin.x + paddingX, textMin.y + (h - msgSz.y) * 0.5f);
        dl->AddText(textFont, textFontSize, textPos, fg, n.mMessage.c_str());

        {
        {
                const float tabH = 3.f;
                const float drawW = textMax.x - textMin.x;
                const float tabMaxW = std::max(0.f, drawW - 12.f);
                const float tabBaseW = std::min(std::max(56.f, drawW * 0.55f), tabMaxW);
                if (tabBaseW > 10.f && h > tabH + 4.f && tFade > 0.001f)
                {
                    ImColor tab = accentBase;
                    tab.Value.w *= tFade;
                    const ImVec2 tabMin = ImVec2(textMin.x + (drawW - tabBaseW) * 0.5f, textMax.y - tabH);
                    const ImVec2 tabMax = ImVec2(tabMin.x + tabBaseW, tabMin.y + tabH);
                    dl->AddRectFilled(tabMin, tabMax, tab, r, ImDrawFlags_RoundCornersTop);
                }
            }
        }

    }

    ImGui::PopFont();
}

void Notifications::onModuleStateChange(ModuleStateChangeEvent& event)
{
    if (event.isCancelled()) return;
    if (!mShowOnToggle.mValue) return;
    std::string msg = std::string("Module \"") + event.mModule->getName() + "\" was " + (event.mEnabled ? "enabled" : "disabled");
    Notification::Type type = event.mEnabled ? Notification::Type::Info : Notification::Type::Warning;
    mNotifications.push_back(Notification(msg, type, 3.0f));
    if (mLimitNotifications.mValue)
    {
        const size_t maxCount = static_cast<size_t>(std::max(1, mMaxNotifications.as<int>()));
        if (mNotifications.size() > maxCount)
        {
            mNotifications.erase(mNotifications.begin(), mNotifications.end() - maxCount);
        }
    }
}

void Notifications::onConnectionRequestEvent(ConnectionRequestEvent& event)
{
    if (!mShowOnJoin.mValue) return;
    mNotifications.push_back(Notification("Connecting to server...", Notification::Type::Info, 5.0f));
    if (mLimitNotifications.mValue)
    {
        const size_t maxCount = static_cast<size_t>(std::max(1, mMaxNotifications.as<int>()));
        if (mNotifications.size() > maxCount)
        {
            mNotifications.erase(mNotifications.begin(), mNotifications.end() - maxCount);
        }
    }
}

void Notifications::onNotifyEvent(NotifyEvent& event)
{
    mNotifications.push_back(event.mNotification);
    if (mLimitNotifications.mValue)
    {
        const size_t maxCount = static_cast<size_t>(std::max(1, mMaxNotifications.as<int>()));
        if (mNotifications.size() > maxCount)
        {
            mNotifications.erase(mNotifications.begin(), mNotifications.end() - maxCount);
        }
    }
}
