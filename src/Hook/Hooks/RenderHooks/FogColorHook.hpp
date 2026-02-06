#pragma once

#include <Hook/Hook.hpp>
#include <SDK/Minecraft/Rendering/MinecraftUIRenderContext.hpp>

class Dimension;

class FogColorHook : public Hook {
public:
    FogColorHook() : Hook() {
        mName = "FogColorHook";
    }

    static std::unique_ptr<Detour> mDimensionFogColorDetour;
    static std::unique_ptr<Detour> mOverworldFogColorDetour;

    static MCCColor& onDimensionFogColor(Dimension* _this, MCCColor& result, const MCCColor& baseColor, float brightness);
    static MCCColor& onOverworldFogColor(Dimension* _this, MCCColor& result, const MCCColor& baseColor, float brightness);

    void init() override;
    void shutdown() override;
};

