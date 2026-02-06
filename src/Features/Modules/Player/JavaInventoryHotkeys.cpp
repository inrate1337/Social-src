#include "JavaInventoryHotkeys.hpp"

#include <Features/Events/ContainerScreenTickEvent.hpp>
#include <Features/Events/ContainerSlotHoveredEvent.hpp>
#include <Features/Events/KeyEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Inventory/ContainerScreenController.hpp>
#include <SDK/Minecraft/Inventory/ItemStack.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>

void JavaInventoryHotkeys::onEnable()
{
    gFeatureManager->mDispatcher->listen<KeyEvent, &JavaInventoryHotkeys::onKeyEvent>(this);
    gFeatureManager->mDispatcher->listen<ContainerSlotHoveredEvent, &JavaInventoryHotkeys::onContainerSlotHovered>(this);
    gFeatureManager->mDispatcher->listen<ContainerScreenTickEvent, &JavaInventoryHotkeys::onContainerTick>(this);
}

void JavaInventoryHotkeys::onDisable()
{
    gFeatureManager->mDispatcher->deafen<KeyEvent, &JavaInventoryHotkeys::onKeyEvent>(this);
    gFeatureManager->mDispatcher->deafen<ContainerSlotHoveredEvent, &JavaInventoryHotkeys::onContainerSlotHovered>(this);
    gFeatureManager->mDispatcher->deafen<ContainerScreenTickEvent, &JavaInventoryHotkeys::onContainerTick>(this);
    clearQueue();
    mLastContainer = nullptr;
}

int JavaInventoryHotkeys::slotFromKey(int key)
{
    if (key >= '1' && key <= '9') return key - '1';
    if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9) return key - VK_NUMPAD1;
    return -1;
}

bool JavaInventoryHotkeys::canSwap(const std::string& collectionName)
{
    const bool isItemContainer = (collectionName.find("_item") != std::string::npos);
    const bool isRecipeContainer = (collectionName.find("recipe_") != std::string::npos);
    const bool isSearchContainerOrBar = (collectionName.find("search") != std::string::npos);
    return isItemContainer || (isRecipeContainer && !isSearchContainerOrBar);
}

bool JavaInventoryHotkeys::isLocalSlotEmpty(const std::string& collectionName, int32_t slot)
{
    if (slot < 0) return true;
    if (collectionName != "hotbar_items" && collectionName != "inventory_items") return false;

    auto* ci = ClientInstance::get();
    if (!ci) return false;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return false;
    auto* inv = lp->getSupplies();
    if (!inv) return false;
    auto* container = inv->getContainer();
    if (!container) return false;

    const int start = (collectionName == "hotbar_items") ? 0 : 9;
    ItemStack* item = container->getItem(start + slot);
    if (!item) return true;
    if (!item->mItem) return true;
    return (*item->mItem) == nullptr;
}

bool JavaInventoryHotkeys::tryControllerMove(ContainerScreenController* controller, const MoveRequest& request)
{
    if (!controller) return false;
    if (!SigManager::ContainerScreenController_handleTakeAll && !SigManager::ContainerScreenController_handleTakeAll_alt) return false;

    controller->_handleTakeAll(request.srcCollectionName, request.srcSlot);
    controller->_handlePlaceAll(request.dstCollectionName, request.dstSlot);
    controller->_handlePlaceAll(request.srcCollectionName, request.srcSlot);
    return true;
}

void JavaInventoryHotkeys::clearQueue()
{
    while (!mMoveRequests.empty()) mMoveRequests.pop();
}

void JavaInventoryHotkeys::onKeyEvent(KeyEvent& event)
{
    if (!mEnabled) return;
    if (!event.mPressed) return;

    auto* ci = ClientInstance::get();
    if (!ci) return;

    const std::string screen = ci->getScreenName();
    if (screen == "hud_screen" || screen == "pause_screen" || screen == "chat_screen") {
        clearQueue();
        return;
    }

    const int targetSlot = slotFromKey(event.mKey);
    if (targetSlot == -1) return;
    if (mCurrentHoveredSlot < 0) return;
    if (!canSwap(mCurrentCollectionName)) return;

    const int32_t hoveredSlot = static_cast<int32_t>(mCurrentHoveredSlot);
    const bool hoveredEmptyLocalInv = (mCurrentCollectionName == "inventory_items" && isLocalSlotEmpty("inventory_items", hoveredSlot));

    if (hoveredEmptyLocalInv)
        mMoveRequests.push(MoveRequest{"hotbar_items", targetSlot, mCurrentCollectionName, hoveredSlot});
    else
        mMoveRequests.push(MoveRequest{mCurrentCollectionName, hoveredSlot, "hotbar_items", targetSlot});
    event.cancel();
}

void JavaInventoryHotkeys::onContainerSlotHovered(ContainerSlotHoveredEvent& event)
{
    if (!mEnabled) return;
    mCurrentHoveredSlot = event.mHoveredSlot;
    mCurrentCollectionName = event.mCollectionName;
}

void JavaInventoryHotkeys::onContainerTick(ContainerScreenTickEvent& event)
{
    if (!mEnabled) return;
    auto* controller = event.mController;
    if (!controller) return;

    if (mLastContainer != controller) {
        clearQueue();
        mLastContainer = controller;
    }

    while (!mMoveRequests.empty())
    {
        const auto request = mMoveRequests.front();
        mMoveRequests.pop();

        if (!canSwap(request.srcCollectionName) && !canSwap(request.dstCollectionName)) continue;

        const auto mode = static_cast<Mode>(mMode.mValue);
        const bool allowController = (mode == Mode::Auto || mode == Mode::Controller);
        const bool allowPacket = (mode == Mode::Auto || mode == Mode::Packet);

        bool moved = false;
        if (allowController) moved = tryControllerMove(controller, request);
        if (!moved && allowPacket)
            controller->swap(request.srcCollectionName, request.srcSlot, request.dstCollectionName, request.dstSlot);
    }
}
