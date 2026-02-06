//
// Created by vastrakai on 7/8/2024.
//

#include "Aura.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/BobHurtEvent.hpp>
#include <Features/Events/BoneRenderEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/ActorRotationComponent.hpp>
#include <SDK/Minecraft/Actor/GameMode.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <SDK/Minecraft/World/HitResult.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/Packets/RemoveActorPacket.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <Utils/MemUtils.hpp>
#include <unordered_set>

static bool auraIsPtrReadable(void* p)
{
    return p && MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(p));
}

static bool auraIsActorValidSafe(Actor* actor)
{
    if (!auraIsPtrReadable(actor)) return false;
    bool valid = false;
    if (!TryCallWrapper([&]() { valid = actor->isValid(); })) return false;
    if (!valid) return false;
    bool destroying = false;
    TryCallWrapper([&]() { destroying = actor->isDestroying(); });
    if (destroying) return false;
    return true;
}

static bool auraIsActorAliveSafe(Actor* actor)
{
    if (!auraIsActorValidSafe(actor)) return false;
    bool dead = false;
    if (TryCallWrapper([&]() { dead = actor->isDead(); }) && dead) return false;
    float hp = 0.f;
    if (TryCallWrapper([&]() { hp = actor->getHealth(); }))
    {
        if (!std::isfinite(hp) || hp <= 0.f) return false;
    }
    return true;
}

static float auraDistanceSafe(Actor* a, Actor* b, float fallback)
{
    if (!a || !b) return fallback;
    float out = fallback;
    if (!TryCallWrapper([&]() { out = a->distanceTo(b); })) return fallback;
    if (!std::isfinite(out)) return fallback;
    return out;
}

int Aura::getSword(Actor* target) {
    if (!target) return -1;
    auto ci = ClientInstance::get();
    if (!ci) return -1;
    auto player = ci->getLocalPlayer();
    if (!player) return -1;

    PlayerInventory* supplies = nullptr;
    if (!TryCallWrapper([&]() { supplies = player->getSupplies(); })) return -1;
    if (!supplies) return -1;

    Inventory* container = nullptr;
    if (!TryCallWrapper([&]() { container = supplies->getContainer(); })) return supplies->mSelectedSlot;
    if (!container) return supplies->mSelectedSlot;
    if (!auraIsActorValidSafe(target)) return supplies->mSelectedSlot;

    bool isFriend = false;
    if (gFriendManager && auraIsPtrReadable(gFriendManager))
    {
        TryCallWrapper([&]() { isFriend = gFriendManager->isFriend(target); });
    }
    if (isFriend && mFistFriends.mValue)
    {
        // Look for a TROPICAL_FISH in the hotbar
        int fishSlot = -1;
        for (int i = 0; i < 36; i++)
        {
            if (mHotbarOnly.mValue && i > 8) break;
            ItemStack* item = nullptr;
            if (!TryCallWrapper([&]() { item = container->getItem(i); })) continue;
            if (!auraIsPtrReadable(item)) continue;
            if (!item->mItem) continue;
            Item* it = nullptr;
            if (!TryCallWrapper([&]() { it = item->getItem(); })) continue;
            if (!it) continue;
            if (it->mItemId == 267)
            {
                fishSlot = i;
                break;
            }
        }

        if (fishSlot != -1)
        {
            return fishSlot;
        }

        // Find a empty sot, OR an innert item
        for (int i = 0; i < 36; i++)
        {
            if (mHotbarOnly.mValue && i > 8) break;
            ItemStack* item = nullptr;
            if (!TryCallWrapper([&]() { item = container->getItem(i); })) continue;
            if (!auraIsPtrReadable(item)) continue;
            Item* it = nullptr;
            if (item->mItem && !TryCallWrapper([&]() { it = item->getItem(); })) it = nullptr;
            const int itemId = it ? it->mItemId : 0;
            if (!item->mItem || itemId == 0)
            {
                return i;
            }
        }

        for (int i = 0; i < 36; i++)
        {
            if (mHotbarOnly.mValue && i > 8) break;
            ItemStack* item = nullptr;
            if (!TryCallWrapper([&]() { item = container->getItem(i); })) continue;
            if (!auraIsPtrReadable(item)) continue;
            if (item->mItem && !ItemUtils::hasItemType(item))
            {
                return i;
            }
        }

        return supplies->mSelectedSlot;
    }

    int bestSword = ItemUtils::getBestItem(SItemType::Sword, mHotbarOnly.mValue);

    if (shouldUseFireSword(target))
    {
        return ItemUtils::getFireSword(mHotbarOnly.mValue);
    }

    return bestSword;
}

bool Aura::shouldUseFireSword(Actor* target)
{
    if (!target) return false;
    if (!auraIsActorAliveSafe(target)) return false;
    auto ci = ClientInstance::get();
    if (!ci) return false;
    auto player = ci->getLocalPlayer();
    if (!player) return false;

    int fireSw = ItemUtils::getFireSword(mHotbarOnly.mValue);
#ifdef __PRIVATE_BUILD__ //anyway doesnt bypass without spoof
    bool onFire = false;
    if (!TryCallWrapper([&]() { onFire = target->isOnFire(); })) return false;
    if (fireSw != -1 && mAutoFireSword.mValue && !onFire)
    {
        return true;
    }
    else
    {
        return false;
    }
#else
    return false;
#endif
}

