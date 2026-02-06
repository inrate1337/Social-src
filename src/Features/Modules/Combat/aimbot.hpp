#pragma once

#include <Features/Modules/Module.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/LookInputEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/RenderEvent.hpp>

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>

class Aimbot : public ModuleBase<Aimbot>
{
public:
    NumberSetting mRange = NumberSetting("Range", "Max distance to aim (blocks)", 6.0f, 1.0f, 200.0f, 0.1f);
    NumberSetting mFov = NumberSetting("FOV", "Field of view angle to consider (deg)", 60.0f, 5.0f, 180.0f, 1.0f);
    NumberSetting mSmooth = NumberSetting("Smooth", "Aiming smoothness (0=instant)", 0.35f, 0.0f, 1.0f, 0.01f);
    NumberSetting mAimSpeed = NumberSetting("Aim Speed", "Max turn speed (deg/s)", 720.0f, 30.0f, 10600.0f, 10.0f);
    BoolSetting   mLock = BoolSetting("Target Lock", "Lock onto the first valid target until lost", true);
    BoolSetting   mWalls = BoolSetting("Through Walls", "Aim through walls (disables visibility checks)", false);
    BoolSetting   mBackview = BoolSetting("Backview", "Flip only the camera by 180° (visual), keep server aim correct", false);
    BoolSetting   mAlwaysCenter = BoolSetting("Always Center", "Always aim at AABB center (ignore Aim Point)", true);
    BoolSetting   mAim360 = BoolSetting("360", "Aim in 360° around you (ignore FOV limit)", false);
    BoolSetting   mNoFriends = BoolSetting("No Friends", "Do not aim at players from Friends/Teams", true);
    Aimbot() : ModuleBase<Aimbot>("Aim Assist", "Rotate camera to target and sync to server", ModuleCategory::Combat, 0, false)
    {
        addSettings(&mRange, &mFov, &mSmooth, &mAimSpeed, &mLock, &mWalls, &mBackview, &mAlwaysCenter, &mAim360, &mNoFriends);
        mFov.mIsVisible = std::function<bool()>([&]() { return !mAim360.mValue; });
        mNames = {
            {Lowercase, "Aim Assist"},
            {LowercaseSpaced, "Aim Assist"},
            {Normal, "Aim Assist"},
            {NormalSpaced, "Aim Assist"}
        };

        gFeatureManager->mDispatcher->listen<LookInputEvent, &Aimbot::onLookInputEvent>(this);
    }

    void onEnable() override;
    void onDisable() override;

    void onBaseTickEvent(BaseTickEvent& event);
    void onLookInputEvent(LookInputEvent& event);
    void onPacketOutEvent(PacketOutEvent& event);
    void onRenderEvent(RenderEvent& event);

private:
    Actor* mTarget{ nullptr };
    AABB   mTargetAABB{};
    bool   mHasTarget{ false };
    glm::vec2 mDesiredRotsDeg{ 0.f, 0.f }; // pitch, yaw
    Actor* acquireTarget(class Actor* player);
    glm::vec3 getAimPos(Actor* actor) const;
    static float angleDistance(float a, float b);
};

