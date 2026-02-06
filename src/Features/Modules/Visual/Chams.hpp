#pragma once

class Chams : public ModuleBase<Chams>
{
public:
    BoolSetting mRenderLocal = BoolSetting("Render Local", "Whether to render chams on the local player", false);
    BoolSetting mShowFriends = BoolSetting("Show Friends", "Whether to render chams on friends", true);
    BoolSetting mDistanceLimited = BoolSetting("Distance Limited", "Whether to only render within a distance", true);
    NumberSetting mDistance = NumberSetting("Distance", "Max distance to render", 100.f, 0.f, 300.f, 1.f);

    BoolSetting mUseInterfaceColor = BoolSetting("Use Interface Color", "Use interface accent color", true);
    ColorSetting mColor = ColorSetting("Color", "Chams color", 1.f, 0.f, 0.f, 0.9f);

    NumberSetting mThickness = NumberSetting("Thickness", "Line thickness", 2.25f, 0.5f, 8.f, 0.1f);
    NumberSetting mGlowStrength = NumberSetting("Glow Strength", "Glow strength", 6.f, 0.f, 16.f, 0.25f);

    Chams() : ModuleBase("Chams", "Draws a glow outline around players", ModuleCategory::Visual, 0, false)
    {
        addSettings(
            &mRenderLocal,
            &mShowFriends,
            &mDistanceLimited,
            &mDistance,
            &mUseInterfaceColor,
            &mColor,
            &mThickness,
            &mGlowStrength
        );

        VISIBILITY_CONDITION(mDistance, mDistanceLimited.mValue);
        VISIBILITY_CONDITION(mColor, !mUseInterfaceColor.mValue);

        mNames = {
            {Lowercase, "chams"},
            {LowercaseSpaced, "chams"},
            {Normal, "Chams"},
            {NormalSpaced, "Chams"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBoneRenderEvent(class BoneRenderEvent& event);
    void onBaseTickEvent(class BaseTickEvent& event);
    void onRenderEvent(class RenderEvent& event);
};