void Aura::onEnable()
{
    if (!gFeatureManager || !gFeatureManager->mDispatcher) return;
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Aura::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &Aura::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &Aura::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &Aura::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BobHurtEvent, &Aura::onBobHurtEvent, nes::event_priority::FIRST>(this);
    gFeatureManager->mDispatcher->listen<BoneRenderEvent, &Aura::onBoneRenderEvent, nes::event_priority::FIRST>(this);

    if (mThirdPerson.mValue && !mThirdPersonOnlyOnAttack.mValue)
    {
        auto ci = ClientInstance::get();
        if (!ci) return;
        Options* opts = nullptr;
        if (!TryCallWrapper([&]() { opts = ci->getOptions(); })) return;
        if (!opts || !opts->mThirdPerson) return;
        opts->mThirdPerson->value = 1;
    }
}

bool chargingBow = false;

void Aura::onDisable()
{
    if (!gFeatureManager || !gFeatureManager->mDispatcher) return;
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Aura::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &Aura::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &Aura::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &Aura::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BobHurtEvent, &Aura::onBobHurtEvent>(this);
    sHasTarget = false;
    sTarget = nullptr;
    sTargetRuntimeID = 0;
    mRotating = false;
    mHasLockedTarget = false;
    mLockedRuntimeID = 0;
    mLockedTarget = nullptr;

    if (mThirdPerson.mValue && !mThirdPersonOnlyOnAttack.mValue)
    {
        auto ci = ClientInstance::get();
        if (!ci) return;
        Options* opts = nullptr;
        if (!TryCallWrapper([&]() { opts = ci->getOptions(); })) return;
        if (!opts || !opts->mThirdPerson) return;
        opts->mThirdPerson->value = 0;
    }
}

void Aura::rotate(Actor* target)
{
    if (mRotateMode.mValue == RotateMode::None) return;
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto player = ci->getLocalPlayer();
    if (!player) return;
    if (!auraIsActorAliveSafe(target)) return;

    AABB aabb;
    if (!TryCallWrapper([&]() { aabb = target->getAABB(); })) return;
    mTargetedAABB = aabb;
    mRotating = true;
}

void Aura::shootBow(Actor* target)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto player = ci->getLocalPlayer();
    if (!player) return;
    if (!auraIsActorAliveSafe(target)) return;

#ifdef __PRIVATE_BUILD__
    if (!mAutoBow.mValue) return;
#else
    return;
#endif

    int bowSlot = -1;
    int arrowSlot = -1;
    PlayerInventory* supplies = nullptr;
    if (!TryCallWrapper([&]() { supplies = player->getSupplies(); })) return;
    if (!supplies) return;
    Inventory* container = nullptr;
    if (!TryCallWrapper([&]() { container = supplies->getContainer(); })) return;
    if (!container) return;
    for (int i = 0; i < 36; i++)
    {
        ItemStack* item = nullptr;
        if (!TryCallWrapper([&]() { item = container->getItem(i); })) continue;
        if (!auraIsPtrReadable(item)) continue;
        if (!item->mItem) continue;
        Item* it = nullptr;
        if (!TryCallWrapper([&]() { it = item->getItem(); })) continue;
        if (!it) continue;
        if (it->mName.contains("bow"))
        {
            bowSlot = i;
        }
        if (it->mName.contains("arrow"))
        {
            arrowSlot = i;
        }
        if (bowSlot != -1 && arrowSlot != -1) break;
    }

    if (mHotbarOnly.mValue && bowSlot > 8) return;

    if (bowSlot == -1 || arrowSlot == -1) return;

    static int useTicks = 0;
    constexpr int maxUseTicks = 17;

    if (useTicks == 0)
    {
        spdlog::info("Starting to use bow");
        TryCallWrapper([&]() { container->startUsingItem(bowSlot); });
        chargingBow = true;
        useTicks++;
    }
    else if (useTicks < maxUseTicks)
    {
        useTicks++;
    }
    else if (useTicks >= maxUseTicks)
    {
        spdlog::info("Releasing bow");
        rotate(target);
        TryCallWrapper([&]() { container->releaseUsingItem(bowSlot); });
        chargingBow = false;
        useTicks = 0;
    }
}

void Aura::throwProjectiles(Actor* target)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    auto player = ci->getLocalPlayer();
    if (!player) return;
    if (!auraIsActorAliveSafe(target)) return;

    static uint64_t lastThrow = 0;
    int64_t throwDelay = mThrowDelay.mValue * 50.f;

    if (NOW - lastThrow < throwDelay) return;

    int snowballSlot = -1;
    PlayerInventory* supplies = nullptr;
    if (!TryCallWrapper([&]() { supplies = player->getSupplies(); })) return;
    if (!supplies) return;
    Inventory* container = nullptr;
    if (!TryCallWrapper([&]() { container = supplies->getContainer(); })) return;
    if (!container) return;
    if (mThrowProjectiles.mValue)
        for (int i = 0; i < 36; i++)
        {
            ItemStack* item = nullptr;
            if (!TryCallWrapper([&]() { item = container->getItem(i); })) continue;
            if (!auraIsPtrReadable(item)) continue;
            if (!item->mItem) continue;
            Item* it = nullptr;
            if (!TryCallWrapper([&]() { it = item->getItem(); })) continue;
            if (!it) continue;
            if (it->mName.contains("snowball"))
            {
                snowballSlot = i;
                break;
            }
        }

    if (mHotbarOnly.mValue && snowballSlot > 8) return;

    if (snowballSlot == -1)
    {
        return;
    }

    const int oldSlot = supplies->mSelectedSlot;
    supplies->mSelectedSlot = snowballSlot;
    GameMode* gm = nullptr;
    if (TryCallWrapper([&]() { gm = player->getGameMode(); }) && gm)
    {
        ItemStack* item = nullptr;
        if (TryCallWrapper([&]() { item = container->getItem(snowballSlot); }) && item)
        {
            TryCallWrapper([&]() { gm->baseUseItem(item); });
        }
    }
    supplies->mSelectedSlot = oldSlot;

    lastThrow = NOW;

}

