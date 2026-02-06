#pragma once
//
// Created by vastrakai on 7/1/2024.
//

#include <chrono>

#define NOW std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()

class ColorUtils {
public:
    enum class UiTheme
    {
        Dark = 0,
        Light = 1
    };

    static ImColor Rainbow(float seconds, float saturation, float brightness, int index);
    static ImColor LerpColors(float seconds, float index, std::vector<ImColor> colors, uint64_t ms = 0);
    static ImColor getThemedColor(float index, uint64_t ms = 0);
    static void setGuiAccentOverride(const ImColor& color);
    static void clearGuiAccentOverride();
    static ImColor getGuiAccentColor(float index, uint64_t ms = 0);
    static void setUiTheme(UiTheme theme);
    static UiTheme getUiTheme();
    static ImColor getUiBackgroundColor(float alpha = 1.0f);
    static ImColor getUiCardColor(float alpha = 1.0f);
    static ImColor getUiBorderColor(float alpha = 1.0f);
    static ImColor getUiTextColor(float alpha = 1.0f);
    static ImColor getUiTextDimColor(float alpha = 1.0f);
    static std::string removeColorCodes(const std::string& text);
};
