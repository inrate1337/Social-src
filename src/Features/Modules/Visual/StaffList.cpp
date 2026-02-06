#include "StaffList.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/MiscUtils/NotifyUtils.hpp>
#include <Utils/StringUtils.hpp>
#include <Features/Modules/Visual/Interface.hpp>

static std::string trimQuotes(std::string s)
{
    if (s.size() >= 2)
    {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))
        {
            s = s.substr(1, s.size() - 2);
        }
    }
    return s;
}

static ImVec2 slCalcSize(ImFont* font, float fontSize, const std::string& text)
{
    if (!font || text.empty()) return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text.c_str());
}

static float slExpApproach(float current, float target, float delta, float speed)
{
    if (delta <= 0.f) return current;
    const float t = 1.f - std::exp(-speed * delta);
    return current + (target - current) * t;
}

static float slEaseInOut(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static float slEaseOutCubic(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

static float slEaseOutBack(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float inv = t - 1.f;
    return 1.f + c3 * inv * inv * inv + c1 * inv * inv;
}

static void slSpringTo(float& value, float& velocity, float target, float delta, float stiffness, float damping)
{
    if (delta <= 0.f) return;
    const float accel = (target - value) * stiffness;
    velocity += accel * delta;
    velocity *= std::exp(-damping * delta);
    value += velocity * delta;
}

static size_t slUtf8CharLen(unsigned char c)
{
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static ImColor slShimmerColor(ImColor base, float x, float t)
{
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

static void slAddTextShadowed(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor color, const char* text)
{
    if (!drawList || !font || !text || text[0] == '\0') return;
    drawList->AddText(font, fontSize, pos, slShimmerColor(color, pos.x, static_cast<float>(ImGui::GetTime())), text);
}

static void slAddTextShadowedNoShimmer(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor color, const char* text)
{
    if (!drawList || !font || !text || text[0] == '\0') return;
    drawList->AddText(font, fontSize, pos, color, text);
}

static bool tryGetMcCodeAt(const std::string& s, size_t i, size_t& prefixLen)
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

static std::string stripMcColorCodes(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();)
    {
        size_t prefixLen = 0;
        if (tryGetMcCodeAt(s, i, prefixLen))
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

static std::string compactMcColorCodes(std::string s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();)
    {
        size_t prefixLen = 0;
        if (tryGetMcCodeAt(s, i, prefixLen))
        {
            for (size_t k = 0; k < prefixLen; k++) out.push_back(s[i + k]);
            size_t j = i + prefixLen;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j < s.size())
            {
                out.push_back(s[j]);
                i = j + 1;
                continue;
            }
        }

        out.push_back(s[i]);
        i++;
    }

    return out;
}

static ImColor mcColorFromCode(char code)
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

static float calcMcTextWidth(ImFont* font, float fontSize, const std::string& text)
{
    return slCalcSize(font, fontSize, stripMcColorCodes(text)).x;
}

static void drawMcColoredText(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor defaultColor, const std::string& text)
{
    if (!drawList || !font || text.empty() || defaultColor.Value.w <= 0.f) return;

    const float t = static_cast<float>(ImGui::GetTime());
    ImColor current = defaultColor;
    char buf[8]{};

    for (size_t i = 0; i < text.size();)
    {
        size_t prefixLen = 0;
        if (tryGetMcCodeAt(text, i, prefixLen))
        {
            size_t j = i + prefixLen;
            while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) j++;
            if (j < text.size())
            {
                char code = text[j];
                if (static_cast<char>(std::tolower(static_cast<unsigned char>(code))) == 'r')
                {
                    current = defaultColor;
                }
                else
                {
                    ImColor mapped = mcColorFromCode(code);
                    mapped.Value.w = defaultColor.Value.w;
                    current = mapped;
                }
                i = j + 1;
                continue;
            }
        }
        const size_t len = std::min(slUtf8CharLen(static_cast<unsigned char>(text[i])), text.size() - i);
        if (len == 0) break;

        const size_t copyLen = std::min(len, sizeof(buf) - 1);
        std::memcpy(buf, text.data() + i, copyLen);
        buf[copyLen] = '\0';

        drawList->AddText(font, fontSize, pos, slShimmerColor(current, pos.x, t), buf);
        pos.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, buf).x;

        i += len;
    }
}

static void drawMcColoredTextShadowed(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 pos, ImColor defaultColor, const std::string& text)
{
    if (!drawList || !font || text.empty() || defaultColor.Value.w <= 0.f) return;
    drawMcColoredText(drawList, font, fontSize, pos, defaultColor, text);
}

static std::string normalizeStaffName(const std::string& name)
{
    std::string out = trimQuotes(std::string(StringUtils::trim(name)));
    out = stripMcColorCodes(out);
    return out;
}

static std::string normalizeStaffDisplayName(const std::string& name)
{
    std::string out = trimQuotes(std::string(StringUtils::trim(name)));
    out = compactMcColorCodes(out);
    return out;
}

static std::string normalizeStaffRank(const std::string& rank)
{
    std::string out = trimQuotes(std::string(StringUtils::trim(rank)));
    out = compactMcColorCodes(out);
    return out;
}

void StaffList::onInit()
{
    mStaffStore.load();

    bool changed = false;
    std::unordered_map<std::string, size_t> indexByLower;
    std::vector<StaffEntry> cleaned;
    cleaned.reserve(mStaffStore.mObjects.size());

    for (auto& entry : mStaffStore.mObjects)
    {
        StaffEntry normalized = entry;
        if (normalized.displayName.empty()) normalized.displayName = normalized.name;
        normalized.displayName = normalizeStaffDisplayName(normalized.displayName);
        normalized.name = normalizeStaffName(normalized.name.empty() ? normalized.displayName : normalized.name);
        normalized.rank = normalizeStaffRank(normalized.rank);

        if (normalized.name.empty())
        {
            changed = true;
            continue;
        }

        const std::string key = StringUtils::toLower(normalized.name);
        auto it = indexByLower.find(key);
        if (it == indexByLower.end())
        {
            indexByLower.emplace(key, cleaned.size());
            cleaned.emplace_back(std::move(normalized));
        }
        else
        {
            cleaned[it->second] = std::move(normalized);
            changed = true;
        }
    }

    if (changed)
    {
        mStaffStore.mObjects = std::move(cleaned);
        mStaffStore.save();
    }

    requestRefresh();
}

void StaffList::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &StaffList::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &StaffList::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = true;
    requestRefresh();
}