float EaseInOutExpo(float pct)
{
    if (pct < 0.5f) {
        return (pow(2.f, 16.f * pct) - 1.f) / 510.f;
    }
    else {
        return 1.f - 0.5f * pow(2.f, -16.f * (pct - 0.5f));
    }
}

void Aura::onRenderEvent(RenderEvent& event)
{
    if (mAPSMin.mValue < 0) mAPSMin.mValue = 0;
    if (mAPSMax.mValue < mAPSMin.mValue + 1) mAPSMax.mValue = mAPSMin.mValue + 1;

    if (mStrafe.mValue)
    {
        auto player = ClientInstance::get()->getLocalPlayer();
        if (!auraIsActorValidSafe(player)) return;

        if (mRotating)
        {
            glm::vec3* ppos = nullptr;
            if (!TryCallWrapper([&]() { ppos = player->getPos(); })) return;
            if (!ppos || !auraIsPtrReadable(ppos)) return;
            glm::vec2 rots = MathUtils::getRots(*ppos, mTargetedAABB);
            ActorRotationComponent* rot = nullptr;
            if (!TryCallWrapper([&]() { rot = player->getActorRotationComponent(); })) return;
            if (!rot || !auraIsPtrReadable(rot)) return;
            if (mRotateMode.mValue == RotateMode::Normal && mStrafe.mValue && mHasOrbitRots) {
                rot->mPitch = mOrbitRots.x;
                rot->mYaw = mOrbitRots.y;
                rot->mOldPitch = mOrbitRots.x;
                rot->mOldYaw = mOrbitRots.y;
            } else {
                rot->mPitch = rots.x;
                rot->mYaw = rots.y;
                rot->mOldPitch = rots.x;
                rot->mOldYaw = rots.y;
            }

        }
    }

    if (mVisuals.mValue) {
        auto ci = ClientInstance::get();
        if (!ci) return;
        auto player = ci->getLocalPlayer();
        if (!auraIsActorValidSafe(player)) return;

        Actor* actor = nullptr;
        if (Aura::sTargetRuntimeID != 0) actor = ActorUtils::getActorFromRuntimeID(Aura::sTargetRuntimeID);
        if (!actor) actor = Aura::sTarget;

        if (!auraIsActorAliveSafe(actor))
        {
            Aura::sTarget = nullptr;
            Aura::sHasTarget = false;
            Aura::sTargetRuntimeID = 0;
            return;
        }

        bool isPlayer = false;
        if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) return;
        if (!isPlayer) return;

        glm::vec3* playerPosPtr = nullptr;
        glm::vec3* actorPosPtr = nullptr;
        StateVectorComponent* state = nullptr;
        AABBShapeComponent* shape = nullptr;
        if (!TryCallWrapper([&]() { playerPosPtr = player->getPos(); })) return;
        if (!TryCallWrapper([&]() { actorPosPtr = actor->getPos(); })) return;
        if (!TryCallWrapper([&]() { state = actor->getStateVectorComponent(); })) return;
        if (!TryCallWrapper([&]() { shape = actor->getAABBShapeComponent(); })) return;
        if (!playerPosPtr || !actorPosPtr || !state || !shape) return;
        if (!auraIsPtrReadable(playerPosPtr) || !auraIsPtrReadable(actorPosPtr)) return;
        if (!auraIsPtrReadable(state) || !auraIsPtrReadable(shape)) return;

        const auto playerPos = *playerPosPtr;
        const auto actorPos = *actorPosPtr;

        glm::vec3 pos = actorPos - glm::vec3(0.f, 1.62f, 0.f);
        glm::vec3 pos2 = state->mPos - glm::vec3(0.f, 1.62f, 0.f);
        glm::vec3 posOld = state->mPosOld - glm::vec3(0.f, 1.62f, 0.f);
        pos = posOld + (pos2 - posOld) * ImGui::GetIO().DeltaTime;

        float hitboxWidth = shape->mWidth;
        float hitboxHeight = shape->mHeight;

        glm::vec3 aabbMin = glm::vec3(pos.x - hitboxWidth / 2, pos.y, pos.z - hitboxWidth / 2);
        glm::vec3 aabbMax = glm::vec3(pos.x + hitboxWidth / 2, pos.y + hitboxHeight, pos.z + hitboxWidth / 2);

        aabbMin = aabbMin - glm::vec3(0.1f, 0.1f, 0.1f);
        aabbMax = aabbMax + glm::vec3(0.1f, 0.1f, 0.1f);

        float distance = glm::distance(playerPos, actorPos) + 2.5f;
        if (distance < 0) distance = 0;

        float scaledSphereSize = 1.0f / distance * 100.0f * mSpheresSizeMultiplier.mValue;
        if (scaledSphereSize < 1.0f) scaledSphereSize = 1.0f;
        if (scaledSphereSize < mSpheresMinSize.mValue) scaledSphereSize = mSpheresMinSize.mValue;

        glm::vec3 bottomOfHitbox = aabbMin;
        glm::vec3 topOfHitbox = aabbMax;
        bottomOfHitbox.x = pos.x;
        bottomOfHitbox.z = pos.z;
        topOfHitbox.x = pos.x;
        topOfHitbox.z = pos.z;
        topOfHitbox.y += 0.1f;

        static float pct = 0.f;
        static bool reversed = false;
        static uint64_t lastTime = NOW;

        float speed = mUpDownSpeed.mValue;
        const float denom = std::max(0.01f, speed - 0.2f);
        uint64_t visualTime = static_cast<uint64_t>(800.0f / denom);

        if (NOW - lastTime > visualTime) {
            reversed = !reversed;
            lastTime = NOW;
            pct = reversed ? 1.f : 0.f;
        }

        pct += !reversed ? (speed * ImGui::GetIO().DeltaTime) : -(speed * ImGui::GetIO().DeltaTime);
        pct = MathUtils::lerp(0.f, 1.f, pct);
        pos = MathUtils::lerp(bottomOfHitbox, topOfHitbox, EaseInOutExpo(pct));

        auto corrected = RenderUtils::transform.mMatrix;

        glm::vec2 screenPos = { 0, 0 };

        static float angleOffset = 0.f;
        angleOffset += (mUpDownSpeed.mValue * 30.f) * ImGui::GetIO().DeltaTime;
        float radius = (float)mSpheresRadius.mValue;

        for (int i = 0; i < mSpheresAmount.mValue; i++) {
            float angle = (i / (float)mSpheresAmount.mValue) * 360.f;
            angle += angleOffset;
            angle = MathUtils::wrap(angle, -180.f, 180.f);

            float rad = angle * (PI / 180.0f);

            float x = pos.x + radius * cosf(rad);
            float y = pos.y;
            float z = pos.z + radius * sinf(rad);

            glm::vec3 thisPos = { x, y, z };

            auto guiData = ci->getGuiData();
            if (!guiData) continue;
            if (!corrected.OWorldToScreen(
                RenderUtils::transform.mOrigin,
                thisPos, screenPos, MathUtils::fov,
                guiData->mResolution))
                continue;

            ImColor color = ColorUtils::getThemedColor(0);
            ImColor glowColor = color;
            glowColor.Value.w = 0.3f;

            ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), scaledSphereSize * 1.5f, glowColor);
            ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), scaledSphereSize, color);
        }
    }

}

