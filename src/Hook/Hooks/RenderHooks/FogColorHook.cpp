#include "FogColorHook.hpp"

#include <Features/Events/FogColorEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/SigManager.hpp>

std::unique_ptr<Detour> FogColorHook::mDimensionFogColorDetour;
std::unique_ptr<Detour> FogColorHook::mOverworldFogColorDetour;

MCCColor& FogColorHook::onDimensionFogColor(Dimension* _this, MCCColor& result, const MCCColor& baseColor, float brightness)
{
    auto original = mDimensionFogColorDetour->getOriginal<&FogColorHook::onDimensionFogColor>();
    MCCColor& out = original(_this, result, baseColor, brightness);
    auto event = nes::make_holder<FogColorEvent>(_this, &out, &baseColor, brightness);
    gFeatureManager->mDispatcher->trigger(event);
    return out;
}
MCCColor& FogColorHook::onOverworldFogColor(Dimension* _this, MCCColor& result, const MCCColor& baseColor, float brightness)
{
    auto original = mOverworldFogColorDetour->getOriginal<&FogColorHook::onOverworldFogColor>();
    MCCColor& out = original(_this, result, baseColor, brightness);
    auto event = nes::make_holder<FogColorEvent>(_this, &out, &baseColor, brightness);
    gFeatureManager->mDispatcher->trigger(event);
    return out;
}
void FogColorHook::init()
{
    if (SigManager::Dimension_getBrightnessDependentFogColor != 0)
        mDimensionFogColorDetour = std::make_unique<Detour>("Dimension::getBrightnessDependentFogColor", reinterpret_cast<void*>(SigManager::Dimension_getBrightnessDependentFogColor), &FogColorHook::onDimensionFogColor);
    if (SigManager::OverworldDimension_getBrightnessDependentFogColor != 0)
        mOverworldFogColorDetour = std::make_unique<Detour>("OverworldDimension::getBrightnessDependentFogColor", reinterpret_cast<void*>(SigManager::OverworldDimension_getBrightnessDependentFogColor), &FogColorHook::onOverworldFogColor);
}
void FogColorHook::shutdown()
{
    mDimensionFogColorDetour.reset();
    mOverworldFogColorDetour.reset();
}

