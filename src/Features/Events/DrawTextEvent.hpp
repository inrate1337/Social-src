#pragma once
#include "Event.hpp"
#include <SDK/Minecraft/Rendering/MinecraftUIRenderContext.hpp>
struct DrawTextEvent : public Event {
    MinecraftUIRenderContext* mContext;
    Font* mFont;
    RectangleArea* mPos;
    std::string* mText;
    MCCColor* mColor;
    float mAlpha;
    ui::TextAlignment mTextAlignment;
    const TextMeasureData* mTextMeasureData;
    const CaretMeasureData* mCaretMeasureData;
    explicit DrawTextEvent(MinecraftUIRenderContext* ctx, Font* font, RectangleArea* pos, std::string* text, MCCColor* color, float alpha,
        ui::TextAlignment textAlignment, const TextMeasureData* textMeasureData, const CaretMeasureData* caretMeasureData)
        : mContext(ctx),
          mFont(font),
          mPos(pos),
          mText(text),
          mColor(color),
          mAlpha(alpha),
          mTextAlignment(textAlignment),
          mTextMeasureData(textMeasureData),
          mCaretMeasureData(caretMeasureData) {}
};