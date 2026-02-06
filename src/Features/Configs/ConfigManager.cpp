//
// Created by vastrakai on 7/2/2024.
//

#include "ConfigManager.hpp"

#include <fstream>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <Features/FeatureManager.hpp>
#include <Utils/FileUtils.hpp>
#include <nlohmann/json.hpp>
#include <Utils/MiscUtils/NotifyUtils.hpp>

#include "spdlog/spdlog.h"

static std::string sanitizeConfigName(const std::string& in)
{
    std::string out;
    out.reserve(in.size());

    auto isInvalid = [](unsigned char c) -> bool {
        if (c < 32) return true;
        switch (c) {
        case '<': case '>': case ':': case '"': case '/': case '\\': case '|': case '?': case '*':
            return true;
        default:
            return false;
        }
    };

    for (unsigned char c : in)
    {
        if (isInvalid(c)) continue;
        out.push_back((char)c);
    }

    while (!out.empty() && std::isspace((unsigned char)out.front())) out.erase(out.begin());
    while (!out.empty() && std::isspace((unsigned char)out.back())) out.pop_back();

    if (out == "." || out == "..") return "";
    return out;
}

std::string ConfigManager::getConfigPath()
{
    return FileUtils::getSolsticeDir() + "Configs\\";
}

std::string ConfigManager::getConfigFilePath(const std::string& name)
{
    const std::string clean = sanitizeConfigName(name);
    return getConfigPath() + clean + ".cfg";
}

std::string ConfigManager::getLegacyConfigFilePath(const std::string& name)
{
    const std::string clean = sanitizeConfigName(name);
    return getConfigPath() + clean + ".json";
}

bool ConfigManager::configExists(const std::string& name)
{
    const std::string clean = sanitizeConfigName(name);
    if (clean.empty()) return false;
    return FileUtils::fileExists(getConfigFilePath(clean)) || FileUtils::fileExists(getLegacyConfigFilePath(clean));
}

void ConfigManager::loadConfig(const std::string& name)
{
    try
    {
        const std::string clean = sanitizeConfigName(name);
        if (clean.empty())
        {
            spdlog::warn("Invalid config name {}", name);
            NotifyUtils::notify("Invalid config name.", 3.f, Notification::Type::Error);
            return;
        }

        FileUtils::createDirectory(FileUtils::getSolsticeDir());
        FileUtils::createDirectory(getConfigPath());

        std::string path = getConfigFilePath(clean);
        if (!FileUtils::fileExists(path))
        {
            std::string legacy = getLegacyConfigFilePath(clean);
            if (FileUtils::fileExists(legacy)) path = legacy;
        }

        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            spdlog::warn("Failed to open config {}", clean);
            NotifyUtils::notify("Failed to open config " + clean + ".", 3.f, Notification::Type::Error);
            return;
        }

        if (file.peek() == std::ifstream::traits_type::eof())
        {
            spdlog::warn("Config {} is empty", clean);
            NotifyUtils::notify("Config " + clean + " is empty.", 3.f, Notification::Type::Error);
            return;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        gFeatureManager->mModuleManager->deserialize(j, false);

        LastLoadedConfig = clean;

        spdlog::info("Loaded config " + clean + " successfully.");
        NotifyUtils::notify("Loaded config " + clean + "!", 3.f, Notification::Type::Info);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Failed to load config {}: {}", name, e.what());
        NotifyUtils::notify("Failed to load config " + name + ".", 3.f, Notification::Type::Error);
    }
}

void ConfigManager::saveConfig(const std::string& name)
{
    const std::string clean = sanitizeConfigName(name);
    if (clean.empty())
    {
        spdlog::warn("Invalid config name {}", name);
        NotifyUtils::notify("Invalid config name.", 3.f, Notification::Type::Error);
        return;
    }

    FileUtils::createDirectory(FileUtils::getSolsticeDir());
    FileUtils::createDirectory(getConfigPath());

    nlohmann::json j = gFeatureManager->mModuleManager->serialize();

    const std::string finalPath = getConfigFilePath(clean);
    const std::string tmpPath = finalPath + ".tmp";

    {
        std::ofstream file(tmpPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            spdlog::error("Failed to write config {}", clean);
            NotifyUtils::notify("Failed to save config " + clean + ".", 3.f, Notification::Type::Error);
            return;
        }
        file << j.dump(4);
        file.flush();
        file.close();
    }

    std::error_code ec;
    std::filesystem::remove(finalPath, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec)
    {
        spdlog::error("Failed to finalize config {}: {}", clean, ec.message());
        NotifyUtils::notify("Failed to finalize config " + clean + ".", 3.f, Notification::Type::Error);
        std::filesystem::remove(tmpPath, ec);
        return;
    }

    LastLoadedConfig = clean;

    spdlog::info("Config saved successfully.");
    NotifyUtils::notify("Saved config as " + clean + ".", 3.f, Notification::Type::Info);
}
