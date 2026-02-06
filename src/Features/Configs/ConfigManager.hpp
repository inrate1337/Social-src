#pragma once
//
// Created by vastrakai on 7/2/2024.
//

#include <string>

class ConfigManager
{
public:
    static inline std::string LastLoadedConfig = "";
    static std::string getConfigPath();
    static std::string getConfigFilePath(const std::string& name);
    static std::string getLegacyConfigFilePath(const std::string& name);
    static bool configExists(const std::string& name);

    static void loadConfig(const std::string& name);
    static void saveConfig(const std::string& name);

};
