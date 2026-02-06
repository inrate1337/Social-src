#include "armorhud.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <vector>

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/FeatureManager.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Inventory/CompoundTag.hpp>
#include <SDK/Minecraft/Inventory/ItemStack.hpp>
#include <SDK/Minecraft/Inventory/SimpleContainer.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Utils/Resources.hpp>
#include <Features/Modules/Visual/Interface.hpp>

static float ahExpApproach(float current, float target, float delta, float speed)
{
    if (delta <= 0.f) return current;
    const float t = 1.f - std::exp(-speed * delta);
    return current + (target - current) * t;
}

static float ahEaseInOut(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static float ahEaseOutCubic(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float inv = 1.f - t;
    return 1.f - inv * inv * inv;
}

static float ahEaseOutBack(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float inv = t - 1.f;
    return 1.f + c3 * inv * inv * inv + c1 * inv * inv;
}

static void ahSpringTo(float& value, float& velocity, float target, float delta, float stiffness, float damping)
{
    if (delta <= 0.f) return;
    const float accel = (target - value) * stiffness;
    velocity += accel * delta;
    velocity *= std::exp(-damping * delta);
    value += velocity * delta;
}

static ImVec2 ahCalcSize(ImFont* font, float fontSize, const std::string& text)
{
    if (!font || text.empty()) return ImVec2(0.f, 0.f);
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text.c_str());
}

static bool ahIsRightAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopRight || anchor == HudElement::Anchor::MiddleRight || anchor == HudElement::Anchor::BottomRight;
}

static bool ahIsMiddleXAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopMiddle || anchor == HudElement::Anchor::Middle || anchor == HudElement::Anchor::BottomMiddle;
}

static bool ahIsBottomAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::BottomLeft || anchor == HudElement::Anchor::BottomMiddle || anchor == HudElement::Anchor::BottomRight;
}

static ImColor ahArmorTierColor(int tier)
{
    switch (tier)
    {
    case 0: return ImColor(160, 101, 64, 255);
    case 1: return ImColor(200, 200, 200, 255);
    case 2: return ImColor(128, 128, 128, 255);
    case 3: return ImColor(216, 219, 226, 255);
    case 4: return ImColor(255, 215, 0, 255);
    case 5: return ImColor(0, 255, 255, 255);
    case 6: return ImColor(80, 80, 80, 255);
    default: return ImColor(255, 255, 255, 255);
    }
}

static ImColor ahDurabilityColor(float percentage)
{
    percentage = std::clamp(percentage, 0.f, 1.f);
    ImVec4 a;
    ImVec4 b;
    float t = 0.f;
    if (percentage <= 0.5f)
    {
        a = ImColor(255, 60, 60, 255).Value;
        b = ImColor(255, 220, 0, 255).Value;
        t = percentage / 0.5f;
    }
    else
    {
        a = ImColor(255, 220, 0, 255).Value;
        b = ImColor(0, 255, 90, 255).Value;
        t = (percentage - 0.5f) / 0.5f;
    }
    ImVec4 v = MathUtils::lerp(a, b, t);
    return ImColor(v);
}

static std::string ahArmorMaterialFromName(const std::string& itemName)
{
    if (itemName.find("elytra") != std::string::npos) return "elytra";
    if (itemName.find("netherite") != std::string::npos) return "netherite";
    if (itemName.find("diamond") != std::string::npos) return "diamond";
    if (itemName.find("iron") != std::string::npos) return "iron";
    if (itemName.find("golden") != std::string::npos || itemName.find("gold") != std::string::npos) return "golden";
    if (itemName.find("chainmail") != std::string::npos) return "chainmail";
    if (itemName.find("leather") != std::string::npos) return "leather";
    return {};
}

