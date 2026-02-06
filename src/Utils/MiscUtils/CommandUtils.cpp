//
// Created by vastrakai on 7/5/2024.
//

#include "CommandUtils.hpp"

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/Packets/CommandRequestPacket.hpp>

#include "spdlog/spdlog.h"

void CommandUtils::executeCommand(const std::string& command)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    std::string cmd = command;
    if (!cmd.empty() && cmd.front() == '/') cmd.erase(0, 1);

    auto crq = MinecraftPackets::createPacket<CommandRequestPacket>();
    crq->mCommand = cmd;
    crq->mOrigin.mType = CommandOriginType::Player;
    crq->mOrigin.mUuid = mce::UUID::generate();
    crq->mOrigin.mRequestId = "";
    crq->mOrigin.mPlayerId = player->getRuntimeID();
    crq->mInternalSource = false;
    ClientInstance::get()->getPacketSender()->send(crq.get());
    spdlog::info("Sent command: {}", cmd);
}
