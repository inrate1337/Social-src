#include "AutobuyController.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/ContainerScreenTickEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Inventory/ContainerManagerModel.hpp>
#include <SDK/Minecraft/Inventory/ContainerScreenController.hpp>
#include <SDK/Minecraft/Inventory/Item.hpp>
#include <SDK/Minecraft/Inventory/ItemStack.hpp>
#include <Utils/FileUtils.hpp>
#include <Utils/Logger.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/CommandUtils.hpp>
#include <Utils/StringUtils.hpp>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <limits>
#include <magic_enum.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "spdlog/spdlog.h"

namespace
{
    uint64_t hashCombine(uint64_t h, uint64_t v)
    {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }

    uint64_t hashString(const std::string& s)
    {
        return static_cast<uint64_t>(std::hash<std::string>{}(s));
    }

    std::vector<std::string> extractLoreLines(CompoundTag* tag)
    {
        std::vector<std::string> lore;
        if (!tag) return lore;

        auto displayVar = tag->get("display");
        if (!displayVar || displayVar->type != Tag::Type::Compound) return lore;

        auto display = displayVar->asCompoundTag();
        if (!display) return lore;

        auto loreVar = display->get("Lore");
        if (!loreVar || loreVar->type != Tag::Type::List) return lore;

        auto list = loreVar->asListTag();
        if (!list) return lore;

        lore.reserve(list->val.size());
        for (Tag* entry : list->val)
        {
            if (!entry) continue;
            if (entry->getId() == Tag::Type::String)
            {
                lore.push_back(reinterpret_cast<StringTag*>(entry)->val);
            }
            else
            {
                lore.push_back(entry->toString());
            }
        }

        return lore;
    }

    std::optional<int> extractDamageValue(CompoundTag* tag)
    {
        if (!tag) return std::nullopt;
        auto dmgVar = tag->get("Damage");
        if (!dmgVar) return std::nullopt;

        if (dmgVar->type == Tag::Type::Int) return dmgVar->asIntTag()->val;
        if (dmgVar->type == Tag::Type::Short) return dmgVar->asShortTag()->val;
        if (dmgVar->type == Tag::Type::Byte) return dmgVar->asByteTag()->val;

        return std::nullopt;
    }

    void appendCompoundTagText(std::string& out, CompoundTag* tag, int depth);

    void appendCompoundTagVariantText(std::string& out, CompoundTagVariant& var, int depth)
    {
        if (out.size() > 12000) return;
        if (depth > 6)
        {
            out += "...";
            return;
        }
        switch (var.getTagType())
        {
        case Tag::Type::Compound:
        {
            auto* comp = var.asCompoundTag();
            appendCompoundTagText(out, comp, depth + 1);
            break;
        }
        case Tag::Type::List:
        {
            auto* list = var.asListTag();
            if (!list)
            {
                out += "ListTag(null)";
                break;
            }
            out += "[";
            bool first = true;
            for (Tag* entry : list->val)
            {
                if (out.size() > 12000) break;
                if (!first) out += ", ";
                first = false;
                if (!entry)
                {
                    out += "null";
                    continue;
                }

                if (entry->getId() == Tag::Type::Compound)
                {
                    appendCompoundTagText(out, reinterpret_cast<CompoundTag*>(entry), depth + 1);
                }
                else if (entry->getId() == Tag::Type::String)
                {
                    out += reinterpret_cast<StringTag*>(entry)->val;
                }
                else
                {
                    out += entry->toString();
                }
            }
            out += "]";
            break;
        }
        default:
            out += var.toString();
            break;
        }
    }

    void appendCompoundTagText(std::string& out, CompoundTag* tag, int depth)
    {
        if (out.size() > 12000) return;
        if (!tag)
        {
            out += "null";
            return;
        }
        if (depth > 6)
        {
            out += "{...}";
            return;
        }

        out += "{";
        bool first = true;
        for (auto& [key, var] : tag->data)
        {
            if (out.size() > 12000) break;
            if (!first) out += ", ";
            first = false;
            out += key;
            out += ":";
            appendCompoundTagVariantText(out, var, depth + 1);
        }
        out += "}";
    }

    void logChunked(const char* prefix, const std::string& text)
    {
        if (text.empty()) return;
        constexpr size_t chunkSize = 1800;
        for (size_t i = 0; i < text.size(); i += chunkSize)
        {
            spdlog::info("{}{}", prefix, text.substr(i, chunkSize));
        }
    }

