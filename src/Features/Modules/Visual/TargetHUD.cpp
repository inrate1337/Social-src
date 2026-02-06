#include "TargetHUD.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Modules/Combat/Aura.hpp>
#include <Features/FeatureManager.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <SDK/Minecraft/Actor/SerializedSkin.hpp>
#include <SDK/Minecraft/Actor/Components/ActorOwnerComponent.hpp>
#include <SDK/Minecraft/Network/Packets/ActorEventPacket.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Features/Modules/Visual/Interface.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <SDK/Minecraft/Inventory/CompoundTag.hpp>
#include <SDK/Minecraft/Inventory/ItemStack.hpp>
#include <SDK/Minecraft/Inventory/SimpleContainer.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/Resources.hpp>
#include <Utils/MemUtils.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace {
    ImColor thDurabilityColor(float percentage)
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

    std::string thArmorMaterialFromName(const std::string& itemName)
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

    void thComputeDurability(const std::string& itemName, SItemType type, int damage, float& outCur, float& outMax, float& outPct)
    {
        outCur = 0.f;
        outMax = 0.f;
        outPct = 0.f;

        const std::string mat = thArmorMaterialFromName(itemName);
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

    int thExtractDamage(ItemStack* item)
    {
        if (!item) return 0;
        int damage = std::max(0, static_cast<int>(item->mAuxValue));
        if (item->mCompoundTag)
        {
            if (auto* v = item->mCompoundTag->get("Damage"))
            {
                if (v->getTagType() == Tag::Type::Int) damage = std::max(0, v->asIntTag()->val);
                else if (v->getTagType() == Tag::Type::Short) damage = std::max(0, static_cast<int>(v->asShortTag()->val));
            }
        }
        return damage;
    }
}

TargetHUD::TargetHUD() : ModuleBase("TargetHUD", "Shows target information", ModuleCategory::Visual, 0, false)
{
    addSettings(
        &mStyle,
        &mFontSize,
        &mHealthCalculation
    );

    mNames = {
        {Lowercase, "targethud"},
        {LowercaseSpaced, "target hud"},
        {Normal, "TargetHUD"},
        {NormalSpaced, "Target HUD"},
    };

    mElement = std::make_unique<HudElement>();
    mElement->mPos = { 500, 500 };
    const char* ModuleBaseType = ModuleBase<TargetHUD>::getTypeID();;
    mElement->mParentTypeIdentifier = const_cast<char*>(ModuleBaseType);
    if (HudEditor::gInstance)
    {
        HudEditor::gInstance->registerElement(mElement.get());
    }
}

//����� �������� � hpp, �������� ���� ���

TargetHUD::~TargetHUD()
{
    for (auto& [actor, textureHolder] : mTargetTextures)
    {
        if (textureHolder.texture) textureHolder.texture->Release();
    }
    mTargetTextures.clear();

    releaseArmorTextures();

    if (HudEditor::gInstance && mElement)
    {
        auto& elements = HudEditor::gInstance->mElements;
        elements.erase(std::remove(elements.begin(), elements.end(), mElement.get()), elements.end());
    }
}

void TargetHUD::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &TargetHUD::onRenderEvent, nes::event_priority::LAST>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &TargetHUD::onBaseTickEvent, nes::event_priority::VERY_LAST>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &TargetHUD::onPacketInEvent>(this);

    loadArmorTextures();
    mElement->mVisible = true;
}

void TargetHUD::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &TargetHUD::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &TargetHUD::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &TargetHUD::onPacketInEvent>(this);

    for (auto& [actor, textureHolder] : mTargetTextures)
    {
        if (textureHolder.texture) textureHolder.texture->Release();
    }
    mTargetTextures.clear();

    releaseArmorTextures();

    mElement->mVisible = false;
}

void TargetHUD::releaseArmorTextures()
{
    auto releaseMap = [](auto& map) {
        map.clear();
        };

    releaseMap(mHelmetTextures);
    releaseMap(mChestTextures);
    releaseMap(mLeggingsTextures);
    releaseMap(mBootsTextures);
    mArmorTexturesLoaded = false;
}

