#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <Features/Modules/Module.hpp>

#include "HudEditor.hpp"

inline class RenderLocal* gRenderLocal = nullptr;

class RenderLocal : public ModuleBase<RenderLocal>
{
public:
    BoolSetting mShowIcon = BoolSetting("Show Icon", "Shows header icon", true);
    BoolSetting mShowDots = BoolSetting("Show Dots", "Shows dots before names", true);

    RenderLocal() : ModuleBase("RenderLocal", "Shows nearby players on HUD", ModuleCategory::Visual, 0, false)
    {
        addSettings(
            &mShowIcon,
            &mShowDots
        );

        mNames = {
            {Lowercase, "renderlocal"},
            {LowercaseSpaced, "render local"},
            {Normal, "RenderLocal"},
            {NormalSpaced, "Render Local"}
        };

        gRenderLocal = this;

        mElement = std::make_unique<HudElement>();
        mElement->mAnchor = HudElement::Anchor::TopRight;
        mElement->mPos = { -12.f, 160.f };

        const char* moduleBaseType = ModuleBase<RenderLocal>::getTypeID();
        mElement->mParentTypeIdentifier = const_cast<char*>(moduleBaseType);

        if (HudEditor::gInstance)
        {
            HudEditor::gInstance->registerElement(mElement.get());
        }
    }

    struct PlayerEntry
    {
        std::string name;
    };

    std::unique_ptr<HudElement> mElement;

    void onEnable() override;
    void onDisable() override;
    void onRenderEvent(class RenderEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);

private:
    std::mutex mMutex;
    std::vector<PlayerEntry> mPlayers;
};
