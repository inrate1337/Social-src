#include "FogColor.hpp"

#include <algorithm>
#include <Features/Events/FogColorEvent.hpp>
#include <Features/FeatureManager.hpp>

void FogColor::onEnable()
{
    gFeatureManager->mDispatcher->listen<FogColorEvent, &FogColor::onFogColorEvent>(this);
}

void FogColor::onDisable()
{
    gFeatureManager->mDispatcher->deafen<FogColorEvent, &FogColor::onFogColorEvent>(this);
}

void FogColor::onFogColorEvent(FogColorEvent& event)
{
    if (!event.mFogColor) return;

    const float t = std::clamp(mAmount.mValue, 0.f, 1.f);
    if (t <= 0.f) return;

    const float tr = mFog.mValue[0];
    const float tg = mFog.mValue[1];
    const float tb = mFog.mValue[2];
    const float ta = mFog.mValue[3];

    const float br = event.mFogColor->r;
    const float bg = event.mFogColor->g;
    const float bb = event.mFogColor->b;
    const float ba = event.mFogColor->a;

    event.mFogColor->r = (br * (1.f - t)) + (tr * t);
    event.mFogColor->g = (bg * (1.f - t)) + (tg * t);
    event.mFogColor->b = (bb * (1.f - t)) + (tb * t);
    event.mFogColor->a = (ba * (1.f - t)) + (ta * t);
}
