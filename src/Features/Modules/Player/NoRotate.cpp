//
// Created by ssi on 10/27/2024.
//

#include "NoRotate.hpp"
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

void NoRotate::onEnable()
{
    gFeatureManager->mDispatcher->listen<PacketInEvent, &NoRotate::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &NoRotate::onPacketOutEvent>(this);
}

void NoRotate::onDisable()
{
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &NoRotate::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &NoRotate::onPacketOutEvent>(this);
}

void NoRotate::onPacketInEvent(PacketInEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) {
        return;
    }

    if (event.mPacket->getId() == PacketID::MovePlayer)
    {
        auto packet = event.getPacket<MovePlayerPacket>();

        if (packet->mPlayerID == player->getRuntimeID())
        {
            auto rot = player->getActorRotationComponent();
            if (rot)
            {
                rot->mPitch = packet->mRot.x;
                rot->mYaw = packet->mRot.y;
                rot->mOldPitch = packet->mRot.x;
                rot->mOldYaw = packet->mRot.y;
            }

            auto headRot = player->getActorHeadRotationComponent();
            if (headRot)
            {
                headRot->mHeadRot = packet->mYHeadRot;
                headRot->mOldHeadRot = packet->mYHeadRot;
            }

            auto bodyRot = player->getMobBodyRotationComponent();
            if (bodyRot)
            {
                bodyRot->yBodyRot = packet->mRot.y;
                bodyRot->yOldBodyRot = packet->mRot.y;
            }
        }
    }
}

void NoRotate::onPacketOutEvent(PacketOutEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) {
        return;
    }

    glm::vec2 rot{};
    float headYaw = 0.f;
    bool hasRot = false;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput)
    {
        auto packet = event.getPacket<PlayerAuthInputPacket>();
        rot = packet->mRot;
        headYaw = packet->mYHeadRot;
        hasRot = true;
    }
    else if (event.mPacket->getId() == PacketID::MovePlayer)
    {
        auto packet = event.getPacket<MovePlayerPacket>();
        if (packet->mPlayerID != player->getRuntimeID())
        {
            return;
        }

        rot = packet->mRot;
        headYaw = packet->mYHeadRot;
        hasRot = true;
    }

    if (!hasRot)
    {
        return;
    }

    auto rotComp = player->getActorRotationComponent();
    if (rotComp)
    {
        rotComp->mPitch = rot.x;
        rotComp->mYaw = rot.y;
        rotComp->mOldPitch = rot.x;
        rotComp->mOldYaw = rot.y;
    }

    auto headComp = player->getActorHeadRotationComponent();
    if (headComp)
    {
        headComp->mHeadRot = headYaw;
        headComp->mOldHeadRot = headYaw;
    }

    auto bodyComp = player->getMobBodyRotationComponent();
    if (bodyComp)
    {
        bodyComp->yBodyRot = rot.y;
        bodyComp->yOldBodyRot = rot.y;
    }
}