void StaffList::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &StaffList::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &StaffList::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = false;

    std::scoped_lock lock(mOnlineMutex);
    mOnlineStaff.clear();
    mFutureValid = false;
}

void StaffList::requestRefresh()
{
    mNeedsRefresh = true;
}

void StaffList::addStaff(const std::string& name, const std::string& rank)
{
    const std::string displayName = normalizeStaffDisplayName(name);
    const std::string cleanedName = normalizeStaffName(displayName);
    const std::string cleanedRank = normalizeStaffRank(rank);
    if (cleanedName.empty() || cleanedRank.empty()) return;

    const std::string key = StringUtils::toLower(cleanedName);

    for (auto& entry : mStaffStore.mObjects)
    {
        if (StringUtils::toLower(entry.name) == key)
        {
            entry.name = cleanedName;
            entry.rank = cleanedRank;
            entry.displayName = displayName.empty() ? cleanedName : displayName;
            if (entry.storedAt == 0) entry.storedAt = NOW;
            mStaffStore.save();
            requestRefresh();
            return;
        }
    }

    StaffEntry entry;
    entry.name = cleanedName;
    entry.rank = cleanedRank;
    entry.displayName = displayName.empty() ? cleanedName : displayName;
    entry.storedAt = NOW;
    mStaffStore.mObjects.emplace_back(std::move(entry));
    mStaffStore.save();
    requestRefresh();
}

bool StaffList::removeStaff(const std::string& name)
{
    const std::string cleanedName = normalizeStaffName(name);
    if (cleanedName.empty()) return false;

    const std::string key = StringUtils::toLower(cleanedName);
    const size_t before = mStaffStore.mObjects.size();

    mStaffStore.mObjects.erase(
        std::remove_if(
            mStaffStore.mObjects.begin(),
            mStaffStore.mObjects.end(),
            [&](const StaffEntry& entry) { return StringUtils::toLower(entry.name) == key; }
        ),
        mStaffStore.mObjects.end()
    );

    const bool removed = mStaffStore.mObjects.size() != before;
    if (removed)
    {
        mStaffStore.save();
        requestRefresh();
    }

    return removed;
}

