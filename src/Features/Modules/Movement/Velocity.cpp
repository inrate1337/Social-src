//
// Created by vastrakai on 7/2/2024.
//

#include "Velocity.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Network/Packets/SetActorMotionPacket.hpp>

void Velocity::onEnable()
{
    gFeatureManager->mDispatcher->listen<PacketInEvent, &Velocity::onPacketInEvent>(this);
}

void Velocity::onDisable()
{
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &Velocity::onPacketInEvent>(this);
}

void Velocity::onPacketInEvent(PacketInEvent& event) const
{
    if (!event.mPacket) return;
    PacketID id{};
    if (!TryCallWrapper([&]() { id = event.mPacket->getId(); })) return;
    if (id == PacketID::SetActorMotion)
    {
        auto packet = std::reinterpret_pointer_cast<SetActorMotionPacket>(event.mPacket);
        if (!packet) return;

        auto ci = ClientInstance::get();
        if (!ci) return;
        auto player = ci->getLocalPlayer();
        if (!player) return;
        int64_t playerRid = 0;
        if (!TryCallWrapper([&]() { playerRid = player->getRuntimeID(); })) return;

        if (packet->mRuntimeID == playerRid)
        {
            if (mMode.mValue == Mode::Full)
            {
                TryCallWrapper([&]() { event.setCancelled(true); });
            }
            else
            {
                glm::vec3 motion = packet->mMotion;
                motion.x *= mHorizontal.mValue;
                motion.z *= mHorizontal.mValue;
                motion.y *= mVertical.mValue;
                packet->mMotion = motion;
            }
        }
    }
}


