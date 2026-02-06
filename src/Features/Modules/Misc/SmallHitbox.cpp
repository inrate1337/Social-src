#include "SmallHitbox.hpp"
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

void SmallHitbox::onEnable() {
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &SmallHitbox::onPacketOut>(this);
}

void SmallHitbox::onDisable() {
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &SmallHitbox::onPacketOut>(this);
}

void SmallHitbox::onPacketOut(PacketOutEvent& event) {
    if (event.mPacket->getId() == PacketID::PlayerAuthInput) {
        auto pkt = event.getPacket<PlayerAuthInputPacket>();

        pkt->mInputData |= AuthInputAction::START_SWIMMING;
        pkt->mInputData &= ~AuthInputAction::STOP_SWIMMING;

        pkt->mInputData &= ~AuthInputAction::START_GLIDING;
        pkt->mInputData &= ~AuthInputAction::STOP_GLIDING;
        pkt->mInputData &= ~AuthInputAction::START_SNEAKING;
        pkt->mInputData &= ~AuthInputAction::SNEAKING;
        pkt->mInputData &= ~AuthInputAction::STOP_SNEAKING;
    }
}