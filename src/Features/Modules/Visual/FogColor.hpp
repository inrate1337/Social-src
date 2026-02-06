#pragma once

class FogColor : public ModuleBase<FogColor>
{
public:
    ColorSetting mFog = ColorSetting("Fog", "Fog color", 1.f, 1.f, 1.f, 1.f);
    NumberSetting mAmount = NumberSetting("Amount", "How strong the fog color override is", 1.f, 0.f, 1.f, 0.01f);

    FogColor() : ModuleBase("FogColor", "Changes fog color", ModuleCategory::Visual, 0, false)
    {
        addSetting(&mFog);
        addSetting(&mAmount);

        mNames = {
            {Lowercase, "fogcolor"},
            {LowercaseSpaced, "fog color"},
            {Normal, "FogColor"},
            {NormalSpaced, "Fog Color"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onFogColorEvent(class FogColorEvent& event);
};