void StaffList::onBaseTickEvent(BaseTickEvent& event)
{
    if (!mEnabled) return;

    if (mFutureValid)
    {
        if (mOnlineFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            auto result = mOnlineFuture.get();
            {
                std::scoped_lock lock(mOnlineMutex);
                mOnlineStaff = std::move(result);
            }
            std::unordered_map<std::string, OnlineEntry> currentByLower;
            {
                std::scoped_lock lock(mOnlineMutex);
                currentByLower.reserve(mOnlineStaff.size());
                for (const auto& entry : mOnlineStaff)
                {
                    currentByLower[StringUtils::toLower(entry.name)] = entry;
                }
            }
            if (mHasInitialOnlineSnapshot)
            {
                for (const auto& [key, entry] : currentByLower)
                {
                    if (!mLastOnlineByLower.contains(key))
                    {
                        const std::string displayName = entry.displayName.empty() ? entry.name : entry.displayName;
                        NotifyUtils::notify("Staff " + displayName + " joined", 5.0f, Notification::Type::Info);
                    }
                }
                for (const auto& [key, entry] : mLastOnlineByLower)
                {
                    if (!currentByLower.contains(key))
                    {
                        const std::string displayName = entry.displayName.empty() ? entry.name : entry.displayName;
                        NotifyUtils::notify("Staff " + displayName + " left", 5.0f, Notification::Type::Warning);
                    }
                }
            }
            mLastOnlineByLower = std::move(currentByLower);
            mHasInitialOnlineSnapshot = true;
            mFutureValid = false;
        }
    }

    const uint64_t now = NOW;
    if (!mNeedsRefresh && now - mLastRefreshMs < 500)
    {
        return;
    }
    if (mFutureValid)
    {
        return;
    }

    mLastRefreshMs = now;
    mNeedsRefresh = false;

    auto ci = ClientInstance::get();
    auto player = ci ? ci->getLocalPlayer() : nullptr;
    auto level = player ? player->getLevel() : nullptr;

    std::vector<std::string> onlineNames;
    if (level)
    {
        auto* playerList = level->getPlayerList();
        if (playerList)
        {
            onlineNames.reserve(playerList->size());
            for (auto& entry : *playerList | std::views::values)
            {
                if (!entry.mName.empty() && entry.mName.length() <= 17)
                {
                    onlineNames.emplace_back(entry.mName);
                }
            }
        }
    }

    auto staffSnapshot = mStaffStore.mObjects;

    mOnlineFuture = std::async(std::launch::async, [onlineNames = std::move(onlineNames), staffSnapshot = std::move(staffSnapshot)]() mutable {
        std::unordered_map<std::string, OnlineEntry> byLower;
        byLower.reserve(staffSnapshot.size());

        for (auto& entry : staffSnapshot)
        {
            if (entry.name.empty() || entry.rank.empty()) continue;
            const std::string key = StringUtils::toLower(entry.name);
            const std::string displayName = entry.displayName.empty() ? entry.name : entry.displayName;
            byLower[key] = OnlineEntry{entry.name, entry.rank, displayName};
        }

        std::vector<OnlineEntry> onlineStaff;
        onlineStaff.reserve(std::min<size_t>(onlineNames.size(), byLower.size()));

        for (auto& playerName : onlineNames)
        {
            const std::string key = StringUtils::toLower(playerName);
            auto it = byLower.find(key);
            if (it != byLower.end())
            {
                onlineStaff.emplace_back(it->second);
            }
        }

        std::ranges::sort(onlineStaff, [](const OnlineEntry& a, const OnlineEntry& b) {
            if (a.rank != b.rank) return a.rank < b.rank;
            return a.name < b.name;
        });

        onlineStaff.erase(
            std::unique(onlineStaff.begin(), onlineStaff.end(), [](const OnlineEntry& a, const OnlineEntry& b) {
                return StringUtils::equalsIgnoreCase(a.name, b.name);
            }),
            onlineStaff.end()
        );

        return onlineStaff;
    });

    mFutureValid = true;
}

static bool isRightAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopRight || anchor == HudElement::Anchor::MiddleRight || anchor == HudElement::Anchor::BottomRight;
}

static bool isMiddleXAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopMiddle || anchor == HudElement::Anchor::Middle || anchor == HudElement::Anchor::BottomMiddle;
}

static bool isBottomAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::BottomLeft || anchor == HudElement::Anchor::BottomMiddle || anchor == HudElement::Anchor::BottomRight;
}

