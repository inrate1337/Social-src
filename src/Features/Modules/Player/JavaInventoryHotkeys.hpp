#pragma once

#include <queue>
#include <string>

class JavaInventoryHotkeys : public ModuleBase<JavaInventoryHotkeys>
{
public:
    enum class Mode {
        Auto = 0,
        Packet = 1,
        Controller = 2
    };

    EnumSetting mMode = EnumSetting("Mode", "How to perform the move", 0, std::vector<std::string>{"Auto", "Packet", "Controller"});

    JavaInventoryHotkeys() : ModuleBase("JavaInventory", "Java-like hotbar swap keys in inventories", ModuleCategory::Player, 0, false)
    {
        addSetting(&mMode);
        mNames = {
            {Lowercase, "javainventory"},
            {LowercaseSpaced, "java inventory"},
            {Normal, "JavaInventory"},
            {NormalSpaced, "Java Inventory"}
        };
    }

    void onEnable() override;
    void onDisable() override;

    void onKeyEvent(class KeyEvent& event);
    void onContainerSlotHovered(class ContainerSlotHoveredEvent& event);
    void onContainerTick(class ContainerScreenTickEvent& event);

private:
    struct MoveRequest {
        std::string srcCollectionName;
        int32_t srcSlot;
        std::string dstCollectionName;
        int32_t dstSlot;
    };

    std::queue<MoveRequest> mMoveRequests{};
    int64_t mCurrentHoveredSlot = -1;
    std::string mCurrentCollectionName{};
    class ContainerScreenController* mLastContainer = nullptr;

    static int slotFromKey(int key);
    static bool canSwap(const std::string& collectionName);
    static bool isLocalSlotEmpty(const std::string& collectionName, int32_t slot);
    static bool tryControllerMove(class ContainerScreenController* controller, const MoveRequest& request);
    void clearQueue();
};
