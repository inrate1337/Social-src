//
// Created by alteik on 25/09/2024.
//

#include "EditionFaker.hpp"
#include <SDK/SigManager.hpp>
#include <Utils/Buffer.hpp>
#include <Features/Events/ConnectionRequestEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/ConnectionRequest.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

static uintptr_t func;
static uintptr_t func2;
static uintptr_t func3;
static uintptr_t func4;
static uintptr_t func5;

static bool isAddrValid(uintptr_t addr, size_t size = 1)
{
    if (!addr) return false;
    if (!MemUtils::isValidPtr(addr)) return false;
    if (size <= 1) return true;
    return MemUtils::isValidPtr(addr + size - 1);
}

void EditionFaker::onInit() {
    func = SigManager::ConnectionRequest_create_DeviceOS;
    func2 = SigManager::ConnectionRequest_create_DefaultInputMode;
    func3 = SigManager::ConnectionRequest_create_CurrentInputMode;
    func4 = SigManager::InputModeBypass;
    func5 = SigManager::InputModeBypassFix;
}

void EditionFaker::inject()
{
    if (sInjected) return;
    if (!isAddrValid(func, 8) || !isAddrValid(func2, 8) || !isAddrValid(func3, 8) || !isAddrValid(func4, 8) || !isAddrValid(func5, 8)) return;

    int32_t DefaultInputMode = (sUseCustom && sCustomInput >= 0) ? sCustomInput : mInputMethod.as<int>();
    int32_t Os = (sUseCustom && sCustomOs >= 0) ? sCustomOs : mOs.as<int>();

    if (!sOriginalsSaved)
    {
        MemUtils::ReadBytes((void*)func2, mOriginalInputData1, sizeof(mOriginalInputData1));
        MemUtils::ReadBytes((void*)func3, mOriginalInputData2, sizeof(mOriginalInputData2));
        MemUtils::ReadBytes((void*)(func + 1), mOriginalData, sizeof(mOriginalData));
        MemUtils::ReadBytes((void*)func4, mOriginalData1, sizeof(mOriginalData1));
        MemUtils::ReadBytes((void*)func5, mOriginalData2, sizeof(mOriginalData2));
        sOriginalsSaved = true;
    }

    { // default & current input mode spoof
        MemUtils::writeBytes(func2, "\xBF", 1);
        MemUtils::writeBytes(func2 + 1, &DefaultInputMode, sizeof(uint32_t));
        MemUtils::NopBytes(func2 + 5, 3);

        MemUtils::writeBytes(func3, "\xBF", 1);
        MemUtils::writeBytes(func3 + 1, &DefaultInputMode, sizeof(uint32_t));
        MemUtils::NopBytes(func3 + 5, 3);
    }

    { // os
        MemUtils::writeBytes(func + 1, &Os, sizeof(uint32_t));
    }

    { // input mode bypass
        mDetour1 = AllocateBuffer((void*)func4);
        mDetour2 = AllocateBuffer((void*)func5);
        if (!mDetour1 || !mDetour2)
        {
            if (mDetour1) FreeBuffer(mDetour1);
            if (mDetour2) FreeBuffer(mDetour2);
            mDetour1 = nullptr;
            mDetour2 = nullptr;
            return;
        }

        MemUtils::writeBytes((uintptr_t)mDetour1, mDetourBytes1, sizeof(mDetourBytes1));
        MemUtils::writeBytes((uintptr_t)mDetour2, mDetourBytes2, sizeof(mDetourBytes2));

        auto toOriginalAddrRip1 = MemUtils::GetRelativeAddress((uintptr_t)mDetour1 + sizeof(mDetourBytes1) + 1, func4 + 5);
        auto toOriginalAddrRip2 = MemUtils::GetRelativeAddress((uintptr_t)mDetour2 + sizeof(mDetourBytes2) + 1, func5 + 5);

        MemUtils::writeBytes((uintptr_t)mDetour1 + sizeof(mDetourBytes1), "\xE9", 1);
        MemUtils::writeBytes((uintptr_t)mDetour2 + sizeof(mDetourBytes2), "\xE9", 1);

        MemUtils::writeBytes((uintptr_t)mDetour1 + sizeof(mDetourBytes1) + 1, &toOriginalAddrRip1, sizeof(int32_t));
        MemUtils::writeBytes((uintptr_t)mDetour2 + sizeof(mDetourBytes2) + 1, &toOriginalAddrRip2, sizeof(int32_t));

        auto newRelRip1 = MemUtils::GetRelativeAddress(func4 + 1, (uintptr_t)mDetour1);
        auto newRelRip2 = MemUtils::GetRelativeAddress(func5 + 1, (uintptr_t)mDetour2);

        MemUtils::writeBytes(func4, "\xE9", 1);
        MemUtils::writeBytes(func5, "\xE9", 1);

        MemUtils::writeBytes(func4 + 1, &newRelRip1, sizeof(int32_t));
        MemUtils::writeBytes(func5 + 1, &newRelRip2, sizeof(int32_t));
    }

    sInjected = true;
    spoofEdition();
}

