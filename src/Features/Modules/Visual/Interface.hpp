#pragma once
#include <Features/Events/ActorRenderEvent.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/ModuleStateChangeEvent.hpp>
#include <Features/Events/DrawImageEvent.hpp>
#include <Features/Events/PreGameCheckEvent.hpp>
//
// Created by vastrakai on 7/1/2024.
//


class Interface : public ModuleBase<Interface>
{
public:
    enum ColorTheme {
        Trans,
        Rainbow,
        Bubblegum,
        Watermelon,
        Sunset,
        Poison,
        Custom
    };

    enum class FontType {
        Mojangles,
        ProductSans,
        /*OpenSans,
        Comfortaa,
        SFProDisplay*/
    };

    ColorTheme randomTheme = (ColorTheme)MathUtils::random(0, ColorTheme::Custom);

    EnumSettingT<NamingStyle> mNamingStyle = EnumSettingT<NamingStyle>("Naming", "The style of the module names.", NamingStyle::NormalSpaced, "lowercase", "lower spaced", "Normal", "Spaced");
    EnumSettingT<ColorTheme> mMode = EnumSettingT<ColorTheme>("Theme", "The mode of the interface.", randomTheme, "Trans", "Rainbow", "Bubblegum", "Watermelon", "Sunset", "Poison", "Custom");
    // make sure you actually have the fonts u put here lol
    EnumSettingT<FontType> mFont = EnumSettingT<FontType>("Font", "The font of the interface.", FontType::ProductSans, "Mojangles", "Product Sans");
    // Single main color that will be used to generate a nice gradient/theme
    ColorSetting mMainColor = ColorSetting("Color", "Main interface color.", 0xFFFF0000);
    NumberSetting mColorSpeed = NumberSetting("Color Speed", "The speed of the color change.", 3.f, 0.01f, 20.f, 0.01);
    NumberSetting mSaturation = NumberSetting("Saturation", "The saturation of the interface.", 1.f, 0.f, 1.f, 0.01);
    BoolSetting mHudBlur = BoolSetting("HUD Blur", "Blur behind HUD cards.", false);
    NumberSetting mHudBlurStrength = NumberSetting("HUD Blur Strength", "Blur strength for HUD cards.", 3.f, 0.5f, 10.f, 0.1f);
    BoolSetting mHoveredItem = BoolSetting("Custom Hover", "Customizes the hovered item gui.", false);
    BoolSetting mSlotEasing = BoolSetting("Slot Easing", "Eases the selection of slots", true);
    NumberSetting mSlotEasingSpeed = NumberSetting("Easing Speed", "The speed of the slot easing", 20.f, 0.1f, 20.f, 0.01f);
#ifdef __DEBUG__
    BoolSetting mForcePackSwitching = BoolSetting("Force Pack Switching", "Allows pack switching in-game", false);
#endif
    //BoolSetting mShowRotations = BoolSetting("Show Rotations", "Shows normally invisible server-sided rotations", false);


    Interface() : ModuleBase("Interface", "Customize the visuals!", ModuleCategory::Visual, 0, true) {
        gFeatureManager->mDispatcher->listen<ModuleStateChangeEvent, &Interface::onModuleStateChange, nes::event_priority::FIRST>(this);
        gFeatureManager->mDispatcher->listen<RenderEvent, &Interface::onRenderEvent, nes::event_priority::NORMAL>(this);
        gFeatureManager->mDispatcher->listen<ActorRenderEvent, &Interface::onActorRenderEvent, nes::event_priority::NORMAL>(this);
        gFeatureManager->mDispatcher->listen<BaseTickEvent, &Interface::onBaseTickEvent>(this);
        gFeatureManager->mDispatcher->listen<PacketOutEvent, &Interface::onPacketOutEvent, nes::event_priority::ABSOLUTE_LAST>(this);
        gFeatureManager->mDispatcher->listen<DrawImageEvent, &Interface::onDrawImageEvent>(this);
        gFeatureManager->mDispatcher->listen<PreGameCheckEvent, &Interface::onPregameCheckEvent>(this);

        addSettings(
            &mNamingStyle,
            &mMode,
            &mFont,
            &mMainColor,
            &mColorSpeed,
            &mSaturation,
            &mHudBlur,
            &mHudBlurStrength,
            //&mHoveredItem,
            &mSlotEasing,
            &mSlotEasingSpeed
#ifdef __DEBUG__
            ,&mForcePackSwitching
#endif
        );

        VISIBILITY_CONDITION(mMainColor, mMode.mValue == Custom);
        VISIBILITY_CONDITION(mHudBlurStrength, mHudBlur.mValue);

        VISIBILITY_CONDITION(mSlotEasingSpeed, mSlotEasing.mValue);

        mNames = {
            {Lowercase, "interface"},
            {LowercaseSpaced, "interface"},
            {Normal, "Interface"},
            {NormalSpaced, "Interface"}
        };
    }

