#pragma once
//
// Created by vastrakai on 7/5/2024.
//

#include <string>

class ContainerScreenController {
public:
    void handleAutoPlace(const std::string& name, int slot);
    void _handlePlaceAll(std::string collectionName, int32_t slot);
    void _handleTakeAll(std::string collectionName, int32_t slot);
    void swap(std::string srcCollectionName, int32_t srcSlot, std::string dstCollectionName, int32_t dstSlot);
    void* _tryExit();
};
