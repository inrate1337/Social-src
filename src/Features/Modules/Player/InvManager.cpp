//
// Created by vastrakai on 7/6/2024.
//

#include "InvManager.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/PingUpdateEvent.hpp>
#include <Features/Events/ContainerScreenTickEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Inventory\ItemStack.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Inventory/ContainerManagerModel.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/Packets/InteractPacket.hpp>
#include <SDK/Minecraft/Network/Packets/ContainerClosePacket.hpp>
#include <SDK/Minecraft/World/BlockLegacy.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <Utils/StringUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <spdlog/spdlog.h>

static bool isStrengthLevel3Potion(ItemStack* stack)
{
    if (!stack || !stack->mCompoundTag) return false;

    auto effectsVariant = stack->mCompoundTag->get("CustomPotionEffects");
    if (!effectsVariant || effectsVariant->getTagType() != Tag::Type::List) return false;

    auto list = effectsVariant->asListTag();
    if (!list) return false;

    auto getIntFromVariant = [](CompoundTagVariant* variant) -> int
    {
        if (!variant) return 0;
        switch (variant->getTagType())
        {
        case Tag::Type::Byte:
            return variant->asByteTag()->val;
        case Tag::Type::Short:
            return variant->asShortTag()->val;
        case Tag::Type::Int:
            return variant->asIntTag()->val;
        case Tag::Type::Int64:
            return static_cast<int>(variant->asInt64Tag()->val);
        default:
            return 0;
        }
    };

    for (auto* entry : list->val)
    {
        if (!entry) continue;
        if (entry->getId() != Tag::Type::Compound) continue;

        auto comp = reinterpret_cast<CompoundTag*>(entry);
        auto idVariant = comp->get("Id");
        auto ampVariant = comp->get("Amplifier");
        if (!idVariant || !ampVariant) continue;

        int effectId = getIntFromVariant(idVariant);
        int amplifier = getIntFromVariant(ampVariant);

        if (effectId == 5 && amplifier == 2)
        {
            return true;
        }
    }

    return false;
}

void InvManager::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &InvManager::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &InvManager::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &InvManager::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->listen<PingUpdateEvent, &InvManager::onPingUpdateEvent>(this);
    gFeatureManager->mDispatcher->listen<ContainerScreenTickEvent, &InvManager::onContainerScreenTickEvent>(this);
}

void InvManager::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &InvManager::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &InvManager::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &InvManager::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<PingUpdateEvent, &InvManager::onPingUpdateEvent>(this);
    gFeatureManager->mDispatcher->deafen<ContainerScreenTickEvent, &InvManager::onContainerScreenTickEvent>(this);
}