static void ahComputeDurability(const std::string& itemName, SItemType type, int damage, float& outCur, float& outMax, float& outPct)
{
    outCur = 0.f;
    outMax = 0.f;
    outPct = 0.f;

    const std::string mat = ahArmorMaterialFromName(itemName);
    if (mat.empty()) return;

    if (mat == "elytra")
    {
        outMax = 432.f;
        outCur = std::max(0.f, outMax - static_cast<float>(damage));
        outPct = outMax > 0.f ? (outCur / outMax) : 0.f;
        return;
    }

    int materialFactor = 0;
    if (mat == "netherite") materialFactor = 37;
    else if (mat == "diamond") materialFactor = 33;
    else if (mat == "iron") materialFactor = 15;
    else if (mat == "golden") materialFactor = 7;
    else if (mat == "chainmail") materialFactor = 15;
    else if (mat == "leather") materialFactor = 5;

    int base = 0;
    switch (type)
    {
    case SItemType::Helmet: base = 11; break;
    case SItemType::Chestplate: base = 16; break;
    case SItemType::Leggings: base = 15; break;
    case SItemType::Boots: base = 13; break;
    default: base = 0; break;
    }

    if (materialFactor <= 0 || base <= 0) return;

    outMax = static_cast<float>(materialFactor * base);
    outCur = outMax - static_cast<float>(std::max(0, damage));
    if (outCur < 0.f) outCur = 0.f;
    if (outCur > outMax) outCur = outMax;
    outPct = outMax > 0.f ? (outCur / outMax) : 0.f;
}

static std::string ahFormatDurability(float cur, float max)
{
    const int iCur = std::max(0, static_cast<int>(std::round(cur)));
    const int iMax = std::max(0, static_cast<int>(std::round(max)));
    if (iMax <= 0) return {};
    return std::to_string(iCur) + "/" + std::to_string(iMax);
}

ArmorHUD::ArmorHUD() : ModuleBase("ArmorHUD", "Shows local player's armor and durability", ModuleCategory::Visual, 0, false)
{
    addSettings(
        &mLayout,
        &mShowBars,
        &mShowNumbers,
        &mIconSize
    );

    mNames = {
        {Lowercase, "armorhud"},
        {LowercaseSpaced, "armor hud"},
        {Normal, "ArmorHUD"},
        {NormalSpaced, "Armor HUD"},
    };

    mElement = std::make_unique<HudElement>();
    mElement->mAnchor = HudElement::Anchor::BottomMiddle;
    mElement->mPos = { 0.f, -92.f };

    const char* moduleBaseType = ModuleBase<ArmorHUD>::getTypeID();
    mElement->mParentTypeIdentifier = const_cast<char*>(moduleBaseType);

    if (HudEditor::gInstance)
    {
        HudEditor::gInstance->registerElement(mElement.get());
    }
}

ArmorHUD::~ArmorHUD()
{
    stopWorker();
    releaseArmorTextures();
}

void ArmorHUD::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &ArmorHUD::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &ArmorHUD::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = true;
    startWorker();
}

void ArmorHUD::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &ArmorHUD::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &ArmorHUD::onBaseTickEvent>(this);
    if (mElement) mElement->mVisible = false;
    stopWorker();
    releaseArmorTextures();
}

