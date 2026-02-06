#pragma once
//
// Created by vastrakai on 7/18/2024.
//

#include <Features/Modules/Module.hpp>

class Animations : public ModuleBase<Animations> {
public:
    enum class Animation {
        Default,
        Blocking,
        Test
    };
    EnumSettingT<Animation> mAnimation = EnumSettingT("Animation", "The animation to use", Animation::Blocking, "Default", "Blocking", "Test");
    BoolSetting mCustomInterpolation = BoolSetting("Custom Interpolation", "Use a custom interpolation curve", true);
    NumberSetting mInterpP1X = NumberSetting("Curve P1 X", "Interpolation curve point 1 X", 0.25f, 0.0f, 1.0f, 0.01f);
    NumberSetting mInterpP1Y = NumberSetting("Curve P1 Y", "Interpolation curve point 1 Y", 0.10f, 0.0f, 1.0f, 0.01f);
    NumberSetting mInterpP2X = NumberSetting("Curve P2 X", "Interpolation curve point 2 X", 0.25f, 0.0f, 1.0f, 0.01f);
    NumberSetting mInterpP2Y = NumberSetting("Curve P2 Y", "Interpolation curve point 2 Y", 1.00f, 0.0f, 1.0f, 0.01f);
    BoolSetting mNoSwitchAnimation = BoolSetting("No Switch Animation", "Disables the switch animation", true);
    BoolSetting mFluxSwing = BoolSetting("Flux Swing", "Flux Client styled swinging", true);
    NumberSetting mSwingSpeed = NumberSetting("Swing Speed", "Multiplies swing animation speed", 1.00f, 0.20f, 3.00f, 0.01f);
    BoolSetting mThirdPersonBlock = BoolSetting("3rd person", "Displays 3rd person animations", false);
    // Holds the rotation for third person blocking, if you need to play with it, simply add da settings
    NumberSetting mXRot = NumberSetting("X Rotation", "The custom X rotation value. (default: 0)", -57.f, -360.f, 360.f, 0.01f);
    NumberSetting mYRot = NumberSetting("Y Rotation", "The custom Y rotation value. (default: 0)", -45.16f, -360.f, 360.f, 0.01f);
    NumberSetting mZRot = NumberSetting("Z Rotation", "The custom Z rotation value. (default: 0)", 13.14f, -360.f, 360.f, 0.01f);
    BoolSetting mCustomSwingAngle = BoolSetting("Custom Swing Angle", "Changes the swing angle", true);
    BoolSetting mOnlyOnBlock = BoolSetting("Only on Block", "Only changes the swing angle when blocking", false);
    NumberSetting mSwingAngleSetting = NumberSetting("Swing Angle", "The custom swing angle value. (default: -80)", -31.f, -360.f, 360.f, 0.01f);
    BoolSetting mSmallItems = BoolSetting("Small Items", "Makes items smaller", true);

    Animations() : ModuleBase("Animations", "Change your animations!", ModuleCategory::Visual, 0, false)
    {
        addSettings(
            &mAnimation,
            &mCustomInterpolation,
            &mInterpP1X,
            &mInterpP1Y,
            &mInterpP2X,
            &mInterpP2Y,
            &mNoSwitchAnimation,
            &mFluxSwing,
            &mSwingSpeed,
            &mThirdPersonBlock,
            // &mXRot,
            // &mYRot,
            // &mZRot,
            &mCustomSwingAngle,
            &mOnlyOnBlock,
            &mSwingAngleSetting,
            &mSmallItems
        );

        VISIBILITY_CONDITION(mOnlyOnBlock, mCustomSwingAngle.mValue);
        VISIBILITY_CONDITION(mSwingAngleSetting, mCustomSwingAngle.mValue);
        mCustomInterpolation.mIsVisible = [] { return false; };
        mInterpP1X.mIsVisible = [] { return false; };
        mInterpP1Y.mIsVisible = [] { return false; };
        mInterpP2X.mIsVisible = [] { return false; };
        mInterpP2Y.mIsVisible = [] { return false; };

        mNames = {
            {Lowercase, "animations"},
            {LowercaseSpaced, "animations"},
            {Normal, "Animations"},
            {NormalSpaced, "Animations"}
        };
    }

    float mSwingDuration = 0.0f;
    float mOldSwingDuration = 0.0f;
    int mSwingDurationMax = 6;
    float* mSwingAngle = nullptr;
    bool mShouldBlock = false;

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onSwingDurationEvent(class SwingDurationEvent& event);
    void onBoneRenderEvent(class BoneRenderEvent& event);
    void onBobHurtEvent(class BobHurtEvent& event);
};
