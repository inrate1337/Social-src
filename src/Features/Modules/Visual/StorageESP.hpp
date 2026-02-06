#pragma once

#include <Features/Modules/Module.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <imgui.h>
#include <unordered_map>
#include <mutex>

class StorageESP : public ModuleBase<StorageESP>
{
public:
    bool mRenderTracers = true;

    StorageESP() : ModuleBase("StorageESP", "Draws boxes around chests instantly", ModuleCategory::Visual, 0, false) {
        mNames = {
            {Lowercase, "storageesp"},
            {LowercaseSpaced, "storage esp"},
            {Normal, "StorageESP"},
            {NormalSpaced, "Storage ESP"}
        };
    }

    struct FoundStorage
    {
        AABB aabb;
        ImColor color;
    };

    std::unordered_map<BlockPos, FoundStorage> mFoundBlocks;
    std::mutex mStorageMutex;

    void reset();
    void scanChunk(ChunkPos chunkPos);
    bool processSub(ChunkPos chunkPos, int subChunkIndex);

    void onEnable() override;
    void onDisable() override;
    void onBlockChangedEvent(class BlockChangedEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketInEvent(class PacketInEvent& event);
    void onRenderEvent(class RenderEvent& event);
};