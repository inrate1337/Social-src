//
// Created by vastrakai on 7/5/2024.
//

#include "ContainerScreenController.hpp"

#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <Utils/MemUtils.hpp>

void ContainerScreenController::handleAutoPlace(const std::string& name, int slot)
{
    static auto func = SigManager::ContainerScreenController_handleAutoPlace;
    return MemUtils::callFastcall<void>(func, this, 0x7FFFFFFF, name, slot);
}

void* ContainerScreenController::_tryExit()
{
    // TODO: implement index signature instead of raw index
    return MemUtils::callVirtualFunc<void*>(OffsetProvider::ContainerScreenController_tryExit, this);
}

void ContainerScreenController::_handlePlaceAll(std::string collectionName, int32_t slot)
{
    MemUtils::callVirtualFunc<void, std::string, int32_t>(OffsetProvider::ContainerScreenController_handlePlaceAll, this, collectionName, slot);
}

void ContainerScreenController::_handleTakeAll(std::string collectionName, int32_t slot)
{
    static auto func = SigManager::ContainerScreenController_handleTakeAll;
    if (!func) func = SigManager::ContainerScreenController_handleTakeAll_alt;
    if (!func) return;
    MemUtils::callFastcall<void, ContainerScreenController*, std::string, int32_t>(func, this, collectionName, slot);
}

enum class ContainerCollectionType {
    Inventory,
    Hotbar,
    ContainerOutput,
    Other
};

static ContainerCollectionType cscGetCollectionType(const std::string& name)
{
    if (name == "hotbar_items") return ContainerCollectionType::Hotbar;
    if (name == "inventory_items") return ContainerCollectionType::Inventory;
    if (name.find("_output") != std::string::npos) return ContainerCollectionType::ContainerOutput;
    return ContainerCollectionType::Other;
}

static class ItemStack* cscGetLocalItemForCollection(ContainerCollectionType type, int slot)
{
    if (type != ContainerCollectionType::Hotbar && type != ContainerCollectionType::Inventory) return nullptr;

    auto* ci = ClientInstance::get();
    if (!ci) return nullptr;
    auto* lp = ci->getLocalPlayer();
    if (!lp) return nullptr;
    auto* inv = lp->getSupplies();
    if (!inv) return nullptr;
    auto* container = inv->getContainer();
    if (!container) return nullptr;

    const int startSlot = (type == ContainerCollectionType::Hotbar) ? 0 : 9;
    return container->getItem(startSlot + slot);
}

void ContainerScreenController::swap(std::string srcCollectionName, int32_t srcSlot, std::string dstCollectionName, int32_t dstSlot)
{
    const auto srcType = cscGetCollectionType(srcCollectionName);
    const auto dstType = cscGetCollectionType(dstCollectionName);

    if ((srcType == ContainerCollectionType::Hotbar || srcType == ContainerCollectionType::Inventory) &&
        (dstType == ContainerCollectionType::Hotbar || dstType == ContainerCollectionType::Inventory))
    {
        auto* ci = ClientInstance::get();
        if (!ci) return;
        auto* lp = ci->getLocalPlayer();
        if (!lp) return;
        auto* inv = lp->getSupplies();
        if (!inv) return;
        auto* container = inv->getContainer();
        if (!container) return;

        const int srcIndex = (srcType == ContainerCollectionType::Hotbar) ? srcSlot : (9 + srcSlot);
        const int dstIndex = (dstType == ContainerCollectionType::Hotbar) ? dstSlot : (9 + dstSlot);
        container->swapSlots(srcIndex, dstIndex);
        return;
    }

    auto* srcItemStack = cscGetLocalItemForCollection(srcType, srcSlot);
    auto* dstItemStack = cscGetLocalItemForCollection(dstType, dstSlot);

    const bool srcHasItem = (srcItemStack && srcItemStack->mItem && *srcItemStack->mItem);
    const bool dstHasItem = (dstItemStack && dstItemStack->mItem && *dstItemStack->mItem);

    if (srcType == ContainerCollectionType::ContainerOutput)
    {
        _handleTakeAll(srcCollectionName, srcSlot);
        _handlePlaceAll(dstCollectionName, dstSlot);
        return;
    }

    if (!srcHasItem && dstHasItem)
    {
        _handleTakeAll(dstCollectionName, dstSlot);
        _handlePlaceAll(srcCollectionName, srcSlot);
        _handlePlaceAll(dstCollectionName, dstSlot);
        return;
    }

    _handleTakeAll(srcCollectionName, srcSlot);
    _handlePlaceAll(dstCollectionName, dstSlot);
    _handlePlaceAll(srcCollectionName, srcSlot);
}
