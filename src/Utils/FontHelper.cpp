//
// Created by vastrakai on 6/29/2024.
//

#include "FontHelper.hpp"

#include <Features/Modules/Visual/Interface.hpp>

#include "Resources.hpp"

static thread_local int gFontHelperStackDepth = 0;

void FontHelper::load()
{
    if (!Fonts.empty()) return;
    ResourceLoader::loadResources();
}

void FontHelper::pushPrefFont(bool large, bool bold, bool mForcePSans)
{
    load();
    auto font = getFont(large, bold, mForcePSans);
    if (!font) font = ImGui::GetFont();
    if (!font) return;
    ImGui::PushFont(font);
    gFontHelperStackDepth++;
}

ImFont* FontHelper::getFont(bool large, bool bold, bool mForcePSans)
{
    auto daInterface = gFeatureManager->mModuleManager->getModule<Interface>();
    if (!daInterface) return nullptr;

    // Всегда используем Comfortaa для GUI (обычный/жирный + большой/обычный)
    return Fonts[large
                 ? (bold ? "comfortaa_bold_large" : "comfortaa_large")
                 : (bold ? "comfortaa_bold" : "comfortaa")];

}

void FontHelper::popPrefFont()
{
    if (gFontHelperStackDepth <= 0) return;
    ImGui::PopFont();
    gFontHelperStackDepth--;
}
