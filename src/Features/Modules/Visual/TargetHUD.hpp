#pragma once
#include <SDK/Minecraft/Actor/EntityId.hpp>
#include "HudEditor.hpp"
#include <Features/Modules/Module.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/SerializedSkin.hpp>
#include <d3d11.h>

class TargetHUD : public ModuleBase<TargetHUD> {
public:
    enum class Style {
        Solstice
    };

    EnumSettingT<Style> mStyle = EnumSettingT("Style", "The style of the target HUD", Style::Solstice, "Solstice");
    NumberSetting mFontSize = NumberSetting("Font Size", "The size of the font", 20, 1, 40, 1);
    BoolSetting mHealthCalculation = BoolSetting("Health Calculation", "Calculate health", true);

    TargetHUD();
    ~TargetHUD();

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(BaseTickEvent& event);
    void onPacketInEvent(PacketInEvent& event);
    void onRenderEvent(RenderEvent& event);

private:
    void loadArmorTextures();
    void releaseArmorTextures();

    struct TargetTextureHolder {
        ID3D11ShaderResourceView* texture = nullptr;
        bool loaded = false;
        EntityId associatedEntity;
    };

    struct HealthInfo {
        float health = 20;
        float damage = 0;
        float lastAbsorption = 0;
    };

    std::unordered_map<Actor*, TargetTextureHolder> mTargetTextures;
    std::unordered_map<std::string, HealthInfo> mHealths;

    std::unique_ptr<HudElement> mElement;

    bool mArmorTexturesLoaded = false;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mHelmetTextures;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mChestTextures;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mLeggingsTextures;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mBootsTextures;

    float mHealth = 20.f;
    float mMaxHealth = 20.f;
    float mAbsorption = 0.f;
    float mMaxAbsorption = 20.f;

    float mLastHealth = 20.f;
    float mLastAbsorption = 0.f;
    float mLastMaxHealth = 20.f;
    float mLastMaxAbsorption = 20.f;

    float mHurtTime = 0.f;
    float mLastHurtTime = 0.f;

    float mLerpedHealth = 20.f;
    float mLerpedAbsorption = 0.f;

    uint64_t mLastHealTime = 0;
    std::string mLastPlayerName = "Player";
    Actor* mLastTarget = nullptr;

    void calculateHealths();
    void validateTextures();
    ID3D11ShaderResourceView* getActorSkinTex(Actor* actor);
};