void TargetHUD::loadArmorTextures()
{
    if (mArmorTexturesLoaded) return;

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

    static const std::vector<std::string> mats = { "netherite", "diamond", "iron", "golden", "chainmail", "leather" };
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
        if (tryLoadWithFallback("elytra", &tex)) mChestTextures["elytra"] = tex;
    }

    mArmorTexturesLoaded = true;
}

void TargetHUD::onBaseTickEvent(BaseTickEvent& event)
{
    (void)event;
    validateTextures();
    if (mHealthCalculation.mValue) calculateHealths();

    Actor* auraTarget = nullptr;
    if (Aura::sTargetRuntimeID > 0) auraTarget = ActorUtils::getActorFromRuntimeID(Aura::sTargetRuntimeID);
    bool hasValidTarget = false;
    if (Aura::sHasTarget && auraTarget && ActorUtils::isActorValid(auraTarget))
    {
        bool dead = false;
        float hp = 0.0f;
        if (!TryCallWrapper([&]() { dead = auraTarget->isDead(); })) dead = true;
        if (!TryCallWrapper([&]() { hp = auraTarget->getHealth(); })) hp = 0.0f;
        auto hurtComp = auraTarget->getMobHurtTimeComponent();
        hasValidTarget = !dead && hp > 0.0f && hurtComp;
    }
    if (hasValidTarget)
    {
        if (!auraTarget->getActorTypeComponent())
        {
            spdlog::warn("TargetHUD: Target has no ActorTypeComponent");//��� ������� �����
            return;
        }
        mHealth = auraTarget->getHealth();
        if (mHealthCalculation.mValue) mHealth = mHealths[auraTarget->getRawName()].health;
        mMaxHealth = auraTarget->getMaxHealth();
        mAbsorption = auraTarget->getAbsorption();
        mMaxAbsorption = auraTarget->getMaxAbsorption();
        if (!auraTarget->isPlayer())
        {
            mLastPlayerName = "Mob";
            return;
        }
        mLastHurtTime = mHurtTime;
        mHurtTime = static_cast<float>(auraTarget->getMobHurtTimeComponent()->mHurtTime);
        mLastHealth = mHealth;
        mLastAbsorption = mAbsorption;
        mLastMaxHealth = mMaxHealth;
        mLastMaxAbsorption = mMaxAbsorption;
        mLastPlayerName = auraTarget->getRawName();

        if (mHurtTime > mLastHurtTime)
        {
            mLastHurtTime = mHurtTime;
        }
    }
}


void TargetHUD::calculateHealths() {
    auto player = ClientInstance::get()->getLocalPlayer();
    auto actors = ActorUtils::getActorList(true, true);

    bool heal = 4000 <= NOW - mLastHealTime;
    if (heal) mLastHealTime = NOW;

    for (auto actor : actors) {
        if (actor == player) continue;
        if (!ActorUtils::isActorValid(actor)) continue;
        auto hurtComp = actor->getMobHurtTimeComponent();
        auto typeComp = actor->getActorTypeComponent();
        if (!hurtComp || !typeComp) continue;
        std::string rawName;
        if (!TryCallWrapper([&]() { rawName = actor->getRawName(); })) continue;
        auto info = &mHealths[rawName];
        float absorption = 0.0f;
        if (!TryCallWrapper([&]() { absorption = actor->getAbsorption(); })) absorption = 0.0f;
        int hurtTime = hurtComp->mHurtTime;
        if (0 < hurtTime) {
            float damage = 0;
            if (absorption < info->lastAbsorption) {
                if (0 < absorption) {
                    info->damage = abs(info->lastAbsorption - absorption);
                    damage = 0;
                }
                else if (0 < info->lastAbsorption) {
                    damage = abs(info->damage - info->lastAbsorption);
                }
            }
            else if (hurtTime == 9)
            {
                damage = info->damage;
            }

            if (absorption == 0 && 0 < damage) {
                if (info->health - damage < 0) info->health = 0;
                else info->health -= damage;
            }
        }
        if (heal) {
            if (info->health + 1 > 20) info->health = 20;
            else info->health++;
        }
        info->lastAbsorption = absorption;
    }
}

