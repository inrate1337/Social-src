#include "StorageESP.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BlockChangedEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Events/BaseTickEvent.hpp>

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerActionPacket.hpp>
#include <SDK/Minecraft/World/BlockLegacy.hpp>
#include <SDK/Minecraft/World/Chunk/LevelChunk.hpp>
#include <SDK/Minecraft/World/Chunk/SubChunkBlockStorage.hpp>

constexpr int CHEST = 54;
constexpr int ENDER_CHEST = 130;
constexpr int TRAPPED_CHEST = 146;
constexpr int BARREL = 458;
constexpr int SHULKER_BOX = 218;
constexpr int UNDYED_SHULKER_BOX = 205;

const ImColor COLOR_CHEST = ImColor(255, 165, 0);
const ImColor COLOR_ENDER = ImColor(128, 0, 128);
const ImColor COLOR_SHULKER = ImColor(128, 0, 128);
const ImColor COLOR_BARREL = ImColor(255, 255, 0);

constexpr int RENDER_DIST_BLOCKS = 100;
constexpr int CHUNK_RADIUS = (RENDER_DIST_BLOCKS / 16) + 1;

bool isStorageBlock(int id)
{
    return id == CHEST || id == ENDER_CHEST || id == TRAPPED_CHEST ||
        id == BARREL || id == SHULKER_BOX || id == UNDYED_SHULKER_BOX;
}

ImColor getStorageColor(int id)
{
    if (id == ENDER_CHEST) return COLOR_ENDER;
    if (id == SHULKER_BOX || id == UNDYED_SHULKER_BOX) return COLOR_SHULKER;
    if (id == BARREL) return COLOR_BARREL;
    return COLOR_CHEST;
}

void StorageESP::reset()
{
    std::lock_guard<std::mutex> lock(mStorageMutex);
    mFoundBlocks.clear();
}

void StorageESP::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &StorageESP::onRenderEvent, nes::event_priority::NORMAL>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &StorageESP::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<BlockChangedEvent, &StorageESP::onBlockChangedEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &StorageESP::onPacketInEvent>(this);
    reset();
}

void StorageESP::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &StorageESP::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &StorageESP::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<BlockChangedEvent, &StorageESP::onBlockChangedEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &StorageESP::onPacketInEvent>(this);
    reset();
}

bool StorageESP::processSub(ChunkPos chunkPos, int index)
{
    auto ci = ClientInstance::get();
    if (!ci->getLevelRenderer()) return false;

    BlockSource* blockSource = ci->getBlockSource();
    if (!blockSource) return false;

    LevelChunk* chunk = blockSource->getChunk(chunkPos);
    if (!chunk) return false;

    if (index < 0 || index >= chunk->getSubChunks()->size()) return false;

    auto& subChunk = (*chunk->getSubChunks())[index];
    SubChunkBlockStorage* blockReader = subChunk.blockReadPtr;
    if (!blockReader) return false;

    bool foundAny = false;

    for (uint16_t x = 0; x < 16; x++)
    {
        for (uint16_t z = 0; z < 16; z++)
        {
            for (uint16_t y = 0; y < 16; y++)
            {
                uint16_t elementId = (x * 0x10 + z) * 0x10 + y;
                const Block* found = blockReader->getElement(elementId);
                int blockId = found->mLegacy->getBlockId();

                if (blockId == 0) continue;

                if (isStorageBlock(blockId))
                {
                    BlockPos pos;
                    pos.x = (chunkPos.x * 16) + x;
                    pos.z = (chunkPos.y * 16) + z;
                    pos.y = y + (subChunk.subchunkIndex * 16);

                    mFoundBlocks[pos] = { AABB(pos, glm::vec3(1.f, 1.f, 1.f)), getStorageColor(blockId) };
                    foundAny = true;
                }
            }
        }
    }
    return foundAny;
}