void InvManager::onBaseTickEvent(BaseTickEvent& event)
{
    auto ci = ClientInstance::get();
    if (ci)
    {
        std::string screenName = ci->getScreenName();
        if (screenName == "hud_screen" || screenName == "no_screen")
        {
            mDumpedThisOpen = false;
        }
    }

    auto player = ci ? ci->getLocalPlayer() : nullptr;
    if (!player) return;
    auto armorContainer = player->getArmorContainer();
    auto supplies = player->getSupplies();
    auto container = supplies->getContainer();

    if (mManagementMode.mValue != ManagementMode::Always && !mHasOpenContainer)
    {
        return;
    }

    // Check how many free slots we have
    int freeSlots = 0;
    for (int i = 0; i < 36; i++)
    {
        if (!container->getItem(i)->mItem) freeSlots++;
    }


    // If we are in a container, don't do anything
    if (ClientInstance::get()->getMouseGrabbed() && player && freeSlots > 0 && mManagementMode.mValue == ManagementMode::Always)
    {
        return;
    }

    std::vector<int> itemsToEquip;
    bool isInstant = mMode.mValue == Mode::Instant;
    if (mLastAction + static_cast<uint64_t>(mDelay.mValue) > NOW)
    {
        return;
    }

    int bestHelmetSlot = -1;
    int bestChestplateSlot = -1;
    int bestLeggingsSlot = -1;
    int bestBootsSlot = -1;
    int bestSwordSlot = -1;
    int bestPickaxeSlot = -1;
    int bestAxeSlot = -1;
    int bestShovelSlot = -1;

    int bestHelmetValue = 0;
    int bestChestplateValue = 0;
    int bestLeggingsValue = 0;
    int bestBootsValue = 0;
    int bestSwordValue = 0;
    int bestPickaxeValue = 0;
    int bestAxeValue = 0;
    int bestShovelValue = 0;

    int equippedHelmetValue = ItemUtils::getItemValue(armorContainer->getItem(0));
    int equippedChestplateValue = ItemUtils::getItemValue(armorContainer->getItem(1));
    int equippedLeggingsValue = ItemUtils::getItemValue(armorContainer->getItem(2));
    int equippedBootsValue = ItemUtils::getItemValue(armorContainer->getItem(3));

    int firstBowSlot = -1;
    int fireSwordSlot = ItemUtils::getFireSword(false);

    for (int i = 0; i < 36; i++)
    {
        auto item = container->getItem(i);
        if (!item->mItem) continue;
        auto itemType = item->getItem()->getItemType();

        if (item->getItem()->mName.contains("bow") && firstBowSlot == -1 && mDropExtraBows.mValue)
        {
            firstBowSlot = i;
        } else if (firstBowSlot != -1 && mDropExtraBows.mValue && item->getItem()->mName.contains("bow"))
        {
            supplies->getContainer()->dropSlot(i);

            mLastAction = NOW;
            if (!isInstant)
            {
                return;
            }
        }

        // This is so that we only ignore the first fire sword we find
        if (mIgnoreFireSword.mValue && fireSwordSlot != -1 && fireSwordSlot == i) continue;

        auto itemValue = ItemUtils::getItemValue(item);
        if (itemType == SItemType::Helmet && itemValue > bestHelmetValue)
        {
            if (equippedHelmetValue >= itemValue)
            {
                bestHelmetSlot = -1;
                continue;
            }

            bestHelmetSlot = i;
            bestHelmetValue = itemValue;
        }
        else if (itemType == SItemType::Chestplate && itemValue > bestChestplateValue)
        {
            if (equippedChestplateValue >= itemValue)
            {
                bestChestplateSlot = -1;
                continue;
            }

            bestChestplateSlot = i;
            bestChestplateValue = itemValue;
        }
        else if (itemType == SItemType::Leggings && itemValue > bestLeggingsValue)
        {
            if (equippedLeggingsValue >= itemValue)
            {
                bestLeggingsSlot = -1;
                continue;
            }

            bestLeggingsSlot = i;
            bestLeggingsValue = itemValue;
        }
        else if (itemType == SItemType::Boots && itemValue > bestBootsValue)
        {
            if (equippedBootsValue >= itemValue)
            {
                bestBootsSlot = -1;
                continue;
            }

            bestBootsSlot = i;
            bestBootsValue = itemValue;
        }
        else if (itemType == SItemType::Sword && itemValue > bestSwordValue)
        {
            bestSwordSlot = i;
            bestSwordValue = itemValue;
        }
        else if (itemType == SItemType::Pickaxe && itemValue > bestPickaxeValue)
        {
            bestPickaxeSlot = i;
            bestPickaxeValue = itemValue;
        }
        else if (itemType == SItemType::Axe && itemValue > bestAxeValue)
        {
            bestAxeSlot = i;
            bestAxeValue = itemValue;
        }
        else if (itemType == SItemType::Shovel && itemValue > bestShovelValue)
        {
            bestShovelSlot = i;
            bestShovelValue = itemValue;
        }
    }

    // Go through and get items to drop
    std::vector<int> itemsToDrop;
    for (int i = 0; i < 36; i++)
    {
        auto item = container->getItem(i);
        if (!item->mItem) continue;
        if (mIgnoreFireSword.mValue && fireSwordSlot != -1 && fireSwordSlot == i) continue;
        auto itemType = item->getItem()->getItemType();
        auto itemValue = ItemUtils::getItemValue(item);
        bool hasFireProtection = item->getEnchantValue(Enchant::FIRE_PROTECTION) > 0;

        if (mStealFireProtection.mValue && hasFireProtection) {
            continue;
        }

        if (itemType == SItemType::Sword && i != bestSwordSlot)
        {
            itemsToDrop.push_back(i);
        }
        else if (itemType == SItemType::Pickaxe && i != bestPickaxeSlot)
        {
            itemsToDrop.push_back(i);
        }
        else if (itemType == SItemType::Axe && i != bestAxeSlot)
        {
            itemsToDrop.push_back(i);
        }
        else if (itemType == SItemType::Shovel && i != bestShovelSlot)
        {
            itemsToDrop.push_back(i);
        }
        else if (itemType == SItemType::Helmet && i != bestHelmetSlot)
        {
            if (!(mStealFireProtection.mValue && hasFireProtection)) {
                itemsToDrop.push_back(i);
            }
        }
        else if (itemType == SItemType::Chestplate && i != bestChestplateSlot)
        {
            if (!(mStealFireProtection.mValue && hasFireProtection)) {
                itemsToDrop.push_back(i);
            }
        }
        else if (itemType == SItemType::Leggings && i != bestLeggingsSlot)
        {
            if (!(mStealFireProtection.mValue && hasFireProtection)) {
                itemsToDrop.push_back(i);
            }
        }
        else if (itemType == SItemType::Boots && i != bestBootsSlot)
        {
            if (!(mStealFireProtection.mValue && hasFireProtection)) {
                itemsToDrop.push_back(i);
            }
        }
    }

    for (auto& item : itemsToDrop)
    {
        supplies->getContainer()->dropSlot(item);

        mLastAction = NOW;
        if (!isInstant)
        {
            return;
        }
    }

    if (mPreferredSlots.mValue)
    {
        if (mPreferredSwordSlot.mValue != 0)
        {
            if (bestSwordSlot != -1 && bestSwordSlot != mPreferredSwordSlot.mValue - 1)
            {
                supplies->getContainer()->swapSlots(bestSwordSlot, mPreferredSwordSlot.mValue - 1);

                mLastAction = NOW;
                if (!isInstant)
                {
                    return;
                }
            }
        }
        if (mPreferredPickaxeSlot.mValue != 0)
        {
            if (bestPickaxeSlot != -1 && bestPickaxeSlot != mPreferredPickaxeSlot.mValue - 1)
            {
                supplies->getContainer()->swapSlots(bestPickaxeSlot, mPreferredPickaxeSlot.mValue - 1);

                mLastAction = NOW;
                if (!isInstant)
                {
                    return;
                }
            }
        }
        if (mPreferredAxeSlot.mValue != 0)
        {
            if (bestAxeSlot != -1 && bestAxeSlot != mPreferredAxeSlot.mValue - 1)
            {
                supplies->getContainer()->swapSlots(bestAxeSlot, mPreferredAxeSlot.mValue - 1);

                mLastAction = NOW;
                if (!isInstant)
                {
                    return;
                }
            }
        }
        if (mPreferredShovelSlot.mValue != 0)
        {
            if (bestShovelSlot != -1 && bestShovelSlot != mPreferredShovelSlot.mValue - 1)
            {
                supplies->getContainer()->swapSlots(bestShovelSlot, mPreferredShovelSlot.mValue - 1);

                mLastAction = NOW;
                if (!isInstant)
                {
                    return;
                }
            }
        }
        if (mPreferredFireSwordSlot.mValue != 0)
        {
            if (fireSwordSlot != -1 && fireSwordSlot != mPreferredFireSwordSlot.mValue - 1 && bestSwordSlot != fireSwordSlot)
            {
                supplies->getContainer()->swapSlots(fireSwordSlot, mPreferredFireSwordSlot.mValue - 1);

                mLastAction = NOW;
                if (!isInstant)
                {
                    return;
                }
            }
        }
        if (mPreferredBlocksSlot.mValue != 0)
        {
            ItemStack* item = container->getItem(mPreferredBlocksSlot.mValue - 1);
            if (!ItemUtils::isUsableBlock(item))
            {
                int firstPlaceable = ItemUtils::getFirstPlaceable(false);

                if (firstPlaceable != -1)
                {
                    supplies->getContainer()->swapSlots(firstPlaceable, mPreferredBlocksSlot.mValue - 1);

                    mLastAction = NOW;
                    if (!isInstant)
                    {
                        return;
                    }
                }
            }
        }
    }

    if (bestHelmetSlot != -1) itemsToEquip.push_back(bestHelmetSlot);
    if (bestChestplateSlot != -1) itemsToEquip.push_back(bestChestplateSlot);
    if (bestLeggingsSlot != -1) itemsToEquip.push_back(bestLeggingsSlot);
    if (bestBootsSlot != -1) itemsToEquip.push_back(bestBootsSlot);

    for (auto& item : itemsToEquip)
    {
        supplies->getContainer()->equipArmor(item);
        mLastAction = NOW;
        if (!isInstant)
        {
            break;
        }
    }
}