void TargetHUD::validateTextures()
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;
    if (!player->mContext.mRegistry) return;
    std::vector<EntityId> foundEntities;
    for (auto&& [daId, moduleOwner, typeComponent] : player->mContext.mRegistry->view<ActorOwnerComponent, ActorTypeComponent>().each())
    {
        Actor* actor = moduleOwner.mActor;
        if (!actor) continue;
        if (!ActorUtils::isActorValid(actor)) continue;
        foundEntities.push_back(actor->mContext.mEntityId);
    }

    for (auto it = mTargetTextures.begin(); it != mTargetTextures.end();)
    {
        if (std::ranges::find(foundEntities, it->second.associatedEntity) == foundEntities.end())
        {
            if (it->second.texture) it->second.texture->Release();
            it = mTargetTextures.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

ID3D11ShaderResourceView* TargetHUD::getActorSkinTex(Actor* actor)
{
    auto player = ClientInstance::get()->getLocalPlayer();

    if (!mTargetTextures.contains(actor)) mTargetTextures[actor] = TargetTextureHolder();

    auto& [texture, loaded, id] = mTargetTextures[actor];


    if (actor)
    {
        auto skin = actor->getSkin();
        if (skin)
        {
            if (!loaded) {
                bool isPlayer = true;
                if (actor->isValid() && !actor->isPlayer())
                {
                    isPlayer = false;
                    skin = player->getSkin();
                }

                int headSize = skin->skinWidth / 8;
                int headOffsetX = skin->skinWidth / 8;
                int headOffsetY = skin->skinHeight / 8;

                std::vector<uint8_t> headData(headSize * headSize * 4);

                for (int y = 0; y < headSize; y++) {
                    for (int x = 0; x < headSize; x++) {
                        int srcIndex = ((y + headOffsetY) * skin->skinWidth + (x + headOffsetX)) * 4;
                        int dstIndex = (y * headSize + x) * 4;
                        std::copy_n(skin->skinData + srcIndex, 4, headData.data() + dstIndex);
                    }
                }

                int scalingFactor = 8;
                std::vector<uint8_t> scaledHeadData(headSize * scalingFactor * headSize * scalingFactor * 4);

                for (int y = 0; y < headSize * scalingFactor; y++) {
                    for (int x = 0; x < headSize * scalingFactor; x++) {
                        int srcX = x / scalingFactor;
                        int srcY = y / scalingFactor;
                        int srcIndex = (srcY * headSize + srcX) * 4;
                        int dstIndex = (y * headSize * scalingFactor + x) * 4;
                        std::copy_n(headData.data() + srcIndex, 4, scaledHeadData.data() + dstIndex);
                    }
                }

                headSize *= scalingFactor;

                headData = std::move(scaledHeadData);
                D3DHook::createTextureFromData(headData.data(), headSize, headSize, &texture);
                loaded = true;
                id = actor->mContext.mEntityId;
            }
        }
    }


    return texture;
}

void TargetHUD::onRenderEvent(RenderEvent& event)
{
    (void)event;
    if (!ClientInstance::get()->getLocalPlayer()) return;
    if (!ClientInstance::get()->getLevelRenderer()) return;

    auto drawList = ImGui::GetBackgroundDrawList();
    if (mStyle.mValue != Style::Solstice)
    {
        return;
    }

    Actor* target = nullptr;
    if (Aura::sTargetRuntimeID > 0) target = ActorUtils::getActorFromRuntimeID(Aura::sTargetRuntimeID);
    bool hasTarget = Aura::sHasTarget && target && target->isValid() && !target->isDead() && target->getHealth() > 0.0f;

    if (mElement->mSampleMode && !hasTarget) {
        target = ClientInstance::get()->getLocalPlayer();
        hasTarget = true;
        mHealth = target->getHealth();
        mMaxHealth = target->getMaxHealth();
        mAbsorption = target->getAbsorption();
        mMaxAbsorption = target->getMaxAbsorption();
        mLastPlayerName = target->getRawName();
        mLastHurtTime = mHurtTime;
        mHurtTime = target->getMobHurtTimeComponent()->mHurtTime;
    }

    static float anim = 0.f;
    static float miniAnim = 0.f;
    float delta = ImGui::GetIO().DeltaTime;

    float lerpedHurtTime = MathUtils::lerp(mLastHurtTime / 10.f, mHurtTime / 10.f, delta);

    static float hurtTimeAnimPerc = 0.f;

    if (hasTarget && target && mLastTarget != target)
    {
        mLastTarget = target;
        mLastHealth = mHealth;
        mLastAbsorption = mAbsorption;
        mLastMaxHealth = mMaxHealth;
        mLastMaxAbsorption = mMaxAbsorption;
        mLerpedHealth = mHealth;
        mLerpedAbsorption = mAbsorption;
    }

    hurtTimeAnimPerc = MathUtils::lerp(hurtTimeAnimPerc, lerpedHurtTime, delta * 20.f);

    mLerpedHealth = MathUtils::lerp(mLerpedHealth, mHealth, delta * 10.f);
    mLerpedAbsorption = MathUtils::lerp(mLerpedAbsorption, mAbsorption, delta * 10.f);

    const bool wantShow = (mEnabled || mElement->mSampleMode) && hasTarget && target;

    anim = MathUtils::lerp(anim, wantShow ? 1.f : 0.f, ImGui::GetIO().DeltaTime * 10.f);
    anim = MathUtils::clamp(anim, 0.f, 1.f);
    miniAnim = MathUtils::lerp(miniAnim, wantShow ? 1.f : 0.f, ImGui::GetIO().DeltaTime * 12.f);
    miniAnim = MathUtils::clamp(miniAnim, 0.f, 1.f);
    const float alphaAnim = anim * anim * (3.f - 2.f * anim);
    const float miniAlphaAnim = miniAnim * miniAnim * (3.f - 2.f * miniAnim);

    float xpad = 5;
    float ypad = 5;

    if (anim < 0.01f)
    {
        return;
    }

    FontHelper::pushPrefFont(true, true);

    auto* textFont = ImGui::GetFont();
    ImFont* energyFont = nullptr;
    {
        auto it = FontHelper::Fonts.find("essence.ttf");
        if (it != FontHelper::Fonts.end() && it->second) energyFont = it->second;
    }
    if (!energyFont) energyFont = textFont;

    std::string name = mLastPlayerName;
    const float textFontSize = mFontSize.mValue;
    auto textNameSize = textFont->CalcTextSizeA(textFontSize, FLT_MAX, 0, name.c_str());
    const std::string icon = "b";
    const float iconGap = 6.f;
    const ImVec2 iconSize = energyFont->CalcTextSizeA(textFontSize, FLT_MAX, 0.0f, icon.c_str(), nullptr);
    const float nameGroupW = textNameSize.x + iconGap + iconSize.x;

    const float boxH = 70.f;
    const float headBaseSize = 58.f;
    const float namePadX = 10.f;
    const float minRightAreaW = 110.f;
    const float desiredRightW = std::max(minRightAreaW, nameGroupW + namePadX * 2.f);
    float boxW = headBaseSize + (xpad * 3.f) + desiredRightW;
    const float maxBoxW = std::min(420.f, ImGui::GetIO().DisplaySize.x - 20.f);
    if (maxBoxW > 0.f) boxW = std::min(boxW, maxBoxW);
    const auto boxSize = ImVec2(boxW, boxH);
    auto boxPos = mElement ? mElement->getPos() : ImVec2(0.f, 0.f);

    boxPos.x -= boxSize.x / 2;
    boxPos.y -= boxSize.y / 2;

    mElement->mSize = glm::vec2(boxSize.x, boxSize.y);
    mElement->mCentered = true;

    auto headSize = ImVec2(headBaseSize, headBaseSize);
    auto headPos = ImVec2(boxPos.x + xpad, boxPos.y + (boxSize.y - headSize.y) * 0.5f);

    auto headSize2 = ImVec2(MathUtils::lerp(headSize.x, 46.f, hurtTimeAnimPerc), MathUtils::lerp(headSize.y, 46.f, hurtTimeAnimPerc));

    ImColor backgroundColor = ColorUtils::getUiCardColor(1.0f);
    ImColor borderColor = ColorUtils::getUiBorderColor(16.0f / 255.0f);
    backgroundColor.Value.w *= alphaAnim;
    borderColor.Value.w *= alphaAnim;

    const ImVec2 boxMin = boxPos;
    const ImVec2 boxMax = ImVec2(boxPos.x + boxSize.x, boxPos.y + boxSize.y);
    const float rounding = 8.f;
    const float borderThickness = 1.6f;

    auto* interfaceModule = gFeatureManager && gFeatureManager->mModuleManager ? gFeatureManager->mModuleManager->getModule<Interface>() : nullptr;
    const bool useHudBlur = interfaceModule && interfaceModule->mHudBlur.mValue;
    const float blurStrength = useHudBlur ? interfaceModule->mHudBlurStrength.mValue : 0.f;
    if (useHudBlur)
    {
        ImRenderUtils::addBlurAlpha(ImVec4(boxMin.x, boxMin.y, boxMax.x, boxMax.y), blurStrength, backgroundColor.Value.w, rounding, drawList, true);
        backgroundColor.Value.w *= 0.85f;
    }
    {
        ImColor shadow = IM_COL32(0, 0, 0, 255);
        shadow.Value.w = 0.60f * alphaAnim;
        drawList->AddShadowRect(boxMin, boxMax, shadow, 24.0f, ImVec2(0.f, 3.f), 0, rounding);
    }
    drawList->AddRectFilled(boxMin, boxMax, backgroundColor, rounding);
    {
        ImColor glow = ColorUtils::getGuiAccentColor(0);
        glow.Value.w = 0.40f * alphaAnim;

        const float drawW = boxMax.x - boxMin.x;
        const float drawH = boxMax.y - boxMin.y;
        const float glowBaseH = drawH * 0.5f;
        const float glowWidth = drawW * 0.5f;
        const float glowX = boxMin.x + (drawW - glowWidth) * 0.5f;
        const float glowLift = glowBaseH * 1.f;

        const ImVec2 glowMin = ImVec2(glowX, boxMin.y - glowLift);
        const ImVec2 glowMax = ImVec2(glowX + glowWidth, boxMin.y + glowBaseH - glowLift);
        drawList->PushClipRect(boxMin, boxMax, true);
        drawList->AddShadowRect(glowMin, glowMax, glow, 45.0f, ImVec2(0.f, 0.f), 0, rounding);
        drawList->PopClipRect();
    }
    drawList->AddRect(boxMin, boxMax, borderColor, rounding, 0, borderThickness);
    {
        const float drawW = boxMax.x - boxMin.x;
        const float drawH = boxMax.y - boxMin.y;
        constexpr float tabHeightBase = 3.f;
        const float tabH = tabHeightBase;
        const float tabW = std::min(std::max(56.f, drawW * 0.45f), std::max(0.f, drawW - 12.f));
        if (tabW > 10.f && drawH > tabH + 4.f)
        {
            ImColor tab = ColorUtils::getGuiAccentColor(0);
            tab.Value.w *= alphaAnim;
            const ImVec2 tabMin = ImVec2(boxMin.x + (drawW - tabW) * 0.5f, boxMax.y - tabH);
            const ImVec2 tabMax = ImVec2(tabMin.x + tabW, tabMin.y + tabH);
            drawList->AddRectFilled(tabMin, tabMax, tab, rounding, ImDrawFlags_RoundCornersTop);
        }
    }

    Actor* renderTarget = target;
    if (!hasTarget)
    {
        if (mLastTarget && mLastTarget->isValid() && !mLastTarget->isDead() && mLastTarget->getHealth() > 0.0f)
        {
            renderTarget = mLastTarget;
        }
        else
        {
            renderTarget = nullptr;
        }
    }
    if (!renderTarget) {
        FontHelper::popPrefFont();
        return;
    }

    ID3D11ShaderResourceView* texture = nullptr;
    texture = getActorSkinTex(renderTarget);


    auto imageColor = ImColor(1.f, 1.f, 1.f, 1.f * alphaAnim);

    imageColor.Value.x = MathUtils::lerp(imageColor.Value.x, 1.f, hurtTimeAnimPerc);
    imageColor.Value.y = MathUtils::lerp(imageColor.Value.y, 1.f - hurtTimeAnimPerc, hurtTimeAnimPerc);
    imageColor.Value.z = MathUtils::lerp(imageColor.Value.z, 1.f - hurtTimeAnimPerc, hurtTimeAnimPerc);

    float healthStartY = boxPos.y + boxSize.y - (ypad + 2.f) - 18.f;
    float ysize = 10.f;
    const float rightAreaMinX = boxPos.x + headSize.x + (xpad * 2.f);
    const float rightAreaMaxX = boxPos.x + boxSize.x - xpad;
    const float rightAreaW = std::max(0.f, rightAreaMaxX - rightAreaMinX);
    const float targetBarW = std::clamp(nameGroupW + namePadX * 2.f, 90.f, rightAreaW);
    const float barW = std::min(rightAreaW * 0.95f, targetBarW);
    const float barMinX = rightAreaMinX + (rightAreaW - barW) * 0.5f;
    auto healthBarStart = ImVec2(barMinX, healthStartY);
    auto healthBarEnd = ImVec2(barMinX + barW, healthStartY + ysize);

    float ydiff = healthBarStart.y - boxPos.y;
    auto textNamePos = ImVec2(
        rightAreaMinX + (rightAreaW - nameGroupW) * 0.5f,
        boxPos.y + ydiff / 2 - textNameSize.y / 2 + ypad
    );

    headPos.x += (headSize.x - headSize2.x) / 2;
    headPos.y += (headSize.y - headSize2.y) / 2;


    if (texture)
        drawList->AddImageRounded(texture, headPos, headPos + headSize2, ImVec2(0, 0), ImVec2(1, 1), imageColor, 10.f);

    auto textStartPos = ImVec2(rightAreaMinX, boxMin.y);
    auto textEndPos = ImVec2(rightAreaMaxX, boxMax.y);
    drawList->PushClipRect(textStartPos, textEndPos, true);
    ImRenderUtils::drawShadowText(drawList, name, textNamePos, ImColor(255, 255, 255, static_cast<int>(255 * alphaAnim)), textFontSize, false);
    {
        const ImVec2 iconPos = ImVec2(
            textNamePos.x + textNameSize.x + iconGap,
            textNamePos.y + (textNameSize.y - iconSize.y) * 0.5f
        );
        ImColor iconCol = ColorUtils::getGuiAccentColor(0);
        iconCol.Value.w *= alphaAnim;
        drawList->AddText(energyFont, textFontSize, iconPos, iconCol, icon.c_str());
    }
    drawList->PopClipRect();


    drawList->AddRectFilled(healthBarStart, healthBarEnd, ImColor(8, 8, 12, (int)((float)210 * alphaAnim)), 7.f);

    float healthPerc = mLerpedHealth / mMaxHealth;
    healthPerc = MathUtils::clamp(healthPerc, 0.f, 1.f);
    auto healthEnd = ImVec2(healthBarEnd.x, healthBarEnd.y);
    healthEnd.x = MathUtils::lerp(healthBarStart.x, healthBarEnd.x, healthPerc);

    float absorptionPerc = mLerpedAbsorption / 20.f;
    absorptionPerc = MathUtils::clamp(absorptionPerc, 0.f, 1.f);
    auto absorpEnd = ImVec2(healthBarEnd.x, healthBarEnd.y);
    absorpEnd.x = MathUtils::lerp(healthBarStart.x, healthBarEnd.x, absorptionPerc);

    ImColor startDark = ColorUtils::getGuiAccentColor(0);
    ImColor endDark = startDark;
    startDark.Value.w *= alphaAnim;
    endDark.Value.x *= 0.35f;
    endDark.Value.y *= 0.35f;
    endDark.Value.z *= 0.35f;
    endDark.Value.w *= alphaAnim;

    if (healthPerc > 0.01f)
    {
        const float fillW = std::max(0.f, healthEnd.x - healthBarStart.x);
        const float r = std::min(7.f, std::min(ysize * 0.5f, fillW * 0.5f));
        const ImVec2 fillMin = healthBarStart;
        const ImVec2 fillMax = ImVec2(healthBarStart.x + fillW, healthBarEnd.y);
        drawList->AddRectFilledMultiColor(fillMin, fillMax, startDark, endDark, endDark, startDark, r, ImDrawCornerFlags_All);
    }

    if (absorptionPerc > 0.01f)
    {
        const float fillW = std::max(0.f, absorpEnd.x - healthBarStart.x);
        const float r = std::min(7.f, std::min(ysize * 0.5f, fillW * 0.5f));
        const ImVec2 fillMin = healthBarStart;
        const ImVec2 fillMax = ImVec2(healthBarStart.x + fillW, healthBarEnd.y);
        drawList->AddRectFilled(fillMin, fillMax, ImColor(244, 204, 0, (int)(255 * alphaAnim)), r, ImDrawCornerFlags_All);
    }

    if (mAbsorption != 0)
    {
        drawList->PushClipRect(textStartPos, textEndPos, true);
        drawList->PopClipRect();
    }

    loadArmorTextures();
    if (renderTarget && renderTarget->isValid() && miniAlphaAnim > 0.01f)
    {
        const float miniGap = 4.f;
        const float miniSize = 22.f;
        const float rowW = miniSize * 4.f + miniGap * 3.f;
        if (miniSize > 4.f)
        {
            const ImGuiIO& io = ImGui::GetIO();
            drawList->PushClipRect(ImVec2(0.f, boxMax.y), io.DisplaySize, true);

            const float baseY = boxMax.y + miniGap;
            const float slideY = MathUtils::lerp(boxMax.y - miniSize, baseY, miniAlphaAnim);

            const ImVec2 rowMin = ImVec2(boxMin.x + (boxSize.x - rowW) * 0.5f, slideY);

            const float rawRounding = 4.f;
            const float miniRounding = rawRounding;

            ImColor miniBg = ColorUtils::getUiCardColor(1.0f);
            ImColor miniBorder = ColorUtils::getUiBorderColor(16.0f / 255.0f);
            miniBg.Value.w *= alphaAnim * miniAlphaAnim;
            miniBorder.Value.w *= alphaAnim * miniAlphaAnim;

            auto* armorContainer = renderTarget->getArmorContainer();
            for (int i = 0; i < 4; i++)
            {
                const ImVec2 panelMin = ImVec2(rowMin.x + (miniSize + miniGap) * static_cast<float>(i), rowMin.y);
                const ImVec2 panelMax = ImVec2(panelMin.x + miniSize, panelMin.y + miniSize);

                if (useHudBlur)
                {
                    ImRenderUtils::addBlurAlpha(ImVec4(panelMin.x, panelMin.y, panelMax.x, panelMax.y), blurStrength, miniBg.Value.w, miniRounding, drawList, true);
                }
                ImColor shadow = IM_COL32(0, 0, 0, 255);
                shadow.Value.w = 0.60f * alphaAnim * miniAlphaAnim;
                drawList->AddShadowRect(panelMin, panelMax, shadow, 24.0f, ImVec2(0.f, 3.f), 0, miniRounding);

                drawList->AddRectFilled(panelMin, panelMax, miniBg, miniRounding);
                drawList->AddRect(panelMin, panelMax, miniBorder, miniRounding, 0, 1.0f);

                ItemStack* item = armorContainer ? armorContainer->getItem(i) : nullptr;
                if (!item || !item->mItem) continue;

                Item* it = item->getItem();
                if (!it) continue;

                const std::string mat = thArmorMaterialFromName(it->mName);
                ID3D11ShaderResourceView* tex = nullptr;
                if (i == 0)
                {
                    auto found = mHelmetTextures.find(mat);
                    tex = found != mHelmetTextures.end() ? found->second : nullptr;
                }
                else if (i == 1)
                {
                    auto found = mChestTextures.find(mat);
                    tex = found != mChestTextures.end() ? found->second : nullptr;
                }
                else if (i == 2)
                {
                    auto found = mLeggingsTextures.find(mat);
                    tex = found != mLeggingsTextures.end() ? found->second : nullptr;
                }
                else if (i == 3)
                {
                    auto found = mBootsTextures.find(mat);
                    tex = found != mBootsTextures.end() ? found->second : nullptr;
                }

                const float iconPad = 2.f;
                const ImVec2 iconMin = ImVec2(panelMin.x + iconPad, panelMin.y + iconPad);
                const ImVec2 iconMax = ImVec2(panelMax.x - iconPad, panelMax.y - iconPad);
                if (tex)
                {
                    drawList->AddImage(tex, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, static_cast<int>(255 * alphaAnim * miniAlphaAnim)));
                }

                float cur = 0.f, max = 0.f, pct = 0.f;
                thComputeDurability(it->mName, it->getItemType(), thExtractDamage(item), cur, max, pct);
                if (max > 0.f)
                {
                    const float barInset = 2.f;
                    const float barH = std::clamp(miniSize * 0.14f, 1.5f, 2.6f) * miniAlphaAnim;
                    const ImVec2 barMin = ImVec2(panelMin.x + barInset, panelMax.y - barInset - barH);
                    const ImVec2 barMax = ImVec2(panelMax.x - barInset, panelMax.y - barInset);
                    const float barRound = std::clamp(barH * 0.5f, 0.f, 6.f);

                    ImColor barBg = IM_COL32(0, 0, 0, 170);
                    barBg.Value.w *= alphaAnim * miniAlphaAnim;
                    if (barH > 0.5f) drawList->AddRectFilled(barMin, barMax, barBg, barRound);

                    const float clampedPct = std::clamp(pct, 0.f, 1.f);
                    const float fillW = (barMax.x - barMin.x) * clampedPct;
                    if (fillW > 0.5f && barH > 0.5f)
                    {
                        ImColor fill = thDurabilityColor(clampedPct);
                        fill.Value.w *= alphaAnim * miniAlphaAnim;
                        const ImDrawFlags flags = fillW >= ((barMax.x - barMin.x) - 0.5f) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft;
                        drawList->AddRectFilled(barMin, ImVec2(barMin.x + fillW, barMax.y), fill, barRound, flags);
                    }
                }
            }

            drawList->PopClipRect();
        }
    }

    FontHelper::popPrefFont();
}

void TargetHUD::onPacketInEvent(PacketInEvent& event)
{
    if (event.mPacket->getId() == PacketID::ActorEvent)
    {
        auto packet = event.getPacket<ActorEventPacket>();

        if (packet->mEvent != ActorEvent::HURT) return;
        if (!ActorUtils::getActorFromRuntimeID(packet->mRuntimeID)) return;
    }
    else if (event.mPacket->getId() == PacketID::ChangeDimension) {
        for (auto& [actor, textureHolder] : mTargetTextures)
        {
            if (textureHolder.texture) textureHolder.texture->Release();
        }
        mTargetTextures.clear();

        mLastHealTime = NOW;
        mHealths.clear();
    }
}
