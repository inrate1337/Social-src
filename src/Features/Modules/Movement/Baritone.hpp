#pragma once

#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <glm/glm.hpp>

class Baritone : public ModuleBase<Baritone> {
public:
    glm::vec3 mTargetPos;
    bool mActive = false;

    float mYaw = 0.f;
    float mPitch = 0.f;

    bool mIsBypassing = false;
    int mBypassSide = 0;
    int mStuckTimer = 0;
    int mBypassTimer = 0;
    glm::vec3 mLastPosition;

    BoolSetting mAutoSprint = BoolSetting("Auto Sprint", "Automatically sprints when moving", true);
    BoolSetting mJump = BoolSetting("Jump", "Auto jump over obstacles", true);
    BoolSetting mAvoidance = BoolSetting("Smart Avoidance", "Avoids voids and walls", true);

    Baritone() : ModuleBase("Baritone", "Pathfinds to coordinates", ModuleCategory::Movement, 0, false) {
        mNames = {
            {Lowercase, "baritone"},
            {LowercaseSpaced, "baritone"},
            {Normal, "Baritone"},
            {NormalSpaced, "Baritone"}
        };
        addSetting(&mAutoSprint);
        addSetting(&mJump);
        addSetting(&mAvoidance);
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);

    void setTarget(const glm::vec3& pos);
    float getDistanceToTarget();
    void calculateRotations();

    bool isPathBlocked(float yaw, float distance, float widthOffset);
    bool isVoid(float yaw, float distance);
    bool canJumpOver(float yaw);
};