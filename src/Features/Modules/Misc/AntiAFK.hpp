#pragma once

#include <Features/FeatureManager.hpp>
#include <chrono>
#include <random>

class AntiAFK : public ModuleBase<AntiAFK> {
public:
    enum class Mode
    {
        JumpEveryMinute,
        ForwardBackward,
        Circle,
        LeftRight,
        SneakPulse,
        Spin,
        RandomMove,
    };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>(
        "Mode",
        "Anti-AFK mode",
        Mode::Circle,
        "Jump (1/min)",
        "W/S",
        "Circle",
        "A/D",
        "Sneak Pulse",
        "Spin",
        "Random"
    );

    AntiAFK() : ModuleBase("AntiAFK", "Walks in a small circle to avoid being kicked for AFK", ModuleCategory::Misc, 0, false) {
        addSetting(&mMode);
        mNames = {
            {Lowercase, "antiafk"},
            {LowercaseSpaced, "anti afk"},
            {Normal, "AntiAFK"},
            {NormalSpaced, "Anti AFK"},
        };
    }

    float mAngle = 0.0f;
    glm::vec3 mCenterPos = glm::vec3(0, 0, 0);
    bool mInitialized = false;
    int mPacketCounter = 0;

    std::chrono::steady_clock::time_point mLastJumpAt = {};
    std::chrono::steady_clock::time_point mLastRandomAt = {};
    std::chrono::steady_clock::time_point mLastSneakAt = {};

    bool mSneakActive = false;
    int mSneakTicksLeft = 0;
    bool mSneakSendStop = false;

    float mRandomMoveX = 0.0f;
    float mRandomMoveY = 0.0f;
    float mRandomYaw = 0.0f;

    std::mt19937 mRng = {};

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
};
