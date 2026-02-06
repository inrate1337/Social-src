//
// Created by vastrakai on 8/10/2024.
//

#include "Nametags.hpp"

#include <Features/Events/NametagRenderEvent.hpp>
#include <Features/IRC/IrcClient.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/MinecraftSim.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/FlagComponent.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <Utils/SysUtils/Base64.hpp>
#include <Utils/MemUtils.hpp>
#include "stb_image.h"

void Nametags::onEnable()
{
    gFeatureManager->mDispatcher->listen<RenderEvent, &Nametags::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Nametags::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<NametagRenderEvent, &Nametags::onNametagRenderEvent>(this);
}
void Nametags::onDisable()
{
    gFeatureManager->mDispatcher->deafen<RenderEvent, &Nametags::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Nametags::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<NametagRenderEvent, &Nametags::onNametagRenderEvent>(this);

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;
}
std::mutex bpsMutex;
std::unordered_map<int64_t, float> bpsMap;
std::unordered_map<int64_t, std::map<int64_t, float>> bpsHistory;
std::unordered_map<int64_t, float> avgBpsMap;

void Nametags::onBaseTickEvent(BaseTickEvent& event)
{
    std::lock_guard<std::mutex> lock(bpsMutex);

    auto actors = ActorUtils::getActorList(true, true);
    auto localPlayer = ClientInstance::get()->getLocalPlayer();
    for (auto actor : actors)
    {
        if (!ActorUtils::isActorValid(actor)) continue;
        bool isPlayer = false;
        if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) continue;
        if (!isPlayer) continue;
        if (actor == localPlayer && !mRenderLocal.mValue) continue;
        AABBShapeComponent* shape = nullptr;
        if (!TryCallWrapper([&]() { shape = actor->getAABBShapeComponent(); })) continue;
        if (!shape) continue;
        RenderPositionComponent* posComp = nullptr;
        if (!TryCallWrapper([&]() { posComp = actor->getRenderPositionComponent(); })) continue;
        if (!posComp) continue;
        int64_t runtimeId = 0;
        if (!TryCallWrapper([&]() { runtimeId = actor->getRuntimeID(); })) continue;
        if (runtimeId <= 0) continue;

        static std::unordered_map<int64_t, glm::vec3> prevPosMap;
        glm::vec3* posPtr = nullptr;
        if (!TryCallWrapper([&]() { posPtr = actor->getPos(); })) continue;
        if (!posPtr) continue;
        const glm::vec3 p = *posPtr;
        const glm::vec3 prevPos = prevPosMap.contains(runtimeId) ? prevPosMap[runtimeId] : p;
        prevPosMap[runtimeId] = p;

        glm::vec2 posxz = { p.x, p.z };
        glm::vec2 prevPosxz = { prevPos.x, prevPos.z };
        float bps = glm::distance(posxz, prevPosxz) * (ClientInstance::get()->getMinecraftSim()->getSimTimer() * ClientInstance::get()->getMinecraftSim()->getSimSpeed());
        if (!bpsHistory.contains(runtimeId)) bpsHistory[runtimeId] = {};

        std::map<int64_t, float>& history = bpsHistory[runtimeId];
        history[NOW] = bps;
        // Remove entries from more than 1 second ago
        for (auto it = history.begin(); it != history.end();)
        {
            if (NOW - it->first > 1000) it = history.erase(it);
            else ++it;
        }

        float total = 0.f;
        int count = 0;
        for (auto it = history.begin(); it != history.end(); ++it)
        {
            total += it->second;
            count++;
        }
        avgBpsMap[runtimeId] = total / count;
        bpsMap[runtimeId] = bps;
    }
}

static float getActorBps(bool avg, int64_t runtimeId) {
    std::lock_guard<std::mutex> lock(bpsMutex);

    if (avg) return avgBpsMap.contains(runtimeId) ? avgBpsMap[runtimeId] : 0;
    return bpsMap.contains(runtimeId) ? bpsMap[runtimeId] : 0;
}

