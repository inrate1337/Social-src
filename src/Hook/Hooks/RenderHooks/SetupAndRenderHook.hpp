#pragma once
#include <Hook/Hook.hpp>
#include <Hook/HookManager.hpp>
#include <SDK/Minecraft/mce.hpp>
#include <SDK/Minecraft/Rendering/MinecraftUIRenderContext.hpp>
class SetupAndRenderHook : public Hook {
public:
    SetupAndRenderHook() : Hook() {
        mName = "ScreenView::setupAndRender";
    }
    static std::unique_ptr<Detour> mSetupAndRenderDetour;
    static std::unique_ptr<Detour> mDrawImageDetour;
    static std::unique_ptr<Detour> mDrawTextDetour;
    static void* onSetupAndRender(void* screenView, void* mcuirc);
    static void* onDrawImage(void* context, mce::TexturePtr* texture, glm::vec2* pos, glm::vec2* size, glm::vec2* uv, mce::Color* color, void* unk);
    static void onDrawText(MinecraftUIRenderContext* ctx, Font* font, RectangleArea* pos, std::string* text, MCCColor* color, float alpha,
        ui::TextAlignment textAlignment, const TextMeasureData* textMeasureData, const CaretMeasureData* caretMeasureData);
    static void initVt(void* ctx);
    void init() override;
};