    uint64_t hashItemStack(ItemStack* stack)
    {
        if (!stack || !stack->mItem) return 0;

        Item* item = stack->getItem();

        uint64_t h = 0xcbf29ce484222325ull;
        h = hashCombine(h, static_cast<uint64_t>(item->mItemId));
        h = hashCombine(h, static_cast<uint64_t>(stack->mAuxValue));
        h = hashCombine(h, static_cast<uint64_t>(static_cast<uint8_t>(stack->mCount)));
        h = hashCombine(h, hashString(item->mName));

        const std::string customName = stack->getCustomName();
        if (!customName.empty()) h = hashCombine(h, hashString(customName));

        auto enchants = stack->gatherEnchants();
        for (const auto& [id, lvl] : enchants)
        {
            h = hashCombine(h, static_cast<uint64_t>(id));
            h = hashCombine(h, static_cast<uint64_t>(lvl));
        }

        if (auto dmg = extractDamageValue(stack->mCompoundTag); dmg.has_value())
        {
            h = hashCombine(h, static_cast<uint64_t>(*dmg));
        }

        auto lore = extractLoreLines(stack->mCompoundTag);
        for (const auto& line : lore)
        {
            h = hashCombine(h, hashString(line));
        }

        return h;
    }

    std::optional<std::string> extractPriceFromLoreLines(const std::vector<std::string>& lore)
    {
        for (const auto& rawLine : lore)
        {
            std::string line = ColorUtils::removeColorCodes(rawLine);
            const size_t pricePos = line.find("Цена");
            if (pricePos == std::string::npos) continue;

            size_t i = line.find(':', pricePos);
            if (i == std::string::npos) i = pricePos;
            else i++;

            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;

            size_t end = line.find('$', i);
            if (end == std::string::npos) end = line.size();
            if (end <= i) continue;

            std::string_view priceView = StringUtils::trim(std::string_view(line).substr(i, end - i));
            if (priceView.empty()) continue;

            bool hasDigit = false;
            for (char c : priceView)
            {
                if (c >= '0' && c <= '9')
                {
                    hasDigit = true;
                    break;
                }
            }

            if (!hasDigit) continue;

            return std::string(priceView);
        }

        return std::nullopt;
    }


    std::vector<AutobuyController::ContainerItem> readContainerItems(ContainerManagerModel* model)
    {
        constexpr int maxSlots = 54;
        std::vector<AutobuyController::ContainerItem> items;
        items.reserve(maxSlots);
        if (!model) return items;

        for (int i = 0; i < maxSlots; i++)
        {
            AutobuyController::ContainerItem ci;
            ci.slot = i;

            ItemStack* stack = model->getSlot(i);
            if (!stack || !stack->mItem)
            {
                items.push_back(std::move(ci));
                continue;
            }

            Item* item = stack->getItem();
            if (!item)
            {
                items.push_back(std::move(ci));
                continue;
            }

            ci.hasItem = true;
            ci.id = static_cast<uint64_t>(item->mItemId);
            ci.aux = static_cast<uint64_t>(stack->mAuxValue);
            ci.count = static_cast<int>(stack->mCount);
            ci.internalName = item->mName;
            ci.customName = stack->getCustomName();

            const std::string displayName = ci.customName.empty() ? ci.internalName : ci.customName;
            ci.name = ColorUtils::removeColorCodes(displayName);

            ci.loreRaw = extractLoreLines(stack->mCompoundTag);
            ci.loreClean.reserve(ci.loreRaw.size());
            for (const auto& rawLine : ci.loreRaw)
            {
                ci.loreClean.push_back(ColorUtils::removeColorCodes(rawLine));
            }
            if (auto price = extractPriceFromLoreLines(ci.loreRaw); price.has_value())
            {
                ci.price = *price;
            }

            items.push_back(std::move(ci));
        }

        return items;
    }

    bool isConfirmBuyItem(const AutobuyController::ContainerItem& ci)
    {
        if (!ci.hasItem) return false;
        if (ci.aux != 0) return false;
        if (StringUtils::containsIgnoreCase(ci.internalName, "light_block_3")) return true;
        if (StringUtils::containsIgnoreCase(ci.name, "Подтвердить покупку")) return true;
        return false;
    }

    bool isAuctionRefreshItem(const AutobuyController::ContainerItem& ci)
    {
        if (!ci.hasItem) return false;
        if (ci.aux != 0) return false;
        if (ci.count != 1) return false;
        if (StringUtils::containsIgnoreCase(ci.internalName, "ender_eye")) return true;
        return false;
    }