void Nametags::onRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci->getLevelRenderer()) return;

    auto actors = ActorUtils::getActorList(true, true);
    std::ranges::sort(actors, [&](Actor* a, Actor* b) {
        if (!ActorUtils::isActorValid(a) || !ActorUtils::isActorValid(b)) return false;
        RenderPositionComponent* aPosComp = nullptr;
        RenderPositionComponent* bPosComp = nullptr;
        if (!TryCallWrapper([&]() { aPosComp = a->getRenderPositionComponent(); })) return false;
        if (!TryCallWrapper([&]() { bPosComp = b->getRenderPositionComponent(); })) return false;
        if (!aPosComp || !bPosComp) return false;
        auto aPos = aPosComp->mPosition;
        auto bPos = bPosComp->mPosition;
        auto origin = RenderUtils::transform.mOrigin;
        return glm::distance(origin, aPos) > glm::distance(origin, bPos);
        });

    auto drawList = ImGui::GetBackgroundDrawList();

    auto localPlayer = ci->getLocalPlayer();

    for (auto actor : actors)
    {
        if (!ActorUtils::isActorValid(actor)) continue;
        bool isPlayer = false;
        if (!TryCallWrapper([&]() { isPlayer = actor->isPlayer(); })) continue;
        if (!isPlayer) continue;
        int64_t runtimeId = 0;
        if (!TryCallWrapper([&]() { runtimeId = actor->getRuntimeID(); })) continue;
        if (runtimeId <= 0) continue;
        if (actor == localPlayer && ci->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) continue;
        if (actor == localPlayer && !mRenderLocal.mValue) continue;
        AABBShapeComponent* shape = nullptr;
        if (!TryCallWrapper([&]() { shape = actor->getAABBShapeComponent(); })) continue;
        if (!shape) continue;
        RenderPositionComponent* posComp = nullptr;
        if (!TryCallWrapper([&]() { posComp = actor->getRenderPositionComponent(); })) continue;
        if (!posComp) continue;

        float bps = getActorBps(false, runtimeId);
        std::string formattedBps = fmt::format("{:.2f}", bps);
        float avgBps = getActorBps(true, runtimeId);
        std::string formattedAvgBps = fmt::format("{:.2f}", avgBps);


        auto themeColor = ImColor(1.f, 1.f, 1.f, 1.f); //ColorUtils::getThemedColor(0);

        if (gFriendManager->isFriend(actor))
        {
            if (mShowFriends.mValue) themeColor = ImColor(0.0f, 1.0f, 0.0f);
            else continue;
        }

        glm::vec3 renderPos = posComp->mPosition;
        if (actor == localPlayer) renderPos = RenderUtils::transform.mPlayerPos;
        renderPos.y += 0.5f;

        glm::vec3 origin = RenderUtils::transform.mOrigin;
        glm::vec2 screen = glm::vec2(0, 0);

        if (!RenderUtils::transform.mMatrix.OWorldToScreen(origin, renderPos, screen, ci->getFov(), ci->getGuiData()->mResolution)) continue;
        if (std::isnan(screen.x) || std::isnan(screen.y)) continue;
        if (screen.x < 0 || screen.y < 0 || screen.x > ci->getGuiData()->mResolution.x * 2 || screen.y > ci->getGuiData()->mResolution.y * 2) continue;


        float fontSize = mFontSize.mValue;
        float padding = 5.f;

        if (mDistanceScaledFont.mValue)
        {
            // use distance to origin, not actor
            float distance = glm::distance(origin, renderPos) + 2.5f;
            if (distance < 0) distance = 0;
            fontSize = 1.0f / distance * 100.0f * mScalingMultiplier.mValue;
            if (fontSize < 1.0f) fontSize = 1.0f;
            if (fontSize < mMinScale.mValue) fontSize = mMinScale.mValue;
            padding = fontSize / 4;
        }

        FontHelper::pushPrefFont(true);

        std::string name = actor->getRawName();
        std::string ircAvatarKey;
        std::string ircDisplayUser;
        bool isIrcUser = false;

        if (actor == localPlayer)
        {
            name = actor->getNameTag();
            // Remove everything after the first newline
            name = name.substr(0, name.find('\n'));
            name = ColorUtils::removeColorCodes(name);
        }

        if (mShowIrcUsers.mValue && IrcManager::mClient) {
            auto ircUsers = IrcManager::mClient->getConnectedUsers();
            for (const auto& user : ircUsers) {
                if (!user.playerName.empty() && name.contains(user.playerName)) {
                    ircAvatarKey = user.username;
                    ircDisplayUser = user.username;
                    name = user.username + " (" + user.playerName + ")";
                    isIrcUser = true;
                    break;
                }
            }
        }

        if (mShowBps.mValue)
        {
            if (mAverageBps.mValue)
            {
                name += " [" + formattedAvgBps + "]";
            }
            else
            {
                name += " [" + formattedBps + "]";
            }
        }

        ImVec2 imFontSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0, name.c_str());
        ImVec2 pos = ImVec2(screen.x - imFontSize.x / 2, screen.y - imFontSize.y - 5);

        ImVec2 rectMin = ImVec2(pos.x - padding, pos.y - padding);
        ImVec2 rectMax = ImVec2(pos.x + imFontSize.x + padding, pos.y + imFontSize.y + padding);

        float rounding = 10.f;
        if (isIrcUser)
        {
            float avatarD = std::max(14.0f, std::min(24.0f, fontSize * 1.05f));
            float avatarR = avatarD * 0.5f;
            float avatarGap = std::max(4.0f, avatarD * 0.30f);
            rectMin.x -= (avatarD + avatarGap);
        }

        if (mBlurStrength.mValue == 0.0f)
        {
            ImColor bg = isIrcUser ? ImColor(0.0f, 0.55f, 0.0f, 0.55f) : ImColor(0.0f, 0.0f, 0.0f, 0.5f);
            drawList->AddRectFilled(rectMin, rectMax, bg, rounding);
        }

        ImRenderUtils::addBlur(ImVec4(rectMin.x, rectMin.y, rectMax.x, rectMax.y), mBlurStrength.mValue, rounding, drawList, true);

        if (isIrcUser && IrcManager::mClient && !ircAvatarKey.empty())
        {
            struct AvatarEntry
            {
                std::string b64;
                ID3D11ShaderResourceView* srv = nullptr;
                int w = 0;
                int h = 0;
            };
            static std::unordered_map<std::string, AvatarEntry> avatarCache;

            std::string avatarB64 = IrcManager::mClient->getUserAvatarB64(ircAvatarKey);
            auto& entry = avatarCache[ircAvatarKey];
            if (!avatarB64.empty() && avatarB64 != entry.b64)
            {
                auto bytes = Base64::decodeBytes(avatarB64);
                if (!bytes.empty())
                {
                    int w = 0, h = 0, ch = 0;
                    unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4);
                    if (rgba && w > 0 && h > 0)
                    {
                        ID3D11ShaderResourceView* srv = nullptr;
                        D3DHook::createTextureFromData(rgba, w, h, &srv);
                        if (srv)
                        {
                            if (entry.srv) entry.srv->Release();
                            entry.srv = srv;
                            entry.w = w;
                            entry.h = h;
                            entry.b64 = avatarB64;
                        }
                    }
                    if (rgba) stbi_image_free(rgba);
                }
            }

            float avatarD = std::max(14.0f, std::min(24.0f, fontSize * 1.05f));
            float avatarR = avatarD * 0.5f;
            float avatarGap = std::max(4.0f, avatarD * 0.30f);
            ImVec2 avMin = ImVec2(rectMin.x + avatarGap, rectMin.y + (rectMax.y - rectMin.y - avatarD) * 0.5f);
            ImVec2 avMax = ImVec2(avMin.x + avatarD, avMin.y + avatarD);
            ImVec2 avC = ImVec2((avMin.x + avMax.x) * 0.5f, (avMin.y + avMax.y) * 0.5f);

            drawList->AddCircleFilled(avC, avatarR, ImColor(0, 0, 0, 80), 32);
            if (entry.srv)
            {
                drawList->AddImageRounded((ImTextureID)entry.srv, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255), avatarR);
            }
            else
            {
                std::string initials = !ircDisplayUser.empty() ? std::string(1, ircDisplayUser[0]) : "?";
                ImColor ic = ImColor(1.f, 1.f, 1.f, 1.f);
                ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fontSize * 0.75f, FLT_MAX, 0, initials.c_str());
                ImVec2 tp = ImVec2(avC.x - ts.x * 0.5f, avC.y - ts.y * 0.5f);
                drawList->AddText(ImGui::GetFont(), fontSize * 0.75f, tp, ic, initials.c_str());
            }
            drawList->AddCircle(avC, avatarR, ImColor(255, 255, 255, 40), 32, 1.0f);
        }

        drawList->AddText(ImGui::GetFont(), fontSize, pos, themeColor, name.c_str());

        FontHelper::popPrefFont();
    }
}

