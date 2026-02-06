//
// Created by vastrakai on 7/8/2024.
//

#include "AntiBot.hpp"

#include <Features/FeatureManager.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <Features/Modules/Player/Teams.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <SDK/Minecraft/Inventory/SimpleContainer.hpp>
#include <Utils/MemUtils.hpp>
#include <cmath>
#include <mutex>

namespace {
    static bool abIsPtrReadable(void* p)
    {
        return p && MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(p));
    }

    static bool abIsActorValidSafe(Actor* actor)
    {
        if (!abIsPtrReadable(actor)) return false;
        bool valid = false;
        if (!TryCallWrapper([&]() { valid = actor->isValid(); })) return false;
        if (!valid) return false;
        bool destroying = false;
        if (!TryCallWrapper([&]() { destroying = actor->isDestroying(); })) return false;
        if (destroying) return false;
        return true;
    }

    static bool abIsActorAliveSafe(Actor* actor)
    {
        if (!abIsActorValidSafe(actor)) return false;
        bool dead = false;
        if (!TryCallWrapper([&]() { dead = actor->isDead(); })) return false;
        if (dead) return false;
        float hp = 0.f;
        if (TryCallWrapper([&]() { hp = actor->getHealth(); }))
        {
            if (!std::isfinite(hp) || hp <= 0.f) return false;
        }
        return true;
    }

    static bool abIsItemStackValid(ItemStack* item)
    {
        if (!abIsPtrReadable(item)) return false;
        Item* it = nullptr;
        if (!TryCallWrapper([&]() { (void)item->mItem; })) return false;
        if (!abIsPtrReadable(item->mItem)) return false;
        if (!TryCallWrapper([&]() { it = item->getItem(); })) return false;
        if (it && !abIsPtrReadable(it)) return false;
        return true;
    }
}

void AntiBot::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &AntiBot::onBaseTickEvent>(this);
}

void AntiBot::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &AntiBot::onBaseTickEvent>(this);
}

void AntiBot::onBaseTickEvent(BaseTickEvent& event)
{
    if (mMode.mValue == Mode::Simple)
    {
        // Set settings to preset values
        mHitboxCheck.mValue = true;
        mPlayerCheck.mValue = true;
        mInvisibleCheck.mValue = true;
        mNameCheck.mValue = true;
    }

    /*auto actors = ActorUtils::getActorList(true);

    for (auto actor : actors)
    {
        if (mPlayerCheck.mValue && actor->isPlayer()) continue;
    }*/
}

constexpr float NORMAL_PLAYER_HEIGHT_MAX = 1.81f;
constexpr float NORMAL_PLAYER_HEIGHT_MIN = 1.35f;
constexpr float NORMAL_PLAYER_WIDTH_MIN = 0.54f;
constexpr float NORMAL_PLAYER_WIDTH_MAX = 0.66f;

std::vector<std::string> AntiBot::getPlayerNames() {
    static std::mutex cacheMutex;
    static std::vector<std::string> cached;
    static uint64_t lastUpdate = 0;
    const uint64_t now = NOW;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (now - lastUpdate < 250) return cached;
    }
    std::vector<std::string> playerNames;
    auto ci = ClientInstance::get();
    if (!ci) return playerNames;
    auto player = ci->getLocalPlayer();
    if (!player) return playerNames;

    Level* level = nullptr;
    if (!TryCallWrapper([&]() { level = player->getLevel(); })) return playerNames;
    if (!level || !abIsPtrReadable(level)) return playerNames;

    decltype(level->getPlayerList()) playerList = nullptr;
    if (!TryCallWrapper([&]() { playerList = level->getPlayerList(); })) return playerNames;
    if (!playerList || !abIsPtrReadable(playerList)) return playerNames;

    for (const auto& entry : *playerList | std::views::values)
    {
        std::string name;
        if (!TryCallWrapper([&]() { name = entry.mName; })) continue;
        playerNames.emplace_back(std::move(name));
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cached = playerNames;
        lastUpdate = now;
        return cached;
    }
}