void InvManager::onPacketInEvent(PacketInEvent& event)
{
    if (event.mPacket->getId() == PacketID::ContainerOpen)
    {
        auto packet = event.getPacket<ContainerOpenPacket>();
        mOpenContainerId = packet->mContainerId;
        mOpenContainerType = packet->mType;
        mDumpedThisOpen = false;
        if (mManagementMode.mValue == ManagementMode::ContainerOnly || mManagementMode.mValue == ManagementMode::InvOnly && packet->mType == ContainerType::Inventory)
        {
            mHasOpenContainer = true;
        }
        if (mDumpContainerInfo.mValue && packet->mType != ContainerType::Inventory)
        {
            mShouldDumpContainer = true;
        }
        if (mFindWinnerPotion.mValue && packet->mType != ContainerType::Inventory)
        {
            mShouldSearchWinnerPotion = true;
        }
    }
    if (event.mPacket->getId() == PacketID::InventoryContent)
    {
        if (!mDumpContainerInfo.mValue) return;

        auto packet = event.getPacket<InventoryContentPacket>();
        if (packet->mInventoryId == ContainerID::Inventory) return;
        if (!mShouldDumpContainer && packet->mInventoryId != mOpenContainerId && packet->mInventoryId != ContainerID::Chest) return;

        ContainerType typeForHeader = mOpenContainerType;
        if (typeForHeader == ContainerType::Inventory) typeForHeader = ContainerType::None;

        std::string header = "Container dump (" + std::to_string(static_cast<int>(typeForHeader)) + ")"
            + " | id=" + std::to_string(static_cast<int>(packet->mInventoryId))
            + " | slots=" + std::to_string(packet->mSlots.size()) + ":";
        spdlog::info("{}", header);

        auto ci = ClientInstance::get();
        auto player = ci ? ci->getLocalPlayer() : nullptr;
        auto level = player ? player->getLevel() : nullptr;
        if (!level) return;

        for (size_t i = 0; i < packet->mSlots.size(); i++)
        {
            auto& desc = packet->mSlots[i];
            auto stack = ItemStack::fromDescriptor(desc);
            if (!stack.mItem) continue;
            auto item = stack.getItem();
            if (!item) continue;
            if (stack.mCount <= 0) continue;

            std::string displayName = stack.getCustomName();
            if (displayName.empty())
            {
                displayName = item->mName;
            }

            std::string line = "Slot " + std::to_string(i) +
                " | id=" + std::to_string(item->mItemId) +
                " | aux=" + std::to_string(stack.mAuxValue) +
                " | name_en=" + item->mName +
                " | name=" + displayName +
                " | count=" + std::to_string(static_cast<int>(static_cast<uint8_t>(stack.mCount)));

            spdlog::info("{}", line);
        }

        mShouldDumpContainer = false;
        mDumpedThisOpen = true;
    }
    if (event.mPacket->getId() == PacketID::ContainerClose)
    {
        mHasOpenContainer = false;
        mOpenContainerId = static_cast<ContainerID>(-1);
        mOpenContainerType = static_cast<ContainerType>(-9);
        mDumpedThisOpen = false;
    }
}