void Nametags::onNametagRenderEvent(NametagRenderEvent& event)
{
    auto actor = event.mActor;
    auto localPlayer = ClientInstance::get()->getLocalPlayer();
    auto ci = ClientInstance::get();

    if (ActorUtils::isBot(actor)) return;
    if (!actor->isPlayer()) return;
    if (actor == localPlayer && ci->getOptions()->mThirdPerson->value == 0 && !localPlayer->getFlag<RenderCameraComponent>()) return;
    if (actor == localPlayer && !mRenderLocal.mValue) return;
    auto shape = actor->getAABBShapeComponent();
    if (!shape) return;
    auto posComp = actor->getRenderPositionComponent();
    if (!posComp) return;

    glm::vec3 renderPos = posComp->mPosition;
    if (actor == localPlayer) renderPos = RenderUtils::transform.mPlayerPos;
    renderPos.y += 0.5f;

    glm::vec3 origin = RenderUtils::transform.mOrigin;
    glm::vec2 screen = glm::vec2(0, 0);

    if (!RenderUtils::transform.mMatrix.OWorldToScreen(origin, renderPos, screen, ci->getFov(), ci->getGuiData()->mResolution)) return;
    if (std::isnan(screen.x) || std::isnan(screen.y)) return;
    if (screen.x < 0 || screen.y < 0 || screen.x > ci->getGuiData()->mResolution.x * 2 || screen.y > ci->getGuiData()->mResolution.y * 2) return;

    event.cancel();
}
