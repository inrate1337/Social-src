#include "NameProtect.hpp"
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/Packets/Packet.hpp>
#include <SDK/Minecraft/Network/Packets/TextPacket.hpp>
#include <Features/Events/DrawTextEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>

static void replacenaxuihuizalupa(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty() || from == to) return;
    size_t startPos = 0;
    while ((startPos = str.find(from, startPos)) != std::string::npos) {
        str.replace(startPos, from.length(), to);
        startPos += to.length();
    } //dont touch pidors
}

void NameProtect::onEnable() {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (player)
    {
        mOldLocalName = player->getLocalName();
        mOldNameTag = player->getNameTag();
    }

    gFeatureManager->mDispatcher->listen<BaseTickEvent, &NameProtect::onBaseTickEvent, nes::event_priority::ABSOLUTE_FIRST>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &NameProtect::onPacketInEvent, nes::event_priority::ABSOLUTE_FIRST>(this);
    gFeatureManager->mDispatcher->listen<DrawTextEvent, &NameProtect::onDrawTextEvent, nes::event_priority::ABSOLUTE_FIRST>(this);
}

void NameProtect::onDisable() {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (player)
    {
        if (!mOldLocalName.empty()) player->setLocalName(mOldLocalName);
        if (!mOldNameTag.empty()) player->setNametag(mOldNameTag);
    }

    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &NameProtect::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &NameProtect::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<DrawTextEvent, &NameProtect::onDrawTextEvent>(this);
}

void NameProtect::onBaseTickEvent(BaseTickEvent& event) {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (Solstice::Prefs) mNewName = Solstice::Prefs->mStreamerName;
    if (mOldLocalName.empty()) mOldLocalName = player->getLocalName();
    if (mOldNameTag.empty()) mOldNameTag = player->getNameTag();

    player->setNametag(mNewName);
}

void NameProtect::onPacketInEvent(PacketInEvent& event) {
    if (event.mPacket->getId() != PacketID::Text) return;

    auto packet = event.getPacket<TextPacket>();
    {
        std::string message = packet->mMessage;
        replacenaxuihuizalupa(message, mOldLocalName, mNewName);
        packet->mMessage = message;
    }

    if (packet->mFilteredMessage)
    {
        std::string filtered = *packet->mFilteredMessage;
        replacenaxuihuizalupa(filtered, mOldLocalName, mNewName);
        *packet->mFilteredMessage = std::move(filtered);
    }
}

void NameProtect::onDrawTextEvent(DrawTextEvent& event)
{
    if (!event.mText) return;
    replacenaxuihuizalupa(*event.mText, mOldLocalName, mNewName);
}
