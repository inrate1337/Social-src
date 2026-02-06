#pragma once
#include <Features/Modules/Module.hpp>

class Nuker : public ModuleBase<Nuker> {
public:
    enum class BlockType {
        All,
        Specified
    };
    EnumSettingT<BlockType> mBlockType = EnumSettingT<BlockType>("BlockType", "The type of block to destroy", BlockType::All, "All", "Specified");
    NumberSetting mRange = NumberSetting("Range", "The max range for destroying blocks", 5, 0, 10, 0.01);
    NumberSetting mDestroySpeed = NumberSetting("Destroy Speed", "The destroy speed for Nuker", 1, 0.01, 1, 0.01);
    EnumSetting mAreaSize = EnumSetting("Area", "The size of the block area to destroy", 0, { "1x1", "3x3", "5x5", "7x7", "9x9", "10x10" });
    NumberSetting mDepth = NumberSetting("Depth", "How many blocks deep to destroy", 1, 1, 10, 1);
    BoolSetting mSwing = BoolSetting("Swing", "Swings when destroying blocks", false);
    BoolSetting mHotbarOnly = BoolSetting("Hotbar Only", "Only switch to tools in the hotbar", true);
    BoolSetting mRenderBlock = BoolSetting("Render Block", "Renders the block you are currently breaking", true);
    BoolSetting mInfiniteDurability = BoolSetting("Infinite Durability", "Infinite durability for tools (may cause issues!)", false);
    BoolSetting mTest = BoolSetting("Test", "test", false);
    BoolSetting mInstaBreak = BoolSetting("Instant Break", "Executes StartDestroy and DestroyBlock in the same tick", false);

    Nuker() : ModuleBase("Nuker", "Automatically breaks blocks", ModuleCategory::Player, 0, false) {
        addSettings(&mBlockType, &mRange, &mDestroySpeed, &mAreaSize, &mDepth, &mSwing, &mHotbarOnly, &mRenderBlock, &mInfiniteDurability, &mTest, &mInstaBreak);

        mNames = {
            {Lowercase, "nuker"},
            {LowercaseSpaced, "nuker"},
            {Normal, "Nuker"},
            {NormalSpaced, "Nuker"}
        };
    }

    glm::ivec3 mCurrentBlockPos = { 0, 0, 0 };
    int mCurrentBlockFace = -1;
    float mBreakingProgress = 0.f;
    bool mIsMiningBlock = false;
    bool mShouldRotate = false;
    bool mShouldSpoofSlot = false;
    bool mShouldSetbackSlot = false;
    int mPreviousSlot = -1;
    int mToolSlot = -1;

    uint64_t mLastBlockPlace = 0;
    int mLastPlacedBlockSlot = 0;

    std::string specifiedBlockID = "minecraft:stone";

    std::vector<glm::ivec3> offsetList = {
        glm::ivec3(0, -1, 0),
        glm::ivec3(0, 1, 0),
        glm::ivec3(0, 0, -1),
        glm::ivec3(0, 0, 1),
        glm::ivec3(-1, 0, 0),
        glm::ivec3(1, 0, 0),
    };

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onRenderEvent(class RenderEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
    void reset();
    void queueBlock(glm::ivec3 blockPos);
    bool isValidBlock(glm::ivec3 blockPos, bool requireExposed = true);
    void destroyArea(glm::ivec3 blockPos, int face);
    int getAreaSize() const;
    int getDepth() const;

    std::string getSettingDisplay() override {
        return mBlockType.mValues[mBlockType.as<int>()];
    }

};
