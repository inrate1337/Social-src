#include "ESP.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/NametagRenderEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Inventory/SimpleContainer.hpp>
#include <SDK/Minecraft/Inventory/Item.hpp>
#include <SDK/Minecraft/Inventory/ItemStack.hpp>

#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/Resources.hpp>

#include <imgui.h>
#include <mutex>
#include <unordered_set>
#include <vector>

static bool espTryGetMcCodeAt(const std::string& s, size_t i, size_t& prefixLen)
{
    if (i >= s.size()) return false;

    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xA7)
    {
        prefixLen = 1;
        return true;
    }

    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7)
    {
        prefixLen = 2;
        return true;
    }

    return false;
}

static std::string espStripMcColorCodes(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();)
    {
        size_t prefixLen = 0;
        if (espTryGetMcCodeAt(s, i, prefixLen))
        {
            size_t j = i + prefixLen;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j < s.size())
            {
                i = j + 1;
                continue;
            }
        }

        out.push_back(s[i]);
        i++;
    }

    return out;
}

static std::string espSanitizeName(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n' || c == '\r' || c == '\0')
        {
            i++;
            continue;
        }

        if (c < 0x20 || c == 0x7F)
        {
            i++;
            continue;
        }

        out.push_back(static_cast<char>(c));
        i++;
    }

    return out;
}

static std::string espFirstLine(std::string s)
{
    const size_t n = s.find('\n');
    if (n != std::string::npos) s.resize(n);
    return s;
}

static ImColor espMcColorFromCode(char code)
{
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(code))))
    {
    case '0': return ImColor(0.f, 0.f, 0.f, 1.f);
    case '1': return ImColor(0.f, 0.f, 0.66f, 1.f);
    case '2': return ImColor(0.f, 0.66f, 0.f, 1.f);
    case '3': return ImColor(0.f, 0.66f, 0.66f, 1.f);
    case '4': return ImColor(0.66f, 0.f, 0.f, 1.f);
    case '5': return ImColor(0.66f, 0.f, 0.66f, 1.f);
    case '6': return ImColor(1.f, 0.66f, 0.f, 1.f);
    case '7': return ImColor(0.66f, 0.66f, 0.66f, 1.f);
    case '8': return ImColor(0.33f, 0.33f, 0.33f, 1.f);
    case '9': return ImColor(0.33f, 0.33f, 1.f, 1.f);
    case 'a': return ImColor(0.33f, 1.f, 0.33f, 1.f);
    case 'b': return ImColor(0.33f, 1.f, 1.f, 1.f);
    case 'c': return ImColor(1.f, 0.33f, 0.33f, 1.f);
    case 'd': return ImColor(1.f, 0.33f, 1.f, 1.f);
    case 'e': return ImColor(1.f, 1.f, 0.33f, 1.f);
    case 'f': return ImColor(1.f, 1.f, 1.f, 1.f);
    default: return ImColor(1.f, 1.f, 1.f, 1.f);
    }
}

static ImColor espShimmerColor(ImColor base, float x)
{
    const float t = static_cast<float>(ImGui::GetTime());
    const float wave = 0.5f + 0.5f * std::sin(t * 2.5f + x * 0.060f);
    const float wave2 = 0.5f + 0.5f * std::sin(t * 3.2f + x * 0.034f + 1.2f);
    const float brighten = 0.86f + 0.14f * wave;
    const float whiteMix = 0.10f + 0.34f * wave2;

    ImVec4 v = base.Value;
    v.x = std::clamp(v.x * brighten, 0.f, 1.f);
    v.y = std::clamp(v.y * brighten, 0.f, 1.f);
    v.z = std::clamp(v.z * brighten, 0.f, 1.f);

    v.x = std::clamp(v.x + (1.f - v.x) * whiteMix, 0.f, 1.f);
    v.y = std::clamp(v.y + (1.f - v.y) * whiteMix, 0.f, 1.f);
    v.z = std::clamp(v.z + (1.f - v.z) * whiteMix, 0.f, 1.f);

    return ImColor(v);
}

