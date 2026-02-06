#pragma once
//
// Created by vastrakai on 7/12/2024.
//
#include <memory>
#include <string>
#include <vector>

struct Preferences
{
    std::string mDefaultConfigName;
    std::vector<std::string> mFriends;
    bool mFallbackToD3D11 = false;
    bool mEnforceDebugging = false;
    std::string mIrcName = "";
    std::string mIrcPrefix = "";
    std::string mIrcServerUri = "wss://server-pzyv.onrender.com:10000";
    std::string mStreamerName = "NewLight User";
};

class PreferenceManager {
public:
    static std::shared_ptr<Preferences> load();
    static void save(const std::shared_ptr<Preferences>& prefs);
};