    std::optional<uint64_t> parsePriceToCents(std::string_view s)
    {
        auto parseDigits = [](std::string_view v) -> std::optional<uint64_t> {
            uint64_t value = 0;
            bool hasDigit = false;
            for (char c : v)
            {
                if (c < '0' || c > '9') continue;
                hasDigit = true;
                const uint64_t digit = static_cast<uint64_t>(c - '0');
                if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10ull) return std::nullopt;
                value = (value * 10ull) + digit;
            }
            if (!hasDigit) return std::nullopt;
            return value;
        };

        size_t lastDigitPos = std::string_view::npos;
        for (size_t i = s.size(); i-- > 0;)
        {
            char c = s[i];
            if (c >= '0' && c <= '9')
            {
                lastDigitPos = i;
                break;
            }
        }
        if (lastDigitPos == std::string_view::npos) return std::nullopt;

        size_t decimalPos = std::string_view::npos;
        if (lastDigitPos >= 2)
        {
            size_t candidate = lastDigitPos - 2;
            char sep = s[candidate];
            if ((sep == '.' || sep == ',') &&
                (s[candidate + 1] >= '0' && s[candidate + 1] <= '9') &&
                (s[candidate + 2] >= '0' && s[candidate + 2] <= '9'))
            {
                decimalPos = candidate;
            }
        }

        if (decimalPos != std::string_view::npos)
        {
            auto whole = parseDigits(s.substr(0, decimalPos));
            if (!whole.has_value()) return std::nullopt;

            const uint64_t cents = static_cast<uint64_t>((s[decimalPos + 1] - '0') * 10 + (s[decimalPos + 2] - '0'));
            if (*whole > (std::numeric_limits<uint64_t>::max() - cents) / 100ull) return std::nullopt;
            return (*whole * 100ull) + cents;
        }

        auto whole = parseDigits(s);
        if (!whole.has_value()) return std::nullopt;
        if (*whole > std::numeric_limits<uint64_t>::max() / 100ull) return std::nullopt;
        return (*whole * 100ull);
    }

    std::string getAutobuyConfigPath()
    {
        return FileUtils::getSolsticeDir() + "Databases\\autobuy.json";
    }

    std::vector<AutobuyController::ItemDefinition> buildDefaultItems()
    {
        std::vector<AutobuyController::ItemDefinition> items;
        items.reserve(27);

        items.push_back({ "nether_sword", "Nether Sword", 640, "p" });
        items.push_back({ "nether_pickaxe", "Nether Pickaxe", 0, "p" });
        items.push_back({ "nether_axe", "Nether Axe", 0, "p" });
        items.push_back({ "nether_shovel", "Nether Shovel", 0, "p" });
        items.push_back({ "nether_hoe", "Nether Hoe", 0, "p" });
        items.push_back({ "nether_helmet", "Nether Helmet", 0, "p" });
        items.push_back({ "nether_chestplate", "Nether Chestplate", 0, "p" });
        items.push_back({ "nether_leggings", "Nether Leggings", 0, "p" });
        items.push_back({ "nether_boots", "Nether Boots", 0, "p" });

        items.push_back({ "diamond_sword", "Diamond Sword", 0, "p" });
        items.push_back({ "diamond_pickaxe", "Diamond Pickaxe", 0, "p" });
        items.push_back({ "diamond_axe", "Diamond Axe", 0, "p" });
        items.push_back({ "diamond_shovel", "Diamond Shovel", 0, "p" });
        items.push_back({ "diamond_hoe", "Diamond Hoe", 0, "p" });
        items.push_back({ "diamond_helmet", "Diamond Helmet", 0, "p" });
        items.push_back({ "diamond_chestplate", "Diamond Chestplate", 0, "p" });
        items.push_back({ "diamond_leggings", "Diamond Leggings", 0, "p" });
        items.push_back({ "diamond_boots", "Diamond Boots", 0, "p" });

        items.push_back({ "sfera_karatelya", "Сфера карателя", 20386, "worldsfera.png" });
        items.push_back({ "sfera_vlastelina", "Сфера властелина", 20388, "afina.png" });
        items.push_back({ "sfera_nadezhdy", "Сфера надежды", 20381, "magmasfera.png" });
        items.push_back({ "sfera_proryva", "Сфера прорыва", 20382, "firesfera.png" });
        items.push_back({ "sfera_vozmezdiya", "Сфера возмездия", 20380, "firesfera.png" });
        items.push_back({ "sfera_skorosti", "Сфера скорости", 20383, "poisonsfera.png" });
        items.push_back({ "sfera_stoykosti", "Сфера стойкости", 20385, "purple2sfera.png" });
        items.push_back({ "sfera_khranitelya", "Сфера хранителя", 20387, "mixsfera.png" });
        items.push_back({ "sfera_sily", "Сфера силы", 20384, "purplesfera.png" });

        return items;
    }

    AutobuyController gAutobuyController;
}

