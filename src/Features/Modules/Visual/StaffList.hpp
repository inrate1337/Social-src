#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Features/Modules/Module.hpp>
#include <Utils/MiscUtils/DataStore.hpp>

#include "HudEditor.hpp"

inline class StaffList* gStaffList = nullptr;

class StaffList : public ModuleBase<StaffList>
{
public:
    BoolSetting mShowRank = BoolSetting("Show Rank", "Shows staff rank", true);

    StaffList() : ModuleBase("StaffList", "Shows online staff members", ModuleCategory::Visual, 0, false)
    {
        addSetting(&mShowRank);

        mNames = {
            {Lowercase, "stafflist"},
            {LowercaseSpaced, "staff list"},
            {Normal, "StaffList"},
            {NormalSpaced, "Staff List"}
        };

        gStaffList = this;

        mElement = std::make_unique<HudElement>();
        mElement->mAnchor = HudElement::Anchor::TopRight;
        mElement->mPos = { -12.f, 120.f };

        const char* moduleBaseType = ModuleBase<StaffList>::getTypeID();
        mElement->mParentTypeIdentifier = const_cast<char*>(moduleBaseType);

        if (HudEditor::gInstance)
        {
            HudEditor::gInstance->registerElement(mElement.get());
        }
    }

    class StaffEntry : public DataObject
    {
    public:
        std::string name{};
        std::string rank{};
        std::string displayName{};
        uint64_t storedAt = 0;

        nlohmann::json toJson() override
        {
            nlohmann::json json;
            json["name"] = name;
            json["rank"] = rank;
            json["displayName"] = displayName;
            json["storedAt"] = storedAt;
            return json;
        }

        void fromJson(nlohmann::json json) override
        {
            name = json.value("name", "");
            rank = json.value("rank", "");
            displayName = json.value("displayName", "");
            storedAt = json.value("storedAt", 0ull);
        }
    };

    struct OnlineEntry
    {
        std::string name;
        std::string rank;
        std::string displayName;
    };

    std::unique_ptr<HudElement> mElement;
    DataStore<StaffEntry> mStaffStore = DataStore<StaffEntry>("stafflist");

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onRenderEvent(class RenderEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);

    void addStaff(const std::string& name, const std::string& rank);
    bool removeStaff(const std::string& name);

private:
    void requestRefresh();

    std::mutex mOnlineMutex;
    std::vector<OnlineEntry> mOnlineStaff;

    std::future<std::vector<OnlineEntry>> mOnlineFuture;
    bool mFutureValid = false;

    uint64_t mLastRefreshMs = 0;
    bool mNeedsRefresh = true;
    std::unordered_map<std::string, OnlineEntry> mLastOnlineByLower;
    bool mHasInitialOnlineSnapshot = false;
};
