//
// Created by vastrakai on 7/7/2024.
//

#include "SetupAndRenderHook.hpp"

#include <SDK/Minecraft/mce.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Rendering/LevelRenderer.hpp>
#include <SDK/Minecraft/Rendering/MinecraftUIRenderContext.hpp>
#include <Features/Events/DrawImageEvent.hpp>
#include <Features/Events/DrawTextEvent.hpp>

#include "D3DHook.hpp"

std::unique_ptr<Detour> SetupAndRenderHook::mSetupAndRenderDetour;
std::unique_ptr<Detour> SetupAndRenderHook::mDrawImageDetour;
std::unique_ptr<Detour> SetupAndRenderHook::mDrawTextDetour;

static thread_local bool gFlushedImageQueueThisFrame = false;
static thread_local bool gInImageFlush = false;

void* SetupAndRenderHook::onSetupAndRender(void* screenView, void* mcuirc)
{
    auto original = mSetupAndRenderDetour->getOriginal<&SetupAndRenderHook::onSetupAndRender>();

    static bool once = false;
    if (!once)
    {
        once = true;
        initVt(mcuirc);
    }

    SDK::renderedText = false;
    gFlushedImageQueueThisFrame = false;

    auto ci = ClientInstance::get();
    if (!ci) return original(screenView, mcuirc);

    auto player = ClientInstance::get()->getLocalPlayer();

    glm::vec3 origin = glm::vec3(0, 0, 0);
    glm::vec3 playerPos = glm::vec3(0, 0, 0);

    if (player && ci->getLevelRenderer())
    {
        origin = *ci->getLevelRenderer()->getRendererPlayer()->getCameraPos();
        playerPos = player->getRenderPositionComponent()->mPosition;
    }

    if (D3DHook::FrameTransforms) D3DHook::FrameTransforms->push({ ci->getViewMatrix(), origin, playerPos, ci->getFov() });

    return original(screenView, mcuirc);
}

void* SetupAndRenderHook::onDrawImage(void* context, mce::TexturePtr* texture, glm::vec2* pos, glm::vec2* size, glm::vec2* uv,
    mce::Color* color, void* unk)
{
    auto original = mDrawImageDetour->getOriginal<&SetupAndRenderHook::onDrawImage>();

    nes::event_holder<DrawImageEvent> holder = nes::make_holder<DrawImageEvent>(context, texture, pos, size, uv, color);
    gFeatureManager->mDispatcher->trigger(holder);
    if (holder->isCancelled()) return nullptr;

    if (texture)
        SDK::cacheTextureIfNamed(*texture);

    auto result = original(context, texture, pos, size, uv, color, unk);

    if (!gFlushedImageQueueThisFrame && !gInImageFlush)
    {
        gFlushedImageQueueThisFrame = true;
        int safety = 0;
        while (safety++ < 16)
        {
            std::vector<SDK::DrawImageQueueEntry> local;
            {
                std::lock_guard<std::mutex> lock(SDK::drawImageQueueMutex);
                if (SDK::drawImageQueue.empty())
                    break;
                local.swap(SDK::drawImageQueue);
            }

            gInImageFlush = true;
            for (auto& entry : local)
            {
                auto texCopy = entry.texture;
                auto posCopy = entry.pos;
                auto sizeCopy = entry.size;
                auto uvPosCopy = entry.uvPos;
                auto colorCopy = entry.color;
                auto uvSizeCopy = entry.uvSize;
                original(context, &texCopy, &posCopy, &sizeCopy, &uvPosCopy, &colorCopy, &uvSizeCopy);
            }
            gInImageFlush = false;
        }
        gInImageFlush = false;
    }

    return result;
}

void SetupAndRenderHook::onDrawText(MinecraftUIRenderContext* ctx, Font* font, RectangleArea* pos, std::string* text, MCCColor* color, float alpha,
    ui::TextAlignment textAlignment, const TextMeasureData* textMeasureData, const CaretMeasureData* caretMeasureData)
{
    auto original = mDrawTextDetour->getOriginal<&SetupAndRenderHook::onDrawText>();
    nes::event_holder<DrawTextEvent> holder = nes::make_holder<DrawTextEvent>(ctx, font, pos, text, color, alpha, textAlignment, textMeasureData, caretMeasureData);
    gFeatureManager->mDispatcher->trigger(holder);
    original(ctx, font, pos, text, color, alpha, textAlignment, textMeasureData, caretMeasureData);
    if (!SDK::renderedText) {
        SDK::renderedText = true;
        int safety = 0;
        while (!SDK::drawTextQueue2.empty() && safety++ < 8) {
            std::vector<SDK::DrawTextQueueEntry> local;
            local.swap(SDK::drawTextQueue2);
            for (auto& entry : local) {
                RectangleArea rectCopy = entry.rect;
                std::string textCopy = entry.text;
                MCCColor colorCopy = entry.color;
                original(ctx, entry.font ? entry.font : font, &rectCopy, &textCopy, &colorCopy, entry.alpha, entry.alignment, entry.textMeasureData, entry.caretMeasureData);
            }
        }
    }
}

void SetupAndRenderHook::initVt(void* ctx)
{
    const auto vtable = *static_cast<uintptr_t**>(ctx);
    mDrawImageDetour = std::make_unique<Detour>("MinecraftUIRenderContext::drawImage", reinterpret_cast<void*>(vtable[OffsetProvider::MinecraftUIRenderContext_drawImage]), &SetupAndRenderHook::onDrawImage);
    mDrawImageDetour->enable();

    mDrawTextDetour = std::make_unique<Detour>("MinecraftUIRenderContext::drawText", reinterpret_cast<void*>(vtable[OffsetProvider::MinecraftUIRenderContext_drawText]), &SetupAndRenderHook::onDrawText);
    mDrawTextDetour->enable();
}

void SetupAndRenderHook::init()
{
    mSetupAndRenderDetour = std::make_unique<Detour>("ScreenView::setupAndRender", reinterpret_cast<void*>(SigManager::ScreenView_setupAndRender), &SetupAndRenderHook::onSetupAndRender);
}
