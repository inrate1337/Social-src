#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <Features/Modules/Module.hpp>
#include <SDK/Minecraft/Inventory/Item.hpp>

#include "HudEditor.hpp"

struct ID3D11ShaderResourceView;

class ArmorHUD : public ModuleBase<ArmorHUD>
{
public:
    enum class Layout
    {
        Vertical,
        Horizontal
    };

    EnumSettingT<Layout> mLayout = EnumSettingT("Layout", "ArmorHUD layout", Layout::Vertical, "Vertical", "Horizontal");
    BoolSetting mShowBars = BoolSetting("Bars", "Show durability bars", true);
    BoolSetting mShowNumbers = BoolSetting("Numbers", "Show durability numbers", true);
    NumberSetting mIconSize = NumberSetting("Icon Size", "Armor icon size", 20.f, 12.f, 40.f, 1.f);

    ArmorHUD();
    ~ArmorHUD();

    void onEnable() override;
    void onDisable() override;

    void onRenderEvent(class RenderEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);

private:
    struct SlotSnapshot
    {
        bool has = false;
        SItemType type = SItemType::None;
        int tier = 0;
        int damage = 0;
        std::string itemName;
    };

    struct SlotRender
    {
        bool has = false;
        SItemType type = SItemType::None;
        int tier = 0;
        float currentDurability = 0.f;
        float maxDurability = 0.f;
        float percentage = 0.f;
        std::string material;
        std::string durabilityText;
    };

    void startWorker();
    void stopWorker();

    void loadArmorTextures();
    void releaseArmorTextures();

    std::unique_ptr<HudElement> mElement;

    std::mutex mDataMutex;
    std::array<SlotSnapshot, 4> mSnapshot{};
    std::array<SlotRender, 4> mCache{};

    std::atomic<bool> mWorkerRunning{ false };
    std::thread mWorker;

    bool mTexturesLoaded = false;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mHelmetTextures;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mChestTextures;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mLeggingsTextures;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> mBootsTextures;
};