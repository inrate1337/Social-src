#pragma once

#include <Features/Modules/Module.hpp>
#include <glm/glm.hpp>

class TargetESP : public ModuleBase<TargetESP> {
public:
    EnumSetting mMode = EnumSetting("Mode", "TargetESP render mode", 0, std::vector<std::string>{"Crystals", "Kvadratik"});

    NumberSetting mCrystalCount = NumberSetting("Crystals", "Crystal count", 6.0f, 1.0f, 20.0f, 1.0f);
    NumberSetting mRadius = NumberSetting("Radius", "Crystal radius", 0.9f, 0.1f, 3.0f, 0.01f);
    NumberSetting mSize = NumberSetting("Size", "Crystal size", 0.55f, 0.15f, 2.0f, 0.01f);
    NumberSetting mSpinSpeed = NumberSetting("Spin Speed", "Crystal spin speed", 1.25f, 0.0f, 6.0f, 0.01f);
    BoolSetting mFilled = BoolSetting("Filled", "Filled crystal faces", true);

    NumberSetting mImageScale = NumberSetting("Kvadratik Size", "Kvadratik size multiplier", 1.0f, 0.2f, 2.5f, 0.01f);
    NumberSetting mImageSpin = NumberSetting("Kvadratik Spin", "Kvadratik rotation speed", 2.0f, 0.0f, 10.0f, 0.01f);
    NumberSetting mImageReverseDegrees = NumberSetting("Kvadratik Reverse", "Max degrees before reversing rotation", 0.0f, 0.0f, 720.0f, 1.0f);
    BoolSetting mImageHitEffect = BoolSetting("Kvadratik Hit Effect", "Hit pulse: red flash + shrink", true);
    BoolSetting mImagePulse = BoolSetting("Kvadratik Pulse", "Smooth pulsation effect", false);

    TargetESP() : ModuleBase("TargetESP", "Draws 3D crystals around Aura target", ModuleCategory::Visual, 0, false) {
        mNames = {
            {Lowercase, "targetesp"},
            {LowercaseSpaced, "target esp"},
            {Normal, "TargetESP"},
            {NormalSpaced, "Target ESP"}
        };

        mMode.mIsVisible = [] { return false; };
        mCrystalCount.mIsVisible = [] { return false; };
        mRadius.mIsVisible = [] { return false; };
        mSize.mIsVisible = [] { return false; };
        mSpinSpeed.mIsVisible = [] { return false; };
        mFilled.mIsVisible = [] { return false; };
        mImageScale.mIsVisible = [] { return false; };
        mImageSpin.mIsVisible = [] { return false; };
        mImageReverseDegrees.mIsVisible = [] { return false; };
        mImageHitEffect.mIsVisible = [] { return false; };
        mImagePulse.mIsVisible = [] { return false; };

        addSettings(
            &mMode,
            &mCrystalCount,
            &mRadius,
            &mSize,
            &mSpinSpeed,
            &mFilled,
            &mImageScale,
            &mImageSpin,
            &mImageReverseDegrees,
            &mImageHitEffect,
            &mImagePulse
        );
    }

    void onEnable() override;
    void onDisable() override;
    void onRenderEvent(class RenderEvent& event);

    float mVisAnim = 0.f;
    bool mHasLast = false;
    glm::vec3 mLastPos = {};
    glm::vec3 mLastAabbMin = {};
    glm::vec3 mLastAabbMax = {};
    float mLastHeight = 0.f;
    float mLastWidth = 0.f;
    int mLastHurtTime = 0;
    int mHurtPeak = 0;
    float mHitAnim = 0.f;
};
