 #pragma once

#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>

class Hitbox : public ModuleBase<Hitbox> {
public:
    NumberSetting mWidth  = NumberSetting("Width",  "Player hitbox width.",  0.8f, 0.3f, 3.0f, 0.01f);
    NumberSetting mHeight = NumberSetting("Height", "Player hitbox height.", 2.0f, 1.0f, 4.0f, 0.01f);
    BoolSetting   mNoFriends = BoolSetting("No Friends", "Do not modify hitboxes of friends/teammates", true);

    Hitbox() : ModuleBase("Hitboxes", "Increases other players' hitboxes locally.", ModuleCategory::Combat, 0, false) {
        addSettings(&mWidth, &mHeight, &mNoFriends);

        mNames = {
            {Lowercase, "hitboxes"},
            {LowercaseSpaced, "hitboxes"},
            {Normal, "Hitboxes"},
            {NormalSpaced, "Hitboxes"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
};
