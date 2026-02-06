#pragma once

#include <Features/Modules/Module.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Modules/Visual/HudEditor.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>

class DeathCoords : public ModuleBase<DeathCoords> {
private:
    bool wDead = false;
    glm::vec3 LAP{ 0.0f, 0.0f, 0.0f };

public:
    DeathCoords() : ModuleBase("DeathCoords", "Show coords in chat of where you death", ModuleCategory::Misc, 0, false) {
        mNames = {
                {Lowercase, "deathcoords"},
                {LowercaseSpaced, "death coords"},
                {Normal, "DeathCoords"},
                {NormalSpaced, "Death Coords"},
        };
    }
    void onEnable() override;
    void onDisable() override;
    void onTick(BaseTickEvent& event);
};