void StorageESP::scanChunk(ChunkPos chunkPos)
{
    auto ci = ClientInstance::get();
    if (!ci->getBlockSource()) return;

    LevelChunk* chunk = ci->getBlockSource()->getChunk(chunkPos);
    if (!chunk) return;

    size_t subChunkCount = chunk->getSubChunks()->size();
    for (int i = 0; i < subChunkCount; i++)
    {
        TRY_CALL([&]() {
            processSub(chunkPos, i);
            });
    }
}

void StorageESP::onBaseTickEvent(BaseTickEvent& event)
{
    if (!ClientInstance::get()->getLevelRenderer()) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    std::lock_guard<std::mutex> lock(mStorageMutex);

    ChunkPos center = ChunkPos(*player->getPos());

    static int currentX = -CHUNK_RADIUS;
    static int currentZ = -CHUNK_RADIUS;
    static ChunkPos lastCenter = { 0, 0 };

    if (lastCenter.x != center.x || lastCenter.y != center.y) {
        lastCenter = center;
        currentX = -CHUNK_RADIUS;
        currentZ = -CHUNK_RADIUS;
    }

    int chunksProcessed = 0;
    int maxChunksPerTick = 45;

    while (chunksProcessed < maxChunksPerTick) {
        ChunkPos pos = { center.x + currentX, center.y + currentZ };
        scanChunk(pos);
        chunksProcessed++;

        currentZ++;
        if (currentZ > CHUNK_RADIUS) {
            currentZ = -CHUNK_RADIUS;
            currentX++;
            if (currentX > CHUNK_RADIUS) {
                currentX = -CHUNK_RADIUS;
                break;
            }
        }
    }

    for (auto it = mFoundBlocks.begin(); it != mFoundBlocks.end();)
    {
        if (glm::distance(glm::vec3(it->first), *player->getPos()) > RENDER_DIST_BLOCKS + 16.f) {
            it = mFoundBlocks.erase(it);
        }
        else {
            ++it;
        }
    }
}

void StorageESP::onBlockChangedEvent(BlockChangedEvent& event)
{
    std::lock_guard<std::mutex> lock(mStorageMutex);
    int id = event.mNewBlock->mLegacy->getBlockId();
    if (isStorageBlock(id)) {
        mFoundBlocks[event.mBlockPos] = { AABB(event.mBlockPos, glm::vec3(1.f, 1.f, 1.f)), getStorageColor(id) };
    }
    else {
        mFoundBlocks.erase(event.mBlockPos);
    }
}

void StorageESP::onPacketInEvent(PacketInEvent& event)
{
    if (event.mPacket->getId() == PacketID::ChangeDimension ||
        (event.mPacket->getId() == PacketID::PlayerAction && event.getPacket<PlayerActionPacket>()->mAction == PlayerActionType::Respawn))
    {
        reset();
    }
}

void StorageESP::onRenderEvent(RenderEvent& event)
{
    if (!ClientInstance::get()->getLevelRenderer()) return;

    std::lock_guard<std::mutex> lock(mStorageMutex);
    auto drawList = ImGui::GetBackgroundDrawList();
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    glm::vec3 playerPos = *player->getPos();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 screenCenter = { displaySize.x / 2.0f, displaySize.y / 2.0f };

    for (auto& [pos, storage] : mFoundBlocks)
    {
        if (glm::distance(glm::vec3(pos), playerPos) > RENDER_DIST_BLOCKS) continue;

        ImColor color = storage.color;
        std::vector<ImVec2> imPoints = MathUtils::getImBoxPoints(storage.aabb);

        if (imPoints.empty()) continue;

        drawList->AddPolyline(imPoints.data(), imPoints.size(), color, 0, 2.0f);
        drawList->AddConvexPolyFilled(imPoints.data(), imPoints.size(), ImColor(color.Value.x, color.Value.y, color.Value.z, 0.25f));

        if (mRenderTracers)
        {
            ImVec2 centerBox = { 0.f, 0.f };
            for (const auto& p : imPoints) {
                centerBox.x += p.x;
                centerBox.y += p.y;
            }
            centerBox.x /= imPoints.size();
            centerBox.y /= imPoints.size();

            drawList->AddLine(screenCenter, centerBox, color, 1.5f);
        }
    }
}