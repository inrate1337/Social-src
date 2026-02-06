#pragma once
#include <mutex>
#include <vector>
#include <unordered_map>
#include <SDK/Minecraft/mce.hpp>
#include <glm/vec2.hpp>
class MinecraftUIRenderContext;
class Font;
struct RectangleArea {
    float x0;
    float y0;
    float x1;
    float y1;
};
struct MCCColor {
    float r;
    float g;
    float b;
    float a;
};
namespace ui {
    enum class TextAlignment : int {
        Left = 0,
        Center = 1,
        Right = 2
    };
}


//dont touch this code, this me try get a texture from data mce
struct TextMeasureData;
struct CaretMeasureData;
namespace SDK {
    struct DrawTextQueueEntry {
        Font* font = nullptr;
        RectangleArea rect{};
        std::string text{};
        MCCColor color{};
        float alpha = 1.0f;
        ui::TextAlignment alignment = ui::TextAlignment::Left;
        const TextMeasureData* textMeasureData = nullptr;
        const CaretMeasureData* caretMeasureData = nullptr;
    };
    inline std::vector<DrawTextQueueEntry> drawTextQueue2{};
    inline bool renderedText = false;
    inline void queueDrawText(Font* font, const RectangleArea& rect, std::string text, const MCCColor& color, float alpha, ui::TextAlignment alignment,
        const TextMeasureData* textMeasureData, const CaretMeasureData* caretMeasureData) {
        drawTextQueue2.push_back(DrawTextQueueEntry{
            .font = font,
            .rect = rect,
            .text = std::move(text),
            .color = color,
            .alpha = alpha,
            .alignment = alignment,
            .textMeasureData = textMeasureData,
            .caretMeasureData = caretMeasureData
        });
    }

    struct DrawImageQueueEntry {
        mce::TexturePtr texture{};
        glm::vec2 pos{};
        glm::vec2 size{};
        glm::vec2 uvPos{};
        glm::vec2 uvSize{};
        mce::Color color{};
    };

    inline std::mutex drawImageQueueMutex{};
    inline std::vector<DrawImageQueueEntry> drawImageQueue{};

    inline void queueDrawImage(mce::TexturePtr texture, const glm::vec2& pos, const glm::vec2& size, const glm::vec2& uvPos, const glm::vec2& uvSize, const mce::Color& color) {
        std::lock_guard<std::mutex> lock(drawImageQueueMutex);
        drawImageQueue.push_back(DrawImageQueueEntry{
            .texture = std::move(texture),
            .pos = pos,
            .size = size,
            .uvPos = uvPos,
            .uvSize = uvSize,
            .color = color
        });
    }

    inline std::mutex textureCacheMutex{};
    inline std::unordered_map<std::string, mce::TexturePtr> textureCache{};

    inline void cacheTextureIfNamed(const mce::TexturePtr& texture)
    {
        if (!texture.mTexture)
            return;

        const std::string& path = texture.mTexture->mFilePath;
        if (path.empty())
            return;

        std::lock_guard<std::mutex> lock(textureCacheMutex);
        if (!textureCache.contains(path))
            textureCache.emplace(path, texture);
    }

    inline bool tryGetCachedTexture(const std::string& path, mce::TexturePtr& out)
    {
        std::lock_guard<std::mutex> lock(textureCacheMutex);
        auto it = textureCache.find(path);
        if (it == textureCache.end())
            return false;
        out = it->second;
        return true;
    }
}