AutobuyController& GetAutobuyController()
{
    return gAutobuyController;
}

void AutobuyController::start()
{
    if (!Logger::initialized) Logger::initialize();

    mRunning = true;
    mSentAh = false;
    mLastContainerType = ContainerType::Inventory;
    mSlotHashes.fill(0);
    mLastSeenPrice.reset();
    mRecentMatches.clear();
    mContainerItems.clear();
    mOpenContainerName.clear();
    mOpenScreenName.clear();
    mLastRefreshClickMs = 0;
    mLastBuyClickMs = 0;
    mLastConfirmClickMs = 0;
    mLastAnyClickMs = 0;
    mLastContainerReadMs = 0;

    startScanThread();

    if (!mListening && gFeatureManager && gFeatureManager->mDispatcher)
    {
        gFeatureManager->mDispatcher->listen<BaseTickEvent, &AutobuyController::onBaseTickEvent>(this);
        mListening = true;
    }
    if (!mContainerTickListening && gFeatureManager && gFeatureManager->mDispatcher)
    {
        gFeatureManager->mDispatcher->listen<ContainerScreenTickEvent, &AutobuyController::onContainerScreenTickEvent>(this);
        mContainerTickListening = true;
    }
}

void AutobuyController::stop()
{
    mRunning = false;
    mSentAh = false;
    mLastContainerType = ContainerType::Inventory;
    mSlotHashes.fill(0);
    mLastSeenPrice.reset();
    mRecentMatches.clear();
    mContainerItems.clear();
    mOpenContainerName.clear();
    mOpenScreenName.clear();
    mLastRefreshClickMs = 0;
    mLastBuyClickMs = 0;
    mLastConfirmClickMs = 0;
    mLastAnyClickMs = 0;
    mLastContainerReadMs = 0;

    stopScanThread();

    if (mListening && gFeatureManager && gFeatureManager->mDispatcher)
    {
        gFeatureManager->mDispatcher->deafen<BaseTickEvent, &AutobuyController::onBaseTickEvent>(this);
        mListening = false;
    }
    if (mContainerTickListening && gFeatureManager && gFeatureManager->mDispatcher)
    {
        gFeatureManager->mDispatcher->deafen<ContainerScreenTickEvent, &AutobuyController::onContainerScreenTickEvent>(this);
        mContainerTickListening = false;
    }
}

bool AutobuyController::isRunning() const
{
    return mRunning;
}

std::optional<std::string> AutobuyController::getLastSeenPrice() const
{
    return mLastSeenPrice;
}

size_t AutobuyController::getItemCount()
{
    ensureConfigLoaded();
    return mItems.size();
}

const AutobuyController::ItemDefinition& AutobuyController::getItemDefinition(size_t index)
{
    ensureConfigLoaded();
    if (index >= mItems.size()) index = 0;
    return mItems[index];
}

AutobuyController::ItemSettings AutobuyController::getItemSettings(size_t index)
{
    ensureConfigLoaded();
    if (index >= mItemSettings.size()) index = 0;
    return mItemSettings[index];
}

void AutobuyController::getConfigSnapshot(std::vector<ItemDefinition>& outItems, std::vector<ItemSettings>& outSettings)
{
    ensureConfigLoaded();
    std::scoped_lock lock(mConfigMutex);
    outItems = mItems;
    outSettings = mItemSettings;
}

const std::vector<AutobuyController::PriceMatch>& AutobuyController::getRecentMatches() const
{
    return mRecentMatches;
}

const std::vector<AutobuyController::ContainerItem>& AutobuyController::getContainerItems() const
{
    return mContainerItems;
}

const std::string& AutobuyController::getOpenContainerName() const
{
    return mOpenContainerName;
}

const std::string& AutobuyController::getOpenScreenName() const
{
    return mOpenScreenName;
}

void AutobuyController::setItemEnabled(size_t index, bool enabled)
{
    ensureConfigLoaded();
    std::scoped_lock lock(mConfigMutex);
    if (index >= mItemSettings.size()) return;
    mConfigUserEdited = true;
    mItemSettings[index].enabled = enabled;
    saveConfig();
}

void AutobuyController::setItemPrice(size_t index, std::string price)
{
    ensureConfigLoaded();
    std::scoped_lock lock(mConfigMutex);
    if (index >= mItemSettings.size()) return;
    mConfigUserEdited = true;
    mItemSettings[index].price = std::move(price);
    saveConfig();
}