void Aura::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor; // Local player
    if (!auraIsActorAliveSafe(player)) return;

    PlayerInventory* supplies = nullptr;
    if (!TryCallWrapper([&]() { supplies = player->getSupplies(); })) return;
    if (!supplies) return;

    auto actors = ActorUtils::getActorList(false, true);
    static std::unordered_map<int64_t, int64_t> lastAttacks = {};
    std::unordered_map<Actor*, int64_t> actorRids;
    actorRids.reserve(actors.size());
    std::unordered_set<int64_t> activeRids;
    activeRids.reserve(actors.size());
    for (auto actor : actors)
    {
        if (!auraIsActorValidSafe(actor)) continue;
        int64_t rid = 0;
        if (TryCallWrapper([&]() { rid = actor->getRuntimeID(); }) && rid > 0)
        {
            actorRids.emplace(actor, rid);
            activeRids.emplace(rid);
        }
    }
    for (auto it = lastAttacks.begin(); it != lastAttacks.end();)
    {
        if (activeRids.contains(it->first)) ++it;
        else it = lastAttacks.erase(it);
    }
    bool isMoving = Keyboard::isUsingMoveKeys();

    auto isValidTarget = [&](Actor* actor, float range) -> bool
    {
        if (!actor || actor == player) return false;
        if (!auraIsActorAliveSafe(actor)) return false;
        if (auraDistanceSafe(actor, player, FLT_MAX) > range) return false;
        if (!mAttackThroughWalls.mValue)
        {
            bool canSee = false;
            if (!TryCallWrapper([&]() { canSee = player->canSee(actor); })) return false;
            if (!canSee) return false;
        }
        bool actorIsPlayer = false;
        if (!TryCallWrapper([&]() { actorIsPlayer = actor->isPlayer(); })) return false;
        if (actorIsPlayer && gFriendManager && auraIsPtrReadable(gFriendManager) && gFriendManager->mEnabled)
        {
            if (gFriendManager->isFriend(actor) && !mFistFriends.mValue) return false;
        }
        return true;
    };

    // Sort actors by lastAttack if mode is switch
    auto getLastAttack = [&](Actor* a) -> int64_t
    {
        auto itRid = actorRids.find(a);
        if (itRid == actorRids.end()) return 0;
        auto it = lastAttacks.find(itRid->second);
        return it == lastAttacks.end() ? 0 : it->second;
    };
    if (mMode.mValue == Mode::Switch)
    {
        std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
            {
                return getLastAttack(a) < getLastAttack(b);
            });
    }
    else if (mMode.mValue == Mode::Multi)
    {
        // Sort actors by distance if mode is multi
        std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
            {
                return auraDistanceSafe(a, player, FLT_MAX) < auraDistanceSafe(b, player, FLT_MAX);
            });
    }

    static int64_t lastAttack = 0;
    int64_t now = NOW;
    float aps = mAPS.mValue;
    if (mRandomizeAPS.mValue)
    {
        // Validate min and max APS
        if (mAPSMin.mValue < 0) mAPSMin.mValue = 0;
        if (mAPSMax.mValue < mAPSMin.mValue + 1) mAPSMax.mValue = mAPSMin.mValue + 1;

        aps = MathUtils::random(mAPSMin.mValue, mAPSMax.mValue);
    }
    int64_t delay = 1000 / aps;

    bool foundAttackable = false;
    Actor* lockedTarget = nullptr;
    if (mAimLock.mValue && mHasLockedTarget)
    {
        if (mLockedRuntimeID != 0) lockedTarget = ActorUtils::getActorFromRuntimeID(mLockedRuntimeID);
        if (!lockedTarget) lockedTarget = mLockedTarget;
        float range = mDynamicRange.mValue && !isMoving ? mDynamicRangeValue.mValue : mRange.mValue;
        if (!isValidTarget(lockedTarget, range)) lockedTarget = nullptr;
        if (!lockedTarget)
        {
            mHasLockedTarget = false;
            mLockedRuntimeID = 0;
            mLockedTarget = nullptr;
        }
    }

    for (auto actor : actors)
    {
        float range = mDynamicRange.mValue && !isMoving ? mDynamicRangeValue.mValue : mRange.mValue;
        if (lockedTarget && actor != lockedTarget) continue;
        if (!isValidTarget(actor, range)) continue;

        if (mRotateMode.mValue == RotateMode::Normal)
            rotate(actor);
        foundAttackable = true;
        if (mAimLock.mValue && !mHasLockedTarget)
        {
            int64_t lockRid = 0;
            if (TryCallWrapper([&]() { lockRid = actor->getRuntimeID(); }) && lockRid != 0)
            {
                mLockedRuntimeID = lockRid;
                mLockedTarget = nullptr;
                mHasLockedTarget = true;
            }
            else
            {
                mLockedRuntimeID = 0;
                mLockedTarget = actor;
                mHasLockedTarget = true;
            }
        }
        int64_t rid = 0;
        if (TryCallWrapper([&]() { rid = actor->getRuntimeID(); }) && rid != 0)
        {
            sTargetRuntimeID = rid;
            sTarget = nullptr;
        }
        else
        {
            sTargetRuntimeID = 0;
            sTarget = actor;
        }

        throwProjectiles(actor);
        shootBow(actor);

        if (now - lastAttack < delay) break;

        if (mSwing.mValue)
        {
            if (mSwingDelay.mValue)
            {
                if (now - mLastSwing >= mSwingDelayValue.mValue * 1000)
                {
                    TryCallWrapper([&]() { player->swing(); });
                }
            }
            else TryCallWrapper([&]() { player->swing(); });
        }
        int slot = -1;
        int bestWeapon = getSword(actor);
        mHotbarOnly.mValue ? slot = bestWeapon : slot = player->getSupplies()->mSelectedSlot;

        auto ogActor = actor;
        actor = findObstructingActor(player, actor);
        if (!auraIsActorAliveSafe(actor)) continue;

        if (mSwitchMode.mValue == SwitchMode::Full && bestWeapon != -1)
        {
            supplies->mSelectedSlot = bestWeapon;
        }

        if (mRotateMode.mValue == RotateMode::Flick)
        {
            rotate(actor);
        }

        {
            Level* level = nullptr;
            if (TryCallWrapper([&]() { level = player->getLevel(); }) && level)
            {
                HitResult* hr = nullptr;
                if (TryCallWrapper([&]() { hr = level->getHitResult(); }) && hr)
                {
                    hr->mType = HitType::ENTITY;
                }
            }
        }


        bool frizmine = mBypassMode.mValue == BypassMode::Frizmine;
        if (frizmine)
        {
            glm::vec3* pPos = nullptr;
            glm::vec3* tPos = nullptr;
            ActorRotationComponent* rot = nullptr;
            if (TryCallWrapper([&]() { pPos = player->getPos(); }) &&
                TryCallWrapper([&]() { tPos = actor->getPos(); }) &&
                TryCallWrapper([&]() { rot = player->getActorRotationComponent(); }))
            {
                if (pPos && tPos && rot &&
                    auraIsPtrReadable(pPos) && auraIsPtrReadable(tPos) && auraIsPtrReadable(rot))
                {
                    auto packet = MinecraftPackets::createPacket<MovePlayerPacket>();
                    packet->mPlayerID = player->getRuntimeID();
                    packet->mPos = *tPos;
                    packet->mRot = { rot->mPitch, rot->mYaw };
                    packet->mYHeadRot = rot->mYaw;
                    packet->mResetPosition = PositionMode::Normal;
                    packet->mOnGround = true;
                    packet->mRidingID = -1;
                    packet->mCause = TeleportationCause::Unknown;
                    packet->mSourceEntityType = ActorType::Player;
                    packet->mTick = 0;
                    PacketUtils::queueSend(packet);
                }
            }
        }

        if (mAttackMode.mValue == AttackMode::Synched)
        {
            if (!auraIsActorAliveSafe(actor)) continue;

            std::shared_ptr<InventoryTransactionPacket> attackTransaction = ActorUtils::createAttackTransaction(actor, mSwitchMode.mValue == SwitchMode::Spoof ? bestWeapon : -1);
            if (!attackTransaction) continue;

        bool shouldUseFire = mLastTransaction + 200 < NOW && shouldUseFireSword(ogActor);
            bool spoofed = false;
            int oldSlot = mLastSlot;

#ifdef __PRIVATE_BUILD__
            if (mFireSwordSpoof.mValue && shouldUseFire)
            {
                spoofed = true;
                auto pkt = PacketUtils::createMobEquipmentPacket(bestWeapon);
                PacketUtils::queueSend(pkt, false);
            }
#endif

            PacketUtils::queueSend(attackTransaction, false);

            if (spoofed)
            {
                auto pkt = PacketUtils::createMobEquipmentPacket(oldSlot);
                PacketUtils::queueSend(pkt, false);
            }
        }
        else {
            if (!auraIsActorAliveSafe(actor)) continue;
            int oldSlot = supplies->mSelectedSlot;
            if (mSwitchMode.mValue == SwitchMode::Spoof && bestWeapon != -1)
            {
                supplies->mSelectedSlot = bestWeapon;
            }

            bool shouldUseFire = mLastTransaction + 200 < NOW && shouldUseFireSword(ogActor);
            bool spoofed = false;
            int oldPktSlot = mLastSlot;

#ifdef __PRIVATE_BUILD__
            if (mFireSwordSpoof.mValue && shouldUseFire)
            {
                auto pkt = PacketUtils::createMobEquipmentPacket(bestWeapon);
                auto ci = ClientInstance::get();
                if (ci)
                {
                    LoopbackPacketSender* sender = nullptr;
                    if (TryCallWrapper([&]() { sender = ci->getPacketSender(); }) && sender)
                    {
                        sender->send(pkt.get());
                    }
                }
                spoofed = true;
            }
#endif

            GameMode* gm = nullptr;
            if (TryCallWrapper([&]() { gm = player->getGameMode(); }) && gm)
            {
                TryCallWrapper([&]() { gm->attack(actor); });
            }
            supplies->mSelectedSlot = oldSlot;

            if (spoofed)
            {
                auto pkt = PacketUtils::createMobEquipmentPacket(oldPktSlot);
                auto ci = ClientInstance::get();
                if (ci)
                {
                    LoopbackPacketSender* sender = nullptr;
                    if (TryCallWrapper([&]() { sender = ci->getPacketSender(); }) && sender)
                    {
                        sender->send(pkt.get());
                    }
                }
            }
        }

        if (frizmine)
        {
            glm::vec3* pPos = nullptr;
            ActorRotationComponent* rot = nullptr;
            if (TryCallWrapper([&]() { pPos = player->getPos(); }) &&
                TryCallWrapper([&]() { rot = player->getActorRotationComponent(); }))
            {
                if (pPos && rot && auraIsPtrReadable(pPos) && auraIsPtrReadable(rot))
                {
                    auto packet = MinecraftPackets::createPacket<MovePlayerPacket>();
                    packet->mPlayerID = player->getRuntimeID();
                    packet->mPos = *pPos;
                    packet->mRot = { rot->mPitch, rot->mYaw };
                    packet->mYHeadRot = rot->mYaw;
                    packet->mResetPosition = PositionMode::Normal;
                    packet->mOnGround = true;
                    packet->mRidingID = -1;
                    packet->mCause = TeleportationCause::Unknown;
                    packet->mSourceEntityType = ActorType::Player;
                    packet->mTick = 0;
                    PacketUtils::queueSend(packet);
                }
            }
        }

        lastAttack = now;
        int64_t attackRid = 0;
        auto itRid = actorRids.find(actor);
        if (itRid != actorRids.end()) attackRid = itRid->second;
        else TryCallWrapper([&]() { attackRid = actor->getRuntimeID(); });
        if (attackRid > 0) lastAttacks[attackRid] = now;
        if (mMode.mValue == Mode::Single || mMode.mValue == Mode::Switch) break;
    }

    if (!foundAttackable)
    {
        mRotating = false;
        sTarget = nullptr;
        sTargetRuntimeID = 0;
        mHasLockedTarget = false;
        mLockedRuntimeID = 0;
        mLockedTarget = nullptr;
    }
    sHasTarget = foundAttackable;

    if (mThirdPerson.mValue && mThirdPersonOnlyOnAttack.mValue && sHasTarget) {
        if (!mIsThirdPerson) {
            auto ci = ClientInstance::get();
            if (!ci) return;
            Options* opts = nullptr;
            if (!TryCallWrapper([&]() { opts = ci->getOptions(); })) return;
            if (!opts || !opts->mThirdPerson) return;
            opts->mThirdPerson->value = 1;
            mIsThirdPerson = true;
        }
    }
    else if (mThirdPerson.mValue && mThirdPersonOnlyOnAttack.mValue && !sHasTarget) {
        if (mIsThirdPerson) {
            auto ci = ClientInstance::get();
            if (!ci) return;
            Options* opts = nullptr;
            if (!TryCallWrapper([&]() { opts = ci->getOptions(); })) return;
            if (!opts || !opts->mThirdPerson) return;
            opts->mThirdPerson->value = 0;
            mIsThirdPerson = false;
        }
    }
}

