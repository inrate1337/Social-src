#include "ContainerSlotHoveredHook.hpp"

#include <Features/Events/ContainerSlotHoveredEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/SigManager.hpp>

std::unique_ptr<Detour> ContainerSlotHoveredHook::mDetour;

int64_t ContainerSlotHoveredHook::onContainerSlotHovered(ContainerScreenController* csc, const std::string* collectionName, int64_t hoveredSlot)
{
    auto original = mDetour->getOriginal<&ContainerSlotHoveredHook::onContainerSlotHovered>();

    if (gFeatureManager && gFeatureManager->mDispatcher && collectionName)
    {
        auto holder = nes::make_holder<ContainerSlotHoveredEvent>(csc, *collectionName, hoveredSlot);
        gFeatureManager->mDispatcher->trigger(holder);
    }

    return original(csc, collectionName, hoveredSlot);
}

void ContainerSlotHoveredHook::init()
{
    uintptr_t func = SigManager::ContainerScreenController_onContainerSlotHovered;
    if (!func) func = SigManager::ContainerScreenController_onContainerSlotHovered_alt1;
    if (!func) func = SigManager::ContainerScreenController_onContainerSlotHovered_alt2;
    if (!func) func = SigManager::ContainerScreenController_onContainerSlotHovered_alt3;
    if (!func) return;
    mDetour = std::make_unique<Detour>("ContainerScreenController::_onContainerSlotHovered", reinterpret_cast<void*>(func), &ContainerSlotHoveredHook::onContainerSlotHovered);
}