void InvManager::onPacketOutEvent(PacketOutEvent& event)
{
    if (event.mPacket->getId() == PacketID::ContainerClose)
    {
        mHasOpenContainer = false;
        mShouldDumpContainer = false;
        mShouldSearchWinnerPotion = false;
        mOpenContainerId = static_cast<ContainerID>(-1);
        mOpenContainerType = static_cast<ContainerType>(-9);
        mDumpedThisOpen = false;
    }
    else if (event.mPacket->getId() == PacketID::ContainerOpen)
    {
        auto packet = event.getPacket<ContainerOpenPacket>();
        mOpenContainerId = packet->mContainerId;
        mOpenContainerType = packet->mType;
        mDumpedThisOpen = false;
        if (mManagementMode.mValue == ManagementMode::ContainerOnly || mManagementMode.mValue == ManagementMode::InvOnly && packet->mType == ContainerType::Inventory)
        {
            mHasOpenContainer = true;
        }
    }
}

void InvManager::onPingUpdateEvent(PingUpdateEvent& event)
{
    mLastPing = event.mPing;
}

void InvManager::onContainerScreenTickEvent(ContainerScreenTickEvent& event)
{
    if (!mDumpContainerInfo.mValue && !mFindWinnerPotion.mValue) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto containerModel = player->getContainerManagerModel();
    if (!containerModel) return;

    ContainerType type = containerModel->mContainerType;
    if (type == ContainerType::Inventory) return;

    if (mDumpContainerInfo.mValue && !mDumpedThisOpen)
    {
        mShouldDumpContainer = true;
    }

    const int hardMaxSlots = 256;
    int nullStreak = 0;
    bool seenNonNull = false;
    int itemsFound = 0;
    std::vector<std::string> dumpLines;
    for (int i = 0; i < hardMaxSlots; i++)
    {
        ItemStack* stack = containerModel->getSlot(i);
        if (!stack)
        {
            nullStreak++;
            if (nullStreak >= 32 && i > 64) break;
            continue;
        }
        seenNonNull = true;
        nullStreak = 0;
        if (!stack->mItem) continue;
        itemsFound++;

        auto item = stack->getItem();
        std::string internalName = item->mName;
        std::string displayName = stack->getCustomName();
        if (displayName.empty())
        {
            displayName = internalName;
        }
        std::string cleanName = ColorUtils::removeColorCodes(displayName);

        if (mDumpContainerInfo.mValue && mShouldDumpContainer)
        {
            std::string line = "Slot " + std::to_string(i) +
                " | id=" + std::to_string(item->mItemId) +
                " | name_en=" + internalName +
                " | name_ru=" + displayName +
                " | count=" + std::to_string(stack->mCount);

            auto enchants = stack->gatherEnchants();
            if (!enchants.empty())
            {
                line += " | enchants=";
                bool first = true;
                for (auto& [enchId, level] : enchants)
                {
                    if (!first) line += ", ";
                    first = false;
                    if (enchId >= 0 && enchId <= static_cast<int>(Enchant::SWIFT_SNEAK))
                    {
                        auto enchantEnum = static_cast<Enchant>(enchId);
                        line += stack->getEnchantName(enchantEnum) + " " + std::to_string(level);
                    }
                    else
                    {
                        line += "id:" + std::to_string(enchId) + " " + std::to_string(level);
                    }
                }
            }

            if (stack->mCompoundTag)
            {
                std::string potionIdStr;
                std::string customEffectsStr;

                line += " | nbt=";
                bool firstTag = true;
                for (auto& [key, variant] : stack->mCompoundTag->data)
                {
                    if (!firstTag) line += "; ";
                    firstTag = false;
                    line += key + "=" + variant.toString();

                    if (key == "Potion" && variant.getTagType() == Tag::Type::String)
                    {
                        potionIdStr = variant.asStringTag()->val;
                    }
                    if (key == "CustomPotionEffects" && variant.getTagType() == Tag::Type::List)
                    {
                        auto list = variant.asListTag();
                        std::string effects;
                        bool firstEff = true;
                        for (auto* entry : list->val)
                        {
                            if (!entry) continue;
                            if (entry->getId() != Tag::Type::Compound) continue;

                            auto comp = reinterpret_cast<CompoundTag*>(entry);
                            std::string one;
                            bool firstField = true;
                            for (auto& [ekey, evar] : comp->data)
                            {
                                if (!firstField) one += ",";
                                firstField = false;
                                one += ekey + ":" + evar.toString();
                            }

                            if (one.empty()) continue;
                            if (!firstEff) effects += " | ";
                            firstEff = false;
                            effects += one;
                        }
                        customEffectsStr = effects;
                    }
                }

                if (!potionIdStr.empty())
                {
                    line += " | potion_id=" + potionIdStr;
                }
                if (!customEffectsStr.empty())
                {
                    line += " | custom_effects=" + customEffectsStr;
                }
            }

            dumpLines.push_back(std::move(line));
        }

        if (!mFindWinnerPotion.mValue || !mShouldSearchWinnerPotion) continue;
        {
            bool matched = false;

            bool hasWordPotion =
                cleanName.find("Зелье") != std::string::npos ||
                cleanName.find("зелье") != std::string::npos;

            bool hasWordWinner =
                cleanName.find("силы") != std::string::npos ||
                cleanName.find("силы") != std::string::npos;

            if (hasWordPotion && hasWordWinner)
            {
                matched = true;
            }
            else if (cleanName.find("Winner Potion") != std::string::npos)
            {
                matched = true;
            }
            else if (stack->mCompoundTag)
            {
                std::string nbtString = stack->mCompoundTag->toString();
                if (nbtString.find("Зелье") != std::string::npos && nbtString.find("Победител") != std::string::npos)
                {
                    matched = true;
                }
            }

            if (!matched && isStrengthLevel3Potion(stack))
            {
                matched = true;
            }

            if (matched)
            {
                spdlog::info("Найден предмет (зелье силы III) в слоте {}", i);
            }
        }
    }
    if (mDumpContainerInfo.mValue && mShouldDumpContainer && seenNonNull)
    {
        std::string header = "Container dump (" + std::to_string(static_cast<int>(type)) + ") | items=" + std::to_string(itemsFound) + ":";
        spdlog::info("{}", header);
        for (auto& line : dumpLines)
        {
            spdlog::info("{}", line);
        }
        mShouldDumpContainer = false;
        mDumpedThisOpen = true;
    }
    if (mFindWinnerPotion.mValue && mShouldSearchWinnerPotion)
    {
        mShouldSearchWinnerPotion = false;
    }
}