void Aura::onPacketOutEvent(PacketOutEvent& event)
{
    if (!mRotating) return;
    if (!event.mPacket) return;

    auto ci = ClientInstance::get();
    if (!ci) return;
    auto player = ci->getLocalPlayer();
    if (!auraIsActorValidSafe(player)) return;

    PacketID id{};
    if (!TryCallWrapper([&]() { id = event.mPacket->getId(); })) return;

    glm::vec3* ppos = nullptr;
    if (!TryCallWrapper([&]() { ppos = player->getPos(); })) return;
    if (!ppos || !auraIsPtrReadable(ppos)) return;

    if (id == PacketID::PlayerAuthInput) {
        auto pkt = event.getPacket<PlayerAuthInputPacket>();
        if (!pkt) return;
        glm::vec3 targetCenter = (mTargetedAABB.mMin + mTargetedAABB.mMax) * 0.5f;
        float dx = targetCenter.x - ppos->x;
        float dz = targetCenter.z - ppos->z;
        float dist = std::sqrt((dx * dx) + (dz * dz));
        float orbitStartDistance = 0.5f;
        float orbitStopDistance = 0.65f;
        if (mInOrbit) mInOrbit = dist <= orbitStopDistance;
        else mInOrbit = dist <= orbitStartDistance;

        glm::vec2 rots = MathUtils::getRots(*ppos, mTargetedAABB);
        if (mRotateMode.mValue == RotateMode::Normal && mStrafe.mValue) {
            if (!mHasOrbitRots) {
                mOrbitRots = rots;
                mHasOrbitRots = true;
            } else {
                float deltaYaw = MathUtils::wrap(rots.y - mOrbitRots.y, -180.f, 180.f);
                mOrbitRots.y += deltaYaw * 0.30f;
                mOrbitRots.x += (rots.x - mOrbitRots.x) * 0.30f;
            }
            pkt->mRot = mOrbitRots;
            pkt->mYHeadRot = mOrbitRots.y;
        } else {
            mHasOrbitRots = false;
            pkt->mRot = rots;
            pkt->mYHeadRot = rots.y;
        }
        if (mRotateMode.mValue == RotateMode::Normal && mStrafe.mValue) {

            static bool wasColliding = false;
            static float strafeDir = 1.0f;
            static float strafeMoveX = 0.0f;
            static float strafeMoveY = 0.0f;

            bool colliding = player->isCollidingHorizontal();
            if (colliding && !wasColliding) {
                mOrbitRight = !mOrbitRight;
            }
            wasColliding = colliding;

            if (auto moveInput = player->getMoveInputComponent()) {
                if (moveInput->mLeft) mOrbitRight = false;
                else if (moveInput->mRight) mOrbitRight = true;
            }

            float desiredRadius = 0.5f;
            float desiredStrafe = 0.0f;
            float desiredForward = 0.0f;
            if (mInOrbit) {
                float radiusErr = dist - desiredRadius;

                float orbitBlendDenom = MathUtils::clamp(desiredRadius * 0.25f, 0.18f, 1.20f);
                float orbitBlend = MathUtils::clamp((dist - desiredRadius) / orbitBlendDenom, 0.0f, 1.0f);

                float forwardScale = 1.75f / MathUtils::clamp(desiredRadius, 0.30f, 6.0f);
                desiredForward = MathUtils::clamp(radiusErr * forwardScale, -0.55f, 1.00f);
                desiredForward *= (0.25f + 0.75f * orbitBlend);
                if (dist < desiredRadius * 0.55f) {
                    desiredForward = std::max(desiredForward, 0.65f);
                }

                float strafeDirTarget = (mOrbitRight ? 1.0f : -1.0f);
                strafeDir += (strafeDirTarget - strafeDir) * 0.18f;

                desiredStrafe = (0.85f - 0.12f * orbitBlend) * strafeDir;
                if (dist > (desiredRadius * 0.60f)) {
                    float desiredAbs = std::min(0.85f, std::max(std::abs(desiredStrafe), 0.40f));
                    desiredStrafe = (desiredStrafe >= 0.0f) ? desiredAbs : -desiredAbs;
                }
            } else {
                float approach = MathUtils::clamp((dist - orbitStartDistance) * 0.9f, 0.35f, 1.0f);
                desiredForward = approach;
                desiredStrafe = 0.0f;
            }

            if (!player->isOnGround())
            {
                float distExcess = std::max(0.0f, dist - (desiredRadius + 0.08f));
                float distExcessT = MathUtils::clamp(distExcess / 0.60f, 0.0f, 1.0f);
                desiredForward = std::max(desiredForward, 0.55f + 0.35f * distExcessT);
                desiredStrafe *= (1.0f - 0.35f * distExcessT);
            }

            float smooth = 0.12f;
            strafeMoveX += (desiredStrafe - strafeMoveX) * smooth;
            strafeMoveY += (desiredForward - strafeMoveY) * smooth;

            glm::vec2 newMoveVec = { strafeMoveX, strafeMoveY };

            pkt->mInputData &= ~AuthInputAction::UP;
            pkt->mInputData &= ~AuthInputAction::DOWN;
            pkt->mInputData &= ~AuthInputAction::LEFT;
            pkt->mInputData &= ~AuthInputAction::RIGHT;
            pkt->mInputData &= ~AuthInputAction::UP_RIGHT;
            pkt->mInputData &= ~AuthInputAction::UP_LEFT;

            bool forward = newMoveVec.y > 0.01f;
            bool backward = newMoveVec.y < -0.01f;
            bool left = newMoveVec.x < -0.01f;
            bool right = newMoveVec.x > 0.01f;

            if (forward) pkt->mInputData |= AuthInputAction::UP;
            if (backward) pkt->mInputData |= AuthInputAction::DOWN;
            if (left) pkt->mInputData |= AuthInputAction::LEFT;
            if (right) pkt->mInputData |= AuthInputAction::RIGHT;

            pkt->mMove = newMoveVec;
            pkt->mAnalogMoveVector = newMoveVec;
            pkt->mVehicleRotation = newMoveVec;
            pkt->mInputMode = InputMode::MotionController;
        }
        if (mRotateMode.mValue == RotateMode::Flick) mRotating = false;
    }
    else if (id == PacketID::MovePlayer) {
        auto pkt = event.getPacket<MovePlayerPacket>();
        if (!pkt) return;
        glm::vec2 rots = MathUtils::getRots(*ppos, mTargetedAABB);
        pkt->mRot = rots;
        pkt->mYHeadRot = rots.y;
    }
    else if (id == PacketID::Animate)
    {
        mLastSwing = NOW;
    }
    else if (id == PacketID::InventoryTransaction)
    {
        auto pkt = event.getPacket<InventoryTransactionPacket>();
        if (!pkt) return;
        auto cit = pkt->mTransaction.get();
        if (!cit) return;

        if (cit->type == ComplexInventoryTransaction::Type::ItemUseTransaction)
        {
            const auto iut = reinterpret_cast<ItemUseInventoryTransaction*>(cit);
            if (iut->mActionType == ItemUseInventoryTransaction::ActionType::Place)
                mLastTransaction = NOW;
        }

        if (cit->type == ComplexInventoryTransaction::Type::ItemUseOnEntityTransaction)
        {
            const auto iut = reinterpret_cast<ItemUseOnActorInventoryTransaction*>(cit);
            if (iut->mActionType == ItemUseOnActorInventoryTransaction::ActionType::Attack)
            {
                if (mDebug.mValue)
                {
                    spdlog::info("clickpos: {}/{}/{}", iut->mClickPos.x, iut->mClickPos.y, iut->mClickPos.z);
                }
            }
        }
    }
    else if (id == PacketID::MobEquipment)
    {
        auto pkt = event.getPacket<MobEquipmentPacket>();
        if (!pkt) return;
        mLastSlot = pkt->mSlot;
    }

}