void AutobuyController::setItemQuantity(size_t index, int quantity)
{
    ensureConfigLoaded();
    std::scoped_lock lock(mConfigMutex);
    if (index >= mItemSettings.size()) return;
    mConfigUserEdited = true;
    mItemSettings[index].quantity = std::max(1, quantity);
    saveConfig();
}

void AutobuyController::ensureConfigLoaded()
{
    {
        std::scoped_lock lock(mConfigMutex);
        if (!mConfigInitialized)
        {
            mItems = buildDefaultItems();
            mItemSettings.clear();
            mItemSettings.resize(mItems.size());
            mConfigInitialized = true;
        }
        if (mConfigLoadStarted) return;
        mConfigLoadStarted = true;
    }

    std::thread([this]() { loadConfig(); }).detach();
}

    void AutobuyController::loadConfig()
    {
        const std::string path = getAutobuyConfigPath();
    if (!FileUtils::fileExists(path))
    {
        saveConfig();
        {
            std::scoped_lock lock(mConfigMutex);
            mConfigLoaded = true;
        }
        return;
    }

    try
    {
        std::ifstream file(path);
        nlohmann::json j;
        file >> j;
        file.close();

        if (!j.is_object())
        {
            std::scoped_lock lock(mConfigMutex);
            mConfigLoaded = true;
            return;
        }
        if (!j.contains("items") || !j["items"].is_array())
        {
            std::scoped_lock lock(mConfigMutex);
            mConfigLoaded = true;
            return;
        }

        bool migrated = false;
        {
            std::scoped_lock lock(mConfigMutex);
            if (!mConfigUserEdited)
            {
                for (const auto& entry : j["items"])
                {
                    if (!entry.is_object()) continue;
                    std::string matchKey;
                    if (entry.contains("key") && entry["key"].is_string())
                    {
                        matchKey = entry["key"].get<std::string>();
                    }
                    else if (entry.contains("name") && entry["name"].is_string())
                    {
                        matchKey = entry["name"].get<std::string>();
                    }
                    else
                    {
                        continue;
                    }

                    auto it = std::find_if(mItems.begin(), mItems.end(), [&](const ItemDefinition& d) {
                        return d.key == matchKey || d.name == matchKey;
                    });
                    if (it == mItems.end()) continue;
                    const size_t idx = static_cast<size_t>(std::distance(mItems.begin(), it));

                    if (entry.contains("enabled") && entry["enabled"].is_boolean())
                    {
                        mItemSettings[idx].enabled = entry["enabled"].get<bool>();
                    }
                    if (entry.contains("price") && entry["price"].is_string())
                    {
                        mItemSettings[idx].price = entry["price"].get<std::string>();
                    }
                    if (entry.contains("quantity") && entry["quantity"].is_number_integer())
                    {
                        mItemSettings[idx].quantity = std::max(1, entry["quantity"].get<int>());
                    }
                    if (entry.contains("id") && entry["id"].is_number_unsigned())
                    {
                        mItems[idx].id = entry["id"].get<uint64_t>();
                    }
                }

                for (auto& def : mItems)
                {
                    if (def.key == "nether_sword" && def.id == 639)
                    {
                        def.id = 640;
                        migrated = true;
                    }
                }
            }
            mConfigLoaded = true;
        }

        if (migrated) saveConfig();
    }
    catch (const std::exception& e)
    {
        spdlog::error("[Autobuy] Failed to load config: {}", e.what());
        std::scoped_lock lock(mConfigMutex);
        mConfigLoaded = true;
    }
}

void AutobuyController::saveConfig()
{
    try
    {
        std::scoped_lock lock(mConfigMutex);
        nlohmann::json j;
        j["items"] = nlohmann::json::array();
        for (size_t i = 0; i < mItems.size(); i++)
        {
            nlohmann::json entry;
            entry["key"] = mItems[i].key;
            entry["name"] = mItems[i].name;
            entry["id"] = mItems[i].id;
            entry["enabled"] = mItemSettings[i].enabled;
            entry["price"] = mItemSettings[i].price;
            entry["quantity"] = mItemSettings[i].quantity;
            j["items"].push_back(std::move(entry));
        }

        std::ofstream file(getAutobuyConfigPath());
        file << j.dump(4);
        file.close();
    }
    catch (const std::exception& e)
    {
        spdlog::error("[Autobuy] Failed to save config: {}", e.what());
    }
}

