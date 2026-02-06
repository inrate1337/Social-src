#include "MacrosCommand.hpp"
#include <Features/FeatureManager.hpp>
#include <Features/Events/KeyEvent.hpp>
#include <Features/Events/ChatEvent.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <Utils/Keyboard.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/Packets/TextPacket.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>

std::map<int, std::string> MacrosCommand::mMacros;

MacrosCommand::MacrosCommand() : Command("macros")
{
    if (gFeatureManager && gFeatureManager->mDispatcher) {
        gFeatureManager->mDispatcher->listen<KeyEvent, &MacrosCommand::onKey>(this);
    }
}

MacrosCommand::~MacrosCommand()
{
    if (gFeatureManager && gFeatureManager->mDispatcher) {
        gFeatureManager->mDispatcher->deafen<KeyEvent, &MacrosCommand::onKey>(this);
    }
}

void MacrosCommand::execute(const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        ChatUtils::displayClientMessage(getUsage());
        return;
    }

    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "add")
    {
        if (args.size() < 4)
        {
            ChatUtils::displayClientMessage("Usage: .macros add <key> <command/message>");
            return;
        }

        std::string keyName = args[2];
        int key = Keyboard::getKeyId(keyName);

        if (key == 0)
        {
            ChatUtils::displayClientMessage("Invalid key: " + keyName);
            return;
        }

        std::string content;
        for (size_t i = 3; i < args.size(); i++)
        {
            content += args[i] + (i == args.size() - 1 ? "" : " ");
        }

        mMacros[key] = content;
        ChatUtils::displayClientMessage("Bound macro to " + keyName + ": " + content);
    }
    else if (action == "del" || action == "remove")
    {
        if (args.size() < 3)
        {
            ChatUtils::displayClientMessage("Usage: .macros del <key>");
            return;
        }

        std::string keyName = args[2];
        int key = Keyboard::getKeyId(keyName);

        if (mMacros.contains(key))
        {
            mMacros.erase(key);
            ChatUtils::displayClientMessage("Removed macro for " + keyName);
        }
        else
        {
            ChatUtils::displayClientMessage("No macro found for " + keyName);
        }
    }
    else if (action == "list")
    {
        if (mMacros.empty())
        {
            ChatUtils::displayClientMessage("No macros currently set.");
            return;
        }

        ChatUtils::displayClientMessage("Current Macros:");
        for (const auto& [key, content] : mMacros)
        {
            std::string keyName = Keyboard::getKey(key);
            ChatUtils::displayClientMessage("[" + keyName + "] " + content);
        }
    }
    else if (action == "clear")
    {
        mMacros.clear();
        ChatUtils::displayClientMessage("All macros cleared.");
    }
    else
    {
        ChatUtils::displayClientMessage(getUsage());
    }
}

std::vector<std::string> MacrosCommand::getAliases() const
{
    return { "m" };
}

std::string MacrosCommand::getDescription() const
{
    return "Execute commands or send messages with a key press";
}

std::string MacrosCommand::getUsage() const
{
    return "Usage: .macros <add/del/list/clear> [key] [text]";
}

void MacrosCommand::onKey(KeyEvent& event)
{
    if (!event.mPressed) return;

    auto instance = ClientInstance::get();
    if (!instance) return;

    if (instance->getScreenName() != "no_screen") return;

    if (!instance->getLocalPlayer()) return;

    if (!mMacros.contains(event.mKey))
        return;

    std::string content = mMacros[event.mKey];

    if (content.starts_with("."))
    {
        nes::event_holder<ChatEvent> eventHolder(content);
        if (gFeatureManager && gFeatureManager->mDispatcher) {
            gFeatureManager->mDispatcher->trigger<ChatEvent>(eventHolder);
            if (eventHolder->isCancelled()) return;
        }
    }

    auto packetSender = instance->getPacketSender();
    if (packetSender) {
        auto packet = MinecraftPackets::createPacket<TextPacket>();
        packet->mMessage = content;
        packet->mType = TextPacketType::Chat;
        packetSender->sendToServer(packet.get());
    }
}