bool AntiBot::isBot(Actor* actor)
{
    if (!mEnabled) return false;
    if (!actor) return false;
    if (!abIsActorAliveSafe(actor)) return true;
    bool isPlayer = false;
    if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) return true;
    if (mPlayerCheck.mValue && !isPlayer) return true;

    AABBShapeComponent* aabbShapeComponent = nullptr;
    if (!TryCallWrapper([&]() { aabbShapeComponent = actor->getAABBShapeComponent(); })) return true;
    if (!aabbShapeComponent || !abIsPtrReadable(aabbShapeComponent)) return true;
    float hitboxWidth = 0.f;
    float hitboxHeight = 0.f;
    if (!TryCallWrapper([&]() { hitboxWidth = aabbShapeComponent->mWidth; hitboxHeight = aabbShapeComponent->mHeight; })) return true;
    if (!std::isfinite(hitboxWidth) || !std::isfinite(hitboxHeight)) return true;

    // Return if the hitbox dimensions are incorrect
    if (mHitboxCheck.mValue && (hitboxWidth < NORMAL_PLAYER_WIDTH_MIN || hitboxWidth > NORMAL_PLAYER_WIDTH_MAX ||
        hitboxHeight < NORMAL_PLAYER_HEIGHT_MIN || hitboxHeight > NORMAL_PLAYER_HEIGHT_MAX))
        return true;

    if (mInvisibleCheck.mValue)
    {
        bool invisible = false;
        if (!TryCallWrapper([&]() { invisible = actor->getStatusFlag(ActorFlags::Invisible); })) return true;
        if (invisible) return true;
    }

    if (mNameCheck.mValue)
    {
        std::string nameTagString;
        if (!TryCallWrapper([&]() { nameTagString = actor->getNameTag(); })) return true;
        if (std::ranges::count(nameTagString, '\n') > 0) return true;
    }

    if (mPlayerListCheck.mValue)
    {
        auto playerList = getPlayerNames();
        std::string nickName;
        if (!TryCallWrapper([&]() { nickName = actor->getNameTag(); })) return true;

        if (nickName.empty()) return true;
        if (std::find(playerList.begin(), playerList.end(), nickName) == playerList.end()) {
            return true;
        }
    }

    if (mHasArmorCheck.mValue && !hasArmor(actor)) return true;

    return false;
}

bool AntiBot::hasArmor(Actor* actor)
{
    if (!actor) return false;
    if (!abIsActorValidSafe(actor)) return false;
    bool isPlayer = false;
    if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) return false;
    if (!isPlayer) return false;

    SimpleContainer* armorContainer = nullptr;
    if (!TryCallWrapper([&]() { armorContainer = actor->getArmorContainer(); })) return false;
    if (!armorContainer || !abIsPtrReadable(armorContainer)) return false;

    ItemStack* helmetItem = nullptr;
    ItemStack* chestplateItem = nullptr;
    ItemStack* legginsItem = nullptr;
    ItemStack* bootsItem = nullptr;
    if (!TryCallWrapper([&]() { helmetItem = armorContainer->getItem(0); })) return false;
    if (!TryCallWrapper([&]() { chestplateItem = armorContainer->getItem(1); })) return false;
    if (!TryCallWrapper([&]() { legginsItem = armorContainer->getItem(2); })) return false;
    if (!TryCallWrapper([&]() { bootsItem = armorContainer->getItem(3); })) return false;
    if (!helmetItem || !chestplateItem || !legginsItem || !bootsItem) return false;
    if (!abIsItemStackValid(helmetItem)) return false;
    if (!abIsItemStackValid(chestplateItem)) return false;
    if (!abIsItemStackValid(legginsItem)) return false;
    if (!abIsItemStackValid(bootsItem)) return false;

    if (mArmorMode.mValue == ArmorMode::Full)
    {
        return helmetItem->mItem && chestplateItem->mItem && legginsItem->mItem && bootsItem->mItem;
    }
    else if (mArmorMode.mValue == ArmorMode::OneElement)
    {
        return helmetItem->mItem || chestplateItem->mItem || legginsItem->mItem || bootsItem->mItem;
    }

    return false;
}