void AutobuyController::startScanThread()
{
    if (mScanThread.joinable()) return;

    {
        std::scoped_lock lock(mScanMutex);
        mScanStop = false;
        mScanHasWork = false;
        mScanSeq = 0;
        mPendingSnapshot.clear();
    }
    {
        std::scoped_lock lock(mPlanMutex);
        mLatestPlan = {};
    }

    mScanThread = std::thread(&AutobuyController::scanThreadMain, this);
}

void AutobuyController::stopScanThread()
{
    {
        std::scoped_lock lock(mScanMutex);
        mScanStop = true;
        mScanHasWork = true;
    }
    mScanCv.notify_one();

    if (mScanThread.joinable())
        mScanThread.join();
}

void AutobuyController::scanThreadMain()
{
    struct ThresholdInfo
    {
        uint64_t thresholdNumber = 0;
        std::string thresholdText;
        std::string itemName;
        std::string matchKey;
        std::string matchName;
        short matchId = 0;
    };

    while (true)
    {
        std::vector<SlotSnapshot> snapshot;
        uint64_t seq = 0;
        {
            std::unique_lock lock(mScanMutex);
            mScanCv.wait(lock, [&]() { return mScanHasWork; });
            if (mScanStop) break;
            mScanHasWork = false;
            snapshot = std::move(mPendingSnapshot);
            seq = mScanSeq;
        }

        std::vector<ItemDefinition> defs;
        std::vector<ItemSettings> cfgs;
        {
            std::scoped_lock lock(mConfigMutex);
            defs = mItems;
            cfgs = mItemSettings;
        }

        std::unordered_map<short, ThresholdInfo> thresholdsById;
        thresholdsById.reserve(defs.size());
        std::vector<ThresholdInfo> thresholdsByText;
        thresholdsByText.reserve(defs.size());
        for (size_t i = 0; i < defs.size(); i++)
        {
            const auto& def = defs[i];
            const auto& cfg = cfgs[i];
            if (!cfg.enabled) continue;
            if (cfg.price.empty()) continue;

            if (auto parsed = parsePriceToCents(cfg.price); parsed.has_value())
            {
                ThresholdInfo info;
                info.thresholdNumber = *parsed;
                info.thresholdText = cfg.price;
                info.itemName = def.name;
                info.matchKey = def.key;
                info.matchName = def.name;
                info.matchId = static_cast<short>(def.id);

                if (def.id != 0)
                    thresholdsById.emplace(static_cast<short>(def.id), info);
                thresholdsByText.push_back(std::move(info));
            }
        }

        auto checkHit = [](const ThresholdInfo& th, const std::string& internalName, const std::string& cleanName) -> bool {
            if (th.matchKey.empty() && th.matchName.empty()) return false;
            if (!th.matchKey.empty() && StringUtils::containsIgnoreCase(internalName, th.matchKey)) return true;
            if (!th.matchName.empty() && StringUtils::containsIgnoreCase(cleanName, th.matchName)) return true;
            return false;
        };

        int confirmSlot = -1;
        int refreshSlot = -1;
        int bestBuySlot = -1;
        uint64_t bestBuyPrice = std::numeric_limits<uint64_t>::max();
        uint64_t bestBuyThreshold = 0;
        short bestBuyItemId = 0;
        std::string bestBuyThresholdText;

        std::optional<uint64_t> bestSeenPrice;
        std::optional<std::string> bestSeenPriceText;

        for (const auto& ss : snapshot)
        {
            if (!ss.hasItem) continue;

            const std::string displayName = ss.customName.empty() ? ss.internalName : ss.customName;
            const std::string cleanName = ColorUtils::removeColorCodes(displayName);

            if (confirmSlot == -1)
            {
                if (ss.aux == 0 &&
                    (StringUtils::containsIgnoreCase(ss.internalName, "light_block_3") ||
                     StringUtils::containsIgnoreCase(cleanName, "Подтвердить покупку")))
                {
                    confirmSlot = ss.slot;
                }
            }

            if (refreshSlot == -1)
            {
                if (ss.aux == 0 && ss.count == 1 && StringUtils::containsIgnoreCase(ss.internalName, "ender_eye"))
                    refreshSlot = ss.slot;
            }

            auto priceText = extractPriceFromLoreLines(ss.loreRaw);
            if (!priceText.has_value()) continue;

            const auto parsedPrice = parsePriceToCents(*priceText);
            if (!parsedPrice.has_value()) continue;

            if (!bestSeenPrice.has_value() || *parsedPrice < *bestSeenPrice)
            {
                bestSeenPrice = *parsedPrice;
                bestSeenPriceText = *priceText;
            }

            const short itemId = static_cast<short>(ss.id);
            auto it = thresholdsById.find(itemId);

            const ThresholdInfo* chosen = nullptr;
            if (it != thresholdsById.end())
            {
                chosen = &it->second;
            }
            else
            {
                for (const auto& th : thresholdsByText)
                {
                    if (checkHit(th, ss.internalName, cleanName))
                    {
                        chosen = &th;
                        break;
                    }
                }
            }
            if (!chosen) continue;

            if (*parsedPrice > chosen->thresholdNumber) continue;

            if (*parsedPrice < bestBuyPrice)
            {
                bestBuySlot = ss.slot;
                bestBuyPrice = *parsedPrice;
                bestBuyThreshold = chosen->thresholdNumber;
                bestBuyItemId = itemId;
                bestBuyThresholdText = chosen->thresholdText;
            }
        }

        ScanPlan plan;
        plan.seq = seq;
        plan.confirmSlot = confirmSlot;
        plan.refreshSlot = refreshSlot;
        plan.buySlot = bestBuySlot;
        plan.buyThresholdCents = bestBuyThreshold;
        plan.buyItemId = bestBuyItemId;
        plan.buyThresholdText = std::move(bestBuyThresholdText);
        if (bestSeenPriceText.has_value())
            plan.lastSeenPrice = *bestSeenPriceText;

        {
            std::scoped_lock lock(mPlanMutex);
            if (plan.seq >= mLatestPlan.seq)
            {
                mLatestPlan = std::move(plan);
                mLastSeenPrice = mLatestPlan.lastSeenPrice;
            }
        }
    }
}