static void espDrawFirstMcSegmentShadowed(ImDrawList* drawList, const std::string& text, ImVec2 pos, float fontSize, bool specializedMultiplier)
{
    if (!drawList) return;

    auto* font = ImGui::GetFont();
    if (!font) return;

    const std::string safeText = espSanitizeName(text);
    const std::string plainAll = espStripMcColorCodes(safeText);
    if (plainAll.empty()) return;

    ImColor defaultColor = ImColor(1.0f, 1.0f, 1.0f, 1.0f);
    ImColor shadowColor = ImColor(defaultColor.Value.x * 0.35f, defaultColor.Value.y * 0.35f, defaultColor.Value.z * 0.35f, 1.f * defaultColor.Value.w);

    const float shadowMul = specializedMultiplier ? fontSize / 24.f : 1.f;
    ImVec2 shadowPos = ImVec2(pos.x + 1.25f * shadowMul, pos.y + 1.25f * shadowMul);
    drawList->AddText(font, fontSize, shadowPos, shadowColor, plainAll.c_str());

    ImColor current = defaultColor;
    std::string segment;
    segment.reserve(safeText.size());

    auto flush = [&]()
    {
        if (segment.empty()) return;
        drawList->AddText(font, fontSize, pos, espShimmerColor(current, pos.x), segment.c_str());
        pos.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, segment.c_str()).x;
        segment.clear();
    };

    for (size_t i = 0; i < safeText.size();)
    {
        size_t prefixLen = 0;
        if (espTryGetMcCodeAt(safeText, i, prefixLen))
        {
            size_t j = i + prefixLen;
            while (j < safeText.size() && (safeText[j] == ' ' || safeText[j] == '\t')) j++;
            if (j < safeText.size())
            {
                flush();

                const char code = safeText[j];
                if (static_cast<char>(std::tolower(static_cast<unsigned char>(code))) == 'r')
                {
                    current = defaultColor;
                }
                else
                {
                    ImColor mapped = espMcColorFromCode(code);
                    mapped.Value.w = defaultColor.Value.w;
                    current = mapped;
                }

                i = j + 1;
                continue;
            }
        }

        segment.push_back(safeText[i]);
        i++;
    }

    flush();
}

struct EspPlayerEntry
{
    int64_t runtimeId;
    glm::vec2 size;
    bool isLocal;
    bool isFriend;
    bool isPlayer;
    std::string name;
    float health;
    float maxHealth;
};
std::vector<EspPlayerEntry> gEspPlayers;
std::mutex gEspMutex;

struct EspItemIconCacheEntry
{
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0;
    int h = 0;
    bool loaded = false;
    double lastUsedTime = 0.0;
};

static std::unordered_map<std::string, EspItemIconCacheEntry> gEspItemIconCache;