void StaffList::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!mElement) return;
    if (ci->getScreenName() != "hud_screen" && !mElement->mSampleMode) return;

    const float delta = ImGui::GetIO().DeltaTime;

    std::vector<OnlineEntry> renderList;
    {
        std::scoped_lock lock(mOnlineMutex);
        renderList = mOnlineStaff;
    }


    //placedholdernaxyu
    if (renderList.empty() && mElement->mSampleMode)
    {
        renderList = {
            {"belyash", "otec swompa", "belyash"}
        };
    }

    static float showAnim = 0.f;
    static float showVel = 0.f;
    const bool wantShow = true;
    slSpringTo(showAnim, showVel, wantShow ? 1.f : 0.f, delta, 130.f, 30.f);
    showAnim = MathUtils::clamp(showAnim, 0.f, 1.f);
    const float alphaAnim = slEaseOutCubic(showAnim);
    const float popAnimRaw = slEaseOutBack(showAnim);
    const float popAnim = std::clamp(popAnimRaw, 0.f, 1.2f);

    FontHelper::pushPrefFont(false, true);

    auto* textFont = ImGui::GetFont();
    ImFont* iconFont = textFont;
    if (auto it = FontHelper::Fonts.find("energy"); it != FontHelper::Fonts.end() && it->second)
    {
        iconFont = it->second;
    }

    const float fontSize = 18.f;
    const float paddingX = 10.f;
    const float paddingY = 8.f;
    const float rounding = 8.f;
    const float lineGap = 4.f;
    const float headerGap = 12.f;
    const float dotRadius = 2.9f;
    const float dotGapX = 7.f;

    const std::string title = "StaffList";
    const std::string icon = "d";

    ImColor bg = ColorUtils::getUiCardColor(1.0f);
    ImColor border = ColorUtils::getUiBorderColor(16.0f / 255.0f);
    ImColor textColor = ColorUtils::getUiTextColor(1.0f);
    ImColor subTextColor = ColorUtils::getUiTextDimColor(1.0f);
    ImColor accent = ColorUtils::getGuiAccentColor(0);
    const bool showRank = mShowRank.mValue;

    static ID3D11ShaderResourceView* goodTexture = nullptr;
    static int goodW = 0;
    static int goodH = 0;
    static ID3D11ShaderResourceView* sadTexture = nullptr;
    static int sadW = 0;
    static int sadH = 0;
    static bool statusIconsLoaded = false;
    if (!statusIconsLoaded)
    {
        D3DHook::loadTextureFromEmbeddedResource("good.png", &goodTexture, &goodW, &goodH);
        D3DHook::loadTextureFromEmbeddedResource("sad.png", &sadTexture, &sadW, &sadH);
        statusIconsLoaded = true;
    }

    std::unordered_set<std::string> nearbyLower;
    if (ci && ci->getLocalPlayer() && ci->getLocalPlayer()->getLevel())
    {
        auto* localPlayer = ci->getLocalPlayer();
        auto actors = ActorUtils::getActorList(true, false);
        nearbyLower.reserve(std::min<size_t>(actors.size(), 256));
        for (auto* actor : actors)
        {
            if (!actor) continue;
            if (actor == localPlayer) continue;
            if (!actor->isValid()) continue;
            if (!actor->isPlayer()) continue;

            const std::string n = normalizeStaffName(actor->getRawName());
            if (n.empty()) continue;
            nearbyLower.emplace(StringUtils::toLower(n));
        }
    }

    ImVec2 iconSz = slCalcSize(iconFont, fontSize, icon);
    ImVec2 titleSz = slCalcSize(textFont, fontSize, title);
    float headerHeight = std::max(iconSz.y, titleSz.y);
    float headerWidth = titleSz.x + 6.f + iconSz.x;

    float lineHeight = slCalcSize(textFont, fontSize, "A").y;

    struct ItemAnim
    {
        float value = 0.f;
        float velocity = 0.f;
    };

    static std::unordered_map<std::string, ItemAnim> itemAnims;
    std::unordered_map<std::string, OnlineEntry> byLower;
    byLower.reserve(renderList.size());
    for (const auto& entry : renderList)
    {
        byLower[StringUtils::toLower(entry.name)] = entry;
    }

    for (auto& [key, entry] : byLower)
    {
        if (!itemAnims.contains(key)) itemAnims.emplace(key, ItemAnim{});
    }

    for (auto& [key, anim] : itemAnims)
    {
        const bool present = byLower.contains(key);
        const float target = present ? 1.f : 0.f;
        slSpringTo(anim.value, anim.velocity, target, delta, 170.f, 38.f);
        anim.value = std::clamp(anim.value, 0.f, 1.f);
        if (present && anim.value > 0.995f) anim.value = 1.f;
        if (!present && anim.value < 0.005f) anim.value = 0.f;
    }

    std::vector<std::pair<std::string, OnlineEntry>> drawItems;
    drawItems.reserve(byLower.size());
    for (auto& [key, entry] : byLower)
    {
        if (itemAnims[key].value > 0.001f) drawItems.emplace_back(key, entry);
    }

    std::ranges::sort(drawItems, [](const auto& a, const auto& b) {
        if (a.second.rank != b.second.rank) return a.second.rank < b.second.rank;
        return a.second.name < b.second.name;
    });

    for (auto it = itemAnims.begin(); it != itemAnims.end();)
    {
        if (it->second.value <= 0.001f && !byLower.contains(it->first)) it = itemAnims.erase(it);
        else ++it;
    }

    float maxLineWidth = headerWidth;
    for (const auto& [key, entry] : drawItems)
    {
        const std::string& displayName = entry.displayName.empty() ? entry.name : entry.displayName;
        const std::string namePlain = stripMcColorCodes(displayName);

        const float iconSize = lineHeight * 0.82f;
        const float iconGap = 6.f;

        float w = 0.f;
        w += dotRadius * 2.f;
        w += dotGapX;
        w += iconSize + iconGap;
        if (showRank)
        {
            w += calcMcTextWidth(textFont, fontSize, entry.rank);
            w += slCalcSize(textFont, fontSize, " ").x;
        }
        w += slCalcSize(textFont, fontSize, namePlain).x;

        maxLineWidth = std::max(maxLineWidth, w);
    }

    float contentHeight = 0.f;
    for (size_t i = 0; i < drawItems.size(); i++)
    {
        const float rawA = itemAnims[drawItems[i].first].value;
        const float listStagger = 0.028f;
        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = slEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));
        const float aLayout = slEaseInOut(rawA) * listGate;

        contentHeight += lineHeight * aLayout;
        if (i + 1 < drawItems.size()) contentHeight += lineGap * aLayout;
    }

    const float targetWidth = maxLineWidth + paddingX * 2.f + 18.f;
    const float targetHeight = paddingY * 2.f + headerHeight + (drawItems.empty() ? 0.f : headerGap + contentHeight);

    static float panelWidth = 0.f;
    static float panelHeight = 0.f;
    panelWidth = slExpApproach(panelWidth, targetWidth, delta, 30.f);
    panelHeight = slExpApproach(panelHeight, targetHeight, delta, 30.f);

    const float panelScale = 0.92f + 0.08f * popAnim;

    if (!wantShow && showAnim < 0.01f && drawItems.empty())
    {
        mElement->mSize = {0.f, 0.f};
        FontHelper::popPrefFont();
        return;
    }

    mElement->mSize = {panelWidth, panelHeight};

    ImVec2 pos = mElement->getPos();
    if (isRightAnchored(mElement->mAnchor)) pos.x -= panelWidth;
    else if (isMiddleXAnchored(mElement->mAnchor)) pos.x -= panelWidth * 0.5f;

    if (isBottomAnchored(mElement->mAnchor)) pos.y -= panelHeight;

    if (mElement->mCentered)
    {
        pos.x -= panelWidth * 0.5f;
        pos.y -= panelHeight * 0.5f;
    }

    ImVec2 min = pos;
    ImVec2 max = {pos.x + panelWidth, pos.y + panelHeight};

    ImVec2 pivot = min;
    if (isRightAnchored(mElement->mAnchor)) pivot.x = max.x;
    else if (isMiddleXAnchored(mElement->mAnchor)) pivot.x = (min.x + max.x) * 0.5f;
    if (isBottomAnchored(mElement->mAnchor)) pivot.y = max.y;
    else pivot.y = min.y;

    ImVec2 scaledMin = {pivot.x + (min.x - pivot.x) * panelScale, pivot.y + (min.y - pivot.y) * panelScale};
    ImVec2 scaledMax = {pivot.x + (max.x - pivot.x) * panelScale, pivot.y + (max.y - pivot.y) * panelScale};
    const float scaledW = scaledMax.x - scaledMin.x;
    const float scaledH = scaledMax.y - scaledMin.y;

    const bool inEditor = HudEditor::gInstance && HudEditor::gInstance->mEnabled;
    const bool hovered = inEditor && ImRenderUtils::isMouseOver(ImVec4(scaledMin.x, scaledMin.y, scaledMax.x, scaledMax.y));
    static float hoverAnim = 0.f;
    hoverAnim = MathUtils::lerp(hoverAnim, hovered ? 1.f : 0.f, delta * 12.f);
    hoverAnim = MathUtils::clamp(hoverAnim, 0.f, 1.f);

    ImColor bgHover = (ColorUtils::getUiTheme() == ColorUtils::UiTheme::Light) ? ImColor(240, 240, 246, 255) : ImColor(26, 25, 32, 255);
    ImColor borderHover = (ColorUtils::getUiTheme() == ColorUtils::UiTheme::Light) ? ImColor(130, 130, 150, 64) : ImColor(255, 255, 255, 42);
    bg.Value = ImLerp(bg.Value, bgHover.Value, hoverAnim);
    border.Value = ImLerp(border.Value, borderHover.Value, hoverAnim);

    bg.Value.w *= alphaAnim;
    border.Value.w *= alphaAnim;
    textColor.Value.w *= alphaAnim;
    subTextColor.Value.w *= alphaAnim;
    accent.Value.w *= alphaAnim;

    auto* drawList = ImGui::GetBackgroundDrawList();
    auto* interfaceModule = gFeatureManager && gFeatureManager->mModuleManager ? gFeatureManager->mModuleManager->getModule<Interface>() : nullptr;
    const bool useHudBlur = interfaceModule && interfaceModule->mHudBlur.mValue;
    const float blurStrength = useHudBlur ? interfaceModule->mHudBlurStrength.mValue : 0.f;
    if (useHudBlur)
    {
        ImRenderUtils::addBlurAlpha(ImVec4(scaledMin.x, scaledMin.y, scaledMax.x, scaledMax.y), blurStrength, bg.Value.w, rounding, drawList, true);
        bg.Value.w *= 0.85f;
    }
    {
        ImColor shadow = IM_COL32(0, 0, 0, 255);
        shadow.Value.w = 0.60f * alphaAnim;
        drawList->AddShadowRect(scaledMin, scaledMax, shadow, 24.0f, ImVec2(0.f, 3.f), 0, rounding);
    }
    drawList->AddRectFilled(scaledMin, scaledMax, bg, rounding);
    drawList->AddRect(scaledMin, scaledMax, border, rounding, 0, 1.4f);
    if (hoverAnim > 0.001f)
    {
        ImColor outline = borderHover;
        outline.Value.w *= alphaAnim * hoverAnim;
        drawList->AddRect(scaledMin, scaledMax, outline, rounding, 0, 2.0f);
    }
    {
        ImColor glow = accent;
        glow.Value.w = 0.40f * alphaAnim;

        const float glowWidth = scaledW * 0.5f;
        const float glowHeight = scaledH * 0.5f;
        const float glowX = scaledMin.x + (scaledW - glowWidth) * 0.5f;
        const float glowLift = glowHeight * 1.f;
        const float glowY = scaledMin.y - glowLift;

        drawList->PushClipRect(scaledMin, scaledMax, true);
        drawList->AddShadowRect(ImVec2(glowX, glowY), ImVec2(glowX + glowWidth, glowY + glowHeight), glow, 45.0f, ImVec2(0.f, 0.f), 0, rounding);
        drawList->PopClipRect();
    }
    {
        ImColor tabColor = accent;
        tabColor.Value.w = 1.f * alphaAnim;

        const float tabHeight = 3.f;
        const float tabWidth = std::max(56.f, scaledW * 0.45f);
        const ImVec2 tabMin = ImVec2(
            scaledMin.x + (scaledW - tabWidth) * 0.5f,
            scaledMax.y - tabHeight
        );
        const ImVec2 tabMax = ImVec2(tabMin.x + tabWidth, tabMin.y + tabHeight);

        drawList->PushClipRect(scaledMin, scaledMax, true);
        drawList->AddRectFilled(tabMin, tabMax, tabColor, rounding, ImDrawFlags_RoundCornersTop);
        drawList->PopClipRect();
    }

    const float headerNudgeY = 1.5f;
    const float showOvershoot = std::max(0.f, popAnim - 1.f);
    const float headerSlideY = (1.f - std::min(popAnim, 1.f)) * -10.f + showOvershoot * 6.f;
    ImVec2 headerPos = {scaledMin.x + paddingX, scaledMin.y + paddingY + headerNudgeY + headerSlideY};
    slAddTextShadowed(drawList, textFont, fontSize, headerPos, textColor, title.c_str());
    slAddTextShadowedNoShimmer(drawList, iconFont, fontSize, {scaledMax.x - paddingX - iconSz.x, headerPos.y}, accent, icon.c_str());

    float y = headerPos.y + headerHeight + headerGap;
    for (size_t i = 0; i < drawItems.size(); i++)
    {
        const auto& entry = drawItems[i].second;
        const float rawA = itemAnims[drawItems[i].first].value;
        if (rawA <= 0.001f) continue;
        const float listStagger = 0.028f;
        const float gateStart = static_cast<float>(i) * listStagger;
        const float listGate = slEaseOutCubic(std::clamp((showAnim - gateStart) / 0.18f, 0.f, 1.f));

        const float aAlpha = std::clamp(slEaseOutCubic(rawA) * listGate, 0.f, 1.f);
        const float aMoveRaw = slEaseOutBack(rawA) * listGate;
        const float aMove = std::clamp(aMoveRaw, 0.f, 1.2f);
        const float overshoot = std::max(0.f, aMove - 1.f);
        const float aLayout = slEaseInOut(rawA) * listGate;

        ImColor lineColor = subTextColor;
        lineColor.Value.w *= aAlpha;

        const std::string& displayName = entry.displayName.empty() ? entry.name : entry.displayName;
        const std::string namePlain = stripMcColorCodes(displayName);

        const float slideX = (1.f - std::min(aMove, 1.f)) * -10.f + overshoot * 7.f;
        const float slideY = (1.f - std::min(aMove, 1.f)) * -7.f + overshoot * 4.f;
        ImVec2 p = {scaledMin.x + paddingX + slideX, y + slideY};
        ImColor dotColor = accent;
        dotColor.Value.w = lineColor.Value.w;
        const float dotR = dotRadius * (0.85f + 0.15f * std::min(aMove, 1.f));
        drawList->AddCircleFilled(ImVec2(p.x + dotR, p.y + lineHeight * 0.47f), dotR, dotColor, 16);
        p.x += dotR * 2.f + dotGapX;

        const bool isNearby = !nearbyLower.empty() && nearbyLower.contains(StringUtils::toLower(entry.name));
        ID3D11ShaderResourceView* statusTexture = isNearby ? sadTexture : goodTexture;
        const float iconSize = lineHeight * 0.82f;
        const float iconGap = 6.f;
        if (statusTexture)
        {
            const ImVec2 iconMin = ImVec2(p.x, p.y + (lineHeight - iconSize) * 0.5f);
            const ImVec2 iconMax = ImVec2(iconMin.x + iconSize, iconMin.y + iconSize);
            const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(255.f * aAlpha));
            drawList->AddImage(statusTexture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tint);
        }
        p.x += iconSize + iconGap;

        if (showRank)
        {
            drawMcColoredTextShadowed(drawList, textFont, fontSize, p, lineColor, entry.rank);
            p.x += calcMcTextWidth(textFont, fontSize, entry.rank);

            slAddTextShadowed(drawList, textFont, fontSize, p, lineColor, " ");
            p.x += slCalcSize(textFont, fontSize, " ").x;
        }

        slAddTextShadowed(drawList, textFont, fontSize, p, lineColor, namePlain.c_str());
        y += lineHeight * aLayout;
        if (i + 1 < drawItems.size()) y += lineGap * aLayout;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    static bool menuTarget = false;
    static float menuAnim = 0.f;

    if (!inEditor) menuTarget = false;

    if (inEditor && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        menuTarget = !menuTarget;
    }

    const float menuSpeed = menuTarget ? 14.f : 16.f;
    menuAnim = MathUtils::lerp(menuAnim, menuTarget ? 1.f : 0.f, delta * menuSpeed);
    menuAnim = MathUtils::clamp(menuAnim, 0.f, 1.f);

    struct Item { const char* label; BoolSetting* s; };
    Item items[] = {
        {"Ранг", &mShowRank}
    };

    static float toggleAnim[1]{};
    for (int i = 0; i < 1; i++)
    {
        const float target = items[i].s->mValue ? 1.f : 0.f;
        const float speed = target > 0.5f ? 9.f : 14.f;
        toggleAnim[i] = MathUtils::lerp(toggleAnim[i], target, delta * speed);
        toggleAnim[i] = MathUtils::clamp(toggleAnim[i], 0.f, 1.f);
    }

    const std::string dot = "·";
    const std::string iconRank = "d";

    ImFont* energyFont = nullptr;
    {
        auto it = FontHelper::Fonts.find("essence.ttf");
        if (it != FontHelper::Fonts.end() && it->second) energyFont = it->second;
    }
    if (!energyFont) energyFont = textFont;
    
    const float pad = 12.f;
    const float rowH = fontSize + 12.f;
    const float indicatorR = 3.2f;
    const float indicatorW = indicatorR * 2.f;
    const float iconGap = 6.f;
    float maxIconW = 0.f;
    float maxLabelW = 0.f;
    for (const auto& it : items)
    {
        maxLabelW = std::max(maxLabelW, slCalcSize(textFont, fontSize, std::string(it.label)).x);
    }
    const std::string* menuIcons[1] = {&iconRank};
    for (int i = 0; i < 1; i++)
    {
        maxIconW = std::max(maxIconW, slCalcSize(energyFont ? energyFont : textFont, fontSize, *menuIcons[i]).x);
    }
    const float indicatorShift = indicatorW + 7.f + maxIconW + iconGap;
    const float menuTextPad = std::max(4.f, fontSize * 0.35f);
    const float menuW = pad * 2.f + maxLabelW + indicatorShift + menuTextPad;
    const float menuH = pad * 2.f + rowH * 1.f;

    const float anchorY = scaledMax.y + 10.f;
    const float centerX = (scaledMin.x + scaledW * 0.5f);
    ImVec2 menuMinTarget = ImVec2(centerX - menuW * 0.5f, anchorY);
    if (menuMinTarget.y + menuH > display.y - 6.f) menuMinTarget.y = scaledMin.y - menuH - 10.f;
    menuMinTarget.x = std::clamp(menuMinTarget.x, 6.f, display.x - menuW - 6.f);
    menuMinTarget.y = std::clamp(menuMinTarget.y, 6.f, display.y - menuH - 6.f);

    static ImVec2 menuMinSmoothed = menuMinTarget;
    static bool menuPrevTarget = false;
    if (!menuPrevTarget && menuTarget) menuMinSmoothed = menuMinTarget;
    menuPrevTarget = menuTarget;

    menuMinSmoothed.x = MathUtils::lerp(menuMinSmoothed.x, menuMinTarget.x, delta * 14.f);
    menuMinSmoothed.y = MathUtils::lerp(menuMinSmoothed.y, menuMinTarget.y, delta * 14.f);
    ImVec2 menuMin = menuMinSmoothed;
    ImVec2 menuMax = ImVec2(menuMin.x + menuW, menuMin.y + menuH);

    const float menuA = slEaseOutCubic(menuAnim);
    const float menuPopRaw = slEaseOutBack(menuAnim);
    const float menuPop = std::clamp(menuPopRaw, 0.f, 1.15f);
    const float menuScale = 0.92f + 0.08f * menuPop;
    const ImVec2 menuCenter = ImVec2((menuMin.x + menuMax.x) * 0.5f, (menuMin.y + menuMax.y) * 0.5f);
    const ImVec2 drawMin = ImVec2(menuCenter.x + (menuMin.x - menuCenter.x) * menuScale, menuCenter.y + (menuMin.y - menuCenter.y) * menuScale);
    const ImVec2 drawMax = ImVec2(menuCenter.x + (menuMax.x - menuCenter.x) * menuScale, menuCenter.y + (menuMax.y - menuCenter.y) * menuScale);
    const bool menuHovered = ImRenderUtils::isMouseOver(ImVec4(drawMin.x, drawMin.y, drawMax.x, drawMax.y));
    if (inEditor && menuTarget)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !menuHovered && !hovered) menuTarget = false;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !menuHovered && !hovered) menuTarget = false;
    }

    if (menuAnim > 0.01f)
    {
        const float a = menuA;
        const float s = std::clamp(menuScale, 0.85f, 1.2f);

        ImColor menuBg = ColorUtils::getUiCardColor(1.0f);
        ImColor menuBorder = ColorUtils::getUiBorderColor(25.0f / 255.0f);
        ImColor menuText = ColorUtils::getUiTextColor(1.0f);
        ImColor menuDot = ColorUtils::getUiTextDimColor(1.0f);
        ImColor menuAccent = ColorUtils::getGuiAccentColor(0);
        menuBg.Value.w *= a;
        menuBorder.Value.w *= a;
        menuText.Value.w *= a;
        menuDot.Value.w *= a;
        menuAccent.Value.w *= a;

        drawList->AddShadowRect(drawMin, drawMax, IM_COL32(0, 0, 0, static_cast<int>(170.f * a)), 30.0f, ImVec2(0.f, 3.f), 0, 7.f);
        drawList->AddRectFilled(drawMin, drawMax, menuBg, 7.f);
        {
            ImColor glow = menuAccent;
            glow.Value.w = 0.40f * a;
            const float w = drawMax.x - drawMin.x;
            const float h = drawMax.y - drawMin.y;
            const float glowBaseH = std::min(std::max(10.f, rowH * s * 0.45f), h * 0.22f);
            const float glowWidth = w * 0.55f;
            const float glowX = drawMin.x + (w - glowWidth) * 0.5f;
            const float glowLift = glowBaseH * 0.95f;
            const ImVec2 glowMin = ImVec2(glowX, drawMin.y - glowLift);
            const ImVec2 glowMax = ImVec2(glowX + glowWidth, drawMin.y + glowBaseH - glowLift);
            drawList->PushClipRect(drawMin, drawMax, true);
            drawList->AddShadowRect(glowMin, glowMax, glow, 45.0f, ImVec2(0.f, 0.f), 0, 7.f);
            drawList->PopClipRect();
        }
        drawList->AddRect(drawMin, drawMax, menuBorder, 7.f, 0, 1.2f);
        {
            ImColor tab = menuAccent;
            tab.Value.w *= a;
            const float w = drawMax.x - drawMin.x;
            const float tabH = 3.f;
            const float tabW = w / 2.5f;
            const float tabX = drawMin.x + (w - tabW) * 0.5f;
            const ImVec2 tabMin = ImVec2(tabX, drawMax.y - tabH);
            const ImVec2 tabMax = ImVec2(tabX + tabW, drawMax.y);
            drawList->AddRectFilled(tabMin, tabMax, tab, 7.f, ImDrawFlags_RoundCornersTop);
        }

        for (int i = 0; i < 1; i++)
        {
            ImVec2 rowMin = ImVec2(drawMin.x + pad * s, drawMin.y + pad * s + rowH * i * s);
            ImVec2 rowMax = ImVec2(drawMax.x - pad * s, rowMin.y + rowH * s);
            const bool rowHovered = inEditor && menuHovered && ImRenderUtils::isMouseOver(ImVec4(rowMin.x, rowMin.y, rowMax.x, rowMax.y));
            if (inEditor && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && rowHovered)
            {
                items[i].s->mValue = !items[i].s->mValue;
            }

            const float t = slEaseOutCubic(toggleAnim[i]);
            if (t > 0.001f)
            {
                ImColor circleCol = menuAccent;
                circleCol.Value.w *= a * t;
                const float r = indicatorR * (0.75f + 0.25f * t) * s;
                const ImVec2 c = ImVec2(rowMin.x + 3.f + indicatorR * s, rowMin.y + (rowH * s) * 0.5f);
                drawList->AddCircleFilled(c, r, circleCol, 18);
            }

            const float fontScaled = fontSize * s;
            const float baseShift = indicatorW + 7.f;
            const float dotW = slCalcSize(textFont, fontSize, dot).x;
            const float textShift = (baseShift + dotW + iconGap + maxIconW + iconGap) * t * s;
            if (t > 0.001f)
            {
                ImVec2 dotP = ImVec2(rowMin.x + 2.f + baseShift * t * s, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(textFont, fontScaled, dotP, menuDot, dot.c_str());
                ImColor iconCol = menuAccent;
                iconCol.Value.w *= a * (0.25f + 0.75f * t);
                ImVec2 iconP = ImVec2(rowMin.x + 2.f + baseShift * t * s + dotW + iconGap, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(energyFont ? energyFont : textFont, fontScaled, iconP, iconCol, menuIcons[i]->c_str());
            }
            ImVec2 textP = ImVec2(rowMin.x + 2.f + textShift, rowMin.y + (rowH * s - fontScaled) * 0.5f);
            drawList->AddText(textFont, fontScaled, textP, menuText, items[i].label);
        }
    }

    FontHelper::popPrefFont();
}
