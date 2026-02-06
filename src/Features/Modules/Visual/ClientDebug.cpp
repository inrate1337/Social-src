#include "ClientDebug.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/RenderEvent.hpp>

#include <Hook/Hooks/RenderHooks/D3DHook.hpp>

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>

#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/MemUtils.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    struct TrackedActor
    {
        int64_t id = 0;
        uintptr_t ptr = 0;
        std::string name;
        bool isValid = false;
        bool isPlayer = false;
        float distance = 0.f;
        uint64_t firstSeenMs = 0;
        uint64_t lastSeenMs = 0;
        uint32_t seenCount = 0;
        bool seenThisTick = false;
    };

    struct RemovedActor
    {
        int64_t id = 0;
        uintptr_t ptr = 0;
        std::string name;
        uint64_t removedMs = 0;
    };

    static int64_t makeFallbackId(Actor* actor)
    {
        auto p = reinterpret_cast<uintptr_t>(actor);
        return -static_cast<int64_t>(p == 0 ? 1 : (p & 0x7FFFFFFFFFFFFFFF));
    }

    static uint64_t nowMs()
    {
        return NOW;
    }
}

static std::unordered_map<int64_t, TrackedActor> gTrackedActors;
static std::deque<RemovedActor> gRecentlyRemoved;
static uint64_t gLastTickMs = 0;
static float gLastTickCostMs = 0.f;
static float gLastRenderCostMs = 0.f;

void ClientDebug::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &ClientDebug::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &ClientDebug::onRenderEvent>(this);
}

void ClientDebug::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &ClientDebug::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &ClientDebug::onRenderEvent>(this);
    gTrackedActors.clear();
    gRecentlyRemoved.clear();
    gLastTickMs = 0;
    gLastTickCostMs = 0.f;
    gLastRenderCostMs = 0.f;
}