void AutobuyController::onContainerScreenTickEvent(ContainerScreenTickEvent& event)
{
    if (!mRunning) return;
    ensureConfigLoaded();

    auto csc = event.mController;
    if (!csc) return;

    auto ci = ClientInstance::get();
    if (!ci) return;

    const std::string screenName = ci->getScreenName();
    const bool isAuctionScreen = (screenName == "large_chest_screen" || screenName == "chest_screen");
    if (!isAuctionScreen) return;

    auto player = ci->getLocalPlayer();
    if (!player) return;

    auto model = player->getContainerManagerModel();
    if (!model) return;

    const uint64_t now = NOW;
    ScanPlan plan;
    {
        std::scoped_lock lock(mPlanMutex);
        plan = mLatestPlan;
    }

    auto slotHasConfirmItem = [&](int slot) -> bool {
        if (slot < 0 || slot >= 54) return false;
        ItemStack* stack = model->getSlot(slot);
        if (!stack || !stack->mItem) return false;
        Item* item = stack->getItem();
        if (!item) return false;
        if (stack->mAuxValue != 0) return false;
        std::string name = stack->getCustomName();
        if (name.empty()) name = item->mName;
        name = ColorUtils::removeColorCodes(name);
        if (StringUtils::containsIgnoreCase(item->mName, "light_block_3")) return true;
        if (StringUtils::containsIgnoreCase(name, "Подтвердить покупку")) return true;
        return false;
    };

    auto slotHasRefreshItem = [&](int slot) -> bool {
        if (slot < 0 || slot >= 54) return false;
        ItemStack* stack = model->getSlot(slot);
        if (!stack || !stack->mItem) return false;
        Item* item = stack->getItem();
        if (!item) return false;
        if (stack->mAuxValue != 0) return false;
        if (stack->mCount != 1) return false;
        return StringUtils::containsIgnoreCase(item->mName, "ender_eye");
    };

    if (plan.confirmSlot != -1 && (now - mLastConfirmClickMs) >= 125 && (now - mLastAnyClickMs) >= 125)
    {
        if (slotHasConfirmItem(plan.confirmSlot))
        {
            csc->handleAutoPlace("container_items", plan.confirmSlot);
            mLastConfirmClickMs = now;
            mLastAnyClickMs = now;
            return;
        }
    }

    if (plan.buySlot != -1 && plan.buyThresholdCents != 0 && (now - mLastBuyClickMs) >= 150 && (now - mLastAnyClickMs) >= 150)
    {
        ItemStack* stack = model->getSlot(plan.buySlot);
        if (stack && stack->mItem)
        {
            Item* item = stack->getItem();
            if (item && (plan.buyItemId == 0 || item->mItemId == plan.buyItemId))
            {
                auto lore = extractLoreLines(stack->mCompoundTag);
                if (auto priceText = extractPriceFromLoreLines(lore); priceText.has_value())
                {
                    if (auto parsed = parsePriceToCents(*priceText); parsed.has_value() && *parsed <= plan.buyThresholdCents)
                    {
                        csc->handleAutoPlace("container_items", plan.buySlot);
                        mLastBuyClickMs = now;
                        mLastAnyClickMs = now;
                        return;
                    }
                }
            }
        }
    }

    if (plan.refreshSlot != -1 && (now - mLastRefreshClickMs) >= 1000 && (now - mLastAnyClickMs) >= 150)
    {
        if (slotHasRefreshItem(plan.refreshSlot))
        {
            csc->handleAutoPlace("container_items", plan.refreshSlot);
            mLastRefreshClickMs = now;
            mLastAnyClickMs = now;
            return;
        }
    }
}

