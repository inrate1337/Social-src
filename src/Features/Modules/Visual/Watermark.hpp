#pragma once
//
// Created by vastrakai on 6/29/2024.
//

#include <Features/Events/PingUpdateEvent.hpp>
#include <Features/Modules/Module.hpp>

#include "HudEditor.hpp"

class Watermark : public ModuleBase<Watermark> {
public:
    enum class Style {
        Solstice,
        SevenDays
    };
    EnumSettingT<Style> mStyle = EnumSettingT<Style>("Style", "The style of the watermark.", Style::Solstice, "NewLight", "7 Days");
    BoolSetting mBold = BoolSetting("Bold", "Enables bold text", true);
    BoolSetting mShowName = BoolSetting("Show Name", "Shows name section", true);
    BoolSetting mShowFps = BoolSetting("Show FPS", "Shows FPS section", true);
    BoolSetting mShowPing = BoolSetting("Show Ping", "Shows ping section", true);
    BoolSetting mShowCoords = BoolSetting("Show Coords", "Shows coordinates row", true);
    Watermark() : ModuleBase("Watermark", "Displays a watermark on the screen", ModuleCategory::Visual, 0, true) {
        addSetting(&mStyle);
        //addSetting(&mBold);
        addSetting(&mShowName);
        addSetting(&mShowFps);
        addSetting(&mShowPing);
        addSetting(&mShowCoords);
        gFeatureManager->mDispatcher->listen<RenderEvent, &Watermark::onRenderEvent>(this);
        gFeatureManager->mDispatcher->listen<PingUpdateEvent, &Watermark::onPingUpdateEvent>(this);

        mNames = {
            {Lowercase, "watermark"},
            {LowercaseSpaced, "watermark"},
            {Normal, "Watermark"},
            {NormalSpaced, "Watermark"}
        };

        mElement = std::make_unique<HudElement>();
        mElement->mAnchor = HudElement::Anchor::TopMiddle;
        mElement->mPos = { 0, 20 };

        const char* ModuleBaseType = ModuleBase<Watermark>::getTypeID();;
        mElement->mParentTypeIdentifier = const_cast<char*>(ModuleBaseType);
    }

    std::unique_ptr<HudElement> mElement;
    __int64 mPing = 0;
    __int64 mPingDisplay = 0;
    int mFpsDisplay = 0;
    uint64_t mLastStatUpdate = 0;

    void onEnable() override;
    void onDisable() override;

    void onRenderEvent(class RenderEvent& event);
    void onPingUpdateEvent(class PingUpdateEvent& event);

    std::string getSettingDisplay() override {
        return mStyle.mValues[mStyle.as<int>()];
    }
};
