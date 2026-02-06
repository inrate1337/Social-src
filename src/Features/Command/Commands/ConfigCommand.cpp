#include "ConfigCommand.hpp"

#include <Features/Configs/ConfigManager.hpp>
#include <Features/Modules/Visual/Notifications.hpp>
#include <Utils/FileUtils.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <Utils/MiscUtils/NotifyUtils.hpp>

void ConfigCommand::execute(const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        ChatUtils::displayClientMessage("§c" + getUsage());
        return;
    }

    if (const std::string& action = args[1]; action == "load" || action == "l")
    {
        if (args.size() < 3)
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        const std::string& name = args[2];

        if (!ConfigManager::configExists(name))
        {
            ChatUtils::displayClientMessage("§cThe configuration §6'" + name + "' §cdoes not exist.");
            return;
        }

        try
        {
            ConfigManager::loadConfig(name); 
            ChatUtils::displayClientMessage("§aLoaded configuration §6" + name + "§a!");
        } catch (const std::exception& e)
        {
            ChatUtils::displayClientMessage("§cFailed to load configuration §6" + name + "§c.");
            ChatUtils::displayClientMessage("§cError: §6" + std::string(e.what()));
        } catch (...)
        {
            ChatUtils::displayClientMessage("§cFailed to load configuration §6" + name + "§c.");
            ChatUtils::displayClientMessage("§cUnknown error occurred.");
        }
    }
    else if (action == "save" || action == "s")
    {
        if (args.size() < 3)
        {
            if (ConfigManager::LastLoadedConfig.empty())
            {
                ChatUtils::displayClientMessage("§cNo configuration loaded. Please load a config first.");
                return;
            }

            ConfigManager::saveConfig(ConfigManager::LastLoadedConfig);
            ChatUtils::displayClientMessage("§aSaved configuration as §6" + ConfigManager::LastLoadedConfig + "§a.");
            return;
        }
        if (args[2].empty())
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        if (ConfigManager::configExists(args[2]) && ConfigManager::LastLoadedConfig != args[2])
        {
            if (args.size() < 4 || args.size() < 4 && args[3] != "overwrite")
            {
                NotifyUtils::notify(args[2] + " already exists!", 3.f, Notification::Type::Warning);
                ChatUtils::displayClientMessage("§eWARNING: §6" + args[2] + " §calready exists. Use §6.config save " + args[2] + " overwrite §cto overwrite it.");
                return;
            }
        }

        const std::string& name = args[2];
        ConfigManager::saveConfig(name);
        ChatUtils::displayClientMessage("§aSaved configuration as §6" + name + "§a.");
    }
    else if (action == "list" || action == "l")
    {
        ChatUtils::displayClientMessage("§aConfigurations:");
        for (const auto& f : FileUtils::listFiles(ConfigManager::getConfigPath()))
        {
            //пока что оставил поодреку json на время
            std::string file = f;
            if (file.ends_with(".cfg"))
            {
                file = file.substr(0, file.size() - 5);
            }
            else if (file.ends_with(".json"))
            {
                file = file.substr(0, file.size() - 5);
            }
            else continue;
            ChatUtils::displayClientMessage("§7- " + file);
        }

        ChatUtils::displayClientMessage("§aType §6.config load <name> §ato load a config.");
    }
    else if (action == "delete" || action == "del")
    {
        if (args.size() < 3)
        {
            ChatUtils::displayClientMessage("§c" + getUsage());
            return;
        }

        const std::string& name = args[2];

        if (!ConfigManager::configExists(name))
        {
            ChatUtils::displayClientMessage("§cThe configuration §6'" + name + "' §cdoes not exist.");
            return;
        }

        if (!FileUtils::deleteFile(ConfigManager::getConfigFilePath(name)) && !FileUtils::deleteFile(ConfigManager::getLegacyConfigFilePath(name)))
        {
            ChatUtils::displayClientMessage("§cUnable to delete configuration!");
            return;
        }

        ChatUtils::displayClientMessage("§aSuccessfully deleted configuration §6" + name + "§a.");
    }
    else if (action == "default" || action == "d")
    {
        if (args.size() < 3)
        {
            // Clear the current config tk mabe crash naxui
            Solstice::Prefs->mDefaultConfigName = "";
            PreferenceManager::save(Solstice::Prefs);
            ChatUtils::displayClientMessage("§eSuccessfully cleared the default configuration.");
            return;
        }

        const std::string& name = args[2];

        if (!ConfigManager::configExists(name))
        {
            ChatUtils::displayClientMessage("§cThe configuration §6'" + name + "' §cdoes not exist.");
            return;
        }

        Solstice::Prefs->mDefaultConfigName = name;
        PreferenceManager::save(Solstice::Prefs);
        ChatUtils::displayClientMessage("§aSuccessfully set the default configuration to §6" + name + "§a.");
    }
    else
    {
        ChatUtils::displayClientMessage("§c" + getUsage());
    }

}

std::vector<std::string> ConfigCommand::getAliases() const
{
    return {"c"};
}

std::string ConfigCommand::getDescription() const
{
    return "Manage configurations.";
}

std::string ConfigCommand::getUsage() const
{
    return "Usage: .config <load/save/list/delete/default> <name>";
}