void ArmorHUD::startWorker()
{
    if (mWorkerRunning.exchange(true)) return;

    mWorker = std::thread([this]() {
        while (mWorkerRunning.load())
        {
            std::array<SlotSnapshot, 4> snap{};
            {
                std::scoped_lock lock(mDataMutex);
                snap = mSnapshot;
            }

            std::array<SlotRender, 4> out{};
            for (size_t i = 0; i < out.size(); i++)
            {
                out[i].has = snap[i].has;
                out[i].type = snap[i].type;
                out[i].tier = snap[i].tier;
                out[i].material = ahArmorMaterialFromName(snap[i].itemName);

                if (snap[i].has)
                {
                    ahComputeDurability(snap[i].itemName, snap[i].type, snap[i].damage, out[i].currentDurability, out[i].maxDurability, out[i].percentage);
                    out[i].durabilityText = ahFormatDurability(out[i].currentDurability, out[i].maxDurability);
                }
            }

            {
                std::scoped_lock lock(mDataMutex);
                mCache = std::move(out);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
        });
}

void ArmorHUD::stopWorker()
{
    if (!mWorkerRunning.exchange(false)) return;
    if (mWorker.joinable()) mWorker.join();
}

void ArmorHUD::onBaseTickEvent(BaseTickEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;

    auto player = ci->getLocalPlayer();
    if (!player)
    {
        std::scoped_lock lock(mDataMutex);
        for (auto& s : mSnapshot) s = SlotSnapshot{};
        return;
    }

    auto* armorContainer = player->getArmorContainer();
    if (!armorContainer) return;

    std::array<SlotSnapshot, 4> snap{};
    for (int i = 0; i < 4; i++)
    {
        ItemStack* item = armorContainer->getItem(i);
        if (!item || !item->mItem) continue;

        snap[i].has = true;
        snap[i].type = item->getItem()->getItemType();
        snap[i].tier = item->getItem()->getItemTier();
        snap[i].itemName = item->getItem()->mName;

        int damage = std::max(0, static_cast<int>(item->mAuxValue));
        if (item->mCompoundTag)
        {
            if (auto* v = item->mCompoundTag->get("Damage"))
            {
                if (v->getTagType() == Tag::Type::Int) damage = std::max(0, v->asIntTag()->val);
                else if (v->getTagType() == Tag::Type::Short) damage = std::max(0, static_cast<int>(v->asShortTag()->val));
            }
        }
        snap[i].damage = damage;
    }

    {
        std::scoped_lock lock(mDataMutex);
        mSnapshot = std::move(snap);
    }
}

void ArmorHUD::releaseArmorTextures()
{
    auto releaseMap = [](auto& map) {
        map.clear();
        };

    releaseMap(mHelmetTextures);
    releaseMap(mChestTextures);
    releaseMap(mLeggingsTextures);
    releaseMap(mBootsTextures);
    mTexturesLoaded = false;
}

void ArmorHUD::loadArmorTextures()
{
    releaseArmorTextures();

    const auto tryLoad = [](const std::string& key, ID3D11ShaderResourceView** out) -> bool {
        if (!out) return false;
        auto it = ResourceLoader::Resources.find(key);
        if (it == ResourceLoader::Resources.end()) return false;
        int w = 0, h = 0;
        return D3DHook::loadTextureFromEmbeddedResource(key.c_str(), out, &w, &h);
        };

    const auto tryLoadWithFallback = [&](const std::string& baseKey, ID3D11ShaderResourceView** out) -> bool {
        if (tryLoad(baseKey, out)) return true;
        if (tryLoad(baseKey + ".png", out)) return true;
        return false;
        };

    const std::vector<std::string> mats = { "netherite", "diamond", "iron", "golden", "chainmail", "leather" };
    for (const auto& mat : mats)
    {
        ID3D11ShaderResourceView* tex = nullptr;
        if (tryLoadWithFallback(mat + "_helmet", &tex)) mHelmetTextures[mat] = tex;
        tex = nullptr;
        if (tryLoadWithFallback(mat + "_chestplate", &tex)) mChestTextures[mat] = tex;
        tex = nullptr;
        if (tryLoadWithFallback(mat + "_leggings", &tex)) mLeggingsTextures[mat] = tex;
        tex = nullptr;
        if (tryLoadWithFallback(mat + "_boots", &tex)) mBootsTextures[mat] = tex;
    }

    {
        ID3D11ShaderResourceView* tex = nullptr;
        if (tryLoadWithFallback("elytra", &tex))
        {
            mChestTextures["elytra"] = tex;
        }
    }

    mTexturesLoaded = true;
}

void ArmorHUD::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;
    if (!mElement) return;

    if (ci->getScreenName() != "hud_screen" && !mElement->mSampleMode) return;

    const float delta = ImGui::GetIO().DeltaTime;

    static float showAnim = 0.f;
    static float showVel = 0.f;
    const bool wantShow = mEnabled || mElement->mSampleMode;
    ahSpringTo(showAnim, showVel, wantShow ? 1.f : 0.f, delta, 130.f, 30.f);
    showAnim = MathUtils::clamp(showAnim, 0.f, 1.f);
    if (showAnim < 0.01f) return;

    const float alphaAnim = ahEaseOutCubic(showAnim);
    const float popAnimRaw = ahEaseOutBack(showAnim);
    const float popAnim = std::clamp(popAnimRaw, 0.f, 1.2f);
    const float panelScale = 0.92f + 0.08f * popAnim;

    FontHelper::pushPrefFont(false, true);

    auto* textFont = ImGui::GetFont();

    const float fontSize = 18.f;
    const float paddingX = 10.f;
    const float paddingY = 8.f;
    const float rounding = 8.f;
    const float contentPadTop = 2.f;

    const float iconSize = std::max(12.f, static_cast<float>(mIconSize.mValue));
    const float barH = std::clamp(iconSize * 0.12f, 2.0f, 4.0f);
    const float barGapY = 2.f;
    const float barInsetX = 1.f;

    ImColor bg = ColorUtils::getUiCardColor(1.0f);
    ImColor border = ColorUtils::getUiBorderColor(16.0f / 255.0f);
    ImColor subTextColor = ColorUtils::getUiTextDimColor(230.0f / 255.0f);
    ImColor accent = ColorUtils::getGuiAccentColor(0);

    const float rowGapY = 6.f;
    const float colGapX = 10.f;
    const float durFont = fontSize - 4.f;
    const float durTextH = ahCalcSize(textFont, durFont, "100%").y;

    static float numbersAnim = 0.f;
    static float numbersVelAnim = 0.f;
    static float barsAnim = 0.f;
    static float barsVelAnim = 0.f;
    ahSpringTo(numbersAnim, numbersVelAnim, mShowNumbers.mValue ? 1.f : 0.f, delta, 150.f, 28.f);
    ahSpringTo(barsAnim, barsVelAnim, mShowBars.mValue ? 1.f : 0.f, delta, 150.f, 28.f);
    numbersAnim = MathUtils::clamp(numbersAnim, 0.f, 1.f);
    barsAnim = MathUtils::clamp(barsAnim, 0.f, 1.f);

    const float numbersA = ahEaseOutCubic(numbersAnim);
    const float barsA = ahEaseOutCubic(barsAnim);

    const bool vertical = mLayout.mValue == Layout::Vertical;

    float targetPanelW = 0.f;
    float targetPanelH = 0.f;

    float numbersW = ahCalcSize(textFont, durFont, "100%").x;

    if (vertical)
    {
        float rowH = iconSize;
        rowH += barGapY * barsA;
        rowH += barH * barsA;

        const float listH = rowH * 4.f + rowGapY * 3.f;
        targetPanelH = paddingY * 2.f + contentPadTop + listH;

        const float baseW = paddingX * 2.f + iconSize;
        const float expandedW = baseW + colGapX + numbersW;
        targetPanelW = MathUtils::lerp(baseW, expandedW, numbersA);
    }
    else
    {
        const float iconGapX = 6.f;
        const float rowW = iconSize * 4.f + iconGapX * 3.f;

        const float numbersGapY = 2.f;
        float contentH = iconSize;
        contentH += barGapY * barsA;
        contentH += barH * barsA;
        contentH += numbersGapY * numbersA;
        contentH += durTextH * numbersA;

        targetPanelW = paddingX * 2.f + rowW;
        targetPanelH = paddingY * 2.f + contentPadTop + contentH;
    }

    static bool panelInit = false;
    static float panelW = 0.f;
    static float panelH = 0.f;
    if (!panelInit)
    {
        panelW = targetPanelW;
        panelH = targetPanelH;
        panelInit = true;
    }
    panelW = ahExpApproach(panelW, targetPanelW, delta, 30.f);
    panelH = ahExpApproach(panelH, targetPanelH, delta, 30.f);
    if (std::abs(panelW - targetPanelW) < 0.25f) panelW = targetPanelW;
    if (std::abs(panelH - targetPanelH) < 0.25f) panelH = targetPanelH;

    mElement->mSize = { panelW, panelH };

    ImVec2 pos = mElement->getPos();
    if (ahIsRightAnchored(mElement->mAnchor)) pos.x -= panelW;
    else if (ahIsMiddleXAnchored(mElement->mAnchor)) pos.x -= panelW * 0.5f;
    if (ahIsBottomAnchored(mElement->mAnchor)) pos.y -= panelH;
    if (mElement->mCentered)
    {
        pos.x -= panelW * 0.5f;
        pos.y -= panelH * 0.5f;
    }

    ImVec2 min = pos;
    ImVec2 max = { pos.x + panelW, pos.y + panelH };

    ImVec2 pivot = min;
    if (ahIsRightAnchored(mElement->mAnchor)) pivot.x = max.x;
    else if (ahIsMiddleXAnchored(mElement->mAnchor)) pivot.x = (min.x + max.x) * 0.5f;
    pivot.y = ahIsBottomAnchored(mElement->mAnchor) ? max.y : min.y;

    ImVec2 scaledMin = { pivot.x + (min.x - pivot.x) * panelScale, pivot.y + (min.y - pivot.y) * panelScale };
    ImVec2 scaledMax = { pivot.x + (max.x - pivot.x) * panelScale, pivot.y + (max.y - pivot.y) * panelScale };
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
        const float tabWidth = std::max(1.f, scaledW / 1.5f);
        const ImVec2 tabMin = ImVec2(scaledMin.x + (scaledW - tabWidth) * 0.5f, scaledMax.y - tabHeight);
        const ImVec2 tabMax = ImVec2(tabMin.x + tabWidth, tabMin.y + tabHeight);
        drawList->PushClipRect(scaledMin, scaledMax, true);
        drawList->AddRectFilled(tabMin, tabMax, tabColor, rounding, ImDrawFlags_RoundCornersTop);
        drawList->PopClipRect();
    }

    drawList->PushClipRect(scaledMin, scaledMax, true);

    if (!mTexturesLoaded) loadArmorTextures();

    std::array<SlotRender, 4> data{};
    {
        std::scoped_lock lock(mDataMutex);
        data = mCache;
    }

    if (mElement->mSampleMode)
    {
        bool any = false;
        for (const auto& s : data) any |= s.has;
        if (!any)
        {
            data[0] = { true, SItemType::Helmet, 5, 275.f, 363.f, 0.76f, "diamond", "275/363" };
            data[1] = { true, SItemType::Chestplate, 6, 510.f, 592.f, 0.86f, "netherite", "510/592" };
            data[2] = { true, SItemType::Leggings, 2, 105.f, 225.f, 0.47f, "iron", "105/225" };
            data[3] = { true, SItemType::Boots, 1, 52.f, 195.f, 0.27f, "chainmail", "52/195" };
        }
    }

    const auto texFor = [&](int slotIndex, const std::string& material) -> ID3D11ShaderResourceView* {
        if (material.empty()) return nullptr;
        if (slotIndex == 0)
        {
            auto it = mHelmetTextures.find(material);
            return it != mHelmetTextures.end() ? it->second : nullptr;
        }
        if (slotIndex == 1)
        {
            auto it = mChestTextures.find(material);
            return it != mChestTextures.end() ? it->second : nullptr;
        }
        if (slotIndex == 2)
        {
            auto it = mLeggingsTextures.find(material);
            return it != mLeggingsTextures.end() ? it->second : nullptr;
        }
        if (slotIndex == 3)
        {
            auto it = mBootsTextures.find(material);
            return it != mBootsTextures.end() ? it->second : nullptr;
        }
        return nullptr;
        };

    const float startX = scaledMin.x + paddingX;
    const float startY = scaledMin.y + paddingY + contentPadTop;
    const float barW = std::max(1.f, iconSize - (barInsetX * 2.f));

    static std::array<ImVec2, 4> slotPos{};
    static std::array<ImVec2, 4> slotVel{};
    static std::array<float, 4> slotPct{};
    static bool slotPosInit = false;
    const float posStiffness = 220.f;
    const float posDamping = 32.f;
    const float pctSpeed = 18.f;

    if (vertical)
    {
        float rowH = iconSize;
        rowH += barGapY * barsA;
        rowH += barH * barsA;

        for (int i = 0; i < 4; i++)
        {
            const auto& s = data[i];

            ImVec2 target = { startX, startY + static_cast<float>(i) * (rowH + rowGapY) };
            if (!slotPosInit)
            {
                slotPos[i] = target;
                slotVel[i] = {};
            }
            ahSpringTo(slotPos[i].x, slotVel[i].x, target.x, delta, posStiffness, posDamping);
            ahSpringTo(slotPos[i].y, slotVel[i].y, target.y, delta, posStiffness, posDamping);
            const float targetPct = s.maxDurability > 0.f ? std::clamp(s.percentage, 0.f, 1.f) : 0.f;
            slotPct[i] = slotPosInit ? ahExpApproach(slotPct[i], targetPct, delta, pctSpeed) : targetPct;

            ImVec2 iconMin = slotPos[i];
            ImVec2 iconMax = { iconMin.x + iconSize, iconMin.y + iconSize };

            if (s.has)
            {
                bool drewTexture = false;
                if (ID3D11ShaderResourceView* tex = texFor(i, s.material); tex)
                {
                    drawList->AddImage(tex, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, static_cast<int>(255 * alphaAnim)));
                    drewTexture = true;
                }

                if (!drewTexture)
                {
                    ImColor col = ahArmorTierColor(s.tier);
                    col.Value.w *= alphaAnim;
                    drawList->AddRectFilled(iconMin, iconMax, col, 4.0f);
                }

                if (numbersA > 0.001f && s.maxDurability > 0.f)
                {
                    const int pct = static_cast<int>(std::round(slotPct[i] * 100.f));
                    std::string pctText = std::to_string(pct) + "%";
                    ImVec2 p = {
                        iconMin.x + iconSize + colGapX * numbersA,
                        iconMin.y + (iconSize - durTextH) * 0.5f
                    };
                    ImColor pctCol = ahDurabilityColor(slotPct[i]);
                    pctCol.Value.w = subTextColor.Value.w * numbersA;
                    drawList->AddText(textFont, durFont, p, pctCol, pctText.c_str());
                }

                const float barDrawGap = barGapY * barsA;
                const float barDrawH = barH * barsA;
                if (barDrawH > 0.75f && s.maxDurability > 0.f)
                {
                    const float barY = iconMin.y + iconSize + barDrawGap;
                    const float barX = iconMin.x + barInsetX;
                    const ImVec2 bgMin = { barX, barY };
                    const ImVec2 bgMax = { barX + barW, barY + barDrawH };
                    const float barRound = std::clamp(barDrawH * 0.5f, 0.f, 6.f);

                    ImColor barBg = IM_COL32(0, 0, 0, 170);
                    barBg.Value.w *= alphaAnim * barsA;
                    drawList->AddRectFilled(bgMin, bgMax, barBg, barRound);

                    ImColor fill = ahDurabilityColor(slotPct[i]);
                    fill.Value.w *= alphaAnim * barsA;

                    const float fillW = barW * slotPct[i];
                    if (fillW > 0.5f)
                    {
                        const ImDrawFlags flags = fillW >= (barW - 0.5f) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft;
                        drawList->AddRectFilled(bgMin, ImVec2(bgMin.x + fillW, bgMax.y), fill, barRound, flags);
                    }
                }
            }
        }
    }
    else
    {
        const float iconGapX = 6.f;
        const float numbersGapY = 2.f;

        for (int i = 0; i < 4; i++)
        {
            const auto& s = data[i];
            ImVec2 target = { startX + (iconSize + iconGapX) * static_cast<float>(i), startY };
            if (!slotPosInit)
            {
                slotPos[i] = target;
                slotVel[i] = {};
            }
            ahSpringTo(slotPos[i].x, slotVel[i].x, target.x, delta, posStiffness, posDamping);
            ahSpringTo(slotPos[i].y, slotVel[i].y, target.y, delta, posStiffness, posDamping);
            const float targetPct = s.maxDurability > 0.f ? std::clamp(s.percentage, 0.f, 1.f) : 0.f;
            slotPct[i] = slotPosInit ? ahExpApproach(slotPct[i], targetPct, delta, pctSpeed) : targetPct;

            ImVec2 iconMin = slotPos[i];
            ImVec2 iconMax = { iconMin.x + iconSize, iconMin.y + iconSize };

            if (!s.has) continue;

            bool drewTexture = false;
            if (ID3D11ShaderResourceView* tex = texFor(i, s.material); tex)
            {
                drawList->AddImage(tex, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, static_cast<int>(255 * alphaAnim)));
                drewTexture = true;
            }
            if (!drewTexture)
            {
                ImColor col = ahArmorTierColor(s.tier);
                col.Value.w *= alphaAnim;
                drawList->AddRectFilled(iconMin, iconMax, col, 4.0f);
            }

            const float barDrawH = barH * barsA;
            if (barDrawH > 0.75f && s.maxDurability > 0.f)
            {
                const float barX = iconMin.x + barInsetX;
                const float barsY = iconMin.y + iconSize + (barGapY * barsA);
                const ImVec2 bgMin = { barX, barsY };
                const ImVec2 bgMax = { barX + barW, barsY + barDrawH };
                const float barRound = std::clamp(barDrawH * 0.5f, 0.f, 6.f);

                ImColor barBg = IM_COL32(0, 0, 0, 170);
                barBg.Value.w *= alphaAnim * barsA;
                drawList->AddRectFilled(bgMin, bgMax, barBg, barRound);

                ImColor fill = ahDurabilityColor(slotPct[i]);
                fill.Value.w *= alphaAnim * barsA;

                const float fillW = barW * slotPct[i];
                if (fillW > 0.5f)
                {
                    const ImDrawFlags flags = fillW >= (barW - 0.5f) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft;
                    drawList->AddRectFilled(bgMin, ImVec2(bgMin.x + fillW, bgMax.y), fill, barRound, flags);
                }
            }

            if (numbersA > 0.001f && s.maxDurability > 0.f)
            {
                const int pct = static_cast<int>(std::round(slotPct[i] * 100.f));
                std::string pctText = std::to_string(pct) + "%";
                ImVec2 sz = ahCalcSize(textFont, durFont, pctText);
                const float numbersY = iconMin.y + iconSize + (barGapY + barH) * barsA + (numbersGapY * numbersA);
                ImVec2 p = { iconMin.x + (iconSize - sz.x) * 0.5f, numbersY };
                ImColor pctCol = ahDurabilityColor(slotPct[i]);
                pctCol.Value.w = subTextColor.Value.w * numbersA;
                drawList->AddText(textFont, durFont, p, pctCol, pctText.c_str());
            }
        }
    }

    slotPosInit = true;

    drawList->PopClipRect();

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
        {"Полоски", &mShowBars},
        {"Цифры", &mShowNumbers}
    };

    static float toggleAnim[2]{};
    for (int i = 0; i < 2; i++)
    {
        const float target = items[i].s->mValue ? 1.f : 0.f;
        const float speed = target > 0.5f ? 9.f : 14.f;
        toggleAnim[i] = MathUtils::lerp(toggleAnim[i], target, delta * speed);
        toggleAnim[i] = MathUtils::clamp(toggleAnim[i], 0.f, 1.f);
    }

    const std::string dot = "·";
    const std::string iconA = "m";
    const std::string iconB = "b";

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
    const float menuIconGap = 6.f;
    float maxIconW = 0.f;
    float maxLabelW = 0.f;
    for (const auto& it : items)
    {
        maxLabelW = std::max(maxLabelW, ahCalcSize(textFont, fontSize, it.label).x);
    }
    const std::string* menuIcons[2] = {&iconA, &iconB};
    for (int i = 0; i < 2; i++)
    {
        maxIconW = std::max(maxIconW, ahCalcSize(energyFont ? energyFont : textFont, fontSize, *menuIcons[i]).x);
    }
    const float indicatorShift = indicatorW + 7.f + maxIconW + menuIconGap;
    const float menuTextPad = std::max(4.f, fontSize * 0.35f);
    const float menuW = pad * 2.f + maxLabelW + indicatorShift + menuTextPad;
    const float menuH = pad * 2.f + rowH * 2.f;

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

    const float menuA = ahEaseOutCubic(menuAnim);
    const float menuPopRaw = ahEaseOutBack(menuAnim);
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

        for (int i = 0; i < 2; i++)
        {
            ImVec2 rowMin = ImVec2(drawMin.x + pad * s, drawMin.y + pad * s + rowH * i * s);
            ImVec2 rowMax = ImVec2(drawMax.x - pad * s, rowMin.y + rowH * s);
            const bool rowHovered = inEditor && menuHovered && ImRenderUtils::isMouseOver(ImVec4(rowMin.x, rowMin.y, rowMax.x, rowMax.y));
            if (inEditor && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && rowHovered)
            {
                items[i].s->mValue = !items[i].s->mValue;
            }

            const float t = ahEaseOutCubic(toggleAnim[i]);
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
            const float dotW = ahCalcSize(textFont, fontSize, dot).x;
            const float textShift = (baseShift + dotW + menuIconGap + maxIconW + menuIconGap) * t * s;
            if (t > 0.001f)
            {
                ImVec2 dotP = ImVec2(rowMin.x + 2.f + baseShift * t * s, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(textFont, fontScaled, dotP, menuDot, dot.c_str());
                ImColor iconCol = menuAccent;
                iconCol.Value.w *= a * (0.25f + 0.75f * t);
                ImVec2 iconP = ImVec2(rowMin.x + 2.f + baseShift * t * s + dotW + menuIconGap, rowMin.y + (rowH * s - fontScaled) * 0.5f);
                drawList->AddText(energyFont ? energyFont : textFont, fontScaled, iconP, iconCol, menuIcons[i]->c_str());
            }
            ImVec2 textP = ImVec2(rowMin.x + 2.f + textShift, rowMin.y + (rowH * s - fontScaled) * 0.5f);
            drawList->AddText(textFont, fontScaled, textP, menuText, items[i].label);
        }
    }

    FontHelper::popPrefFont();
}
