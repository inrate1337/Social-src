//
// Created by vastrakai on 7/1/2024.
//

#include "ColorUtils.hpp"

#include <mutex>
#include <regex>
#include <Features/FeatureManager.hpp>
#include <Features/Modules/Visual/Interface.hpp>

static std::mutex gGuiAccentMutex;
static bool gGuiAccentHasOverride = false;
static ImColor gGuiAccentOverride = ImColor(255, 255, 255, 255);

static std::mutex gUiThemeMutex;
static ColorUtils::UiTheme gUiTheme = ColorUtils::UiTheme::Dark;

ImColor ColorUtils::Rainbow(float seconds, float saturation, float brightness, int index)
{
    float hue = ((NOW + index) % (int)(seconds * 1000)) / (float)(seconds * 1000);
    float r, g, b = 0;
    return ImColor::HSV(hue, saturation, brightness);
}

ImColor ColorUtils::LerpColors(float seconds, float index, std::vector<ImColor> colors, uint64_t ms) {
    if (colors.empty()) return { 255, 255, 255, 255};
    float time = 10000.0f / seconds;
    auto angle = static_cast<float>(((ms == 0 ? NOW : ms) + static_cast<int>(index)) % static_cast<int>(time));
    float segmentTime = time / colors.size();

    int segmentIndex = static_cast<int>(angle / segmentTime);
    float segmentIndexFloat = angle / segmentTime - segmentIndex;

    ImColor startColor = colors[segmentIndex];
    ImColor endColor = colors[(segmentIndex + 1) % colors.size()];
    return startColor.Lerp(endColor, segmentIndexFloat);
}

ImColor ColorUtils::getThemedColor(float index, uint64_t ms)
{
    auto daInterface = gFeatureManager->mModuleManager->getModule<Interface>();
    if (!daInterface) return { 255, 255, 255, 255 };

    auto theme = daInterface->mMode.mValue;
    auto colors = Interface::ColorThemes[theme];
    if (theme == Interface::Rainbow) return Rainbow(daInterface->mColorSpeed.mValue, daInterface->mSaturation.mValue, 1.f, index);
    else if (theme == Interface::Custom)
    {
        colors = daInterface->getCustomColors();
    }

    return LerpColors(daInterface->mColorSpeed.mValue, index, colors, ms);

}

void ColorUtils::setGuiAccentOverride(const ImColor& color)
{
    std::lock_guard<std::mutex> lock(gGuiAccentMutex);
    gGuiAccentOverride = color;
    gGuiAccentHasOverride = true;
}

void ColorUtils::clearGuiAccentOverride()
{
    std::lock_guard<std::mutex> lock(gGuiAccentMutex);
    gGuiAccentHasOverride = false;
}

ImColor ColorUtils::getGuiAccentColor(float index, uint64_t ms)
{
    {
        std::lock_guard<std::mutex> lock(gGuiAccentMutex);
        if (gGuiAccentHasOverride) return gGuiAccentOverride;
    }
    return getThemedColor(index, ms);
}

void ColorUtils::setUiTheme(UiTheme theme)
{
    std::lock_guard<std::mutex> lock(gUiThemeMutex);
    gUiTheme = theme;
}

ColorUtils::UiTheme ColorUtils::getUiTheme()
{
    std::lock_guard<std::mutex> lock(gUiThemeMutex);
    return gUiTheme;
}

ImColor ColorUtils::getUiBackgroundColor(float alpha)
{
    ImColor c = (getUiTheme() == UiTheme::Light) ? ImColor(248, 248, 252, 255) : ImColor(8, 8, 12, 255);
    c.Value.w = alpha;
    return c;
}

ImColor ColorUtils::getUiCardColor(float alpha)
{
    ImColor c = (getUiTheme() == UiTheme::Light) ? ImColor(255, 255, 255, 255) : ImColor(16, 16, 20, 255);
    c.Value.w = alpha;
    return c;
}

ImColor ColorUtils::getUiBorderColor(float alpha)
{
    ImColor c = (getUiTheme() == UiTheme::Light) ? ImColor(214, 214, 224, 255) : ImColor(18, 18, 24, 255);
    c.Value.w = alpha;
    return c;
}

ImColor ColorUtils::getUiTextColor(float alpha)
{
    ImColor c = (getUiTheme() == UiTheme::Light) ? ImColor(18, 18, 22, 255) : ImColor(230, 230, 240, 255);
    c.Value.w = alpha;
    return c;
}

ImColor ColorUtils::getUiTextDimColor(float alpha)
{
    ImColor c = (getUiTheme() == UiTheme::Light) ? ImColor(90, 90, 104, 255) : ImColor(110, 110, 125, 255);
    c.Value.w = alpha;
    return c;
}

std::string ColorUtils::removeColorCodes(const std::string& text)
{
    static std::regex colorCodeRegex("§.");
    static std::string emptyString = "";
    return std::regex_replace(text, colorCodeRegex, emptyString);
}

