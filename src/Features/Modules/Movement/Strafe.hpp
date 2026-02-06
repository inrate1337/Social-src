#pragma once
#include <Features/Modules/Module.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <Features/Modules/Setting.hpp>

class Strafe : public ModuleBase<Strafe> {
public:
    Strafe() : ModuleBase("Strafe", "Simple strafe movement", ModuleCategory::Movement, 0, false) {
        mNames = {
            {Lowercase, "strafe"},
            {LowercaseSpaced, "strafe"},
            {Normal, "Strafe"},
            {NormalSpaced, "Strafe"}
        };
        addSetting(&mSpeed);
    }
    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    const char* getTypeID() const override { return "Strafe"; }
    NumberSetting mSpeed = NumberSetting("Speed", "Strafe speed", 0.5f, 0.1f, 2.0f, 0.01f);
};