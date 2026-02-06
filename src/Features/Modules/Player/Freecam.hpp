#pragma once

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/LookInputEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Modules/Module.hpp>

class Freecam : public ModuleBase<Freecam> {
public:
    NumberSetting mSpeed = NumberSetting("Speed", "Freecam speed", 8.0f, 0.1f, 50.0f, 0.1f);
    BoolSetting mCancelMovePackets = BoolSetting("Cancel Move Packets", "Prevent movement packets from being sent", true);
    BoolSetting mFreezeRotation = BoolSetting("Freeze Rotation", "Keep server rotation unchanged", true);
    BoolSetting mShowBody = BoolSetting("Show Body", "Render player model while in freecam", true);

    Freecam() : ModuleBase("Freecam", "Detach your camera from your body", ModuleCategory::Player, 0, false) {
        addSettings(&mSpeed, &mCancelMovePackets, &mFreezeRotation, &mShowBody);

        mNames = {
            {Lowercase, "freecam"},
            {LowercaseSpaced, "free cam"},
            {Normal, "Freecam"},
            {NormalSpaced, "Free Cam"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onLookInputEvent(class LookInputEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);

private:
    glm::vec3 mAnchorPos = {};
    glm::vec2 mAnchorRot = {};
    glm::vec3 mCamPos = {};
    glm::vec3 mTargetCamPos = {};

    int mPrevThirdPerson = 0;
    bool mPrevRenderCameraFlag = false;
    bool mPrevRenderPlayerModelFlag = false;

    bool mInitialized = false;
};
