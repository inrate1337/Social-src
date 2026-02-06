#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <Features/Modules/Module.hpp>

#include "HudEditor.hpp"

inline class KeyBinds* gKeyBinds = nullptr;

class KeyBinds : public ModuleBase<KeyBinds>
{
public:
    BoolSetting mShowCategoryIcons = BoolSetting("Category Icons", "Shows category icons in the keybind list", true);

    KeyBinds() : ModuleBase("KeyBinds", "Shows active keybinds on screen", ModuleCategory::Visual, 0, false)
    {
        addSetting(&mShowCategoryIcons);

        mNames = {
            {Lowercase, "keybinds"},
            {LowercaseSpaced, "key binds"},
            {Normal, "KeyBinds"},
            {NormalSpaced, "Key Binds"}
        };

        gKeyBinds = this;

        mElement = std::make_unique<HudElement>();
        mElement->mAnchor = HudElement::Anchor::TopRight;
        mElement->mPos = { -12.f, 200.f };

        const char* moduleBaseType = ModuleBase<KeyBinds>::getTypeID();
        mElement->mParentTypeIdentifier = const_cast<char*>(moduleBaseType);

        if (HudEditor::gInstance)
        {
            HudEditor::gInstance->registerElement(mElement.get());
        }
    }

    struct BindEntry
    {
        std::string moduleName;
        int key = 0;
        std::string keyName;
        ModuleCategory category = ModuleCategory::Misc;
    };

    std::unique_ptr<HudElement> mElement;

    void onEnable() override;
    void onDisable() override;
    void onRenderEvent(class RenderEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);

private:
    std::mutex mMutex;
    std::vector<BindEntry> mBinds;
};