void EditionFaker::eject() {
    {
        if (!sInjected) return;
        if (!sOriginalsSaved) return;
        if (!isAddrValid(func, 8) || !isAddrValid(func2, 8) || !isAddrValid(func3, 8) || !isAddrValid(func4, 8) || !isAddrValid(func5, 8)) return;

        { // default & current input mode spoof
            MemUtils::writeBytes(func2, mOriginalInputData1, sizeof(mOriginalInputData1));
            MemUtils::writeBytes(func3, mOriginalInputData2, sizeof(mOriginalInputData2));
        }

        { // os
            MemUtils::writeBytes(func + 1, mOriginalData, sizeof(mOriginalData));
        }

        {  // input mode bypass
            MemUtils::writeBytes(func4, mOriginalData1, sizeof(mOriginalData1));
            MemUtils::writeBytes(func5, mOriginalData2, sizeof(mOriginalData2));

            if (mDetour1) FreeBuffer(mDetour1);
            if (mDetour2) FreeBuffer(mDetour2);
            mDetour1 = nullptr;
            mDetour2 = nullptr;
        }

        sInjected = false;
    }
}

void EditionFaker::spoofEdition() {


    int32_t InputMode = (sUseCustom && sCustomInput >= 0) ? sCustomInput : mInputMethod.as<int>();
    int32_t Os = (sUseCustom && sCustomOs >= 0) ? sCustomOs : mOs.as<int>();
    if (!sInjected) return;
    if (!isAddrValid(func, 8) || !isAddrValid(func2, 8) || !isAddrValid(func3, 8)) return;

    { // default & current input mode spoof
        MemUtils::writeBytes(func2 + 1, &InputMode, sizeof(uint32_t));
        MemUtils::writeBytes(func3 + 1, &InputMode, sizeof(uint32_t));
    }

    { // os
        MemUtils::writeBytes(func + 1, &Os, sizeof(uint32_t)); // changing directly, u can get this addr by using debugger in CE while joining world
    }

    {  // input mode bypass
        if (mDetour1 && MemUtils::isValidPtr((uintptr_t)mDetour1 + 2))
        {
            MemUtils::writeBytes((uintptr_t)mDetour1 + 2, &InputMode, sizeof(int32_t)); // spoofing only this cuz "bypass fix" should always be on 1 (mouse)
        }
    }
}

void EditionFaker::onEnable() {
    inject();
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &EditionFaker::onPacketOutEvent, nes::event_priority::ABSOLUTE_LAST>(this);
    gFeatureManager->mDispatcher->listen<ConnectionRequestEvent, &EditionFaker::onConnectionRequestEvent, nes::event_priority::ABSOLUTE_LAST>(this);
}

void EditionFaker::onDisable() {
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &EditionFaker::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<ConnectionRequestEvent, &EditionFaker::onConnectionRequestEvent>(this);
    eject();
}

void EditionFaker::onConnectionRequestEvent(ConnectionRequestEvent& event) {
    spoofEdition();
    event.mInputMode = (sUseCustom && sCustomInput >= 0) ? sCustomInput : mInputMethod.as<int>();
    event.mUiProfile = mUiProfile.as<int>();
}

void EditionFaker::onPacketOutEvent(PacketOutEvent& event) {
    if (!event.mPacket) return;
    if (event.mPacket->getId() == PacketID::PlayerAuthInput) {
        auto paip = event.getPacket<PlayerAuthInputPacket>();
        if (!paip) return;
        paip->mInputMode = (InputMode)mInputMethod.as<int>();
    }
}