void Aura::onPacketInEvent(PacketInEvent& event)
{
    if (!event.mPacket) return;
    PacketID id{};
    if (!TryCallWrapper([&]() { id = event.mPacket->getId(); })) return;

    if (id == PacketID::RemoveActor)
    {
        auto packet = event.getPacket<RemoveActorPacket>();
        if (packet && sTargetRuntimeID != 0 && sTargetRuntimeID == packet->mRuntimeID)
        {
            sHasTarget = false;
            sTarget = nullptr;
            sTargetRuntimeID = 0;
        }
    }

    if (id == PacketID::ChangeDimension)
    {
        if (mDisableOnDimensionChange.mValue)
        {
            this->setEnabled(false);
        }
    }
}

void Aura::onBobHurtEvent(BobHurtEvent& event)
{
    if (sHasTarget)
    {
        event.mDoBlockAnimation = true;
    }
}

void Aura::onBoneRenderEvent(BoneRenderEvent& event)
{
    if (sHasTarget)
    {
        event.mDoBlockAnimation = true;
    }
}

Actor* Aura::findObstructingActor(Actor* player, Actor* target)
{
    if (mBypassMode.mValue == BypassMode::None) return target;
    if (!auraIsActorAliveSafe(player)) return target;
    if (!auraIsActorAliveSafe(target)) return target;

    bool isMoving = Keyboard::isUsingMoveKeys();
    auto actors = ActorUtils::getActorList(false, false);
    std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
        {
            return auraDistanceSafe(a, player, FLT_MAX) < auraDistanceSafe(b, player, FLT_MAX);
        });

    if (mBypassMode.mValue == BypassMode::Raycast)
    {
        glm::vec3* ppos = nullptr;
        glm::vec3* tpos = nullptr;
        if (!TryCallWrapper([&]() { ppos = player->getPos(); })) return target;
        if (!TryCallWrapper([&]() { tpos = target->getPos(); })) return target;
        if (!ppos || !tpos) return target;

        glm::vec3 rayStart = *ppos;
        glm::vec3 rayEnd = *tpos;
        glm::vec3 currentRayPos = rayStart;

        // Check for obstructing actors
        for (auto actor : actors)
        {
            if (actor == player || actor == target) continue;
            float range = mDynamicRange.mValue && !isMoving ? mDynamicRangeValue.mValue : mRange.mValue;
            if (!auraIsActorValidSafe(actor)) continue;
            if (auraDistanceSafe(actor, player, FLT_MAX) > range) continue;

            AABBShapeComponent* shape = nullptr;
            if (!TryCallWrapper([&]() { shape = actor->getAABBShapeComponent(); })) continue;
            if (!shape) continue;
            auto hitbox = *shape;

            if (MathUtils::rayIntersectsAABB(currentRayPos, rayEnd, hitbox.mMin, hitbox.mMax))
            {
                if (mDebug.mValue)
                {
                    spdlog::info("Found obstructing actor: {}", actor->mEntityIdentifier);
                    ChatUtils::displayClientMessage("Attacking obstructing actor: " + actor->mEntityIdentifier);
                }
                target = actor;
                break;
            }
        }

        return target;
    }
    else if (mBypassMode.mValue != BypassMode::None)
    {
        auto actors = ActorUtils::getActorList(false, false);
        std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
            {
                return auraDistanceSafe(a, player, FLT_MAX) < auraDistanceSafe(b, player, FLT_MAX);
            });

        for (auto actor : actors)
        {
            if (actor == player || actor == target) continue;
            if (!auraIsActorValidSafe(actor)) continue;
            float distance = auraDistanceSafe(actor, target, FLT_MAX);
            if (distance > 3.f) continue;

            std::string id = actor->mEntityIdentifier;
            AABBShapeComponent* shape = nullptr;
            if (!TryCallWrapper([&]() { shape = actor->getAABBShapeComponent(); })) continue;
            if (!shape) continue;
            float hitboxWidth = shape->mWidth;
            float hitboxHeight = shape->mHeight;

            if (id == "hivecommon:shadow" && distance < 3.f && mBypassMode.mValue == BypassMode::FlareonV2)
            {
                if (hitboxWidth != 0.86f || hitboxHeight != 2.32f) // Identify Correct Shadow
                {
                    continue;
                }

                if (mDebug.mValue)
                {
                    spdlog::info("Found shadow: {}", actor->mEntityIdentifier);
                    ChatUtils::displayClientMessage("Found shadow: " + actor->mEntityIdentifier);
                }


                return actor;
            }
        }

        return target;
    }

    return target;
}
