//
// Created by vastrakai on 7/12/2024.
//

#include "PreferenceManager.hpp"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <Utils/FileUtils.hpp>
#include <nlohmann/json.hpp>

#include "spdlog/spdlog.h"

static std::string trimCopy(const std::string& s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) start++;
    size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1])) end--;
    return s.substr(start, end - start);
}

static bool parseBoolLoose(const std::string& s, bool& out)
{
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    v = trimCopy(v);
    if (v == "1" || v == "true" || v == "yes" || v == "on") { out = true; return true; }
    if (v == "0" || v == "false" || v == "no" || v == "off") { out = false; return true; }
    return false;
}

static std::vector<std::string> splitList(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    std::istringstream iss(s);
    while (std::getline(iss, cur, ','))
    {
        cur = trimCopy(cur);
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

static std::string joinList(const std::vector<std::string>& v)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (i) out += ",";
        out += v[i];
    }
    return out;
}

std::shared_ptr<Preferences> PreferenceManager::load()
{
    std::string cfgPath = FileUtils::getSolsticeDir() + "preferences.cfg";
    std::string jsonPath = FileUtils::getSolsticeDir() + "preferences.json";

    auto prefs = std::make_shared<Preferences>();

    if (!FileUtils::fileExists(cfgPath) && FileUtils::fileExists(jsonPath))
    {
        try
        {
            std::ifstream file(jsonPath);
            if (file.good() && file.peek() != std::ifstream::traits_type::eof())
            {
                nlohmann::json j;
                file >> j;
                prefs->mDefaultConfigName = j.value("DefaultConfigName", "");
                if (j.contains("Friends") && j["Friends"].is_array())
                    prefs->mFriends = j["Friends"].get<std::vector<std::string>>();
                prefs->mFallbackToD3D11 = j.value("FallbackToD3D11", false);
                prefs->mEnforceDebugging = j.value("EnforceDebugging", false);
                prefs->mIrcName = j.value("IrcName", "");
                prefs->mIrcPrefix = j.value("IrcPrefix", "");
                prefs->mIrcServerUri = j.value("IrcServerUri", prefs->mIrcServerUri);
                prefs->mStreamerName = j.value("StreamerName", prefs->mStreamerName);
            }
        }
        catch (...)
        {
        }
        save(prefs);
        return prefs;
    }

    if (!FileUtils::fileExists(cfgPath))
    {
        spdlog::warn("Preferences file not found, creating new one.");
        save(prefs);
        return prefs;
    }

    std::ifstream file(cfgPath);
    if (!file.good()) {
        spdlog::error("Failed to open preferences file!");
        return prefs;
    }

    try
    {
        std::string line;
        while (std::getline(file, line))
        {
            line = trimCopy(line);
            if (line.empty()) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trimCopy(line.substr(0, eq));
            std::string value = trimCopy(line.substr(eq + 1));

            if (key == "DefaultConfigName") prefs->mDefaultConfigName = value;
            else if (key == "Friends") prefs->mFriends = splitList(value);
            else if (key == "FallbackToD3D11") parseBoolLoose(value, prefs->mFallbackToD3D11);
            else if (key == "EnforceDebugging") parseBoolLoose(value, prefs->mEnforceDebugging);
            else if (key == "IrcName") prefs->mIrcName = value;
            else if (key == "IrcPrefix") prefs->mIrcPrefix = value;
            else if (key == "IrcServerUri") prefs->mIrcServerUri = value;
            else if (key == "StreamerName") prefs->mStreamerName = value;
        }
    }
    catch (std::exception& e)
    {
        spdlog::error("Error parsing preferences file: {}", e.what());
        save(prefs);
        return prefs;
    }

    spdlog::info("Successfully loaded preferences!");
    return prefs;
}

void PreferenceManager::save(const std::shared_ptr<Preferences>& prefs)
{
    if (!prefs)
    {
        spdlog::error("Failed to save preferences, preferences object is null.");
        return;
    }

    FileUtils::createDirectory(FileUtils::getSolsticeDir());
    std::string path = FileUtils::getSolsticeDir() + "preferences.cfg";
    std::string tmpPath = path + ".tmp";

    try
    {
        std::ofstream file(tmpPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            spdlog::error("Error saving preferences: failed to open file");
            return;
        }

        file << "DefaultConfigName=" << prefs->mDefaultConfigName << "\n";
        file << "Friends=" << joinList(prefs->mFriends) << "\n";
        file << "FallbackToD3D11=" << (prefs->mFallbackToD3D11 ? "true" : "false") << "\n";
        file << "EnforceDebugging=" << (prefs->mEnforceDebugging ? "true" : "false") << "\n";
        file << "IrcName=" << prefs->mIrcName << "\n";
        file << "IrcPrefix=" << prefs->mIrcPrefix << "\n";
        file << "IrcServerUri=" << prefs->mIrcServerUri << "\n";
        file << "StreamerName=" << prefs->mStreamerName << "\n";
        file.flush();
        file.close();
    }
    catch (std::exception& e)
    {
        spdlog::error("Error saving preferences: {}", e.what());
        return;
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (ec)
    {
        spdlog::error("Error saving preferences: {}", ec.message());
        std::filesystem::remove(tmpPath, ec);
        return;
    }
    spdlog::info("Successfully saved preferences!");
}
