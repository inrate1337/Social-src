#pragma once

#include <Features/Modules/Module.hpp>

class ClientDebug : public ModuleBase<ClientDebug>
{
public:
    BoolSetting mShowActors = BoolSetting("Actors", "Show actor tracker", true);
    BoolSetting mShowModules = BoolSetting("Modules", "Show module list", true);
    BoolSetting mOnlyPlayers = BoolSetting("Only Players", "Show only player actors", false);
    NumberSetting mMaxActors = NumberSetting("Max Actors", "Max actors shown in list", 250.f, 50.f, 2000.f, 10.f);
    NumberSetting mForgetAfterMs = NumberSetting("Forget After", "Forget missing actors after N ms", 15000.f, 1000.f, 600000.f, 250.f);

    ClientDebug() : ModuleBase("Client Debug", "Debug overlay with runtime diagnostics", ModuleCategory::Visual, 0, false)
    {
        addSettings(&mShowActors, &mShowModules, &mOnlyPlayers, &mMaxActors, &mForgetAfterMs);

        mNames = {
            {Lowercase, "clientdebug"},
            {LowercaseSpaced, "client debug"},
            {Normal, "ClientDebug"},
            {NormalSpaced, "Client Debug"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onRenderEvent(class RenderEvent& event);
};
