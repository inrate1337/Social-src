#pragma once
//
// Created by vastrakai on 10/4/2024.
//

#include <unordered_map>

class Tracers : public ModuleBase<Tracers> {
public:
    enum class CenterPoint {
        Top,
        Center,
        Bottom
    };

    EnumSettingT<CenterPoint> mCenterPoint = EnumSettingT<CenterPoint>("Center Point", "The point to center the text on.", CenterPoint::Center, "Top", "Center", "Bottom");
    BoolSetting mRenderFilled = BoolSetting("Render Filled", "Whether or not to render the ESP filled.", true);
    BoolSetting mRenderLocal = BoolSetting("Render Local", "Whether or not to render the ESP on the local player.", false);
    BoolSetting mShowFriends = BoolSetting("Show Friends", "Whether or not to render the ESP on friends.", true);
    NumberSetting mRingRadius = NumberSetting("Ring Radius", "Radius of the center ring (pixels).", 30.0f, 5.0f, 300.0f, 1.0f);

    Tracers() : ModuleBase("Tracers", "Draws arrows ring pointing to entities", ModuleCategory::Visual, 0, false) {
        addSetting(&mCenterPoint);
        addSetting(&mRenderFilled);
        addSetting(&mRenderLocal);
        addSetting(&mShowFriends);
        addSetting(&mRingRadius);

        mNames = {
            {Lowercase, "tracers"},
            {LowercaseSpaced, "tracers"},
            {Normal, "Tracers"},
            {NormalSpaced, "Tracers"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onRenderEvent(class RenderEvent& event);

private:
    std::unordered_map<int64_t, float> mSmoothedAngles;
    float mSmoothedRingRadius = 0.0f;
};
