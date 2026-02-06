#pragma once
//
// 09/09/2025.
//

#include <Features/Modules/Module.hpp>

class Surround : public ModuleBase<Surround> {
public:
    NumberSetting mRange = NumberSetting("Range", "The range at which to surround entities", 5, 3, 8, 0.1);
    NumberSetting mDelay = NumberSetting("Delay", "The delay in ms to place block", 0, 0, 3000, 10);
    BoolSetting mHotbarOnly = BoolSetting("Hotbar Only", "Only switch to blocks in the hotbar", true);
    BoolSetting mDebug = BoolSetting("Debug", "Send debug messages", false);
    BoolSetting mPlaceAll = BoolSetting("Place All", "Place all required blocks instantly each tick", true);
    NumberSetting mBlocksPerTick = NumberSetting("Blocks Per Tick", "Max blocks to place per tick when Place All is off", 8, 1, 64, 1);
    BoolSetting mPredict = BoolSetting("Predict", "Predict target movement and place blocks ahead", true);
    NumberSetting mPredictTicks = NumberSetting("Predict Ticks", "How many ticks ahead to predict", 2, 0, 10, 1);
    NumberSetting mPredictClamp = NumberSetting("Predict Clamp", "Max horizontal distance to predict (blocks)", 3.0f, 0.0f, 6.0f, 0.1f);
    BoolSetting mNoFriends = BoolSetting("No Friends", "Do not target friends/teammates", true);

    Surround() : ModuleBase("Surround", "Surround a player with blocks", ModuleCategory::Player, 0, false)
    {
        addSetting(&mRange);
        addSetting(&mDelay);
        addSetting(&mHotbarOnly);
        addSetting(&mDebug);
        addSetting(&mPlaceAll);
        addSetting(&mBlocksPerTick);
        addSetting(&mPredict);
        addSetting(&mPredictTicks);
        addSetting(&mPredictClamp);
        addSetting(&mNoFriends);

        mNames = {
              {Lowercase, "surround"},
                {LowercaseSpaced, "surround"},
                {Normal, "Surround"},
                {NormalSpaced, "Surround"}
        };
    };

    uint64_t mLastBlockPlaced = 0;
    int mLastSlot = 0;

    void onEnable();
    void onDisable() override;
    std::vector<glm::ivec3> getCollidingBlocks(Actor* target, bool usePrediction);
    std::vector<glm::ivec3> getPlacePositions(std::vector<glm::ivec3> blockList);
    glm::ivec3 getClosestPlacePos(glm::ivec3 pos, float distance, std::vector<glm::ivec3> collidingBlocks);
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
};