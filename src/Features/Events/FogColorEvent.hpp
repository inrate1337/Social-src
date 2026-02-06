#pragma once

#include <Features/Events/Event.hpp>
#include <SDK/Minecraft/Rendering/MinecraftUIRenderContext.hpp>

class Dimension;

struct FogColorEvent : public Event {
    Dimension* mDimension;
    MCCColor* mFogColor;
    const MCCColor* mBaseColor;
    float mBrightness;

    explicit FogColorEvent(Dimension* dimension, MCCColor* fogColor, const MCCColor* baseColor, float brightness)
        : mDimension(dimension), mFogColor(fogColor), mBaseColor(baseColor), mBrightness(brightness) {}
};