void AutobuyController::onBaseTickEvent(BaseTickEvent& event)
{
    if (!mRunning) return;
    ensureConfigLoaded();

    if (!mSentAh)
    {
        CommandUtils::executeCommand("/ah");
        mSentAh = true;
        spdlog::info("[Autobuy] Sent /ah");
    }

    auto player = event.mActor;
    if (!player) return;

    auto model = player->getContainerManagerModel();
    if (!model) return;

    ContainerType type = model->mContainerType;
    if (type == ContainerType::None || type == ContainerType::Hud) return;

    std::string screenName = "no_screen";
    if (auto ci = ClientInstance::get(); ci != nullptr)
    {
        screenName = ci->getScreenName();
    }

    const bool isAuctionScreen = (screenName == "large_chest_screen" || screenName == "chest_screen");
    if (!isAuctionScreen && type == ContainerType::Inventory) return;

    if (type != mLastContainerType)
    {
        mLastContainerType = type;
        mSlotHashes.fill(0);
        mLastSeenPrice.reset();
        spdlog::info("[Autobuy] Container type: {} ({})", magic_enum::enum_name(type), static_cast<int>(type));
    }

    if (mOpenScreenName != screenName)
    {
        mOpenScreenName = screenName;
        spdlog::info("[Autobuy] Screen: {}", mOpenScreenName);
    }

    {
        std::string containerName = std::string(magic_enum::enum_name(type));
        if (!mOpenScreenName.empty()) containerName += " | " + mOpenScreenName;
        if (mOpenContainerName != containerName)
        {
            mOpenContainerName = std::move(containerName);
            spdlog::info("[Autobuy] Open container: {}", mOpenContainerName);
        }
    }

    if (!isAuctionScreen)
    {
        mContainerItems.clear();
        return;
    }

    constexpr int maxSlots = 54;
    const uint64_t now = NOW;
    if ((now - mLastContainerReadMs) < 200)
        return;
    mLastContainerReadMs = now;

    std::vector<SlotSnapshot> snapshot;
    snapshot.reserve(maxSlots);

    bool anySlotChanged = false;
    for (int i = 0; i < maxSlots; i++)
    {
        SlotSnapshot ss;
        ss.slot = i;

        ItemStack* stack = model->getSlot(i);
        if (!stack || !stack->mItem)
        {
            snapshot.push_back(std::move(ss));
            if (mSlotHashes[i] != 0)
            {
                mSlotHashes[i] = 0;
                anySlotChanged = true;
            }
            continue;
        }

        Item* item = stack->getItem();
        if (!item)
        {
            snapshot.push_back(std::move(ss));
            if (mSlotHashes[i] != 0)
            {
                mSlotHashes[i] = 0;
                anySlotChanged = true;
            }
            continue;
        }

        ss.hasItem = true;
        ss.id = static_cast<uint64_t>(item->mItemId);
        ss.aux = static_cast<uint64_t>(stack->mAuxValue);
        ss.count = static_cast<int>(stack->mCount);
        ss.internalName = item->mName;
        ss.customName = stack->getCustomName();
        ss.loreRaw = extractLoreLines(stack->mCompoundTag);

        uint64_t h = 0xcbf29ce484222325ull;
        h = hashCombine(h, ss.id);
        h = hashCombine(h, ss.aux);
        h = hashCombine(h, static_cast<uint64_t>(static_cast<uint8_t>(ss.count)));
        h = hashCombine(h, hashString(ss.internalName));
        if (!ss.customName.empty()) h = hashCombine(h, hashString(ss.customName));
        for (const auto& line : ss.loreRaw)
            h = hashCombine(h, hashString(line));

        if (mSlotHashes[i] != h)
        {
            mSlotHashes[i] = h;
            anySlotChanged = true;
        }

        snapshot.push_back(std::move(ss));
    }

    if (!anySlotChanged)
        return;

    {
        std::scoped_lock lock(mScanMutex);
        mPendingSnapshot = std::move(snapshot);
        mScanSeq++;
        mScanHasWork = true;
    }
    mScanCv.notify_one();
}
