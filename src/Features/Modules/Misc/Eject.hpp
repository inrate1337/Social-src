#pragma once
#include <Features/Modules/Module.hpp>

class Eject : public ModuleBase<Eject> {
public:
    Eject() : ModuleBase("Eject", "Uninjects the client from the game process", ModuleCategory::Misc, 0, false) {
        mNames = {
            {Lowercase, "eject"},
            {LowercaseSpaced, "eject"},
            {Normal, "Eject"},
            {NormalSpaced, "Eject"},
        };
    }
    void onEnable() override;
};