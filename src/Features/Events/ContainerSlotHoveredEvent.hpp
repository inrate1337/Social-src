#pragma once

#include "Event.hpp"

#include <cstdint>
#include <string>

class ContainerSlotHoveredEvent : public Event {
public:
    class ContainerScreenController* mController{};
    std::string mCollectionName{};
    int64_t mHoveredSlot{};

    explicit ContainerSlotHoveredEvent(ContainerScreenController* controller, std::string collectionName, int64_t hoveredSlot)
        : mController(controller), mCollectionName(std::move(collectionName)), mHoveredSlot(hoveredSlot) {}
};

