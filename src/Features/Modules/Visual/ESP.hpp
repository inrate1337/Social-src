#pragma once

#include <Features/Modules/Module.hpp>
#include <imgui.h>

class ESP : public ModuleBase<ESP>
{
public:
    BoolSetting mRenderFilled = BoolSetting("Render Filled", "Whether or not to render the ESP filled.", false);
    BoolSetting mRenderLocal = BoolSetting("Render Local", "Whether or not to render the ESP on the local player.", false);
    BoolSetting mShowFriends = BoolSetting("Show Friends", "Whether or not to render the ESP on friends.", true);
    BoolSetting mShowBox = BoolSetting("2D Box", "Whether or not to render the 2D box.", true);
    BoolSetting mShowName = BoolSetting("Name", "Whether or not to render player name.", true);
    BoolSetting mShowDistance = BoolSetting("Distance", "Whether or not to render player distance.", true);
    BoolSetting mShowHealth = BoolSetting("Health Bar", "Whether or not to render health bar.", true);
    BoolSetting mShowItems = BoolSetting("Items", "Whether or not to render armor/hand items.", true);

    EnumSetting mNameAnchor = EnumSetting("Name Anchor", "Where to anchor the name around the box.", 0, { "Top", "Bottom", "Left", "Right" });
    EnumSetting mDistanceAnchor = EnumSetting("Distance Anchor", "Where to anchor the distance around the box.", 1, { "Top", "Bottom", "Left", "Right" });
    EnumSetting mHealthAnchor = EnumSetting("Health Anchor", "Where to anchor the health bar around the box.", 0, { "Left", "Right" });
    EnumSetting mItemsAnchor = EnumSetting("Items Anchor", "Where to anchor the items around the box.", 0, { "Top", "Bottom", "Left", "Right" });

    NumberSetting mNameOffsetX = NumberSetting("Name Offset X", "Name X offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    NumberSetting mNameOffsetY = NumberSetting("Name Offset Y", "Name Y offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    NumberSetting mDistanceOffsetX = NumberSetting("Distance Offset X", "Distance X offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    NumberSetting mDistanceOffsetY = NumberSetting("Distance Offset Y", "Distance Y offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    NumberSetting mHealthOffsetX = NumberSetting("Health Offset X", "Health bar X offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    NumberSetting mHealthOffsetY = NumberSetting("Health Offset Y", "Health bar Y offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    NumberSetting mItemsOffsetX = NumberSetting("Items Offset X", "Items X offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    NumberSetting mItemsOffsetY = NumberSetting("Items Offset Y", "Items Y offset in pixels.", 0.0f, -300.0f, 300.0f, 1.0f);
    BoolSetting mDistanceLimited = BoolSetting("Distance Limited", "Whether to only show players within a certain distance", true);
    NumberSetting mDistance = NumberSetting("Distance", "The distance to show players within", 100.f, 0.f, 200.f, 1.f);

    ESP() : ModuleBase("ESP", "Draws 2D boxes around players", ModuleCategory::Visual, 0, false)
    {
        addSettings(
            &mRenderFilled,
            &mRenderLocal,
            &mShowFriends,
            &mShowBox,
            &mShowName,
            &mShowDistance,
            &mShowHealth,
            &mShowItems,
            &mNameAnchor,
            &mDistanceAnchor,
            &mHealthAnchor,
            &mItemsAnchor,
            &mNameOffsetX,
            &mNameOffsetY,
            &mDistanceOffsetX,
            &mDistanceOffsetY,
            &mHealthOffsetX,
            &mHealthOffsetY,
            &mItemsOffsetX,
            &mItemsOffsetY,
            &mDistanceLimited,
            &mDistance
        );

        VISIBILITY_CONDITION(mDistance, mDistanceLimited.mValue);
        VISIBILITY_CONDITION(mNameOffsetX, mShowName.mValue);
        VISIBILITY_CONDITION(mNameOffsetY, mShowName.mValue);
        VISIBILITY_CONDITION(mNameAnchor, mShowName.mValue);
        VISIBILITY_CONDITION(mDistanceOffsetX, mShowDistance.mValue);
        VISIBILITY_CONDITION(mDistanceOffsetY, mShowDistance.mValue);
        VISIBILITY_CONDITION(mDistanceAnchor, mShowDistance.mValue);
        VISIBILITY_CONDITION(mHealthOffsetX, mShowHealth.mValue);
        VISIBILITY_CONDITION(mHealthOffsetY, mShowHealth.mValue);
        VISIBILITY_CONDITION(mHealthAnchor, mShowHealth.mValue);
        VISIBILITY_CONDITION(mItemsOffsetX, mShowItems.mValue);
        VISIBILITY_CONDITION(mItemsOffsetY, mShowItems.mValue);
        VISIBILITY_CONDITION(mItemsAnchor, mShowItems.mValue);

        mNames = {
            {Lowercase, "esp"},
            {LowercaseSpaced, "esp"},
            {Normal, "ESP"},
            {NormalSpaced, "ESP"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onRenderEvent(class RenderEvent& event);
    void onNametagRenderEvent(class NametagRenderEvent& event);
};