static void espPruneIconCache()
{
    const double now = ImGui::GetTime();
    constexpr double kStaleSeconds = 30.0;
    constexpr size_t kMaxCacheSize = 512;

    if (gEspItemIconCache.empty()) return;

    for (auto it = gEspItemIconCache.begin(); it != gEspItemIconCache.end();)
    {
        const bool stale = (now - it->second.lastUsedTime) > kStaleSeconds;
        if (stale)
        {
            it = gEspItemIconCache.erase(it);
            continue;
        }
        ++it;
    }

    if (gEspItemIconCache.size() <= kMaxCacheSize) return;

    std::vector<std::pair<std::string, double>> byAge;
    byAge.reserve(gEspItemIconCache.size());
    for (auto& [k, v] : gEspItemIconCache) byAge.emplace_back(k, v.lastUsedTime);
    std::sort(byAge.begin(), byAge.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

    const size_t extra = gEspItemIconCache.size() - kMaxCacheSize;
    for (size_t i = 0; i < extra && i < byAge.size(); i++)
    {
        auto it = gEspItemIconCache.find(byAge[i].first);
        if (it == gEspItemIconCache.end()) continue;
        gEspItemIconCache.erase(it);
    }
}

static ID3D11ShaderResourceView* getEspItemIcon(const std::string& key)
{
    if (key.empty()) return nullptr;

    auto& cached = gEspItemIconCache[key];
    cached.lastUsedTime = ImGui::GetTime();
    if (cached.loaded) return cached.srv;
    cached.loaded = true;

    auto tryLoad = [&](const std::string& name) -> bool {
        auto it = ResourceLoader::Resources.find(name);
        if (it == ResourceLoader::Resources.end()) return false;
        if (it->second.data() == nullptr) return false;
        return D3DHook::loadTextureFromEmbeddedResource(name.c_str(), &cached.srv, &cached.w, &cached.h);
    };

    if (key.ends_with(".png"))
    {
        tryLoad(key);
        return cached.srv;
    }

    tryLoad(key + ".png");
    if (!cached.srv)
    {
        tryLoad(std::string("items/") + key + ".png");
    }

    return cached.srv;
}
void ESP::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &ESP::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &ESP::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<NametagRenderEvent, &ESP::onNametagRenderEvent>(this);
}
void ESP::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &ESP::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &ESP::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<NametagRenderEvent, &ESP::onNametagRenderEvent>(this);
    std::lock_guard<std::mutex> lock(gEspMutex);
    gEspPlayers.clear();

    for (auto& [_, tex] : gEspItemIconCache)
    {
    }
    gEspItemIconCache.clear();
}
void ESP::onBaseTickEvent(BaseTickEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!ci->getLevelRenderer()) return;
    auto localPlayer = ci->getLocalPlayer();
    if (!localPlayer) return;
    auto actors = ActorUtils::getActorList(true, true);
    std::vector<EspPlayerEntry> entries;
    entries.reserve(actors.size());
    for (auto actor : actors)
    {
        if (!actor) continue;
        bool isLocal = actor == localPlayer;
        if (isLocal)
        {
            if (!mRenderLocal.mValue) continue;
            if (ci->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) continue;
        }
        auto shape = actor->getAABBShapeComponent();
        if (!shape) continue;
        float distance = localPlayer->distanceTo(actor);
        if (distance < 0.f) continue;
        if (mDistanceLimited.mValue && distance > mDistance.mValue) continue;
        bool isFriend = false;
        if (actor->isPlayer() && gFriendManager)
        {
            if (gFriendManager->isFriend(actor))
            {
                if (!mShowFriends.mValue) continue;
                isFriend = true;
            }
        }
        std::string name = espFirstLine(actor->getNameTag());
        if (name.empty()) name = actor->getRawName();
        float health = actor->getHealth();
        float maxHealth = actor->getMaxHealth();
        if (maxHealth <= 0.0f) maxHealth = 1.0f;

        entries.push_back({
            actor->getRuntimeID(),
            glm::vec2(shape->mWidth, shape->mHeight),
            isLocal,
            isFriend,
            actor->isPlayer(),
            name,
            health,
            maxHealth
        });
    }

    std::lock_guard<std::mutex> lock(gEspMutex);
    gEspPlayers.swap(entries);
}
void ESP::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!ci->getLevelRenderer()) return;
    auto localPlayer = ci->getLocalPlayer();
    if (!localPlayer) return;
    std::vector<EspPlayerEntry> players;
    {
        std::lock_guard<std::mutex> lock(gEspMutex);
        if (gEspPlayers.empty()) return;
        players = gEspPlayers;
    }
    auto drawList = ImGui::GetBackgroundDrawList();
    FontHelper::pushPrefFont();
    for (auto& entry : players)
    {
        Actor* actor = nullptr;
        if (entry.isLocal)
            actor = localPlayer;
        else
            actor = ActorUtils::getActorFromRuntimeID(entry.runtimeId);
        if (!actor || !actor->isValid())
            continue;

        float distance = entry.isLocal ? 0.f : localPlayer->distanceTo(actor);
        if (distance < 0.f) continue;
        if (mDistanceLimited.mValue && distance > mDistance.mValue) continue;

        glm::vec3 renderPos{};
        if (entry.isLocal)
        {
            renderPos = RenderUtils::transform.mPlayerPos;
        }
        else if (auto renderPosComp = actor->getRenderPositionComponent())
        {
            renderPos = renderPosComp->mPosition;
        }
        else if (auto pos = actor->getPos())
        {
            renderPos = *pos;
        }
        else
        {
            continue;
        }
        if (entry.isPlayer)
            renderPos -= PLAYER_HEIGHT_VEC;
        float hitboxWidth = entry.size.x;
        float hitboxHeight = entry.size.y;
        glm::vec3 aabbMin = renderPos - glm::vec3(hitboxWidth / 2, 0, hitboxWidth / 2);
        glm::vec3 aabbMax = renderPos + glm::vec3(hitboxWidth / 2, hitboxHeight, hitboxWidth / 2);
        aabbMin = aabbMin - glm::vec3(0.1f, 0.1f, 0.1f);
        aabbMax = aabbMax + glm::vec3(0.1f, 0.1f, 0.1f);
        AABB aabb = AABB(aabbMin, aabbMax, true);
        std::vector<ImVec2> points = MathUtils::getImBoxPoints(aabb);
        if (points.empty()) continue;
        ImVec2 minPos = points[0];
        ImVec2 maxPos = points[0];
        for (size_t i = 1; i < points.size(); i++)
        {
            ImVec2 p = points[i];
            if (p.x < minPos.x) minPos.x = p.x;
            if (p.y < minPos.y) minPos.y = p.y;
            if (p.x > maxPos.x) maxPos.x = p.x;
            if (p.y > maxPos.y) maxPos.y = p.y;
        }
        ImColor color = ColorUtils::getGuiAccentColor(0);
        if (entry.isFriend) color = ImColor(0.0f, 1.0f, 0.0f);
        ImVec2 boxCenter = ImVec2((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
        float boxHeight = maxPos.y - minPos.y;
        float padText = 2.0f;
        if (mShowBox.mValue)
        {
            ImColor outlineColor = ImColor(0.0f, 0.0f, 0.0f, 1.0f);
            float outlineThickness = 0.4f;
            if (mRenderFilled.mValue)
            {
                constexpr float shrinkFactor = 1.7f;
                float fillWidth = (maxPos.x - minPos.x) / shrinkFactor;
                float fillHeight = (maxPos.y - minPos.y) / shrinkFactor;
                ImVec2 fillMin = ImVec2(boxCenter.x - fillWidth * 0.5f, boxCenter.y - fillHeight * 0.5f);
                ImVec2 fillMax = ImVec2(boxCenter.x + fillWidth * 0.5f, boxCenter.y + fillHeight * 0.5f);
                drawList->AddRectFilled(fillMin, fillMax, ImColor(color.Value.x, color.Value.y, color.Value.z, 0.25f));
            }
            drawList->AddRect(minPos, maxPos, color, 0.0f, 0, 1.5f);
            ImVec2 innerMin = ImVec2(minPos.x + 1.0f, minPos.y + 1.0f);
            ImVec2 innerMax = ImVec2(maxPos.x - 1.0f, maxPos.y - 1.0f);
            ImVec2 outerMin = ImVec2(minPos.x - 1.0f, minPos.y - 1.0f);
            ImVec2 outerMax = ImVec2(maxPos.x + 1.0f, maxPos.y + 1.0f);
            drawList->AddRect(innerMin, innerMax, outlineColor, 0.0f, 0, outlineThickness);
            drawList->AddRect(outerMin, outerMax, outlineColor, 0.0f, 0, outlineThickness);
        }

        if (mShowName.mValue)
        {
            const std::string name = espSanitizeName(espFirstLine(entry.name));
            constexpr float nameFontSize = 15.0f;
            const std::string namePlain = espStripMcColorCodes(name);
            ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(nameFontSize, FLT_MAX, 0, namePlain.c_str());
            float width = textSize.x;
            float height = textSize.y;
            int anchor = mNameAnchor.mValue;
            float extraTop = 0.0f;
            float extraBottom = 0.0f;
            if (mShowItems.mValue && mItemsAnchor.mValue == anchor && (anchor == 0 || anchor == 1))
            {
                if (anchor == 0) extraTop = 14.0f + padText;
                if (anchor == 1) extraBottom = 14.0f + padText;
            }

            ImVec2 basePos = ImVec2(boxCenter.x - width * 0.5f, minPos.y - height - padText - extraTop);
            if (anchor == 1)
                basePos = ImVec2(boxCenter.x - width * 0.5f, maxPos.y + padText + extraBottom);
            else if (anchor == 2)
                basePos = ImVec2(minPos.x - padText - width, boxCenter.y - height * 0.5f);
            else if (anchor == 3)
                basePos = ImVec2(maxPos.x + padText, boxCenter.y - height * 0.5f);

            ImVec2 drawPos = ImVec2(basePos.x + mNameOffsetX.mValue, basePos.y + mNameOffsetY.mValue);
            espDrawFirstMcSegmentShadowed(drawList, name, drawPos, nameFontSize, true);
        }
        if (mShowDistance.mValue)
        {
            int distInt = static_cast<int>(distance);
            std::string distText = std::to_string(distInt) + "m";
            constexpr float distFontSize = 14.0f;
            ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(distFontSize, FLT_MAX, 0, distText.c_str());
            float width = textSize.x;
            float height = textSize.y;
            int anchor = mDistanceAnchor.mValue;

            ImVec2 basePos = ImVec2(boxCenter.x - width * 0.5f, maxPos.y + padText);
            if (anchor == 0)
                basePos = ImVec2(boxCenter.x - width * 0.5f, minPos.y - height - padText);
            else if (anchor == 2)
                basePos = ImVec2(minPos.x - padText - width, boxCenter.y - height * 0.5f);
            else if (anchor == 3)
                basePos = ImVec2(maxPos.x + padText, boxCenter.y - height * 0.5f);

            ImVec2 drawPos = ImVec2(basePos.x + mDistanceOffsetX.mValue, basePos.y + mDistanceOffsetY.mValue);
            espDrawFirstMcSegmentShadowed(drawList, distText, drawPos, distFontSize, true);
        }
        if (mShowHealth.mValue)
        {
            float barWidth = 4.0f;
            float barHeight = boxHeight;
            bool right = mHealthAnchor.mValue == 1;
            ImVec2 baseMin = ImVec2(minPos.x - padText - barWidth, minPos.y);
            if (right)
                baseMin = ImVec2(maxPos.x + padText, minPos.y);

            ImVec2 barMin = ImVec2(baseMin.x + mHealthOffsetX.mValue, baseMin.y + mHealthOffsetY.mValue);
            ImVec2 barMax = ImVec2(barMin.x + barWidth, barMin.y + barHeight);

            float ratio = entry.health / entry.maxHealth;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;

            drawList->AddRect(barMin, barMax, ImColor(0.0f, 0.0f, 0.0f, 0.9f), 2.0f, 0, 1.2f);

            float filledHeight = barHeight * ratio;
            ImVec2 fillMin = ImVec2(barMin.x, barMax.y - filledHeight);
            ImVec2 fillMax = ImVec2(barMax.x, barMax.y);

            ImColor themed = ColorUtils::getGuiAccentColor(0);
            ImColor topColor = ImColor(themed.Value.x, themed.Value.y, themed.Value.z, 1.0f);
            ImColor bottomColor = ImColor(0.0f, 0.0f, 0.0f, 1.0f);

            drawList->AddRectFilledMultiColor(
                fillMin,
                fillMax,
                topColor,
                topColor,
                bottomColor,
                bottomColor
            );
        }

        if (mShowItems.mValue)
        {
            auto sanitizeItemName = [](std::string name) -> std::string
            {
                if (name.starts_with("minecraft:"))
                    name = name.substr(std::string("minecraft:").size());

                if (name.starts_with("item.") && name.ends_with(".name") && name.size() > 10)
                    name = name.substr(5, name.size() - 5 - 5);

                if (name.starts_with("tile.") && name.ends_with(".name") && name.size() > 10)
                    name = name.substr(5, name.size() - 5 - 5);

                auto lastDot = name.find_last_of('.');
                if (lastDot != std::string::npos && lastDot + 1 < name.size())
                    name = name.substr(lastDot + 1);

                return name;
            };

            std::vector<ItemStack*> stacks;
            stacks.reserve(6);
            std::unordered_set<ItemStack*> seen;
            seen.reserve(8);

            auto addStack = [&](ItemStack* st) {
                if (!st) return;
                if (seen.insert(st).second)
                {
                    stacks.push_back(st);
                }
            };

            if (auto armor = actor->getArmorContainer())
            {
                for (int i = 0; i < 4; i++)
                {
                    addStack(armor->getItem(i));
                }
            }

            if (auto supplies = actor->getSupplies())
            {
                if (auto inv = supplies->getContainer())
                {
                    int slot = supplies->mSelectedSlot;
                    if (slot >= 0 && slot < 36)
                    {
                        addStack(inv->getItem(slot));
                    }
                }
            }

            if (auto offhand = actor->getOffhandContainer())
            {
                addStack(offhand->getItem(0));
            }

            std::vector<ID3D11ShaderResourceView*> icons;
            icons.reserve(stacks.size());
            for (auto st : stacks)
            {
                if (!st)
                    continue;
                if (st->mCount <= 0)
                    continue;
                if (!st->mItem || !*st->mItem)
                    continue;

                auto item = *st->mItem;
                auto key = sanitizeItemName(item->mName);
                if (key.empty())
                    continue;
                if (auto* srv = getEspItemIcon(key))
                {
                    icons.push_back(srv);
                }
            }

            if (!icons.empty())
            {
                constexpr float iconSize = 14.0f;
                constexpr float iconGap = 2.0f;
                float groupW = icons.size() * iconSize + (icons.size() > 0 ? (icons.size() - 1) * iconGap : 0.0f);
                float groupH = iconSize;
                int anchor = mItemsAnchor.mValue;
                ImVec2 basePos = ImVec2(boxCenter.x - groupW * 0.5f, minPos.y - groupH - padText);
                if (anchor == 1)
                    basePos = ImVec2(boxCenter.x - groupW * 0.5f, maxPos.y + padText);
                else if (anchor == 2)
                    basePos = ImVec2(minPos.x - padText - groupW, boxCenter.y - groupH * 0.5f);
                else if (anchor == 3)
                    basePos = ImVec2(maxPos.x + padText, boxCenter.y - groupH * 0.5f);

                ImVec2 drawPos = ImVec2(basePos.x + mItemsOffsetX.mValue, basePos.y + mItemsOffsetY.mValue);

                for (size_t i = 0; i < icons.size(); i++)
                {
                    float x0 = drawPos.x + i * (iconSize + iconGap);
                    ImVec2 iconMin = ImVec2(x0, drawPos.y);
                    ImVec2 iconMax = ImVec2(x0 + iconSize, drawPos.y + iconSize);
                    drawList->AddImage(icons[i], iconMin, iconMax);
                }
            }
        }
    }
    FontHelper::popPrefFont();
    espPruneIconCache();
}

void ESP::onNametagRenderEvent(NametagRenderEvent& event)
{
    auto* ci = ClientInstance::get();
    if (!ci) return;

    auto* localPlayer = ci->getLocalPlayer();
    if (!localPlayer) return;

    auto* actor = event.mActor;
    if (!actor) return;
    if (ActorUtils::isBot(actor)) return;
    if (!actor->isPlayer()) return;

    if (!mShowName.mValue) return;

    if (actor == localPlayer)
    {
        if (!mRenderLocal.mValue) return;
        if (ci->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) return;
    }

    float distance = actor == localPlayer ? 0.f : localPlayer->distanceTo(actor);
    if (distance < 0.f) return;
    if (mDistanceLimited.mValue && distance > mDistance.mValue) return;

    if (gFriendManager && gFriendManager->isFriend(actor) && !mShowFriends.mValue) return;

    event.cancel();
}
