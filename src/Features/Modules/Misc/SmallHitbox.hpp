#pragma once
#include <Features/Modules/Module.hpp>
#include <Features/Events/PacketOutEvent.hpp>

class SmallHitbox : public ModuleBase<SmallHitbox> {
public:
    SmallHitbox() : ModuleBase("SmallHitbox", "Reduces hitbox height by forcing swimming state", ModuleCategory::Misc, 0, false) {
        mNames = {
            {Lowercase, "smallhitbox"},
            {LowercaseSpaced, "small hitbox"},
            {Normal, "SmallHitbox"},
            {NormalSpaced, "Small Hitbox"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onPacketOut(PacketOutEvent& event);
};