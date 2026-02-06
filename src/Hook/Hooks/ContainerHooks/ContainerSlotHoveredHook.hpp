#pragma once

#include <Hook/Hook.hpp>

class ContainerSlotHoveredHook : public Hook {
public:
    ContainerSlotHoveredHook() : Hook() {
        mName = "ContainerScreenController::_onContainerSlotHovered";
    }

    static std::unique_ptr<Detour> mDetour;

    static int64_t onContainerSlotHovered(class ContainerScreenController* csc, const std::string* collectionName, int64_t hoveredSlot);
    void init() override;
};

