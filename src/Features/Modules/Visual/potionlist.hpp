#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Features/Modules/Module.hpp>

#include "HudEditor.hpp"

inline class PotionList* gPotionList = nullptr;

class PotionList : public ModuleBase<PotionList>
{
public:
    BoolSetting mShowIcons = BoolSetting("Show Icons", "Shows potion icons", true);
    BoolSetting mShowLevel = BoolSetting("Show Level", "Shows potion amplifier", true);
    BoolSetting mShowTime = BoolSetting("Show Time", "Shows potion duration", true);

    PotionList() : ModuleBase("PotionList", "Shows active effects on local player", ModuleCategory::Visual, 0, false)
    {
        addSetting(&mShowIcons);
        addSetting(&mShowLevel);
        addSetting(&mShowTime);

        mNames = {
            {Lowercase, "potionlist"},
            {LowercaseSpaced, "potion list"},
            {Normal, "PotionList"},
            {NormalSpaced, "Potion List"}
        };

        gPotionList = this;

        mElement = std::make_unique<HudElement>();
        mElement->mAnchor = HudElement::Anchor::TopRight;
        mElement->mPos = {-12.f, 260.f};

        const char* moduleBaseType = ModuleBase<PotionList>::getTypeID();
        mElement->mParentTypeIdentifier = const_cast<char*>(moduleBaseType);

        if (HudEditor::gInstance)
        {
            HudEditor::gInstance->registerElement(mElement.get());
        }
    }

    struct EffectEntry
    {
        unsigned int id = 0;
        std::string name;
        int amplifier = 0;
        int durationTicks = 0;
        bool showParticles = false;
        bool notifiedTenSeconds = false;
        bool notifiedEnded = false;
    };

    std::unique_ptr<HudElement> mElement;

    void onEnable() override;
    void onDisable() override;
    void onRenderEvent(class RenderEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketInEvent(class PacketInEvent& event);

private:
    std::mutex mMutex;
    std::unordered_map<unsigned int, EffectEntry> mEffects;
    uintptr_t mLastPlayerPtr = 0;
    uint64_t mLastRuntimeId = 0;
    bool mHadPlayer = false;
};