bool InvManager::isItemUseless(ItemStack* item, int slot)
{
    if (!item->mItem) return true;
    auto player = ClientInstance::get()->getLocalPlayer();
    SItemType itemType = item->getItem()->getItemType();
    auto itemValue = ItemUtils::getItemValue(item);
    auto Inv_Manager = gFeatureManager->mModuleManager->getModule<InvManager>();

    if (itemType == SItemType::Helmet || itemType == SItemType::Chestplate || itemType == SItemType::Leggings || itemType == SItemType::Boots)
    {
        int equippedItemValue = ItemUtils::getItemValue(player->getArmorContainer()->getItem(static_cast<int>(itemType)));
        bool hasFireProtection = item->getEnchantValue(Enchant::FIRE_PROTECTION) > 0;

        if (Inv_Manager->mStealFireProtection.mValue && hasFireProtection) {
            return false;
        }

        return equippedItemValue >= itemValue;
    }

    if (itemType == SItemType::Sword || itemType == SItemType::Pickaxe || itemType == SItemType::Axe || itemType == SItemType::Shovel)
    {
        int bestSlot = ItemUtils::getBestItem(itemType);
        int bestValue = ItemUtils::getItemValue(player->getSupplies()->getContainer()->getItem(bestSlot));

        return bestValue >= itemValue && bestSlot != slot;
    }

    return false;
}