    static inline std::unordered_map<int, std::vector<ImColor>> ColorThemes = {
        {Trans,     {
            ImColor(91, 206, 250, 255),
            ImColor(245, 169, 184, 255),
            ImColor(255, 255, 255, 255),
            ImColor(245, 169, 184, 255),
    }},
{Rainbow,   {}},
{Bubblegum, {
    ImColor(255, 99, 202, 255),
    ImColor(255, 195, 195, 255),
    ImColor(146, 245, 255, 255),
    ImColor(249, 255, 148, 255),
    ImColor(135, 255, 176, 255),
}},
{Watermelon, {
    ImColor(255, 70, 70, 255),
    ImColor(139, 0, 0, 255),
    ImColor(144, 238, 144, 255),
    ImColor(34, 139, 34, 255),
    ImColor(204, 255, 204, 255),
}},
{Sunset, {
    ImColor(213,32,0, 255),
    ImColor(239, 118, 39, 255),
    ImColor(255, 154, 86, 255),
    ImColor(255, 255, 255, 255),
    ImColor(209, 98, 164, 255),
    ImColor(181, 86, 144, 255),
}},
{Poison, {
    ImColor(115,222,70, 255),
    ImColor(67, 201, 89, 255),
    ImColor(41, 230, 94, 255),
    ImColor(12, 210, 83, 255),
    ImColor(87, 211, 72, 255),
    ImColor(57, 210, 124, 255),
}},
{Custom,    {}}
    };

    std::vector<ImColor> getCustomColors() {
        std::vector<ImColor> result;
        result.push_back(mMainColor.getAsImColor());
        return result;
    }

    void onEnable() override;
    void onDisable() override;
    void renderHoverText();
    void onModuleStateChange(ModuleStateChangeEvent& event);
    void onPregameCheckEvent(class PreGameCheckEvent& event);
    void onRenderEvent(class RenderEvent& event);
    void onActorRenderEvent(class ActorRenderEvent& event);
    void onDrawImageEvent(class DrawImageEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
};

class BodyYaw
{
public:
    static inline float bodyYaw = 0.f;
    static inline glm::vec3 posOld = glm::vec3(0, 0, 0);
    static inline glm::vec3 pos = glm::vec3(0, 0, 0);

    static inline void updateRenderAngles(Actor* plr, float headYaw)
    {
        posOld = pos;
        pos = *plr->getPos();

        float diffX = pos.x - posOld.x;
        float diffZ = pos.z - posOld.z;
        float diff = diffX * diffX + diffZ * diffZ;

        float body = bodyYaw;
        if (diff > 0.0025000002F)
        {
            float anglePosDiff = atan2f(diffZ, diffX) * 180.f / 3.14159265358979323846f - 90.f;
            float degrees = abs(wrapAngleTo180_float(headYaw) - anglePosDiff);
            if (95.f < degrees && degrees < 265.f)
            {
                body = anglePosDiff - 180.f;
            }
            else
            {
                body = anglePosDiff;
            }
        }

        turnBody(body, headYaw);
    };

    static inline void turnBody(float bodyRot, float headYaw)
    {
        float amazingDegreeDiff = wrapAngleTo180_float(bodyRot - bodyYaw);
        bodyYaw += amazingDegreeDiff * 0.3f;
        float bodyDiff = wrapAngleTo180_float(headYaw - bodyYaw);
        if (bodyDiff < -75.f)
            bodyDiff = -75.f;

        if (bodyDiff >= 75.f)
            bodyDiff = 75.f;

        bodyYaw = headYaw - bodyDiff;
        if (bodyDiff * bodyDiff > 2500.f)
        {
            bodyYaw += bodyDiff * 0.2f;
        }
    };

    static inline float wrapAngleTo180_float(float value)
    {
        value = fmodf(value, 360.f);

        if (value >= 180.0F)
        {
            value -= 360.0F;
        }

        if (value < -180.0F)
        {
            value += 360.0F;
        }

        return value;
    };
};