void ClientDebug::onBaseTickEvent(BaseTickEvent& event)
{
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto ci = ClientInstance::get();
    if (!ci)
        return;

    auto localPlayer = event.mActor ? event.mActor : ci->getLocalPlayer();
    const uint64_t now = nowMs();
    gLastTickMs = now;

    for (auto& [_, entry] : gTrackedActors)
        entry.seenThisTick = false;

    auto actors = ActorUtils::getActorList(true, true);
    for (auto actor : actors)
    {
        if (!actor)
            continue;
        if (!ActorUtils::isActorValid(actor))
            continue;

        int64_t rid = actor->getRuntimeID();
        if (rid <= 0)
            rid = makeFallbackId(actor);

        auto& tr = gTrackedActors[rid];
        if (tr.firstSeenMs == 0)
            tr.firstSeenMs = now;

        tr.id = rid;
        tr.ptr = reinterpret_cast<uintptr_t>(actor);
        tr.isValid = true;
        tr.isPlayer = false;
        TryCallWrapper([&]() { tr.isPlayer = actor->isPlayer(); });
        tr.name.clear();
        if (tr.isPlayer) TryCallWrapper([&]() { tr.name = actor->getRawName(); });
        else TryCallWrapper([&]() { tr.name = actor->getNameTag(); });
        tr.lastSeenMs = now;
        tr.seenCount += 1;
        tr.seenThisTick = true;
        tr.distance = (localPlayer && actor != localPlayer) ? localPlayer->distanceTo(actor) : 0.f;
    }

    for (auto it = gTrackedActors.begin(); it != gTrackedActors.end();)
    {
        if (!it->second.seenThisTick)
        {
            gRecentlyRemoved.push_front({
                it->second.id,
                it->second.ptr,
                it->second.name,
                now
            });
            it = gTrackedActors.erase(it);
            continue;
        }
        ++it;
    }

    while (gRecentlyRemoved.size() > 200)
        gRecentlyRemoved.pop_back();

    const auto t1 = std::chrono::high_resolution_clock::now();
    gLastTickCostMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

void ClientDebug::onRenderEvent(RenderEvent& event)
{
    const auto t0 = std::chrono::high_resolution_clock::now();

    if (!ImGui::GetCurrentContext())
        return;

    auto ci = ClientInstance::get();
    auto mm = gFeatureManager ? gFeatureManager->mModuleManager : nullptr;

    ImGui::SetNextWindowSize(ImVec2(720.f, 520.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Client Debug", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const float dt = ImGui::GetIO().DeltaTime;
    const float fps = dt > 0.f ? (1.f / dt) : 0.f;

    ImGui::Text("FPS: %.1f | Delta: %.3f ms", fps, dt * 1000.f);
    ImGui::Text("BaseTick: %.3f ms | Render: %.3f ms", gLastTickCostMs, gLastRenderCostMs);
    ImGui::Separator();

    const bool hasCI = ci != nullptr;
    const bool hasLevelRenderer = hasCI && ci->getLevelRenderer();
    const bool mouseGrabbed = hasCI && ci->getMouseGrabbed();
    int thirdPersonState = 0;
    if (hasCI)
        if (auto opts = ci->getOptions())
            thirdPersonState = opts->mThirdPerson->value;

    ImGui::Text("ClientInstance: 0x%p | LevelRenderer: %s | MouseGrabbed: %s | CamState: %d",
        ci,
        hasLevelRenderer ? "yes" : "no",
        mouseGrabbed ? "yes" : "no",
        thirdPersonState
    );
    ImGui::Text("Transform Origin: (%.2f, %.2f, %.2f) | PlayerPos: (%.2f, %.2f, %.2f) | Fov: (%.3f, %.3f)",
        RenderUtils::transform.mOrigin.x, RenderUtils::transform.mOrigin.y, RenderUtils::transform.mOrigin.z,
        RenderUtils::transform.mPlayerPos.x, RenderUtils::transform.mPlayerPos.y, RenderUtils::transform.mPlayerPos.z,
        RenderUtils::transform.mFov.x, RenderUtils::transform.mFov.y
    );

    const bool hasQueue = D3DHook::FrameTransforms != nullptr;
    size_t queueSize = 0;
    if (hasQueue)
        queueSize = D3DHook::FrameTransforms->size();
    ImGui::Separator();

    if (mShowModules.mValue && mm)
    {
        if (ImGui::CollapsingHeader("Modules", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto& modules = mm->getModules();
            int enabledCount = 0;
            for (const auto& mod : modules)
                if (mod && mod->mEnabled)
                    enabledCount++;

            ImGui::Text("Total: %d | Enabled: %d", static_cast<int>(modules.size()), enabledCount);

            if (ImGui::BeginTable("modules_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Category");
                ImGui::TableSetupColumn("Enabled");
                ImGui::TableSetupColumn("Wanted");
                ImGui::TableSetupColumn("Key");
                ImGui::TableHeadersRow();

                for (const auto& mod : modules)
                {
                    if (!mod)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(mod->mName.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(mod->getCategory().c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(mod->mEnabled ? "true" : "false");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(mod->mWantedState ? "true" : "false");
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", mod->mKey);
                }

                ImGui::EndTable();
            }
        }
    }

    if (mShowActors.mValue)
    {
        if (ImGui::CollapsingHeader("Actors", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int total = 0;
            int players = 0;
            int invalid = 0;
            for (const auto& [_, a] : gTrackedActors)
            {
                total++;
                if (a.isPlayer) players++;
                if (!a.isValid) invalid++;
            }

            ImGui::Text("Tracked: %d | Players: %d | Invalid: %d | Removed(log): %d",
                total,
                players,
                invalid,
                static_cast<int>(gRecentlyRemoved.size())
            );

            if (ImGui::BeginTable("actors_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("RID");
                ImGui::TableSetupColumn("Ptr");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Player");
                ImGui::TableSetupColumn("Valid");
                ImGui::TableSetupColumn("Dist");
                ImGui::TableSetupColumn("Seen(ms ago)");
                ImGui::TableHeadersRow();

                std::vector<const TrackedActor*> list;
                list.reserve(gTrackedActors.size());
                for (const auto& [_, a] : gTrackedActors)
                {
                    if (mOnlyPlayers.mValue && !a.isPlayer)
                        continue;
                    list.push_back(&a);
                }

                std::sort(list.begin(), list.end(), [](const TrackedActor* a, const TrackedActor* b) {
                    return a->distance < b->distance;
                });

                const int maxActors = static_cast<int>(mMaxActors.mValue);
                const int drawCount = std::min<int>(maxActors, static_cast<int>(list.size()));
                const uint64_t now = nowMs();

                for (int i = 0; i < drawCount; i++)
                {
                    const auto& a = *list[i];
                    const uint64_t age = now >= a.lastSeenMs ? (now - a.lastSeenMs) : 0;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%" PRId64, a.id);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%p", reinterpret_cast<void*>(a.ptr));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(a.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(a.isPlayer ? "true" : "false");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(a.isValid ? "true" : "false");
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f", a.distance);
                    ImGui::TableNextColumn();
                    ImGui::Text("%" PRIu64, age);
                }

                ImGui::EndTable();
            }

            if (ImGui::TreeNode("Removed actors log"))
            {
                const uint64_t now = nowMs();
                for (const auto& r : gRecentlyRemoved)
                {
                    const uint64_t ago = now >= r.removedMs ? (now - r.removedMs) : 0;
                    ImGui::Text("[%" PRIu64 "ms ago] RID=%" PRId64 " Ptr=0x%p Name=%s",
                        ago,
                        r.id,
                        reinterpret_cast<void*>(r.ptr),
                        r.name.c_str()
                    );
                }
                ImGui::TreePop();
            }
        }
    }

    ImGui::End();

    const auto t1 = std::chrono::high_resolution_clock::now();
    gLastRenderCostMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}
