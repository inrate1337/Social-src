//
// Created by vastrakai on 8/24/2024.
//

#include "IrcCommand.hpp"

#include <NewLight.hpp>
#include <Features/Configs/PreferenceManager.hpp>
#include <Features/IRC/IrcClient.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>

static std::string joinFrom(const std::vector<std::string>& args, size_t startIndex)
{
    std::string out;
    for (size_t i = startIndex; i < args.size(); i++)
    {
        if (!out.empty()) out += " ";
        out += args[i];
    }
    return out;
}

void IrcCommand::execute(const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        ChatUtils::displayClientMessage("§c" + getUsage());
        return;
    }

    if (args[1] == "list")
    {
        if (!IrcManager::isConnected())
        {
            ChatUtils::displayClientMessage("§cYou are not connected to IRC.");
            return;
        }
        // The server will send back the response message itself,
        // no need to display anything here
        IrcManager::requestListUsers();
    }
    else if (args[1] == "name")
    {
        if (args.size() < 3)
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        Solstice::Prefs->mIrcName = args[2];
        PreferenceManager::save(Solstice::Prefs);

        if (IrcManager::mClient) IrcManager::mClient->changeUsername();
        ChatUtils::displayClientMessage("§aIRC name set to §6" + Solstice::Prefs->mIrcName + "§a.");
    }
    else if (args[1] == "prefix")
    {
        if (args.size() < 3)
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        static const std::vector<std::string> presets = {
            "подстилка исо",
            "сосатель пенисов",
        };

        std::string prefixArg = args[2];
        if (prefixArg == "0" || prefixArg == "off" || prefixArg == "clear")
        {
            Solstice::Prefs->mIrcPrefix.clear();
        }
        else if (prefixArg == "1" || prefixArg == "2")
        {
            const size_t idx = static_cast<size_t>(std::stoul(prefixArg));
            Solstice::Prefs->mIrcPrefix = presets[idx - 1];
        }
        else
        {
            Solstice::Prefs->mIrcPrefix = joinFrom(args, 2);
        }

        PreferenceManager::save(Solstice::Prefs);
        if (IrcManager::mClient) IrcManager::mClient->sendPlayerIdentity(true);

        if (Solstice::Prefs->mIrcPrefix.empty())
            ChatUtils::displayClientMessage("§aIRC prefix cleared.");
        else
            ChatUtils::displayClientMessage("§aIRC prefix set to §6" + Solstice::Prefs->mIrcPrefix + "§a.");
    }
    else
    {
        ChatUtils::displayClientMessage("§c" + getUsage());
    }
}

std::vector<std::string> IrcCommand::getAliases() const
{
    return {};
}

std::string IrcCommand::getDescription() const
{
    return "Interact with the IRC client";
}

std::string IrcCommand::getUsage() const
{
    return "Usage: .irc <list|name|prefix> [name/prefix]";
}
