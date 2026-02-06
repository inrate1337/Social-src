#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <string>
#include <vector>

class BaseTickEvent;
class ContainerScreenTickEvent;
enum class ContainerType : char;

class AutobuyController
{
public:
    struct ItemDefinition
    {
        std::string key;
        std::string name;
        uint64_t id = 0;
        std::string icon;
    };

    struct ItemSettings
    {
        bool enabled = false;
        std::string price;
        int quantity = 1;
    };

    struct PriceMatch
    {
        std::string itemName;
        uint64_t itemId = 0;
        std::string price;
        std::string threshold;
        uint64_t timeMs = 0;
    };

    struct ContainerItem
    {
        int slot = 0;
        bool hasItem = false;
        std::string name;
        std::string internalName;
        std::string customName;
        uint64_t id = 0;
        uint64_t aux = 0;
        int count = 0;
        std::string price;
        std::vector<std::string> loreRaw;
        std::vector<std::string> loreClean;
        std::vector<std::pair<int, int>> enchants;
        std::optional<int> damage;
        std::vector<std::string> nbtKeys;
        std::string nbtText;
    };

    void start();
    void stop();
    bool isRunning() const;
    std::optional<std::string> getLastSeenPrice() const;

    size_t getItemCount();
    const ItemDefinition& getItemDefinition(size_t index);
    ItemSettings getItemSettings(size_t index);
    void getConfigSnapshot(std::vector<ItemDefinition>& outItems, std::vector<ItemSettings>& outSettings);
    const std::vector<PriceMatch>& getRecentMatches() const;
    const std::vector<ContainerItem>& getContainerItems() const;
    const std::string& getOpenContainerName() const;
    const std::string& getOpenScreenName() const;

    void setItemEnabled(size_t index, bool enabled);
    void setItemPrice(size_t index, std::string price);
    void setItemQuantity(size_t index, int quantity);

private:
    struct SlotSnapshot
    {
        int slot = 0;
        bool hasItem = false;
        uint64_t id = 0;
        uint64_t aux = 0;
        int count = 0;
        std::string internalName;
        std::string customName;
        std::vector<std::string> loreRaw;
    };

    struct ScanPlan
    {
        uint64_t seq = 0;
        int confirmSlot = -1;
        int refreshSlot = -1;
        int buySlot = -1;
        uint64_t buyThresholdCents = 0;
        short buyItemId = 0;
        std::string buyThresholdText;
        std::optional<std::string> lastSeenPrice;
    };

    void onBaseTickEvent(BaseTickEvent& event);
    void onContainerScreenTickEvent(ContainerScreenTickEvent& event);
    void ensureConfigLoaded();
    void loadConfig();
    void saveConfig();
    void startScanThread();
    void stopScanThread();
    void scanThreadMain();

    bool mRunning = false;
    bool mListening = false;
    bool mContainerTickListening = false;
    bool mSentAh = false;
    ContainerType mLastContainerType;
    std::array<uint64_t, 54> mSlotHashes{};
    std::optional<std::string> mLastSeenPrice;
    uint64_t mLastRefreshClickMs = 0;
    uint64_t mLastBuyClickMs = 0;
    uint64_t mLastConfirmClickMs = 0;
    uint64_t mLastAnyClickMs = 0;
    uint64_t mLastContainerReadMs = 0;

    std::mutex mConfigMutex{};

    bool mConfigInitialized = false;
    bool mConfigLoadStarted = false;
    bool mConfigUserEdited = false;
    bool mConfigLoaded = false;
    std::vector<ItemDefinition> mItems;
    std::vector<ItemSettings> mItemSettings;
    std::vector<PriceMatch> mRecentMatches;
    std::vector<ContainerItem> mContainerItems;
    std::string mOpenContainerName;
    std::string mOpenScreenName;

    std::thread mScanThread{};
    std::mutex mScanMutex{};
    std::condition_variable mScanCv{};
    bool mScanStop = false;
    bool mScanHasWork = false;
    uint64_t mScanSeq = 0;
    std::vector<SlotSnapshot> mPendingSnapshot{};

    std::mutex mPlanMutex{};
    ScanPlan mLatestPlan{};
};

AutobuyController& GetAutobuyController();
