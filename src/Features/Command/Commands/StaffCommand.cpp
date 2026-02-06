#include "StaffCommand.hpp"

#include <Features/Modules/Visual/StaffList.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <Utils/StringUtils.hpp>

static std::string trimQuotes(std::string s)
{
    s = std::string(StringUtils::trim(s));
    if (s.size() >= 2)
    {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))
        {
            s = s.substr(1, s.size() - 2);
        }
    }
    return s;
}

void StaffCommand::execute(const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        ChatUtils::displayClientMessage("§c" + getUsage());
        return;
    }

    if (!gStaffList)
    {
        ChatUtils::displayClientMessage("§cStaffList not find naxui.");
        return;
    }

    const std::string& subCommand = args[1];

    if (subCommand == "add")
    {
        if (args.size() < 4)
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        std::string name = trimQuotes(args[2]);
        std::string rank;
        for (size_t i = 3; i < args.size(); i++)
        {
            if (!rank.empty()) rank += " ";
            rank += args[i];
        }
        rank = trimQuotes(rank);

        if (name.empty() || rank.empty())
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        gStaffList->addStaff(name, rank);
        ChatUtils::displayClientMessage("§aAdded §6" + name + "§a as §6" + rank + "§a.");
        return;
    }

    if (subCommand == "remove")
    {
        if (args.size() < 3)
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        std::string name = trimQuotes(args[2]);
        if (name.empty())
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        if (!gStaffList->removeStaff(name))
        {
            ChatUtils::displayClientMessage("§c" + name + " is not in StaffList.");
            return;
        }

        ChatUtils::displayClientMessage("§cRemoved §6" + name + "§c.");
        return;
    }

    ChatUtils::displayClientMessage("§c" + getUsage());
}

std::vector<std::string> StaffCommand::getAliases() const
{
    return {};
}

std::string StaffCommand::getDescription() const
{
    return "Manage StaffList database";
}

std::string StaffCommand::getUsage() const
{
    return "Usage: .staff add \"<name>\" \"<rank>\" | .staff remove \"<name>\"";
}
