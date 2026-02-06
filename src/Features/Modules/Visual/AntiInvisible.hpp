#pragma once

#include <Features/Modules/Module.hpp>

class AntiInvisible : public ModuleBase<AntiInvisible>
{
public:
    BoolSetting mShowFriends = BoolSetting("Show Friends", "Whether or not to make invisible friends visible.", true);

    AntiInvisible() : ModuleBase("AntiInvisible", "Makes invisible players visible", ModuleCategory::Visual, 0, false) {
        addSetting(&mShowFriends);

        mNames = {
            {Lowercase, "antiinvisible"},
            {LowercaseSpaced, "anti invisible"},
            {Normal, "AntiInvisible"},
            {NormalSpaced, "Anti Invisible"}
        };
    }
    void onEnable() override;
    void onDisable() override;
    void onActorRenderEvent(class ActorRenderEvent& event);
};
