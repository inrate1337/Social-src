#pragma once

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>

#include <SDK/Minecraft/Actor/Actor.hpp>

class ElytraTarget : public ModuleBase<ElytraTarget> {
public:
    enum class FollowMode {
        Behind,
        Ahead,
        Direct
    };

    BoolSetting mUseAuraTarget = BoolSetting("Use Aura Target", "Follow Aura target if available", true);
    BoolSetting mNoFriends = BoolSetting("No Friends", "Do not follow friends", true);
    NumberSetting mRange = NumberSetting("Range", "Max follow range (blocks)", 120.f, 5.f, 500.f, 1.f);
    EnumSettingT<FollowMode> mFollowMode = EnumSettingT<FollowMode>("Follow Mode", "Where to stick relative to target", FollowMode::Ahead, "Behind", "Ahead", "Direct");
    NumberSetting mFollowDistance = NumberSetting("Follow Distance", "Distance from target", 2.5f, 0.f, 15.f, 0.1f);
    NumberSetting mDeadzone = NumberSetting("Deadzone", "Stop when within this distance", 0.5f, 0.f, 5.f, 0.05f);
    NumberSetting mSpeed = NumberSetting("Speed", "Horizontal speed", 25.f, 0.f, 200.f, 0.5f);
    NumberSetting mVerticalSpeed = NumberSetting("Vertical Speed", "Max vertical speed", 12.f, 0.f, 100.f, 0.5f);
    NumberSetting mPrediction = NumberSetting("Prediction", "Target movement prediction (blocks)", 1.0f, 0.f, 10.f, 0.1f);
    BoolSetting mRotate = BoolSetting("Rotate", "Rotate to look at target", true);
    BoolSetting mOrbit = BoolSetting("Orbit", "Orbit around target when close", false);
    NumberSetting mOrbitRadius = NumberSetting("Orbit Radius", "Orbit radius (blocks)", 2.5f, 0.2f, 10.f, 0.1f);
    NumberSetting mOrbitSpeed = NumberSetting("Orbit Speed", "Orbit speed (deg/s)", 90.f, 1.f, 720.f, 1.f);
    NumberSetting mOrbitStartDistance = NumberSetting("Orbit Start Distance", "Start orbit when this close", 3.0f, 0.5f, 10.f, 0.1f);

    ElytraTarget();

    void onEnable() override;
    void onDisable() override;

    void onBaseTickEvent(BaseTickEvent& event);
    void onPacketOutEvent(PacketOutEvent& event);

private:
    Actor* mTarget{ nullptr };
    bool mHasTarget{ false };
    glm::vec2 mDesiredRotsDeg{ 0.f, 0.f };
    glm::vec2 mDesiredMove{ 0.f, 0.f };
    float mDesiredDeltaY{ 0.f };
    float mOrbitAngleDeg{ 0.f };
    uint64_t mLastOrbitTick{ 0 };

    Actor* acquireTarget(Actor* player);
    glm::vec3 getPredictedTargetPos(Actor* target) const;
};