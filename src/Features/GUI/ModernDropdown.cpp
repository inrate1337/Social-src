#include "ModernDropdown.hpp"
#include <Features/Autobuy/AutobuyController.hpp>
#include <Features/Modules/ModuleCategory.hpp>
#include <Features/Modules/Visual/ClickGui.hpp>
#include <Utils/FontHelper.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <Features/Modules/Setting.hpp>
#include <Features/Modules/Visual/Interface.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <Utils/Keyboard.hpp>
#include <Utils/StringUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/FileUtils.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <Utils/SysUtils/Base64.hpp>
#include <Features/Modules/Visual/ESP.hpp>
#include <Features/Modules/Visual/Animations.hpp>
#include <Features/Modules/Visual/TargetESP.hpp>
#include <Features/Modules/Misc/IRC.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <Features/IRC/IrcClient.hpp>
#include <Features/Configs/ConfigManager.hpp>
#include <Utils/MiscUtils/NotifyUtils.hpp>
#include "stb_image.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <cctype>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//тут локина синхрона так что не трогай вообще а то сломается все к хуям

static float finiteOr(float v, float fallback)
{
    return std::isfinite(v) ? v : fallback;
}

struct GuiTextureCacheEntry
{
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0;
    int h = 0;
    bool loaded = false;
};

struct LoadingGifCache
{
    std::vector<ID3D11ShaderResourceView*> frames;
    std::vector<uint8_t> pixels;
    std::vector<int> delays;
    std::vector<int> cumulative;
    int w = 0;
    int h = 0;
    int frameCount = 0;
    int total = 0;
};

static std::unordered_map<std::string, GuiTextureCacheEntry> gGuiTextures;
static LoadingGifCache gLoadingGif;
static std::mutex gLoadingGifMutex;
static std::atomic<bool> gLoadingGifReady{ false };
static std::atomic<bool> gLoadingGifDecoding{ false };
static std::mutex gPreloadMutex;
static std::vector<std::string> gPreloadKeys;
static std::atomic<bool> gPreloadReady{ false };
static std::atomic<bool> gPreloadStarted{ false };
static std::atomic<size_t> gPreloadIndex{ 0 };
static std::atomic<bool> gPreloadCompleted{ false };

static ID3D11ShaderResourceView* getGuiTexture(const char* name, int* w, int* h)
{
    auto& entry = gGuiTextures[name];
    if (!entry.loaded)
    {
        entry.loaded = D3DHook::loadTextureFromEmbeddedResource(name, &entry.srv, &entry.w, &entry.h);
    }
    if (w) *w = entry.w;
    if (h) *h = entry.h;
    return entry.srv;
}

static void preloadGuiTextures()
{
    static bool done = false;
    if (done) return;
    const char* names[] = { "find.png", "color.png", "notch.png", "steve.png", "bg.png", "nur.png", "keybinds.png", "kbsmell.png" };
    for (const char* n : names)
    {
        int w = 0;
        int h = 0;
        getGuiTexture(n, &w, &h);
    }
    done = true;
}

static void ensureLoadingGif()
{
    if (gLoadingGifReady.load()) return;
    if (gLoadingGifDecoding.exchange(true)) return;
    auto it = ResourceLoader::Resources.find("loading.gif");
    if (it == ResourceLoader::Resources.end()) { gLoadingGifDecoding.store(false); return; }
    const auto& res = it->second;
    if (res.data() == nullptr) { gLoadingGifDecoding.store(false); return; }
    std::string raw((const char*)res.data2(), (size_t)res.size());
    std::thread([raw = std::move(raw)]() mutable {
        int* delays = nullptr;
        int w = 0;
        int h = 0;
        int frames = 0;
        int comp = 0;
        auto* data = stbi_load_gif_from_memory(reinterpret_cast<const stbi_uc*>(raw.data()), (int)raw.size(), &delays, &w, &h, &frames, &comp, 4);
        if (!data || frames <= 0 || w <= 0 || h <= 0) {
            if (data) stbi_image_free(data);
            if (delays) stbi_image_free(delays);
            gLoadingGifDecoding.store(false);
            return;
        }
        std::vector<uint8_t> pixels;
        pixels.assign(data, data + (size_t)frames * w * h * 4);
        std::vector<int> dlys(frames, 100);
        std::vector<int> cumulative(frames, 0);
        int total = 0;
        for (int i = 0; i < frames; ++i)
        {
            int d = delays ? delays[i] : 100;
            if (d <= 0) d = 100;
            dlys[i] = d;
            total += d;
            cumulative[i] = total;
        }
        stbi_image_free(data);
        if (delays) stbi_image_free(delays);
        {
            std::lock_guard<std::mutex> lock(gLoadingGifMutex);
            gLoadingGif.w = w;
            gLoadingGif.h = h;
            gLoadingGif.frameCount = frames;
            gLoadingGif.pixels = std::move(pixels);
            gLoadingGif.frames.assign(frames, nullptr);
            gLoadingGif.delays = std::move(dlys);
            gLoadingGif.cumulative = std::move(cumulative);
            gLoadingGif.total = total;
        }
        gLoadingGifReady.store(true);
        gLoadingGifDecoding.store(false);
    }).detach(); //потом дофикшу
}

static void startPreloadKeyScan()
{
    if (gPreloadCompleted.load()) return;
    if (gPreloadStarted.exchange(true)) return;
    std::thread([] {
        std::vector<std::string> keys;
        keys.reserve(ResourceLoader::Resources.size());
        for (const auto& it : ResourceLoader::Resources)
        {
            if (!it.first.empty()) keys.push_back(it.first);
        }
        {
            std::lock_guard<std::mutex> lock(gPreloadMutex);
            gPreloadKeys.swap(keys);
        }
        gPreloadReady.store(true);
    }).detach();
}

void ModernGui::startPreload()
{
    if (gPreloadCompleted.load())
    {
        isInitialLoading = false;
        initialLoadStart = 0;
        return;
    }
    startPreloadKeyScan();
    isInitialLoading = true;
    if (initialLoadStart == 0) initialLoadStart = NOW;
}

void ModernGui::resetTransientState()
{
    std::scoped_lock<std::mutex> lk(uiMutex);
    lastMod = nullptr;
    isBinding = false;
    isBoolSettingBinding = false;
    lastBoolSetting = nullptr;
    lastColorSetting = nullptr;
    displayColorPicker = false;
    openEnumSetting = nullptr;
    enumDropdownAnims.clear();

    isSearching = false;
    searchWantsFocus = false;
    searchAnim = 0.0f;
    searchTextAnim = 0.0f;
    searchText[0] = 0;

    isConfigDropdownOpen = false;
    configDropdownAnim = 0.0f;
    configSelectedIndex = -1;
    selectedConfigName.clear();
    configNames.clear();
    configSearchText[0] = 0;
    configSearchWantsFocus = false;
    configSortLengthDesc = true;
    configSortAnim = 0.0f;
    configRowAnims.clear();
    configRowY.clear();
    configListScroll = 0.0f;
    configListScrollTarget = 0.0f;

    isThemePickerOpen = false;
    themePickerAnim = 0.0f;
    uiTheme = 0;

    colorPickerWidth = 180.0f;
    themePickerWidth = 180.0f;
    resizingColorPicker = false;
    colorPickerResizeStartMouse = ImVec2(0.0f, 0.0f);
    colorPickerResizeStartWidth = 0.0f;
    resizingThemePicker = false;
    themePickerResizeStartMouse = ImVec2(0.0f, 0.0f);
    themePickerResizeStartWidth = 0.0f;

    autobuySelectedIndex = 0;
    autobuyLastSelectedIndex = -1;
    autobuyPriceInput[0] = 0;
    autobuyQuantityInput = 1;
    autobuyQuantityInputBuf[0] = 0;

    isTargetEspPanelOpen = false;
    targetEspPanelAnim = 0.0f;
    isAnimationsPanelOpen = false;
    animationsPanelAnim = 0.0f;
    isTargetEspModeSwitching = false;
    isTargetEspModeFadingOut = false;
    targetEspModeFade = 1.0f;
    targetEspModeShown = -1;
    targetEspModeTarget = -1;
    espPreviewAnim = 0.0f;

    isIrcPanelOpen = false;
    ircPanelAnim = 0.0f;
    ircAutoScroll = true;
    ircInput[0] = 0;
    ircDmTargetKey.clear();
    ircDmTargetLabel.clear();
}

void ModernGui::refreshConfigList()
{
    configNames.clear();
    FileUtils::createDirectory(FileUtils::getSolsticeDir());
    FileUtils::createDirectory(ConfigManager::getConfigPath());
    for (const auto& f : FileUtils::listFiles(ConfigManager::getConfigPath()))
    {
        std::string file = f;
        if (file.ends_with(".cfg"))
        {
            file = file.substr(0, file.size() - 4);
        }
        else if (file.ends_with(".json"))
        {
            file = file.substr(0, file.size() - 5);
        }
        else
        {
            continue;
        }
        if (!file.empty()) configNames.push_back(file);
    }
    std::sort(configNames.begin(), configNames.end(), [&](const std::string& a, const std::string& b) {
        if (a.size() == b.size()) return a < b;
        return configSortLengthDesc ? a.size() > b.size() : a.size() < b.size();
    });
    configNames.erase(std::unique(configNames.begin(), configNames.end()), configNames.end());

    std::string target = selectedConfigName;
    if (target.empty()) target = ConfigManager::LastLoadedConfig;
    configSelectedIndex = -1;
    if (!target.empty())
    {
        for (size_t i = 0; i < configNames.size(); ++i)
        {
            if (configNames[i] == target)
            {
                configSelectedIndex = (int)i;
                break;
            }
        }
    }
    if (configSelectedIndex == -1 && !configNames.empty())
    {
        configSelectedIndex = 0;
        selectedConfigName = configNames[0];
    }
    else if (configSelectedIndex >= 0 && configSelectedIndex < (int)configNames.size())
    {
        selectedConfigName = configNames[configSelectedIndex];
    }
}

void ModernGui::openGui()
{
    resetTransientState();
    mCloseRequested.store(false, std::memory_order_release);
    startPreload();
    selectedConfigName = ConfigManager::LastLoadedConfig;
    refreshConfigList();
}

void ModernGui::closeGui()
{
    resetTransientState();
    mCloseRequested.store(false, std::memory_order_release);
    isInitialLoading = false;
    initialLoadStart = 0;
}

void ModernGui::requestClose()
{
    mCloseRequested.store(true, std::memory_order_release);
}

bool ModernGui::consumeCloseRequest()
{
    return mCloseRequested.exchange(false, std::memory_order_acq_rel);
}

ModernGui::ModernGui()
{
    mSearchThread = std::thread(&ModernGui::moduleSearchWorker, this);
    mDescThread = std::thread(&ModernGui::descriptionTokenWorker, this);
}

ModernGui::~ModernGui()
{
    mSearchStop.store(true, std::memory_order_release);
    mSearchCv.notify_one();
    if (mSearchThread.joinable()) mSearchThread.join();

    mDescStop.store(true, std::memory_order_release);
    mDescCv.notify_one();
    if (mDescThread.joinable()) mDescThread.join();
}

void ModernGui::requestModuleSearch(std::string query, std::vector<std::shared_ptr<Module>> snapshot)
{
    {
        std::lock_guard<std::mutex> lock(mSearchMutex);
        mSearchRequestId++;
        mSearchRequestedQuery = std::move(query);
        mSearchRequestedSnapshot = std::move(snapshot);
    }
    mSearchCv.notify_one();
}

bool ModernGui::tryConsumeModuleSearch(const std::string& query, uint64_t& outReadyId, std::vector<std::shared_ptr<Module>>& outSnapshot)
{
    std::lock_guard<std::mutex> lock(mSearchMutex);
    if (mSearchReadyId == 0) return false;
    if (mSearchReadyQuery != query) return false;
    outReadyId = mSearchReadyId;
    outSnapshot = mSearchReadySnapshot;
    return true;
}

void ModernGui::moduleSearchWorker()
{
    std::unique_lock<std::mutex> lock(mSearchMutex);
    for (;;)
    {
        mSearchCv.wait(lock, [&]() { return mSearchStop.load(std::memory_order_acquire) || (mSearchRequestId != mSearchProcessingId); });
        if (mSearchStop.load(std::memory_order_acquire)) return;

        mSearchProcessingId = mSearchRequestId;
        std::string query = mSearchRequestedQuery;
        std::vector<std::shared_ptr<Module>> snapshot = mSearchRequestedSnapshot;
        uint64_t jobId = mSearchProcessingId;

        lock.unlock();

        std::vector<std::shared_ptr<Module>> filtered;
        filtered.reserve(snapshot.size());
        size_t i = 0;
        for (const auto& m : snapshot)
        {
            if ((++i & 0x3F) == 0 && mSearchStop.load(std::memory_order_relaxed)) {
                return;
            }
            if (!m) continue;
            if (StringUtils::containsIgnoreCase(m->mName, query)) filtered.push_back(m);
        }

        lock.lock();
        if (mSearchStop.load(std::memory_order_acquire)) return;
        if (jobId >= mSearchReadyId)
        {
            mSearchReadyId = jobId;
            mSearchReadyQuery = std::move(query);
            mSearchReadySnapshot = std::move(filtered);
        }
    }
}

void ModernGui::requestDescriptionTokens(const Setting* setting, std::string description)
{
    if (!setting) return;
    {
        std::lock_guard<std::mutex> lock(mDescMutex);
        auto itReady = mDescReady.find(setting);
        if (itReady != mDescReady.end() && itReady->second.description == description) return;
        mDescRequested[setting] = std::move(description);
    }
    mDescCv.notify_one();
}

bool ModernGui::tryGetDescriptionTokens(const Setting* setting, const std::string& description, std::vector<std::string>& outTokens)
{
    if (!setting) return false;
    std::lock_guard<std::mutex> lock(mDescMutex);
    auto it = mDescReady.find(setting);
    if (it == mDescReady.end()) return false;
    if (it->second.description != description) return false;
    it->second.lastUse = ++mDescUseCounter;
    outTokens = it->second.tokens;
    return true;
}

void ModernGui::descriptionTokenWorker()
{
    std::unique_lock<std::mutex> lock(mDescMutex);
    for (;;)
    {
        mDescCv.wait(lock, [&]() { return mDescStop.load(std::memory_order_acquire) || !mDescRequested.empty(); });
        if (mDescStop.load(std::memory_order_acquire)) return;

        std::unordered_map<const Setting*, std::string> jobs;
        jobs.swap(mDescRequested);

        lock.unlock();

        std::unordered_map<const Setting*, DescTokensCache> completed;
        completed.reserve(jobs.size());
        size_t i = 0;
        for (auto& [setting, desc] : jobs)
        {
            if ((++i & 0x3F) == 0 && mDescStop.load(std::memory_order_relaxed)) {
                return;
            }
            DescTokensCache entry;
            entry.description = desc;
            entry.tokens.reserve(32);

            std::string word;
            word.reserve(32);
            auto flushWord = [&]()
            {
                if (word.empty()) return;
                entry.tokens.push_back(word);
                word.clear();
            };

            for (char c : desc)
            {
                if (c == '\n')
                {
                    flushWord();
                    entry.tokens.push_back("\n");
                }
                else if (c == ' ')
                {
                    flushWord();
                }
                else
                {
                    word.push_back(c);
                }
            }
            flushWord();

            completed.emplace(setting, std::move(entry));
        }

        lock.lock();
        if (mDescStop.load(std::memory_order_acquire)) return;
        for (auto& [setting, entry] : completed)
        {
            auto it = mDescReady.find(setting);
            if (it != mDescReady.end() && it->second.description == entry.description) continue;
            entry.lastUse = ++mDescUseCounter;
            mDescReady[setting] = std::move(entry);
        }

        if (mDescReady.size() > kMaxDescReadyEntries)
        {
            const size_t target = (kMaxDescReadyEntries * 9) / 10;
            while (mDescReady.size() > target)
            {
                auto oldest = mDescReady.begin();
                for (auto it = std::next(mDescReady.begin()); it != mDescReady.end(); ++it)
                {
                    if (it->second.lastUse < oldest->second.lastUse) oldest = it;
                }
                mDescReady.erase(oldest);
            }
        }
    }
}

bool ModernGui::isMouseOver(const ImVec4& rect)
{
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    return mousePos.x >= rect.x && mousePos.y >= rect.y && mousePos.x < rect.z && mousePos.y < rect.w;
}

void ModernGui::render(float animation, float inScale, int& scrollDirection, char* h, float blurStrength, float midclickRounding, bool isPressingShift)
{
    render(animation, inScale, scrollDirection, h, midclickRounding, isPressingShift);
}

void ModernGui::render(float animation, float inScale, int& scrollDirection, char* h, float midclickRounding, bool isPressingShift)
{
    if (!gFeatureManager || !gFeatureManager->mModuleManager) return;
    auto* clickGui = gFeatureManager->mModuleManager->getModule<ClickGui>();
    if (!clickGui) return;
    if (!ImGui::GetCurrentContext()) return;
    std::scoped_lock<std::mutex> lk(uiMutex);
    bool isEnabled = clickGui->mEnabled;
    bool isEnabledRaw = isEnabled;
    uiTheme = 0;

    ModuleManager* moduleManager = gFeatureManager->mModuleManager.get();
    const auto& allModsRef = moduleManager->getModules();
    std::unordered_set<const Module*> aliveModules;
    std::unordered_set<const Setting*> aliveSettings;
    aliveModules.reserve(allModsRef.size());
    aliveSettings.reserve(allModsRef.size() * 8);
    for (const auto& m : allModsRef)
    {
        if (!m) continue;
        aliveModules.insert(m.get());
        for (const auto& s : m->mSettings)
        {
            aliveSettings.insert(s);
        }
    }
    auto isSettingAlive = [&](const Setting* s) -> bool
    {
        return s && aliveSettings.find(s) != aliveSettings.end();
    };
    auto isModuleAlive = [&](const Module* m) -> bool
    {
        return m && aliveModules.find(m) != aliveModules.end();
    };

    if (lastMod && !isModuleAlive(lastMod.get()))
    {
        lastMod = nullptr;
        isBinding = false;
    }
    if (lastBoolSetting && !isSettingAlive(lastBoolSetting))
    {
        lastBoolSetting = nullptr;
        isBoolSettingBinding = false;
    }
    if (lastColorSetting && !isSettingAlive(lastColorSetting))
    {
        lastColorSetting = nullptr;
        displayColorPicker = false;
        resizingColorPicker = false;
    }
    if (openEnumSetting && !isSettingAlive(openEnumSetting))
    {
        openEnumSetting = nullptr;
    }
    if (!enumDropdownAnims.empty())
    {
        for (auto it = enumDropdownAnims.begin(); it != enumDropdownAnims.end();)
        {
            if (!isSettingAlive(it->first)) it = enumDropdownAnims.erase(it);
            else ++it;
        }
    }

    if (configNames.empty())
    {
        refreshConfigList();
    }
    if (!ConfigManager::LastLoadedConfig.empty() && ConfigManager::LastLoadedConfig != selectedConfigName)
    {
        selectedConfigName = ConfigManager::LastLoadedConfig;
        if (configSelectedIndex < 0 || configSelectedIndex >= (int)configNames.size() || configNames[configSelectedIndex] != selectedConfigName)
        {
            configSelectedIndex = -1;
            for (size_t i = 0; i < configNames.size(); ++i)
            {
                if (configNames[i] == selectedConfigName)
                {
                    configSelectedIndex = (int)i;
                    break;
                }
            }
        }
    }
    if (configSelectedIndex >= (int)configNames.size()) configSelectedIndex = -1;

    colorPickerWidth = finiteOr(colorPickerWidth, 180.0f);
    themePickerWidth = finiteOr(themePickerWidth, 180.0f);

    ImVec2 screenSize = ImRenderUtils::getScreenSize();
    float deltaTime = ImGui::GetIO().DeltaTime;
    auto drawList = ImGui::GetBackgroundDrawList();
    if (gPreloadCompleted.load())
    {
        isInitialLoading = false;
        initialLoadStart = 0;
    }
    if (isInitialLoading)
    {
        if (initialLoadStart == 0) initialLoadStart = NOW;
        startPreloadKeyScan();
        preloadGuiTextures();

        if (gPreloadReady.load())
        {
            size_t batch = 1;
            std::vector<std::string> local;
            {
                std::lock_guard<std::mutex> lock(gPreloadMutex);
                size_t total = gPreloadKeys.size();
                size_t idx = gPreloadIndex.load();
                if (idx < total)
                {
                    size_t end = std::min(idx + batch, total);
                    local.assign(gPreloadKeys.begin() + idx, gPreloadKeys.begin() + end);
                    gPreloadIndex.store(end);
                }
            }
            for (const auto& key : local)
            {
                int w = 0;
                int h = 0;
                getGuiTexture(key.c_str(), &w, &h);
            }
        }

        bool timeOk = NOW - initialLoadStart >= 2000;
        bool preloadOk = false;
        if (gPreloadReady.load())
        {
            std::lock_guard<std::mutex> lock(gPreloadMutex);
            preloadOk = gPreloadIndex.load() >= gPreloadKeys.size();
        }

        if (timeOk && preloadOk)
        {
            isInitialLoading = false;
            initialLoadStart = 0;
            gPreloadCompleted.store(true);
            if (gPreloadReady.load())
            {
                std::lock_guard<std::mutex> lock(gPreloadMutex);
                gPreloadKeys.clear();
                gPreloadKeys.shrink_to_fit();
            }
        }
    }
    static float loadingFade = 0.0f;
    float loadingTarget = isInitialLoading ? 1.0f : 0.0f;
    loadingFade = MathUtils::animate(loadingTarget, loadingFade, deltaTime * 6.0f);

    if (isEnabledRaw)
    {
        ImGuiWindowFlags inputFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(screenSize);
        ImGui::Begin("##clickgui_input_blocker", nullptr, inputFlags);
        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("##clickgui_input_blocker_btn", screenSize);
        ImGui::End();
    }

    FontHelper::load();
    bool pushedComfortaa = false;
    {
        auto itFont = FontHelper::Fonts.find("comfortaa");
        if (itFont != FontHelper::Fonts.end() && itFont->second) {
            ImGui::PushFont(itFont->second);
            pushedComfortaa = true;
        }
    }

    static int currentCategoryIndex = 0;
    static int targetCategoryIndex = -1;
    static bool isCategorySwitching = false;
    static bool isCategoryFadingOut = false;
    static float categoryFade = 1.0f;
    static float mScrollY = 0.0f;
    static float mScrollTarget = 0.0f;
    static float maxScrollY = 0.0f;
    int oldCategoryIndex = currentCategoryIndex;

    float windowWidth = 850.0f * inScale;
    float windowHeight = 580.0f * inScale;
    float centerX = screenSize.x / 2.0f;
    float centerY = screenSize.y / 2.0f;

    static ImVec2 guiOffset = ImVec2(0.0f, 0.0f);
    static ImVec2 guiOffsetTarget = ImVec2(0.0f, 0.0f);
    guiOffset.x = MathUtils::animate(guiOffsetTarget.x, guiOffset.x, deltaTime * 18.0f);
    guiOffset.y = MathUtils::animate(guiOffsetTarget.y, guiOffset.y, deltaTime * 18.0f);

    ImVec4 mainRect = ImVec4(
        centerX - windowWidth / 2.0f + guiOffset.x,
        centerY - windowHeight / 2.0f + guiOffset.y,
        centerX + windowWidth / 2.0f + guiOffset.x,
        centerY + windowHeight / 2.0f + guiOffset.y
    );

    ColorUtils::setUiTheme(ColorUtils::UiTheme::Dark);

    ImColor bgColor = ColorUtils::getUiBackgroundColor(animation);
    ImColor cardColor = ColorUtils::getUiCardColor(animation);
    ImColor textMain = ColorUtils::getUiTextColor(animation);
    ImColor textDim = ColorUtils::getUiTextDimColor(animation);
    ImColor outlineColor = ColorUtils::getUiBorderColor(animation);
    ImColor panelBorderColor = (uiTheme == 1) ? ImColor(232, 232, 240, (int)(255 * animation)) : ImColor(12, 12, 16, (int)(255 * animation));

    static const ImColor themeAccents[] = {
        ImColor(99, 102, 241, 255),
        ImColor(59, 130, 246, 255),
        ImColor(56, 189, 248, 255),
        ImColor(34, 211, 238, 255),
        ImColor(6, 182, 212, 255),
        ImColor(20, 184, 166, 255),
        ImColor(16, 185, 129, 255),
        ImColor(34, 197, 94, 255),
        ImColor(132, 204, 22, 255),
        ImColor(234, 179, 8, 255),
        ImColor(245, 158, 11, 255),
        ImColor(249, 115, 22, 255),
        ImColor(251, 146, 60, 255),
        ImColor(239, 68, 68, 255),
        ImColor(244, 63, 94, 255),
        ImColor(236, 72, 153, 255),
        ImColor(217, 70, 239, 255),
        ImColor(168, 85, 247, 255),
        ImColor(139, 92, 246, 255),
        ImColor(138, 43, 226, 255),
        ImColor(255, 90, 195, 255),
        ImColor(255, 64, 110, 255),
        ImColor(255, 176, 59, 255),
        ImColor(0, 214, 255, 255)
    };
    const int themeCount = (int)(sizeof(themeAccents) / sizeof(themeAccents[0]));
    themeIndex = std::clamp(themeIndex, 0, themeCount - 1);
    ImColor accentBase = useCustomTheme
        ? ImColor(customThemeColor[0], customThemeColor[1], customThemeColor[2], customThemeColor[3])
        : themeAccents[themeIndex];
    ImColor accentOverride = accentBase;
    accentOverride.Value.w = 1.0f;
    ColorUtils::setGuiAccentOverride(accentOverride);
    ImColor accentColor = accentBase;
    accentColor.Value.w *= animation;

    auto tokenizeDescription = [&](const std::string& text) -> std::vector<std::string> {
        std::vector<std::string> tokens;
        tokens.reserve(32);

        std::string word;
        word.reserve(32);
        auto flushWord = [&]() {
            if (word.empty()) return;
            tokens.push_back(word);
            word.clear();
        };

        for (char c : text)
        {
            if (c == '\n')
            {
                flushWord();
                tokens.push_back("\n");
            }
            else if (c == ' ')
            {
                flushWord();
            }
            else
            {
                word.push_back(c);
            }
        }
        flushWord();
        return tokens;
    };

    auto getWrappedDescription = [&](const Setting* setting, const std::string& text, float fontSize, float maxWidth) -> const WrappedDescCacheValue* {
        float fontPx = fontSize * 18.0f;
        int fontPx10 = (int)std::lround(fontPx);
        int maxWidth10 = (int)std::lround(maxWidth);
        WrappedDescCacheKey key;
        key.setting = setting;
        key.fontPx10 = fontPx10;
        key.maxWidth10 = maxWidth10;

        auto it = mWrappedDescCache.find(key);
        if (it != mWrappedDescCache.end() && it->second.description == text) {
            it->second.lastUse = ++mWrappedDescUseCounter;
            return &it->second;
        }

        WrappedDescCacheValue value;
        value.description = text;

        if (!text.empty() && maxWidth > 1.0f && setting)
        {
            requestDescriptionTokens(setting, text);

            std::vector<std::string> tokens;
            bool hasTokens = tryGetDescriptionTokens(setting, text, tokens);
            if (!hasTokens) tokens = tokenizeDescription(text);

            std::unordered_map<std::string, float> wordWidth;
            wordWidth.reserve(tokens.size());
            auto measureWordW = [&](const std::string& s) -> float {
                auto itW = wordWidth.find(s);
                if (itW != wordWidth.end()) return itW->second;
                float w = ImGui::GetFont()->CalcTextSizeA(fontPx, FLT_MAX, -1.0f, s.c_str()).x;
                wordWidth.emplace(s, w);
                return w;
            };
            auto measureTextW = [&](const std::string& s) -> float {
                return ImGui::GetFont()->CalcTextSizeA(fontPx, FLT_MAX, -1.0f, s.c_str()).x;
            };
            float spaceW = ImGui::GetFont()->CalcTextSizeA(fontPx, FLT_MAX, -1.0f, " ").x;

            auto wrapFromTokens = [&](const std::vector<std::string>& tks) -> std::vector<std::string> {
                std::vector<std::string> lines;
                lines.reserve(8);
                std::string current;
                current.reserve(text.size());
                float currentW = 0.0f;

                auto pushLine = [&]() {
                    if (!current.empty()) {
                        lines.push_back(current);
                        current.clear();
                        currentW = 0.0f;
                    }
                };

                for (const auto& tk : tks)
                {
                    if (tk == "\n")
                    {
                        pushLine();
                        continue;
                    }

                    const std::string& word = tk;
                    if (word.empty()) continue;

                    auto putLongWord = [&]() {
                        std::string chunk;
                        chunk.reserve(word.size());
                        for (char c : word)
                        {
                            chunk.push_back(c);
                            if (chunk.size() > 1 && measureTextW(chunk) > maxWidth)
                            {
                                chunk.pop_back();
                                if (!chunk.empty()) lines.push_back(chunk);
                                chunk.clear();
                                chunk.push_back(c);
                            }
                        }
                        if (!chunk.empty())
                        {
                            current = chunk;
                            currentW = measureTextW(current);
                        }
                    };

                    float w = measureWordW(word);
                    if (current.empty())
                    {
                        if (w <= maxWidth)
                        {
                            current = word;
                            currentW = w;
                        }
                        else
                        {
                            putLongWord();
                        }
                    }
                    else
                    {
                        if ((currentW + spaceW + w) <= maxWidth)
                        {
                            current.push_back(' ');
                            current += word;
                            currentW += spaceW + w;
                        }
                        else
                        {
                            pushLine();
                            if (w <= maxWidth)
                            {
                                current = word;
                                currentW = w;
                            }
                            else
                            {
                                putLongWord();
                            }
                        }
                    }
                }

                pushLine();
                return lines;
            };

            value.lines = wrapFromTokens(tokens);
            if (!value.lines.empty())
            {
                value.lineH = ImGui::GetFont()->CalcTextSizeA(fontPx, FLT_MAX, -1.0f, "Ag").y;
            }
        }

        auto res = mWrappedDescCache.insert_or_assign(key, std::move(value));
        res.first->second.lastUse = ++mWrappedDescUseCounter;

        if (mWrappedDescCache.size() > kMaxWrappedDescEntries)
        {
            const size_t target = (kMaxWrappedDescEntries * 9) / 10;
            while (mWrappedDescCache.size() > target)
            {
                auto oldest = mWrappedDescCache.begin();
                for (auto itOld = std::next(mWrappedDescCache.begin()); itOld != mWrappedDescCache.end(); ++itOld)
                {
                    if (itOld->second.lastUse < oldest->second.lastUse) oldest = itOld;
                }
                mWrappedDescCache.erase(oldest);
            }
        }

        return &res.first->second;
    };

    auto wrappedDescriptionHeight = [&](const Setting* setting, const std::string& text, float fontSize, float maxWidth, float lineSpacing) -> float {
        if (!setting || text.empty()) return 0.0f;
        const auto* wrap = getWrappedDescription(setting, text, fontSize, maxWidth);
        if (!wrap || wrap->lines.empty()) return 0.0f;
        return (wrap->lineH * (float)wrap->lines.size()) + (lineSpacing * (float)(std::max(0, (int)wrap->lines.size() - 1)));
    };

    for (int i = 0; i < 25; i++) {
        float size = (30.0f - i) * inScale;
        float alpha = (float)i / 30.0f * 0.03f * animation;
        drawList->AddRect(
            ImVec2(mainRect.x - size, mainRect.y - size),
            ImVec2(mainRect.z + size, mainRect.w + size),
            ImColor(0, 0, 0, (int)(alpha * 255)),
            16.0f + size
        );
    }
    drawList->AddShadowRect(ImVec2(mainRect.x, mainRect.y), ImVec2(mainRect.z, mainRect.w), ImColor(0, 0, 0, (int)(170 * animation)), 85.0f * inScale, ImVec2(0.f, 0.f), 0, 16.0f);
    drawList->AddRectFilled(ImVec2(mainRect.x, mainRect.y), ImVec2(mainRect.z, mainRect.w), bgColor, 16.0f);
    drawList->AddRect(ImVec2(mainRect.x, mainRect.y), ImVec2(mainRect.z, mainRect.w), outlineColor, 16.0f);

    ImColor sidebarBg = (uiTheme == 1) ? ImColor(242, 242, 248, (int)(255 * animation)) : ImColor(12, 12, 16, (int)(255 * animation));

    float topBarHeight = 50.0f * inScale;
    float topBarPad = 12.0f * inScale;
    float topBarR = 14.0f * inScale;
    ImVec4 topBarRect = ImVec4(
        mainRect.x + topBarPad,
        mainRect.y + topBarPad,
        mainRect.z - topBarPad,
        mainRect.y + topBarPad + topBarHeight
    );
    ImColor topBarBg = sidebarBg;
    ImColor topBarBorder = outlineColor;
    drawList->AddRectFilled(ImVec2(topBarRect.x, topBarRect.y), ImVec2(topBarRect.z, topBarRect.w), topBarBg, topBarR);
    drawList->AddRect(ImVec2(topBarRect.x, topBarRect.y), ImVec2(topBarRect.z, topBarRect.w), topBarBorder, topBarR, 0, 1.4f);

    std::string titleText = "Social.cc";
    std::string subText = "Minecraft Bedrock Edition";
    float titleFont = 1.05f * inScale;
    float subFont = 0.78f * inScale;
    float logoBoxSize = 34.0f * inScale;
    float textX = topBarRect.x + 10.0f * inScale + logoBoxSize + 10.0f * inScale;
    float titleW = ImRenderUtils::getTextWidth(&titleText, titleFont);
    float subW = ImRenderUtils::getTextWidth(&subText, subFont);
    float textBlockW = std::max(titleW, subW);
    float textRight = textX + textBlockW;

    ImVec4 configSaveRect = ImVec4(0, 0, 0, 0);
    ImVec4 configDropRect = ImVec4(0, 0, 0, 0);
    ImVec4 configPanelRect = ImVec4(0, 0, 0, 0);
    bool showConfigControls = false;
    std::string displayConfigName;

    float bodyTopY = topBarRect.w + 12.0f * inScale;
    float bodyBottomY = mainRect.w - 12.0f * inScale;
    float sidebarW = 60.0f * inScale;
    ImVec4 sidebarRect = ImVec4(
        mainRect.x + 12.0f * inScale,
        bodyTopY,
        mainRect.x + 12.0f * inScale + sidebarW,
        bodyBottomY
    );
    drawList->AddRectFilled(ImVec2(sidebarRect.x, sidebarRect.y), ImVec2(sidebarRect.z, sidebarRect.w), sidebarBg, 14.0f * inScale);
    drawList->AddRect(ImVec2(sidebarRect.x, sidebarRect.y), ImVec2(sidebarRect.z, sidebarRect.w), outlineColor, 14.0f * inScale, 0, 1.2f);
    bool pushedIconFont = false;
    {
        auto itFont = FontHelper::Fonts.find("essence.ttf_large");
        if (itFont != FontHelper::Fonts.end() && itFont->second) {
            ImGui::PushFont(itFont->second);
            pushedIconFont = true;
        }
    }
    ImFont* combatIconFont = nullptr;
    {
        auto itFont = FontHelper::Fonts.find("clickgui.ttf_large");
        if (itFont != FontHelper::Fonts.end() && itFont->second) combatIconFont = itFont->second;
    }
    static std::vector<std::string> categories;
    if (categories.empty())
    {
        categories = ModuleCategoryNames;
        categories.push_back("Autobuy");
    }
    const int autobuyCategoryIndexForUi = std::max(0, (int)categories.size() - 1);
    static std::vector<float> catAnims;
    if (catAnims.size() != categories.size()) catAnims.resize(categories.size(), 0.0f);
    float iconSize = 22.0f * inScale;
    float slotH = 40.0f * inScale;
    float slotGap = 14.0f * inScale;
    float panelPadX = 8.0f * inScale;
    float panelPadY = 14.0f * inScale;

    float panelW = (sidebarRect.z - sidebarRect.x) - panelPadX * 2.0f;
    float panelH = (categories.empty() ? 0.0f : ((float)categories.size() * slotH) + ((float)(categories.size() - 1) * slotGap)) + panelPadY * 2.0f;
    float sidebarH = sidebarRect.w - sidebarRect.y;
    float panelX = sidebarRect.x + panelPadX;
    float panelY = sidebarRect.y + (sidebarH - panelH) * 0.5f;

    drawList->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), bgColor, panelW * 0.5f);
    drawList->AddRect(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), panelBorderColor, panelW * 0.5f, 0, 1.5f);
    drawList->PushClipRect(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), true);
    static ImVec2 defaultIconOffset = ImVec2(10.0f, 10.0f);
    //иконкы для кнопик xyz
    static std::unordered_map<std::string, ImVec2> iconOffsetsByCategory = {
        {"Combat", ImVec2(defaultIconOffset.x + 2.0f, defaultIconOffset.y)},
        {"Movement", ImVec2(defaultIconOffset.x + 3.0f, defaultIconOffset.y)},
        {"Visual", ImVec2(defaultIconOffset.x + 3.0f, defaultIconOffset.y)},
        {"Player", ImVec2(defaultIconOffset.x + 3.0f, defaultIconOffset.y)},
        {"Misc", ImVec2(defaultIconOffset.x + 2.0f, defaultIconOffset.y)},
        {"Autobuy", ImVec2(defaultIconOffset.x + 3.0f, defaultIconOffset.y)},
    };
    int displayedCategoryIndex = (targetCategoryIndex != -1) ? targetCategoryIndex : currentCategoryIndex;
    for (size_t i = 0; i < categories.size(); i++)
    {
        float itemY = panelY + panelPadY + (float)i * (slotH + slotGap);
        ImVec4 slotRect = ImVec4(panelX, itemY, panelX + panelW, itemY + slotH);
        float itemCenterX = panelX + panelW * 0.5f;
        float itemCenterY = (slotRect.y + slotRect.w) * 0.5f;
        float hitPad = 10.0f * inScale;
        ImVec4 itemHitbox = ImVec4(panelX - hitPad, slotRect.y - hitPad, panelX + panelW + hitPad, slotRect.w + hitPad);
        bool isSelected = (displayedCategoryIndex == (int)i);
        bool hovered = isMouseOver(itemHitbox) && isEnabled;
        if (hovered)
        {
            if (ImGui::IsMouseClicked(0)) {
                if ((int)i != displayedCategoryIndex) {
                    targetCategoryIndex = (int)i;
                    isCategorySwitching = true;
                    isCategoryFadingOut = true;
                    if (isIrcPanelOpen) isIrcPanelOpen = false;
                }
            }
        }
        catAnims[i] = MathUtils::animate(isSelected ? 1.0f : 0.0f, catAnims[i], deltaTime * 12.0f);
        std::string catName = categories[i];
        std::string iconStr = "A";
        bool isCombat = StringUtils::equalsIgnoreCase(catName, "Combat");
        if (isCombat) iconStr = "A";
        else if (StringUtils::equalsIgnoreCase(catName, "Movement")) iconStr = "c";
        else if (StringUtils::equalsIgnoreCase(catName, "Player")) iconStr = "b";
        else if (StringUtils::equalsIgnoreCase(catName, "Misc")) iconStr = "e";
        else if (StringUtils::equalsIgnoreCase(catName, "Autobuy")) iconStr = "d";
        else if (StringUtils::equalsIgnoreCase(catName, "Visual")) iconStr = "v";
        float anim = catAnims[i];
        ImColor iconColor = MathUtils::lerpImColor(textDim, accentColor, anim);
        if (anim > 0.01f) {
            float glow = anim;
            float glowScale = 1.0f / 3.0f;
            float iconGlowPad = 10.0f * inScale;
            ImVec4 iconGlowRect = ImVec4(
                itemCenterX - iconSize * 0.5f - iconGlowPad,
                itemCenterY - iconSize * 0.5f - iconGlowPad,
                itemCenterX + iconSize * 0.5f + iconGlowPad,
                itemCenterY + iconSize * 0.5f + iconGlowPad
            );
            ImRenderUtils::drawRectGlow(iconGlowRect, iconColor, (0.16f * glow) * glowScale, (10.0f * inScale) * glowScale, 1.06f, 32.0f, drawList);
        }
        bool pushedCombatFont = false;
        if (isCombat && combatIconFont) {
            ImGui::PushFont(combatIconFont);
            pushedCombatFont = true;
        }
        float fontSize = 0.98f * inScale;
        ImVec2 textSize = ImGui::CalcTextSize(iconStr.c_str());
        float textX = itemCenterX - (textSize.x * fontSize) / 2.0f;
        float textY = itemCenterY - (textSize.y * fontSize) / 2.0f;
        auto itOff = iconOffsetsByCategory.find(catName);
        ImVec2 offBase = (itOff != iconOffsetsByCategory.end()) ? itOff->second : defaultIconOffset;
        textX += offBase.x * inScale;
        textY += offBase.y * inScale;
        ImRenderUtils::drawText(ImVec2(textX, textY), iconStr, iconColor, fontSize, 1.0f, true);
        if (pushedCombatFont) ImGui::PopFont();
    }
    drawList->PopClipRect();
    if (pushedIconFont) ImGui::PopFont();

    ImVec4 themeBtnRect = ImVec4(0, 0, 0, 0);
    ImVec4 ircBtnRect = ImVec4(0, 0, 0, 0);
    searchAnim = MathUtils::animate(isSearching ? 1.0f : 0.0f, searchAnim, deltaTime * 16.0f);
    ImVec4 themePanelRect = ImVec4(0, 0, 0, 0);
    {
        int searchIconW = 16;
        int searchIconH = 16;
        ID3D11ShaderResourceView* searchIconTexture = getGuiTexture("find.png", &searchIconW, &searchIconH);

        int themeIconW = 16;
        int themeIconH = 16;
        ID3D11ShaderResourceView* themeIconTexture = getGuiTexture("color.png", &themeIconW, &themeIconH);

        float searchH = 30.0f * inScale;
        float searchY = topBarRect.y + (topBarHeight - searchH) / 2.0f;
        float searchWClosed = searchH;
        float searchWOpen = 170.0f * inScale;
        float searchW = MathUtils::lerp(searchWClosed, searchWOpen, searchAnim);
        float searchRightPad = 18.0f * inScale;
        float searchX1 = topBarRect.z - searchRightPad;
        float searchX0 = searchX1 - searchW;
        ImVec4 searchRect = ImVec4(searchX0, searchY, searchX1, searchY + searchH);
        ImVec4 searchIconRect = ImVec4(searchRect.x, searchRect.y, searchRect.x + searchH, searchRect.y + searchH);

        float themeGap = 10.0f * inScale;
        float themeX1 = searchRect.x - themeGap;
        float themeX0 = themeX1 - searchH;
        themeBtnRect = ImVec4(themeX0, searchY, themeX1, searchY + searchH);
        float ircX1 = themeBtnRect.x - themeGap;
        float ircX0 = ircX1 - searchH;
        ircBtnRect = ImVec4(ircX0, searchY, ircX1, searchY + searchH);

        displayConfigName = selectedConfigName;
        if (displayConfigName.empty() && configSelectedIndex >= 0 && configSelectedIndex < (int)configNames.size())
        {
            displayConfigName = configNames[configSelectedIndex];
        }
        if (displayConfigName.empty() && !ConfigManager::LastLoadedConfig.empty())
        {
            displayConfigName = ConfigManager::LastLoadedConfig;
        }
        if (displayConfigName.empty())
        {
            displayConfigName = "No configs";
        }

        float configControlH = 24.0f * inScale;
        float configY = topBarRect.y + (topBarHeight - configControlH) / 2.0f;
        float configLeft = textRight + 12.0f * inScale;
        float configRight = ircBtnRect.x - 12.0f * inScale;
        float configGap = 6.0f * inScale;
        float saveW = 58.0f * inScale;
        float dropMinW = 75.0f * inScale;
        float configFont = 0.86f * inScale;
        float dropPadX = 10.0f * inScale;
        float dropArrowW = 7.0f * inScale;
        float dropArrowPad = 7.0f * inScale;
        float desiredDropW = (dropPadX * 2.0f) + dropArrowPad + dropArrowW + ImRenderUtils::getTextWidth(&displayConfigName, configFont);
        float available = configRight - configLeft;
        showConfigControls = available >= (saveW + configGap + dropMinW);
        if (showConfigControls)
        {
            float maxDropW = available - saveW - configGap;
            float dropW = std::clamp(desiredDropW, dropMinW, maxDropW);
            configSaveRect = ImVec4(configLeft, configY, configLeft + saveW, configY + configControlH);
            configDropRect = ImVec4(configSaveRect.z + configGap, configY, configSaveRect.z + configGap + dropW, configY + configControlH);
        }

        static bool isDraggingGui = false;
        static ImVec2 dragStartMouse = ImVec2(0.0f, 0.0f);
        static ImVec2 dragStartOffset = ImVec2(0.0f, 0.0f);
        ImVec4 dragHit = topBarRect;
        float dragStop = ircBtnRect.x - (8.0f * inScale);
        if (showConfigControls) dragStop = std::min(dragStop, configSaveRect.x - 6.0f * inScale);
        dragHit.z = dragStop;
        bool canDrag = isEnabled && !isBinding && isMouseOver(dragHit);
        if (canDrag && ImGui::IsMouseClicked(0))
        {
            isDraggingGui = true;
            dragStartMouse = ImGui::GetIO().MousePos;
            dragStartOffset = guiOffsetTarget;
        }
        if (isDraggingGui)
        {
            if (!ImGui::IsMouseDown(0))
            {
                isDraggingGui = false;
            }
            else
            {
                ImVec2 mp = ImGui::GetIO().MousePos;
                ImVec2 next = ImVec2(dragStartOffset.x + (mp.x - dragStartMouse.x), dragStartOffset.y + (mp.y - dragStartMouse.y));

                float baseX = centerX - windowWidth / 2.0f;
                float baseY = centerY - windowHeight / 2.0f;
                float minOffX = 10.0f - baseX;
                float maxOffX = (screenSize.x - 10.0f) - (baseX + windowWidth);
                float minOffY = 10.0f - baseY;
                float maxOffY = (screenSize.y - 10.0f) - (baseY + windowHeight);
                next.x = std::clamp(next.x, minOffX, maxOffX);
                next.y = std::clamp(next.y, minOffY, maxOffY);
                guiOffsetTarget = next;
            }
        }

        bool searchHovered = isMouseOver(searchRect) && isEnabled;
        const bool clickedL = isEnabled && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isBinding;
        if (clickedL) {
            if (isMouseOver(ircBtnRect)) {
                if (clickedL)
                {
                    if (currentCategoryIndex != (int)ModuleCategory::Misc) {
                        currentCategoryIndex = (int)ModuleCategory::Misc;
                        targetCategoryIndex = -1;
                        isCategorySwitching = false;
                        isCategoryFadingOut = false;
                        categoryFade = 1.0f;
                        mScrollTarget = 0.0f;
                        mScrollY = 0.0f;
                        isTargetEspPanelOpen = false;
                        isAnimationsPanelOpen = false;
                    }
                    isIrcPanelOpen = !isIrcPanelOpen;
                    if (isIrcPanelOpen) {
                        isThemePickerOpen = false;
                        isSearching = false;
                        searchText[0] = 0;
                        searchTextAnim = 0.0f;
                        openEnumSetting = nullptr;
                        isTargetEspPanelOpen = false;
                        isAnimationsPanelOpen = false;
                    }
                }
            }
            else if (isMouseOver(themeBtnRect)) {
                bool next = !isThemePickerOpen;
                isThemePickerOpen = next;
                if (next) {
                    customThemeColor[0] = accentColor.Value.x;
                    customThemeColor[1] = accentColor.Value.y;
                    customThemeColor[2] = accentColor.Value.z;
                    customThemeColor[3] = accentColor.Value.w;
                    useCustomTheme = true;
                }
            }
            else if (isMouseOver(searchIconRect)) {
                if (clickedL)
                {
                    isSearching = !isSearching;
                    searchWantsFocus = isSearching;
                    if (!isSearching) {
                        searchText[0] = 0;
                        searchTextAnim = 0.0f;
                    }
                }
            }
            else if (isSearching && searchHovered) {
                if (clickedL) searchWantsFocus = true;
            }
        }

        ImColor searchBg = (uiTheme == 1) ? ImColor(245, 245, 250, (int)(255 * animation)) : ImColor(22, 22, 28, (int)(255 * animation));
        ImColor searchBorder = (uiTheme == 1) ? ImColor(206, 206, 218, (int)(255 * animation)) : ImColor(44, 44, 54, (int)(255 * animation));
        ImColor topBtnBg = (uiTheme == 1) ? ImColor(238, 238, 244, (int)(255 * animation)) : ImColor(22, 22, 28, (int)(255 * animation));
        ImColor topBtnBorder = searchBorder;
        float searchR = searchH / 2.0f;
        drawList->AddRectFilled(ImVec2(searchRect.x, searchRect.y), ImVec2(searchRect.z, searchRect.w), searchBg, searchR);
        if (searchAnim < 0.15f) {
            drawList->AddRect(ImVec2(searchRect.x, searchRect.y), ImVec2(searchRect.z, searchRect.w), searchBorder, searchR, 0, 1.4f);
        } else {
            drawList->AddRect(ImVec2(searchRect.x, searchRect.y), ImVec2(searchRect.z, searchRect.w), topBtnBorder, searchR, 0, 1.4f);
        }

        drawList->AddRectFilled(ImVec2(ircBtnRect.x, ircBtnRect.y), ImVec2(ircBtnRect.z, ircBtnRect.w), topBtnBg, searchR);
        drawList->AddRect(ImVec2(ircBtnRect.x, ircBtnRect.y), ImVec2(ircBtnRect.z, ircBtnRect.w), topBtnBorder, searchR, 0, 1.4f);

        drawList->AddRectFilled(ImVec2(themeBtnRect.x, themeBtnRect.y), ImVec2(themeBtnRect.z, themeBtnRect.w), topBtnBg, searchR);
        drawList->AddRect(ImVec2(themeBtnRect.x, themeBtnRect.y), ImVec2(themeBtnRect.z, themeBtnRect.w), topBtnBorder, searchR, 0, 1.4f);

        {
            std::string label = "IRC";
            float font = 0.85f * inScale;
            float tw = ImRenderUtils::getTextWidth(&label, font);
            float th = ImRenderUtils::getTextHeightStr(&label, font);
            float tx = ircBtnRect.x + ((ircBtnRect.z - ircBtnRect.x) - tw) * 0.5f;
            float ty = ircBtnRect.y + ((ircBtnRect.w - ircBtnRect.y) - th) * 0.5f;
            ImColor col = isIrcPanelOpen ? accentColor : textDim;
            ImRenderUtils::drawText(ImVec2(tx, ty), label, col, font, 1.0f, true);
        }

        float themeIconPad = 5.0f * inScale;
        float themeIconS = (searchH - themeIconPad * 2.0f) * 0.85f;
        float themeIconX = themeBtnRect.x + (searchH - themeIconS) / 2.0f;
        float themeIconY = themeBtnRect.y + (searchH - themeIconS) / 2.0f;
        if (themeIconTexture != nullptr) {
            drawList->AddImage(themeIconTexture, ImVec2(themeIconX, themeIconY), ImVec2(themeIconX + themeIconS, themeIconY + themeIconS), ImVec2(0, 0), ImVec2(1, 1), textDim);
        }

        float iconPad = 5.0f * inScale;
        float iconS = (searchH - iconPad * 2.0f) * 0.85f;
        float iconY = searchRect.y + (searchH - iconS) / 2.0f;
        float iconXCenter = searchRect.x + (searchW - iconS) / 2.0f;
        float iconXLeft = searchRect.x + (searchH - iconS) / 2.0f;
        float iconX = MathUtils::lerp(iconXCenter, iconXLeft, searchAnim);
        if (searchIconTexture != nullptr) {
            drawList->AddImage(searchIconTexture, ImVec2(iconX, iconY), ImVec2(iconX + iconS, iconY + iconS), ImVec2(0, 0), ImVec2(1, 1), textDim);
        }
//smooth naxui
        if (searchAnim > 0.15f) {
            float inputX = searchRect.x + searchH + 6.0f * inScale;
            float inputW = std::max(0.0f, searchRect.z - inputX - 12.0f * inScale);
            ImGui::SetNextWindowPos(ImVec2(searchRect.x, searchRect.y));
            ImGui::SetNextWindowSize(ImVec2(searchRect.z - searchRect.x, searchRect.w - searchRect.y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
            ImGui::Begin("##module_search_overlay", nullptr, winFlags);
            ImGui::PushID("module_search");
            float lineH = ImGui::GetTextLineHeight();
            float cursorY = std::floor((searchH - lineH) / 2.0f);
            ImGui::SetCursorPos(ImVec2(inputX - searchRect.x, cursorY));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(accentColor.Value.x, accentColor.Value.y, accentColor.Value.z, 0.35f));
            ImGui::SetNextItemWidth(inputW);
            if (searchWantsFocus && searchAnim > 0.85f) {
                ImGui::SetKeyboardFocusHere();
                searchWantsFocus = false;
            }
            ImGui::InputText("##q", searchText, IM_ARRAYSIZE(searchText));
            bool inputActive = ImGui::IsItemActive();
            ImGui::PopStyleColor(4);
            ImGui::PopID();
            ImGui::End();
            ImGui::PopStyleVar(4);

            float drawFont = 0.92f * inScale;
            static std::string cachedSearchText;
            static float cachedSearchFont = 0.0f;
            static float cachedSearchW = 0.0f;
            static float cachedSearchH = 0.0f;
            std::string_view sv(searchText);
            if (cachedSearchFont != drawFont || cachedSearchText.size() != sv.size() || std::memcmp(cachedSearchText.data(), sv.data(), sv.size()) != 0)
            {
                cachedSearchText.assign(sv);
                cachedSearchFont = drawFont;
                cachedSearchW = cachedSearchText.empty() ? 0.0f : ImRenderUtils::getTextWidth(&cachedSearchText, drawFont);
                cachedSearchH = cachedSearchText.empty() ? 0.0f : ImRenderUtils::getTextHeightStr(&cachedSearchText, drawFont);
            }
            float targetW = cachedSearchW;
            searchTextAnim = MathUtils::animate(targetW, searchTextAnim, deltaTime * 10.0f);
            searchTextAnim = std::clamp(searchTextAnim, 0.0f, inputW);

            float textY = std::floor(searchRect.y + (searchH - cachedSearchH) / 2.0f);
            float clipW = std::min(searchTextAnim, inputW);
            drawList->PushClipRect(ImVec2(inputX, searchRect.y), ImVec2(inputX + clipW, searchRect.w), true);
            ImRenderUtils::drawText(ImVec2(inputX, textY), cachedSearchText, textMain, drawFont, 1.0f, true, 0, drawList);
            drawList->PopClipRect();
            if (inputActive) {
                float caretW = 1.4f * inScale;
                float caretH = lineH * 0.92f;
                float caretX = inputX + clipW + 0.8f * inScale;
                float blink = std::fmod((float)ImGui::GetTime(), 1.0f);
                if (blink < 0.55f) {
                    drawList->AddRectFilled(ImVec2(caretX, searchRect.y + (searchH - caretH) / 2.0f), ImVec2(caretX + caretW, searchRect.y + (searchH + caretH) / 2.0f), textMain, 1.0f);
                }
            }
        }
    }

    themePickerAnim = MathUtils::animate(isThemePickerOpen ? 1.0f : 0.0f, themePickerAnim, deltaTime * 14.0f);
    if (themePickerAnim > 0.001f) {
        float t = std::clamp(themePickerAnim, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float scale = MathUtils::lerp(0.92f, 1.0f, eased);

        float panelW = themePickerWidth * inScale;
        float panelH = panelW + 74.0f * inScale;

        float panelX = themeBtnRect.x;
        float panelY = themeBtnRect.w + 8.0f * inScale;

        float minX = mainRect.x + 12.0f * inScale;
        float maxX = mainRect.z - 12.0f * inScale - panelW;
        panelX = std::clamp(panelX, minX, maxX);

        float maxY = mainRect.w - 12.0f * inScale - panelH;
        if (panelY > maxY) {
            panelY = themeBtnRect.y - 8.0f * inScale - panelH;
        }

        float cx = panelX + panelW / 2.0f;
        float cy = panelY + panelH / 2.0f;
        float w = panelW * scale;
        float h = panelH * scale;
        themePanelRect = ImVec4(cx - w / 2.0f, cy - h / 2.0f, cx + w / 2.0f, cy + h / 2.0f);

        if (isThemePickerOpen && themePickerAnim > 0.65f && isEnabledRaw && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
            if (!isMouseOver(themePanelRect) && !isMouseOver(themeBtnRect)) {
                isThemePickerOpen = false;
            }
        }
    }

    if (themePickerAnim > 0.01f) {
        isEnabled = false;
    }

    if (!showConfigControls) isConfigDropdownOpen = false;
    configDropdownAnim = MathUtils::animate(isConfigDropdownOpen ? 1.0f : 0.0f, configDropdownAnim, deltaTime * 14.0f);
    if (configDropdownAnim > 0.01f) {
        isEnabled = false;
    }

    float enumOverlayAnim = 0.0f;
    if (openEnumSetting != nullptr)
    {
        auto it = enumDropdownAnims.find(openEnumSetting);
        if (it != enumDropdownAnims.end()) enumOverlayAnim = it->second;
    }
    const bool enumModalActive = enumOverlayAnim > 0.01f;
    if (enumModalActive) {
        isEnabled = false;
    }

    float uiDelta = deltaTime;
    if (uiDelta > (1.0f / 30.0f)) uiDelta = (1.0f / 30.0f);

    if (isCategorySwitching) {
        if (isCategoryFadingOut) {
            categoryFade = MathUtils::animate(0.0f, categoryFade, uiDelta * 14.0f);
            if (categoryFade < 0.02f) {
                currentCategoryIndex = (targetCategoryIndex != -1) ? targetCategoryIndex : currentCategoryIndex;
                targetCategoryIndex = -1;
                mScrollTarget = 0.0f;
                mScrollY = 0.0f;
                isCategoryFadingOut = false;
            }
        }
        else {
            categoryFade = MathUtils::animate(1.0f, categoryFade, uiDelta * 14.0f);
            if (categoryFade > 0.98f) {
                isCategorySwitching = false;
                categoryFade = 1.0f;
            }
        }
    }        
    bool categoryChanged = (oldCategoryIndex != currentCategoryIndex);
    if (categoryChanged) {
        isTargetEspPanelOpen = false;
        isAnimationsPanelOpen = false;
        isIrcPanelOpen = false;
    }
    const int visualCategoryIndex = (int)ModuleCategory::Visual;
    const bool isVisualCategory = (currentCategoryIndex == visualCategoryIndex);
    if (!isVisualCategory) {
        isTargetEspPanelOpen = false;
        isAnimationsPanelOpen = false;
    }
    const int miscCategoryIndex = (int)ModuleCategory::Misc;
    const bool isMiscCategory = (currentCategoryIndex == miscCategoryIndex);
    if (!isMiscCategory) {
        isIrcPanelOpen = false;
    }
    const bool targetEspPanelTargetOn = (isVisualCategory && isTargetEspPanelOpen);
    const float targetEspPanelSpeed = uiDelta * (targetEspPanelTargetOn ? 6.0f : 18.0f);
    targetEspPanelAnim = MathUtils::animate(targetEspPanelTargetOn ? 1.0f : 0.0f, targetEspPanelAnim, targetEspPanelSpeed);
    const bool animationsPanelTargetOn = (isVisualCategory && isAnimationsPanelOpen);
    const float animationsPanelSpeed = uiDelta * (animationsPanelTargetOn ? 6.0f : 18.0f);
    animationsPanelAnim = MathUtils::animate(animationsPanelTargetOn ? 1.0f : 0.0f, animationsPanelAnim, animationsPanelSpeed);
    const bool ircPanelTargetOn = (isMiscCategory && isIrcPanelOpen);
    ircPanelAnim = MathUtils::animate(ircPanelTargetOn ? 1.0f : 0.0f, ircPanelAnim, uiDelta * 14.0f);

    const float panelActiveThreshold = 0.05f;
    bool panelActiveNow = (targetEspPanelAnim > panelActiveThreshold) || (animationsPanelAnim > panelActiveThreshold) || (ircPanelAnim > panelActiveThreshold);

    const bool espPreviewTargetOn = isVisualCategory && !panelActiveNow;
    const float espPreviewSpeed = uiDelta * (espPreviewTargetOn ? 5.0f : 16.0f);
    espPreviewAnim = MathUtils::animate(espPreviewTargetOn ? 1.0f : 0.0f, espPreviewAnim, espPreviewSpeed);

    if (targetEspPanelAnim > panelActiveThreshold) isEnabled = false;
    if (animationsPanelAnim > panelActiveThreshold) isEnabled = false;
    if (ircPanelAnim > panelActiveThreshold) isEnabled = false;
    if (targetEspPanelAnim <= 0.01f) {
        isTargetEspModeSwitching = false;
        isTargetEspModeFadingOut = false;
        targetEspModeFade = 1.0f;
        targetEspModeShown = -1;
        targetEspModeTarget = -1;
    }
    static float moduleListAppearAnim = 1.0f;
    static bool moduleListPrevPanelActive = false;
    if (moduleListPrevPanelActive && !panelActiveNow) {
        moduleListAppearAnim = 0.0f;
    }
    moduleListPrevPanelActive = panelActiveNow;
    if (panelActiveNow) {
        moduleListAppearAnim = 0.0f;
    }
    else {
        moduleListAppearAnim = MathUtils::animate(1.0f, moduleListAppearAnim, uiDelta * 14.0f);
    }
    float moduleListT = std::clamp(moduleListAppearAnim, 0.0f, 1.0f);
    float moduleListEased = moduleListT * moduleListT * (3.0f - 2.0f * moduleListT);
    float moduleListSlideY = (1.0f - moduleListEased) * 18.0f * inScale;
    float contentX = sidebarRect.z + 12.0f * inScale;
    float contentY = bodyTopY;
    float previewGap = 16.0f * inScale;
    float previewWidth = 210.0f * inScale;
    float contentW = (mainRect.z - 12.0f * inScale) - contentX;
    float contentH = bodyBottomY - contentY;
    ImVec4 contentRect = ImVec4(contentX, contentY, contentX + contentW, contentY + contentH);

    float previewHeight = contentH * 0.75f;
    float mainCy = (mainRect.y + mainRect.w) * 0.5f;
    ImVec2 espPreviewPos = ImVec2(
        mainRect.z + previewGap,
        mainCy - previewHeight * 0.5f
    );
    ImVec2 espPreviewSize = ImVec2(previewWidth, previewHeight);
    ImVec4 espPreviewRect = ImVec4(
        espPreviewPos.x,
        espPreviewPos.y,
        espPreviewPos.x + espPreviewSize.x,
        espPreviewPos.y + espPreviewSize.y
    );
    float contentSlideY = 0.0f;
    if (isCategorySwitching) {
        float inv = 1.0f - categoryFade;
        contentSlideY = inv * 18.0f * inScale;
    }

    {
        static ID3D11ShaderResourceView* notchTexture = nullptr;
        static int notchW = 0;
        static int notchH = 0;
        static bool notchLoaded = false;
        if (!notchLoaded) {
            D3DHook::loadTextureFromEmbeddedResource("notch.png", &notchTexture, &notchW, &notchH);
            notchLoaded = true;
        }

        float boxSize = logoBoxSize;
        float boxX = topBarRect.x + 10.0f * inScale;
        float boxY = topBarRect.y + (topBarHeight - boxSize) / 2.0f;
        ImVec2 boxMin = ImVec2(boxX, boxY);
        ImVec2 boxMax = ImVec2(boxX + boxSize, boxY + boxSize);

        ImColor backgroundColor = ColorUtils::getUiCardColor(animation);
        ImColor borderColor = ColorUtils::getUiBorderColor((16.0f / 255.0f) * animation);
        float rounding = 10.0f * inScale;
        float borderThickness = 1.2f;

        drawList->AddRectFilled(boxMin, boxMax, backgroundColor, rounding);
        drawList->AddShadowRect(boxMin, boxMax, ImColor(0, 0, 0, (int)(170 * animation)), 55.0f * inScale, ImVec2(0.f, 0.f), 0, rounding);
        drawList->AddRect(boxMin, boxMax, borderColor, rounding, 0, borderThickness);

        if (notchTexture && notchW > 0 && notchH > 0)
        {
            float pad = 6.0f * inScale;
            float iconSize = std::max(8.0f * inScale, boxSize - pad * 2.0f);
            ImVec2 iconMin = ImVec2((boxMin.x + boxMax.x) * 0.5f - iconSize * 0.5f, (boxMin.y + boxMax.y) * 0.5f - iconSize * 0.5f);
            ImVec2 iconMax = ImVec2(iconMin.x + iconSize, iconMin.y + iconSize);
            ImVec2 iconCenter = ImVec2((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
            const float nudgeX = 1.0f * inScale;
            const float nudgeY = -1.0f * inScale;
            iconMin.x += nudgeX;
            iconMax.x += nudgeX;
            iconMin.y += nudgeY;
            iconMax.y += nudgeY;
            iconCenter.x += nudgeX;
            iconCenter.y += nudgeY;
            ImColor glow = accentColor;
            glow.Value.w = 0.90f;
            drawList->AddShadowCircle(iconCenter, iconSize * 0.24f, glow, 40, ImVec2(0.f, 0.f), 0, 64);
            ImColor tint = accentColor;
            tint.Value.w = std::clamp(animation, 0.0f, 1.0f);
            drawList->AddImage(notchTexture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), ImGui::ColorConvertFloat4ToU32(tint.Value));
        }

        float gapY = 2.0f * inScale;

        float titleH = ImRenderUtils::getTextHeightStr(&titleText, titleFont);
        float subH = ImRenderUtils::getTextHeightStr(&subText, subFont);
        float totalH = titleH + gapY + subH;
        float startY = topBarRect.y + (topBarHeight - totalH) * 0.5f;

        ImRenderUtils::drawText(ImVec2(textX, startY), titleText, textMain, titleFont, 1.0f, true, 0, drawList);
        ImRenderUtils::drawText(ImVec2(textX, startY + titleH + gapY), subText, textDim, subFont, 1.0f, true, 0, drawList);
    }

    if (showConfigControls)
    {
        bool configInputEnabled = isEnabledRaw && !isBinding;
        bool saveHovered = isMouseOver(configSaveRect) && configInputEnabled;
        bool dropHovered = isMouseOver(configDropRect) && configInputEnabled;

        ImColor ctrlBg = (uiTheme == 1) ? ImColor(238, 238, 244, (int)(255 * animation)) : ImColor(22, 22, 28, (int)(255 * animation));
        ImColor ctrlBgHover = (uiTheme == 1) ? ImColor(230, 230, 238, (int)(255 * animation)) : ImColor(30, 30, 38, (int)(255 * animation));
        ImColor ctrlBorder = (uiTheme == 1) ? ImColor(176, 176, 190, (int)(255 * animation)) : ImColor(28, 28, 38, (int)(255 * animation));
        float ctrlR = (configSaveRect.w - configSaveRect.y) * 0.5f;

        ImColor saveBg = saveHovered ? ctrlBgHover : ctrlBg;
        ImColor dropBg = dropHovered ? ctrlBgHover : ctrlBg;

        {
            float pad = 5.0f * inScale;
            ImVec4 groupRect = ImVec4(configSaveRect.x - pad, configSaveRect.y - pad, configDropRect.z + pad, configSaveRect.w + pad);
            ImColor groupBg = (uiTheme == 1) ? ImColor(246, 246, 250, (int)(220 * animation)) : ImColor(14, 14, 18, (int)(210 * animation));
            ImColor groupBorder = (uiTheme == 1) ? ImColor(172, 172, 186, (int)(240 * animation)) : ImColor(26, 26, 34, (int)(230 * animation));
            float groupR = ctrlR + pad * 0.6f;
            drawList->AddRectFilled(ImVec2(groupRect.x, groupRect.y), ImVec2(groupRect.z, groupRect.w), groupBg, groupR);
            drawList->AddRect(ImVec2(groupRect.x, groupRect.y), ImVec2(groupRect.z, groupRect.w), groupBorder, groupR, 0, 1.2f);
        }

        drawList->AddRectFilled(ImVec2(configSaveRect.x, configSaveRect.y), ImVec2(configSaveRect.z, configSaveRect.w), saveBg, ctrlR);
        drawList->AddRect(ImVec2(configSaveRect.x, configSaveRect.y), ImVec2(configSaveRect.z, configSaveRect.w), ctrlBorder, ctrlR, 0, 1.4f);
        drawList->AddRectFilled(ImVec2(configDropRect.x, configDropRect.y), ImVec2(configDropRect.z, configDropRect.w), dropBg, ctrlR);
        drawList->AddRect(ImVec2(configDropRect.x, configDropRect.y), ImVec2(configDropRect.z, configDropRect.w), ctrlBorder, ctrlR, 0, 1.4f);

        {
            std::string saveLabel = "Save";
            float font = 0.80f * inScale;
            float tw = ImRenderUtils::getTextWidth(&saveLabel, font);
            float th = ImRenderUtils::getTextHeightStr(&saveLabel, font);
            float tx = configSaveRect.x + ((configSaveRect.z - configSaveRect.x) - tw) * 0.5f;
            float ty = configSaveRect.y + ((configSaveRect.w - configSaveRect.y) - th) * 0.5f;
            ImRenderUtils::drawText(ImVec2(tx, ty), saveLabel, textMain, font, 1.0f, true);
        }

        {
            float font = 0.85f * inScale;
            float padX = 8.0f * inScale;
            float padY = 5.0f * inScale;
            float arrowW = 7.0f * inScale;
            float arrowH = 4.5f * inScale;
            float arrowX = configDropRect.z - padX - arrowW;
            float arrowY = configDropRect.y + (configDropRect.w - configDropRect.y - arrowH) * 0.5f;

            ImVec2 a = ImVec2(arrowX, arrowY);
            ImVec2 b = ImVec2(arrowX + arrowW, arrowY);
            ImVec2 c = ImVec2(arrowX + arrowW * 0.5f, arrowY + arrowH);
            drawList->AddTriangleFilled(a, b, c, textDim);

            float textX0 = configDropRect.x + padX;
            float textX1 = arrowX - padX;
            float textY = configDropRect.y + (configDropRect.w - configDropRect.y - ImRenderUtils::getTextHeightStr(&displayConfigName, font)) * 0.5f;
            drawList->PushClipRect(ImVec2(textX0, configDropRect.y + padY), ImVec2(textX1, configDropRect.w - padY), true);
            ImRenderUtils::drawText(ImVec2(textX0, textY), displayConfigName, textMain, font, 1.0f, true);
            drawList->PopClipRect();
        }

        if (configInputEnabled && ImGui::IsMouseClicked(0))
        {
            if (saveHovered)
            {
                std::string name = displayConfigName;
                if (name == "No configs") name.clear();
                if (name.empty() && !ConfigManager::LastLoadedConfig.empty()) name = ConfigManager::LastLoadedConfig;
                if (name.empty())
                {
                    NotifyUtils::notify("No config selected.", 3.f, Notification::Type::Warning);
                }
                else
                {
                    ConfigManager::saveConfig(name);
                    selectedConfigName = name;
                    if (configSelectedIndex < 0 || configSelectedIndex >= (int)configNames.size() || configNames[configSelectedIndex] != name)
                    {
                        configSelectedIndex = -1;
                        for (size_t i = 0; i < configNames.size(); ++i)
                        {
                            if (configNames[i] == name)
                            {
                                configSelectedIndex = (int)i;
                                break;
                            }
                        }
                    }
                }
            }
            else if (dropHovered)
            {
                bool next = !isConfigDropdownOpen;
                isConfigDropdownOpen = next;
                if (next)
                {
                    refreshConfigList();
                    configSearchText[0] = 0;
                    configSearchWantsFocus = true;
                    isThemePickerOpen = false;
                    isSearching = false;
                    searchText[0] = 0;
                    searchTextAnim = 0.0f;
                    openEnumSetting = nullptr;
                }
            }
        }
    }

    if (showConfigControls && configDropdownAnim > 0.001f)
    {
        float t = std::clamp(configDropdownAnim, 0.0f, 1.0f);

        float innerPad = 9.0f * inScale;
        float rowH = 32.0f * inScale;
        float rowGap = 5.0f * inScale;
        float searchH = 32.0f * inScale;
        float searchGap = 10.0f * inScale;
        float headerH = 26.0f * inScale;
        float headerGap = 6.0f * inScale;
        float panelW = 240.0f * inScale;
        float panelMaxW = (mainRect.z - mainRect.x) - 24.0f * inScale;
        if (panelMaxW > 0.0f) panelW = std::min(panelW, panelMaxW);
        float panelX = (configSaveRect.x + configDropRect.z) * 0.5f - panelW * 0.5f;
        float panelY = (configSaveRect.y + configSaveRect.w) * 0.5f;

        std::vector<size_t> filtered;
        std::string searchQuery = configSearchText;
        if (!searchQuery.empty())
        {
            for (size_t i = 0; i < configNames.size(); ++i)
            {
                if (StringUtils::containsIgnoreCase(configNames[i], searchQuery))
                {
                    filtered.push_back(i);
                }
            }
        }

        size_t listCount = configNames.empty() ? 0 : (searchQuery.empty() ? configNames.size() : filtered.size());
        size_t displayCount = 3;
        float panelH = innerPad * 2.0f + headerH + headerGap * 2.0f + searchH + searchGap + (rowH + rowGap) * (float)displayCount;
        float minX = mainRect.x + 12.0f * inScale;
        float maxX = mainRect.z - 12.0f * inScale - panelW;
        panelX = std::clamp(panelX, minX, maxX);

        float minY = mainRect.y + 12.0f * inScale;
        float maxY = mainRect.w - 12.0f * inScale - panelH;
        panelY = std::clamp(panelY - panelH * 0.5f, minY, maxY);

        auto snap = [](float v) { return std::round(v * 2.0f) / 2.0f; };
        panelW = snap(panelW);
        panelX = snap(panelX);
        panelY = snap(panelY);

        float drawH = panelH;
        configPanelRect = ImVec4(panelX, panelY, panelX + panelW, panelY + drawH);

        float panelAlpha = std::clamp(animation * t, 0.0f, 1.0f);
        ImColor panelBg = bgColor;
        panelBg.Value.x = std::min(1.0f, panelBg.Value.x + 0.01f);
        panelBg.Value.y = std::min(1.0f, panelBg.Value.y + 0.01f);
        panelBg.Value.z = std::min(1.0f, panelBg.Value.z + 0.01f);
        panelBg.Value.w = panelAlpha;
        ImColor nonActiveBorder = (uiTheme == 1)
            ? ImColor(168, 168, 186, (int)(255 * panelAlpha))
            : ImColor(24, 24, 34, (int)(255 * panelAlpha));
        configSortAnim = 0.0f;
        float sortEase = 0.0f;
        float listAlpha = panelAlpha;

        ImColor panelBorder = nonActiveBorder;
        float panelR = 13.0f * inScale;

        ImDrawList* panelList = ImGui::GetForegroundDrawList();
        panelList->AddRectFilled(ImVec2(configPanelRect.x, configPanelRect.y), ImVec2(configPanelRect.z, configPanelRect.w), panelBg, panelR);
        panelList->AddRect(ImVec2(configPanelRect.x, configPanelRect.y), ImVec2(configPanelRect.z, configPanelRect.w), panelBorder, panelR, 0, 1.4f);

        panelList->PushClipRect(ImVec2(configPanelRect.x, configPanelRect.y), ImVec2(configPanelRect.z, configPanelRect.w), true);

        float iconGap = 6.0f * inScale;
        float iconBtnW = headerH;
        float headerY = panelY + innerPad;
        float headerX = panelX + innerPad;
        float headerRight = panelX + panelW - innerPad;
        ImVec4 plusBtnRect = ImVec4(headerRight - iconBtnW, headerY, headerRight, headerY + headerH);
        float searchTop = headerY + headerH + headerGap * 2.0f;
        float sortBtnW = searchH;
        float sortBtnX1 = panelX + panelW - innerPad;
        float sortBtnX0 = sortBtnX1 - sortBtnW;
        ImVec4 sortBtnRect = ImVec4(sortBtnX0, searchTop, sortBtnX1, searchTop + searchH);
        float searchRight = std::max(panelX + innerPad, sortBtnX0 - iconGap);
        ImVec4 searchRect = ImVec4(panelX + innerPad, searchTop, searchRight, searchTop + searchH);
        float searchIconPad = 10.0f * inScale;
        float searchIconSize = std::max(16.0f * inScale, searchH - searchIconPad * 2.0f);
        float searchIconX = searchRect.x + searchIconPad;
        float searchIconY = searchRect.y + (searchH - searchIconSize) * 0.5f;
        float searchTextStartX = searchIconX + searchIconSize + 10.0f * inScale;
        ImColor searchBg = (uiTheme == 1) ? ImColor(235, 235, 242, (int)(255 * panelAlpha)) : ImColor(14, 14, 20, (int)(255 * panelAlpha));
        ImColor searchBorder = nonActiveBorder;
        float searchR = 6.5f * inScale;
        panelList->AddRectFilled(ImVec2(searchRect.x, searchRect.y), ImVec2(searchRect.z, searchRect.w), searchBg, searchR);
        panelList->AddRect(ImVec2(searchRect.x, searchRect.y), ImVec2(searchRect.z, searchRect.w), searchBorder, searchR, 0, 1.2f);
        {
            std::string searchIcon = "g";
            float iconFontSize = 0.95f * inScale;
            ImColor iconCol = textDim;
            iconCol.Value.w = std::clamp(iconCol.Value.w * panelAlpha, 0.0f, 1.0f);
            auto itFont = FontHelper::Fonts.find("essence.ttf");
            if (itFont != FontHelper::Fonts.end() && itFont->second)
            {
                ImGui::PushFont(itFont->second);
            }
            float tw = ImRenderUtils::getTextWidth(&searchIcon, iconFontSize);
            float th = ImRenderUtils::getTextHeightStr(&searchIcon, iconFontSize);
            float tx = searchIconX + (searchIconSize - tw) * 0.5f;
            float ty = searchIconY + (searchIconSize - th) * 0.5f;
            ImRenderUtils::drawText(ImVec2(tx, ty), searchIcon, iconCol, iconFontSize, panelAlpha, true, 0, panelList);
            if (itFont != FontHelper::Fonts.end() && itFont->second)
            {
                ImGui::PopFont();
            }
        }

        {
            std::string headerText = "Presets";
            float headerFont = 0.82f * inScale;
            float headerTextW = ImRenderUtils::getTextWidth(&headerText, headerFont);
            float headerTextH = ImRenderUtils::getTextHeightStr(&headerText, headerFont);
            float headerIconSize = headerH * 0.55f;
            float headerPadX = 8.0f * inScale;
            float headerGapX = 6.0f * inScale;
            float headerW = headerPadX * 2.0f + headerIconSize + headerGapX + headerTextW;
            ImVec4 headerRect = ImVec4(headerX, headerY, headerX + headerW, headerY + headerH);
            ImColor headerBg = panelBg;
            float lift = (uiTheme == 1) ? 0.02f : 0.08f;
            headerBg.Value.x = std::min(1.0f, headerBg.Value.x + lift);
            headerBg.Value.y = std::min(1.0f, headerBg.Value.y + lift);
            headerBg.Value.z = std::min(1.0f, headerBg.Value.z + lift);
            headerBg.Value.w = std::clamp(panelAlpha * 0.95f, 0.0f, 1.0f);
            panelList->AddRectFilled(ImVec2(headerRect.x, headerRect.y), ImVec2(headerRect.z, headerRect.w), headerBg, 6.0f * inScale);

            int kbW = 0;
            int kbH = 0;
            ID3D11ShaderResourceView* kbTex = getGuiTexture("kbsmell.png", &kbW, &kbH);
            float iconX = headerRect.x + headerPadX;
            float iconY = headerRect.y + (headerH - headerIconSize) * 0.5f;
            if (kbTex != nullptr && kbW > 0 && kbH > 0)
            {
                ImColor iconTint = accentColor;
                iconTint.Value.w = std::clamp(iconTint.Value.w * panelAlpha, 0.0f, 1.0f);
                panelList->AddImage(kbTex, ImVec2(iconX, iconY), ImVec2(iconX + headerIconSize, iconY + headerIconSize), ImVec2(0, 0), ImVec2(1, 1), iconTint);
            }

            float textX = iconX + headerIconSize + headerGapX;
            float textY = headerRect.y + (headerH - headerTextH) * 0.5f;
            ImColor headerTextCol = accentColor;
            headerTextCol.Value.w = std::clamp(headerTextCol.Value.w * panelAlpha, 0.0f, 1.0f);
            ImRenderUtils::drawText(ImVec2(textX, textY), headerText, headerTextCol, headerFont, panelAlpha, true, 0, panelList);
        }
        {
            std::string plusIcon = "+";
            float iconFontSize = 0.95f * inScale;
            ImColor iconCol = accentColor;
            iconCol.Value.w = std::clamp(iconCol.Value.w * panelAlpha, 0.0f, 1.0f);
            float tw = ImRenderUtils::getTextWidth(&plusIcon, iconFontSize);
            float th = ImRenderUtils::getTextHeightStr(&plusIcon, iconFontSize);
            float tx = plusBtnRect.x + ((plusBtnRect.z - plusBtnRect.x) - tw) * 0.5f;
            float ty = plusBtnRect.y + ((plusBtnRect.w - plusBtnRect.y) - th) * 0.5f;
            ImRenderUtils::drawText(ImVec2(tx, ty), plusIcon, iconCol, iconFontSize, panelAlpha, true, 0, panelList);
        }
        {
            std::string sortIcon = "l";
            float iconFontSize = 0.85f * inScale;
            ImColor iconCol = textDim;
            iconCol.Value.w = std::clamp(iconCol.Value.w * panelAlpha, 0.0f, 1.0f);
            auto itFont = FontHelper::Fonts.find("icons2.ttf");
            if (itFont != FontHelper::Fonts.end() && itFont->second)
            {
                ImGui::PushFont(itFont->second);
            }
            float tw = ImRenderUtils::getTextWidth(&sortIcon, iconFontSize);
            float th = ImRenderUtils::getTextHeightStr(&sortIcon, iconFontSize);
            float tx = sortBtnRect.x + ((sortBtnRect.z - sortBtnRect.x) - tw) * 0.5f;
            float ty = sortBtnRect.y + ((sortBtnRect.w - sortBtnRect.y) - th) * 0.5f;
            ImRenderUtils::drawText(ImVec2(tx, ty), sortIcon, iconCol, iconFontSize, panelAlpha, true, 0, panelList);
            if (itFont != FontHelper::Fonts.end() && itFont->second)
            {
                ImGui::PopFont();
            }
        }

        {
            float lineY = headerY + headerH + headerGap;
            ImColor sep = panelBorder;
            panelList->AddLine(ImVec2(panelX + innerPad, lineY), ImVec2(panelX + panelW - innerPad, lineY), sep, 1.0f);
        }

        {
            float lineY = searchRect.w + searchGap * 0.5f;
            ImColor sep = panelBorder;
            panelList->AddLine(ImVec2(panelX + innerPad, lineY), ImVec2(panelX + panelW - innerPad, lineY), sep, 1.0f);
        }

        {
            ImGui::SetNextWindowPos(ImVec2(searchRect.x, searchRect.y));
            ImGui::SetNextWindowSize(ImVec2(searchRect.z - searchRect.x, searchRect.w - searchRect.y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
            ImGui::Begin("##config_search_overlay", nullptr, winFlags);
            ImGui::PushID("config_search");
            float lineH = ImGui::GetTextLineHeight();
            float cursorY = std::floor((searchH - lineH) * 0.5f);
            float padX = searchTextStartX - searchRect.x;
            ImGui::SetCursorPos(ImVec2(padX, cursorY));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(accentColor.Value.x, accentColor.Value.y, accentColor.Value.z, 0.35f));
            ImGui::SetNextItemWidth((searchRect.z - searchRect.x) - padX - (8.0f * inScale));
            if (configSearchWantsFocus && configDropdownAnim > 0.85f) {
                ImGui::SetKeyboardFocusHere();
                configSearchWantsFocus = false;
            }
            ImGui::InputText("##q", configSearchText, IM_ARRAYSIZE(configSearchText));
            bool inputActive = ImGui::IsItemActive();
            ImGui::PopStyleColor(4);
            ImGui::PopID();
            ImGui::End();
            ImGui::PopStyleVar(4);

            std::string searchTextStr = configSearchText;
            std::string placeholder = "Search";
            std::string& drawStr = searchTextStr.empty() ? placeholder : searchTextStr;
            ImColor drawCol = searchTextStr.empty() ? textDim : textMain;
            drawCol.Value.w = std::clamp(drawCol.Value.w * panelAlpha, 0.0f, 1.0f);
            float font = 0.86f * inScale;
            float textY = searchRect.y + (searchH - ImRenderUtils::getTextHeightStr(&drawStr, font)) * 0.5f;
            float textX0 = searchRect.x + padX;
            float textX1 = searchRect.z - (8.0f * inScale);
            panelList->PushClipRect(ImVec2(textX0, searchRect.y), ImVec2(textX1, searchRect.w), true);
            ImRenderUtils::drawText(ImVec2(textX0, textY), drawStr, drawCol, font, panelAlpha, true, 0, panelList);
            panelList->PopClipRect();

            if (inputActive && !searchTextStr.empty())
            {
                float caretW = 1.4f * inScale;
                float caretH = lineH * 0.92f;
                float textW = ImRenderUtils::getTextWidth(&searchTextStr, font);
                float caretX = textX0 + std::min(textW, (textX1 - textX0 - caretW));
                float blink = std::fmod((float)ImGui::GetTime(), 1.0f);
                if (blink < 0.55f) {
                    ImColor caret = textMain;
                    caret.Value.w = std::clamp(caret.Value.w * panelAlpha, 0.0f, 1.0f);
                    panelList->AddRectFilled(ImVec2(caretX, searchRect.y + (searchH - caretH) * 0.5f), ImVec2(caretX + caretW, searchRect.y + (searchH + caretH) * 0.5f), caret, 1.0f);
                }
            }
        }

        bool sortHovered = isMouseOver(sortBtnRect) && isEnabledRaw && !isBinding;
        bool plusHovered = isMouseOver(plusBtnRect) && isEnabledRaw && !isBinding;
        if (ImGui::IsMouseClicked(0))
        {
            if (sortHovered)
            {
                configSortLengthDesc = !configSortLengthDesc;
                refreshConfigList();
            }
            else if (plusHovered)
            {
                int index = 1;
                std::string name;
                do
                {
                    name = "Preset" + std::to_string(index++);
                } while (ConfigManager::configExists(name));
                ConfigManager::saveConfig(name);
                refreshConfigList();
                selectedConfigName = name;
                configSelectedIndex = -1;
                for (size_t i = 0; i < configNames.size(); ++i)
                {
                    if (configNames[i] == name)
                    {
                        configSelectedIndex = (int)i;
                        break;
                    }
                }
            }
        }

        float listStartY = searchRect.w + searchGap + 2.0f * inScale;
        float listViewH = (rowH + rowGap) * (float)displayCount - rowGap;
        ImVec4 listRect = ImVec4(panelX + innerPad, listStartY, panelX + panelW - innerPad, listStartY + listViewH);
        if (isMouseOver(listRect) && isEnabledRaw && !isBinding && scrollDirection != 0)
        {
            configListScrollTarget += scrollDirection * 60.0f;
            scrollDirection = 0;
        }
        float listContentH = listCount > 0 ? (rowH + rowGap) * (float)listCount - rowGap : rowH;
        float maxListScroll = std::max(0.0f, listContentH - listViewH);
        if (maxListScroll <= 0.0f)
        {
            configListScrollTarget = 0.0f;
            configListScroll = 0.0f;
        }
        else
        {
            configListScrollTarget = std::clamp(configListScrollTarget, 0.0f, maxListScroll);
            configListScroll = MathUtils::animate(configListScrollTarget, configListScroll, deltaTime * 12.0f);
        }
        panelList->PushClipRect(ImVec2(listRect.x, listRect.y), ImVec2(listRect.z, listRect.w), true);
        if (listCount == 0)
        {
            std::string emptyLabel = configNames.empty() ? "No configs" : "No results";
            float font = 0.86f * inScale;
            float tw = ImRenderUtils::getTextWidth(&emptyLabel, font);
            float th = ImRenderUtils::getTextHeightStr(&emptyLabel, font);
            float tx = panelX + (panelW - tw) * 0.5f;
            float ty = listStartY + (listViewH - th) * 0.5f;
            ImColor emptyCol = textDim;
            emptyCol.Value.w = std::clamp(emptyCol.Value.w * listAlpha, 0.0f, 1.0f);
            ImRenderUtils::drawText(ImVec2(tx, ty), emptyLabel, emptyCol, font, listAlpha, true, 0, panelList);
        }
        else
        {
            bool configInputEnabled = isEnabledRaw && !isBinding;
            size_t count = searchQuery.empty() ? configNames.size() : filtered.size();
            if (configRowAnims.size() > configNames.size())
            {
                for (auto it = configRowAnims.begin(); it != configRowAnims.end();)
                {
                    if (std::find(configNames.begin(), configNames.end(), it->first) == configNames.end())
                    {
                        it = configRowAnims.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            if (configRowY.size() > configNames.size())
            {
                for (auto it = configRowY.begin(); it != configRowY.end();)
                {
                    if (std::find(configNames.begin(), configNames.end(), it->first) == configNames.end())
                    {
                        it = configRowY.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            for (size_t i = 0; i < count; ++i)
            {
                size_t idx = searchQuery.empty() ? i : filtered[i];
                float targetY = listStartY + (rowH + rowGap) * (float)i - configListScroll;
                float& rowY = configRowY[configNames[idx]];
                if (rowY == 0.0f)
                {
                    rowY = targetY;
                }
                rowY = MathUtils::animate(targetY, rowY, deltaTime * 18.0f);
                ImVec4 rowRect = ImVec4(panelX + innerPad, rowY, panelX + panelW - innerPad, rowY + rowH);
                bool hovered = configInputEnabled && isMouseOver(rowRect);
                bool selected = (configNames[idx] == selectedConfigName);

                ImColor rowBase = panelBg;
                rowBase.Value.w = std::clamp(0.9f * listAlpha, 0.0f, 1.0f);
                panelList->AddRectFilled(ImVec2(rowRect.x, rowRect.y), ImVec2(rowRect.z, rowRect.w), rowBase, 7.0f * inScale);

                float& rowSel = configRowAnims[configNames[idx]];
                rowSel = MathUtils::animate(selected ? 1.0f : 0.0f, rowSel, deltaTime * 12.0f);
                float selEase = rowSel * rowSel * (3.0f - 2.0f * rowSel);
                if (selEase > 0.001f)
                {
                    ImColor rowBg = (uiTheme == 1)
                        ? ImColor(230, 230, 240, (int)(170 * listAlpha * selEase))
                        : ImColor(28, 28, 36, (int)(150 * listAlpha * selEase));
                    panelList->AddRectFilled(ImVec2(rowRect.x, rowRect.y), ImVec2(rowRect.z, rowRect.w), rowBg, 7.0f * inScale);
                }
                ImColor rowBorder = panelBorder;
                {
                    ImColor baseBorder = nonActiveBorder;
                    ImColor accent = accentColor;
                    rowBorder = MathUtils::lerpImColor(baseBorder, accent, selEase);
                }
                rowBorder.Value.w = std::clamp(rowBorder.Value.w * listAlpha, 0.0f, 1.0f);
                panelList->AddRect(ImVec2(rowRect.x, rowRect.y), ImVec2(rowRect.z, rowRect.w), rowBorder, 7.0f * inScale, 0, 1.2f);

                float font = 0.92f * inScale;
                float iconD = rowH * 0.62f;
                float iconPadX = 10.0f * inScale;
                float iconGapRow = 6.0f * inScale;
                ImVec4 loadRect = ImVec4(rowRect.z - iconPadX - iconD, rowRect.y + (rowH - iconD) * 0.5f, rowRect.z - iconPadX, rowRect.y + (rowH + iconD) * 0.5f);
                ImVec4 deleteRect = ImVec4(loadRect.x - iconGapRow - iconD, loadRect.y, loadRect.x - iconGapRow, loadRect.w);
                float textClipRight = deleteRect.x - 6.0f * inScale;
                float textY = rowRect.y + (rowH - ImRenderUtils::getTextHeightStr(&configNames[idx], font)) * 0.5f;
                ImColor dim = textDim;
                ImColor main = textMain;
                ImColor rowCol = MathUtils::lerpImColor(dim, main, 0.4f * selEase);
                rowCol.Value.w = std::clamp(rowCol.Value.w * listAlpha, 0.0f, 1.0f);
                panelList->PushClipRect(ImVec2(rowRect.x, rowRect.y), ImVec2(textClipRight, rowRect.w), true);
                ImRenderUtils::drawText(ImVec2(rowRect.x + 14.0f * inScale, textY), configNames[idx], rowCol, font, listAlpha, true, 0, panelList);
                panelList->PopClipRect();

                ImColor iconCol = textDim;
                iconCol.Value.w = std::clamp(iconCol.Value.w * listAlpha, 0.0f, 1.0f);
                auto itFont = FontHelper::Fonts.find("icons2.ttf");
                if (itFont != FontHelper::Fonts.end() && itFont->second)
                {
                    ImGui::PushFont(itFont->second);
                }
                {
                    std::string deleteIcon = "v";
                    float tw = ImRenderUtils::getTextWidth(&deleteIcon, 0.85f * inScale);
                    float th = ImRenderUtils::getTextHeightStr(&deleteIcon, 0.85f * inScale);
                    float tx = deleteRect.x + ((deleteRect.z - deleteRect.x) - tw) * 0.5f;
                    float ty = deleteRect.y + ((deleteRect.w - deleteRect.y) - th) * 0.5f;
                    ImRenderUtils::drawText(ImVec2(tx, ty), deleteIcon, iconCol, 0.85f * inScale, listAlpha, true, 0, panelList);
                }
                {
                    std::string loadIcon = "j";
                    float tw = ImRenderUtils::getTextWidth(&loadIcon, 0.85f * inScale);
                    float th = ImRenderUtils::getTextHeightStr(&loadIcon, 0.85f * inScale);
                    float tx = loadRect.x + ((loadRect.z - loadRect.x) - tw) * 0.5f;
                    float ty = loadRect.y + ((loadRect.w - loadRect.y) - th) * 0.5f;
                    ImRenderUtils::drawText(ImVec2(tx, ty), loadIcon, iconCol, 0.85f * inScale, listAlpha, true, 0, panelList);
                }
                if (itFont != FontHelper::Fonts.end() && itFont->second)
                {
                    ImGui::PopFont();
                }

                bool loadHovered = configInputEnabled && isMouseOver(loadRect);
                bool deleteHovered = configInputEnabled && isMouseOver(deleteRect);
                if (ImGui::IsMouseClicked(0) && configInputEnabled)
                {
                    if (loadHovered)
                    {
                        std::string target = configNames[idx];
                        ConfigManager::loadConfig(target);
                        selectedConfigName = target;
                        configSelectedIndex = (int)idx;
                    }
                    else if (deleteHovered)
                    {
                        std::string target = configNames[idx];
                        if (!ConfigManager::configExists(target))
                        {
                            NotifyUtils::notify("Config does not exist.", 3.f, Notification::Type::Warning);
                        }
                        else
                        {
                            bool removed = FileUtils::deleteFile(ConfigManager::getConfigFilePath(target));
                            removed = FileUtils::deleteFile(ConfigManager::getLegacyConfigFilePath(target)) || removed;
                            if (!removed)
                            {
                                NotifyUtils::notify("Failed to delete config.", 3.f, Notification::Type::Error);
                            }
                            else
                            {
                                if (selectedConfigName == target) selectedConfigName.clear();
                                if (ConfigManager::LastLoadedConfig == target) ConfigManager::LastLoadedConfig.clear();
                                refreshConfigList();
                            }
                        }
                    }
                    else if (hovered)
                    {
                        selectedConfigName = configNames[idx];
                        configSelectedIndex = (int)idx;
                        ConfigManager::loadConfig(selectedConfigName);
                    }
                }
            }
        }
        panelList->PopClipRect();
        panelList->PopClipRect();

        if (isConfigDropdownOpen && configDropdownAnim > 0.65f && isEnabledRaw && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)))
        {
            if (!isMouseOver(mainRect) && !isMouseOver(configDropRect) && !isMouseOver(configSaveRect) && !isMouseOver(configPanelRect))
            {
                isConfigDropdownOpen = false;
            }
        }
    }

    if (espPreviewAnim > 0.05f)
    {
        float tEsp = std::clamp(espPreviewAnim, 0.0f, 1.0f);
        float eased = tEsp * tEsp * (3.0f - 2.0f * tEsp);
        float previewAlpha = eased;

        float slideX = (1.0f - eased) * (previewGap + previewWidth + 28.0f * inScale);
        ImVec4 espPreviewRectAnim = ImVec4(
            espPreviewRect.x - slideX,
            espPreviewRect.y,
            espPreviewRect.z - slideX,
            espPreviewRect.w
        );
        if (espPreviewRectAnim.z > 0.0f && espPreviewRectAnim.x < screenSize.x && espPreviewRectAnim.w > 0.0f && espPreviewRectAnim.y < screenSize.y)
        {

        auto alphaColor = [&](ImColor c, float aMul) -> ImColor
        {
            ImVec4 v = c.Value;
            v.w = std::clamp(v.w * aMul, 0.0f, 1.0f);
            return ImColor(v);
        };

        ImVec2 previewMin = ImVec2(espPreviewRectAnim.x, espPreviewRectAnim.y);
        ImVec2 previewMax = ImVec2(espPreviewRectAnim.z, espPreviewRectAnim.w);

        ImVec2 outerMin = previewMin;
        ImVec2 outerMax = previewMax;
        ImVec2 innerMin = ImVec2(previewMin.x + 6.0f * inScale, previewMin.y + 6.0f * inScale);
        ImVec2 innerMax = ImVec2(previewMax.x - 6.0f * inScale, previewMax.y - 6.0f * inScale);

        drawList->PushClipRect(ImVec2(mainRect.z, mainRect.y), ImVec2(mainRect.z + previewGap + previewWidth + 40.0f * inScale, mainRect.w), true);
        drawList->AddRectFilled(outerMin, outerMax, alphaColor(cardColor, previewAlpha), 12.0f);
        drawList->AddRect(outerMin, outerMax, alphaColor(panelBorderColor, previewAlpha), 12.0f, 0, 1.6f);

        ImColor innerBg = (uiTheme == 1)
            ? ImColor(241, 241, 246, (int)(255 * animation))
            : ImColor(8, 7, 12, (int)(255 * animation));
        drawList->AddRectFilled(innerMin, innerMax, alphaColor(innerBg, previewAlpha), 10.0f);
        ImVec4 glowRect = ImVec4(innerMin.x, innerMin.y, innerMax.x, innerMax.y);
        ImColor glow = accentColor;
        glow.Value.w = std::clamp(glow.Value.w * previewAlpha, 0.0f, 1.0f);
        ImRenderUtils::drawRectGlow(glowRect, glow, 0.10f * previewAlpha, 16.0f * inScale, 1.03f, 32.0f, drawList);

        ImGui::SetNextWindowPos(innerMin);
        ImGui::SetNextWindowSize(ImVec2(innerMax.x - innerMin.x, innerMax.y - innerMin.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("##esp_layout_preview", nullptr, winFlags);

        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        ImVec2 pad = ImVec2(10.0f * inScale, 10.0f * inScale);

        ImVec2 previewAreaMin = ImVec2(winPos.x + pad.x, winPos.y + pad.y);
        ImVec2 previewAreaMax = ImVec2(winPos.x + winSize.x - pad.x, winPos.y + winSize.y - pad.y * 1.6f);

        ImVec2 areaSize = ImVec2(previewAreaMax.x - previewAreaMin.x, previewAreaMax.y - previewAreaMin.y);
        float boxW = areaSize.x * 0.72f;
        float boxH = areaSize.y * 0.74f;
        ImVec2 boxCenter = ImVec2(previewAreaMin.x + areaSize.x * 0.5f, previewAreaMin.y + areaSize.y * 0.5f);
        ImVec2 boxMin = ImVec2(boxCenter.x - boxW * 0.5f, boxCenter.y - boxH * 0.5f);
        ImVec2 boxMax = ImVec2(boxCenter.x + boxW * 0.5f, boxCenter.y + boxH * 0.5f);

        auto* espModule = gFeatureManager->mModuleManager->getModule<ESP>();
        bool showBox = !espModule || espModule->mShowBox.mValue;
        bool showName = !espModule || espModule->mShowName.mValue;
        bool showDistance = !espModule || espModule->mShowDistance.mValue;
        bool showHealth = !espModule || espModule->mShowHealth.mValue;
        bool showItems = !espModule || espModule->mShowItems.mValue;
        ImColor previewTextMain = ColorUtils::getUiTextColor(1.0f);
        ImColor previewTextDim = ColorUtils::getUiTextDimColor(1.0f);
        float previewTextAlpha = std::clamp(previewAlpha * animation, 0.0f, 1.0f);

        if (showBox)
        {
            ImColor color = accentColor;
            color.Value.w = std::clamp(color.Value.w * previewAlpha, 0.0f, 1.0f);

            ImColor outlineColor = ImColor(0.0f, 0.0f, 0.0f, std::clamp(previewAlpha * animation, 0.0f, 1.0f));
            float outlineThickness = 0.4f;

            if (espModule && espModule->mRenderFilled.mValue)
            {
                constexpr float shrinkFactor = 1.7f;
                float fillWidth = (boxMax.x - boxMin.x) / shrinkFactor;
                float fillHeight = (boxMax.y - boxMin.y) / shrinkFactor;
                ImVec2 fillMin = ImVec2(boxCenter.x - fillWidth * 0.5f, boxCenter.y - fillHeight * 0.5f);
                ImVec2 fillMax = ImVec2(boxCenter.x + fillWidth * 0.5f, boxCenter.y + fillHeight * 0.5f);
                drawList->AddRectFilled(fillMin, fillMax, ImColor(color.Value.x, color.Value.y, color.Value.z, 0.25f * previewAlpha * animation));
            }

            drawList->AddRect(boxMin, boxMax, color, 0.0f, 0, 1.5f);

            ImVec2 innerMin = ImVec2(boxMin.x + 1.0f, boxMin.y + 1.0f);
            ImVec2 innerMax = ImVec2(boxMax.x - 1.0f, boxMax.y - 1.0f);
            ImVec2 outerMin = ImVec2(boxMin.x - 1.0f, boxMin.y - 1.0f);
            ImVec2 outerMax = ImVec2(boxMax.x + 1.0f, boxMax.y + 1.0f);

            drawList->AddRect(innerMin, innerMax, outlineColor, 0.0f, 0, outlineThickness);
            drawList->AddRect(outerMin, outerMax, outlineColor, 0.0f, 0, outlineThickness);
        }
        {
            static ID3D11ShaderResourceView* steveTexture = nullptr;
            static int steveW = 0;
            static int steveH = 0;
            static bool steveLoaded = false;
            if (!steveLoaded) {
                D3DHook::loadTextureFromEmbeddedResource("steve.png", &steveTexture, &steveW, &steveH);
                steveLoaded = true;
            }

            if (steveTexture != nullptr && steveW > 0 && steveH > 0)
            {
                float boxWpx = boxMax.x - boxMin.x;
                float boxHpx = boxMax.y - boxMin.y;
                float maxW = boxWpx * 0.88f;
                float maxH = boxHpx * 0.88f;
                float scale = std::min(maxW / (float)steveW, maxH / (float)steveH);
                float w = (float)steveW * scale;
                float h = (float)steveH * scale;
                ImVec2 center = ImVec2((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f);
                ImVec2 p0 = ImVec2(center.x - w * 0.5f, center.y - h * 0.5f);
                ImVec2 p1 = ImVec2(center.x + w * 0.5f, center.y + h * 0.5f);
                drawList->AddImage(steveTexture, p0, p1, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, (int)(255.0f * previewAlpha * animation)));
            }
        }

        ImVec2 boxCenterScreen = ImVec2((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f);
        float boxHeight = boxMax.y - boxMin.y;
        float padText = 3.0f * inScale;

        enum EspPreviewDragId
        {
            DragNone = 0,
            DragItems = 1,
            DragName = 2,
            DragDistance = 3,
            DragHealth = 4
        };

        static int dragActive = DragNone;
        static ImVec2 dragGrabOffset = ImVec2(0, 0);
        static ImVec2 dragPosSmooth = ImVec2(0, 0);
        static int dragAnchorStable = 0;

        auto animateVec2 = [&](ImVec2 target, ImVec2& current, float speed)
        {
            current.x = MathUtils::animate(target.x, current.x, deltaTime * speed);
            current.y = MathUtils::animate(target.y, current.y, deltaTime * speed);
        };

        ImVec2 nameOffsetTarget = ImVec2(0, 0);
        ImVec2 distOffsetTarget = ImVec2(0, 0);
        ImVec2 healthOffsetTarget = ImVec2(0, 0);
        ImVec2 itemsOffsetTarget = ImVec2(0, 0);
        if (espModule)
        {
            nameOffsetTarget = ImVec2(espModule->mNameOffsetX.mValue * inScale, espModule->mNameOffsetY.mValue * inScale);
            distOffsetTarget = ImVec2(espModule->mDistanceOffsetX.mValue * inScale, espModule->mDistanceOffsetY.mValue * inScale);
            healthOffsetTarget = ImVec2(espModule->mHealthOffsetX.mValue * inScale, espModule->mHealthOffsetY.mValue * inScale);
            itemsOffsetTarget = ImVec2(espModule->mItemsOffsetX.mValue * inScale, espModule->mItemsOffsetY.mValue * inScale);
        }

        static ImVec2 nameOffsetAnim = ImVec2(0, 0);
        static ImVec2 distOffsetAnim = ImVec2(0, 0);
        static ImVec2 healthOffsetAnim = ImVec2(0, 0);
        static ImVec2 itemsOffsetAnim = ImVec2(0, 0);

        static bool namePosInit = false;
        static bool distPosInit = false;
        static bool healthPosInit = false;
        static bool itemsPosInit = false;
        static ImVec2 namePosAnim = ImVec2(0, 0);
        static ImVec2 distPosAnim = ImVec2(0, 0);
        static ImVec2 healthPosAnim = ImVec2(0, 0);
        static ImVec2 itemsPosAnim = ImVec2(0, 0);

        float baseSpeed = 28.0f;
        float dragSpeed = 60.0f;
        animateVec2(nameOffsetTarget, nameOffsetAnim, dragActive == DragName ? dragSpeed : baseSpeed);
        animateVec2(distOffsetTarget, distOffsetAnim, dragActive == DragDistance ? dragSpeed : baseSpeed);
        animateVec2(healthOffsetTarget, healthOffsetAnim, dragActive == DragHealth ? dragSpeed : baseSpeed);
        animateVec2(itemsOffsetTarget, itemsOffsetAnim, dragActive == DragItems ? dragSpeed : baseSpeed);

        auto animatePos2 = [&](int id, ImVec2 target, ImVec2& current, bool& inited, float speed) -> ImVec2
        {
            if (!inited)
            {
                current = target;
                inited = true;
                return target;
            }

            if (dragActive == id)
            {
                current = target;
                return target;
            }

            animateVec2(target, current, speed);
            return current;
        };

        auto clampToPreview = [&](ImVec2 pos, ImVec2 size) -> ImVec2
        {
            float minX = previewAreaMin.x;
            float minY = previewAreaMin.y;
            float maxX = previewAreaMax.x - size.x;
            float maxY = previewAreaMax.y - size.y;
            if (maxX < minX) maxX = minX;
            if (maxY < minY) maxY = minY;
            return ImVec2(std::clamp(pos.x, minX, maxX), std::clamp(pos.y, minY, maxY));
        };

        auto nearestBoxSide = [&](const ImVec2& p) -> int
        {
            float dl = fabsf(p.x - boxMin.x);
            float dr = fabsf(p.x - boxMax.x);
            float dt = fabsf(p.y - boxMin.y);
            float db = fabsf(p.y - boxMax.y);
            float m = dt;
            int side = 0;
            if (db < m) { m = db; side = 1; }
            if (dl < m) { m = dl; side = 2; }
            if (dr < m) { m = dr; side = 3; }
            return side;
        };

        auto sideDist = [&](int side, const ImVec2& p) -> float
        {
            if (side == 0) return fabsf(p.y - boxMin.y);
            if (side == 1) return fabsf(p.y - boxMax.y);
            if (side == 2) return fabsf(p.x - boxMin.x);
            return fabsf(p.x - boxMax.x);
        };

        auto pickAnchorWithHysteresis = [&](int current, const ImVec2& p) -> int
        {
            int best = nearestBoxSide(p);
            float hysteresis = 10.0f * inScale;
            float bestD = sideDist(best, p);
            float curD = sideDist(current, p);
            if (best != current && bestD + hysteresis < curD) return best;
            return current;
        };

        auto clampOffsetsForAnchor = [&](int anchor, float& ox, float& oy)
        {
            float maxParallel = 300.0f;
            float maxPerp = 70.0f;

            if (anchor == 0 || anchor == 1)
            {
                ox = std::clamp(ox, -maxParallel, maxParallel);
                oy = std::clamp(oy, -maxPerp, maxPerp);
                return;
            }

            ox = std::clamp(ox, -maxPerp, maxPerp);
            oy = std::clamp(oy, -maxParallel, maxParallel);
        };

        auto beginDragIfHit = [&](int id, const ImVec2& pos, const ImVec2& size, int currentAnchor)
        {
            ImVec2 hitMin = pos;
            ImVec2 hitMax = ImVec2(pos.x + size.x, pos.y + size.y);
            ImVec4 hitRect = ImVec4(hitMin.x, hitMin.y, hitMax.x, hitMax.y);
            bool hovered = isMouseOver(hitRect) && isEnabled && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (hovered && ImGui::IsMouseClicked(0))
            {
                dragActive = id;
                dragAnchorStable = currentAnchor;
                dragGrabOffset = ImVec2(ImGui::GetIO().MousePos.x - pos.x, ImGui::GetIO().MousePos.y - pos.y);
                dragPosSmooth = pos;
            }
        };

        auto driveDrag = [&](int id, const ImVec2& size, int& anchor, NumberSetting& ox, NumberSetting& oy, bool allowAllSides, const auto& baseForAnchor)
        {
            if (dragActive != id) return;
            if (!ImGui::IsMouseDown(0))
            {
                dragActive = DragNone;
                return;
            }

            ImVec2 mouse = ImGui::GetIO().MousePos;
            ImVec2 desiredPos = ImVec2(mouse.x - dragGrabOffset.x, mouse.y - dragGrabOffset.y);
            desiredPos = clampToPreview(desiredPos, size);
            dragPosSmooth.x = MathUtils::animate(desiredPos.x, dragPosSmooth.x, deltaTime * 55.0f);
            dragPosSmooth.y = MathUtils::animate(desiredPos.y, dragPosSmooth.y, deltaTime * 55.0f);
            desiredPos = dragPosSmooth;

            if (allowAllSides)
            {
                dragAnchorStable = pickAnchorWithHysteresis(dragAnchorStable, mouse);
                anchor = dragAnchorStable;
            }
            else
            {
                float margin = 12.0f * inScale;
                if (dragAnchorStable == 0 && mouse.x > boxCenterScreen.x + margin) dragAnchorStable = 1;
                else if (dragAnchorStable == 1 && mouse.x < boxCenterScreen.x - margin) dragAnchorStable = 0;
                anchor = dragAnchorStable;
            }

            ImVec2 base = baseForAnchor(anchor);
            float nx = (desiredPos.x - base.x) / std::max(inScale, 0.001f);
            float ny = (desiredPos.y - base.y) / std::max(inScale, 0.001f);

            if (allowAllSides)
                clampOffsetsForAnchor(anchor, nx, ny);
            else
            {
                nx = std::clamp(nx, -70.0f, 70.0f);
                ny = std::clamp(ny, -300.0f, 300.0f);
            }

            ox.setValue(std::clamp(nx, ox.mMin, ox.mMax));
            oy.setValue(std::clamp(ny, oy.mMin, oy.mMax));
        };

        if (showItems)
        {
            const int iconCount = 6;
            float iconSize = 16.0f * inScale;
            float iconGap = 2.0f * inScale;
            float groupW = (iconCount * iconSize) + ((iconCount - 1) * iconGap);
            float groupH = iconSize;

            int itemsAnchor = espModule ? espModule->mItemsAnchor.mValue : 0;
            ImVec2 itemsBase = ImVec2(boxCenterScreen.x - groupW * 0.5f, boxMin.y - groupH - padText);
            if (itemsAnchor == 1)
                itemsBase = ImVec2(boxCenterScreen.x - groupW * 0.5f, boxMax.y + padText);
            else if (itemsAnchor == 2)
                itemsBase = ImVec2(boxMin.x - padText - groupW, boxCenterScreen.y - groupH * 0.5f);
            else if (itemsAnchor == 3)
                itemsBase = ImVec2(boxMax.x + padText, boxCenterScreen.y - groupH * 0.5f);

            ImVec2 itemsOffsetDraw = (dragActive == DragItems) ? itemsOffsetTarget : itemsOffsetAnim;
            ImVec2 itemDrawPos = ImVec2(itemsBase.x + itemsOffsetDraw.x, itemsBase.y + itemsOffsetDraw.y);
            itemDrawPos = animatePos2(DragItems, itemDrawPos, itemsPosAnim, itemsPosInit, 26.0f);

            beginDragIfHit(DragItems, itemDrawPos, ImVec2(groupW, groupH), itemsAnchor);
            if (espModule)
            {
                driveDrag(
                    DragItems,
                    ImVec2(groupW, groupH),
                    espModule->mItemsAnchor.mValue,
                    espModule->mItemsOffsetX,
                    espModule->mItemsOffsetY,
                    true,
                    [&](int a) -> ImVec2
                    {
                        ImVec2 base = ImVec2(boxCenterScreen.x - groupW * 0.5f, boxMin.y - groupH - padText);
                        if (a == 1) base = ImVec2(boxCenterScreen.x - groupW * 0.5f, boxMax.y + padText);
                        else if (a == 2) base = ImVec2(boxMin.x - padText - groupW, boxCenterScreen.y - groupH * 0.5f);
                        else if (a == 3) base = ImVec2(boxMax.x + padText, boxCenterScreen.y - groupH * 0.5f);
                        return base;
                    }
                );
                itemsAnchor = espModule->mItemsAnchor.mValue;
            }
            if (espModule && dragActive == DragItems)
            {
                itemsBase = ImVec2(boxCenterScreen.x - groupW * 0.5f, boxMin.y - groupH - padText);
                if (itemsAnchor == 1)
                    itemsBase = ImVec2(boxCenterScreen.x - groupW * 0.5f, boxMax.y + padText);
                else if (itemsAnchor == 2)
                    itemsBase = ImVec2(boxMin.x - padText - groupW, boxCenterScreen.y - groupH * 0.5f);
                else if (itemsAnchor == 3)
                    itemsBase = ImVec2(boxMax.x + padText, boxCenterScreen.y - groupH * 0.5f);

                itemDrawPos = ImVec2(itemsBase.x + itemsOffsetTarget.x, itemsBase.y + itemsOffsetTarget.y);
            }

            ImColor fill = ImColor(accentColor.Value.x, accentColor.Value.y, accentColor.Value.z, 0.9f * previewAlpha);
            ImColor outline = ImColor(0.0f, 0.0f, 0.0f, 0.9f * previewAlpha);
            for (int i = 0; i < iconCount; i++)
            {
                float x0 = itemDrawPos.x + i * (iconSize + iconGap);
                ImVec2 rMin = ImVec2(x0, itemDrawPos.y);
                ImVec2 rMax = ImVec2(x0 + iconSize, itemDrawPos.y + iconSize);
                drawList->AddRectFilled(rMin, rMax, fill, 2.0f);
                drawList->AddRect(rMin, rMax, outline, 2.0f, 0, 1.0f);
            }
        }

        if (showName)
        {
            std::string namePreview = "Player";
            float nameFont = 0.95f * inScale;
            float nameWidth = ImRenderUtils::getTextWidth(&namePreview, nameFont);
            float nameHeight = ImRenderUtils::getTextHeightStr(&namePreview, nameFont);
            int nameAnchor = espModule ? espModule->mNameAnchor.mValue : 0;

            float extraTop = 0.0f;
            float extraBottom = 0.0f;
            if (espModule && showItems && espModule->mItemsAnchor.mValue == nameAnchor && (nameAnchor == 0 || nameAnchor == 1))
            {
                if (nameAnchor == 0) extraTop = 16.0f * inScale + padText;
                if (nameAnchor == 1) extraBottom = 16.0f * inScale + padText;
            }

            ImVec2 nameBase = ImVec2(boxCenterScreen.x - nameWidth * 0.5f, boxMin.y - nameHeight - padText - extraTop);
            if (nameAnchor == 1)
                nameBase = ImVec2(boxCenterScreen.x - nameWidth * 0.5f, boxMax.y + padText + extraBottom);
            else if (nameAnchor == 2)
                nameBase = ImVec2(boxMin.x - padText - nameWidth, boxCenterScreen.y - nameHeight * 0.5f);
            else if (nameAnchor == 3)
                nameBase = ImVec2(boxMax.x + padText, boxCenterScreen.y - nameHeight * 0.5f);

            ImVec2 nameOffsetDraw = (dragActive == DragName) ? nameOffsetTarget : nameOffsetAnim;
            ImVec2 nameDrawPos = ImVec2(nameBase.x + nameOffsetDraw.x, nameBase.y + nameOffsetDraw.y);
            nameDrawPos = animatePos2(DragName, nameDrawPos, namePosAnim, namePosInit, 26.0f);

            beginDragIfHit(DragName, nameDrawPos, ImVec2(nameWidth, nameHeight), nameAnchor);
            if (espModule)
            {
                driveDrag(
                    DragName,
                    ImVec2(nameWidth, nameHeight),
                    espModule->mNameAnchor.mValue,
                    espModule->mNameOffsetX,
                    espModule->mNameOffsetY,
                    true,
                    [&](int a) -> ImVec2
                    {
                        float topExtra = 0.0f;
                        float bottomExtra = 0.0f;
                        if (showItems && espModule->mItemsAnchor.mValue == a && (a == 0 || a == 1))
                        {
                            if (a == 0) topExtra = 16.0f * inScale + padText;
                            if (a == 1) bottomExtra = 16.0f * inScale + padText;
                        }

                        ImVec2 base = ImVec2(boxCenterScreen.x - nameWidth * 0.5f, boxMin.y - nameHeight - padText - topExtra);
                        if (a == 1) base = ImVec2(boxCenterScreen.x - nameWidth * 0.5f, boxMax.y + padText + bottomExtra);
                        else if (a == 2) base = ImVec2(boxMin.x - padText - nameWidth, boxCenterScreen.y - nameHeight * 0.5f);
                        else if (a == 3) base = ImVec2(boxMax.x + padText, boxCenterScreen.y - nameHeight * 0.5f);
                        return base;
                    }
                );
                nameAnchor = espModule->mNameAnchor.mValue;
            }
            if (espModule && dragActive == DragName)
            {
                float topExtra = 0.0f;
                float bottomExtra = 0.0f;
                if (showItems && espModule->mItemsAnchor.mValue == nameAnchor && (nameAnchor == 0 || nameAnchor == 1))
                {
                    if (nameAnchor == 0) topExtra = 16.0f * inScale + padText;
                    if (nameAnchor == 1) bottomExtra = 16.0f * inScale + padText;
                }

                nameBase = ImVec2(boxCenterScreen.x - nameWidth * 0.5f, boxMin.y - nameHeight - padText - topExtra);
                if (nameAnchor == 1)
                    nameBase = ImVec2(boxCenterScreen.x - nameWidth * 0.5f, boxMax.y + padText + bottomExtra);
                else if (nameAnchor == 2)
                    nameBase = ImVec2(boxMin.x - padText - nameWidth, boxCenterScreen.y - nameHeight * 0.5f);
                else if (nameAnchor == 3)
                    nameBase = ImVec2(boxMax.x + padText, boxCenterScreen.y - nameHeight * 0.5f);

                nameDrawPos = ImVec2(nameBase.x + nameOffsetTarget.x, nameBase.y + nameOffsetTarget.y);
            }
            ImRenderUtils::drawText(nameDrawPos, namePreview, previewTextMain, nameFont, previewTextAlpha, false, 0, drawList);
        }

        if (showDistance)
        {
            std::string distPreview = "12m";
            float distFont = 0.85f * inScale;
            float distWidth = ImRenderUtils::getTextWidth(&distPreview, distFont);
            float distHeight = ImRenderUtils::getTextHeightStr(&distPreview, distFont);
            int distAnchor = espModule ? espModule->mDistanceAnchor.mValue : 1;

            ImVec2 distBase = ImVec2(boxCenterScreen.x - distWidth * 0.5f, boxMax.y + padText);
            if (distAnchor == 0)
                distBase = ImVec2(boxCenterScreen.x - distWidth * 0.5f, boxMin.y - distHeight - padText);
            else if (distAnchor == 2)
                distBase = ImVec2(boxMin.x - padText - distWidth, boxCenterScreen.y - distHeight * 0.5f);
            else if (distAnchor == 3)
                distBase = ImVec2(boxMax.x + padText, boxCenterScreen.y - distHeight * 0.5f);

            ImVec2 distOffsetDraw = (dragActive == DragDistance) ? distOffsetTarget : distOffsetAnim;
            ImVec2 distDrawPos = ImVec2(distBase.x + distOffsetDraw.x, distBase.y + distOffsetDraw.y);
            distDrawPos = animatePos2(DragDistance, distDrawPos, distPosAnim, distPosInit, 26.0f);

            beginDragIfHit(DragDistance, distDrawPos, ImVec2(distWidth, distHeight), distAnchor);
            if (espModule)
            {
                driveDrag(
                    DragDistance,
                    ImVec2(distWidth, distHeight),
                    espModule->mDistanceAnchor.mValue,
                    espModule->mDistanceOffsetX,
                    espModule->mDistanceOffsetY,
                    true,
                    [&](int a) -> ImVec2
                    {
                        ImVec2 base = ImVec2(boxCenterScreen.x - distWidth * 0.5f, boxMax.y + padText);
                        if (a == 0) base = ImVec2(boxCenterScreen.x - distWidth * 0.5f, boxMin.y - distHeight - padText);
                        else if (a == 2) base = ImVec2(boxMin.x - padText - distWidth, boxCenterScreen.y - distHeight * 0.5f);
                        else if (a == 3) base = ImVec2(boxMax.x + padText, boxCenterScreen.y - distHeight * 0.5f);
                        return base;
                    }
                );
                distAnchor = espModule->mDistanceAnchor.mValue;
            }
            if (espModule && dragActive == DragDistance)
            {
                distBase = ImVec2(boxCenterScreen.x - distWidth * 0.5f, boxMax.y + padText);
                if (distAnchor == 0)
                    distBase = ImVec2(boxCenterScreen.x - distWidth * 0.5f, boxMin.y - distHeight - padText);
                else if (distAnchor == 2)
                    distBase = ImVec2(boxMin.x - padText - distWidth, boxCenterScreen.y - distHeight * 0.5f);
                else if (distAnchor == 3)
                    distBase = ImVec2(boxMax.x + padText, boxCenterScreen.y - distHeight * 0.5f);

                distDrawPos = ImVec2(distBase.x + distOffsetTarget.x, distBase.y + distOffsetTarget.y);
            }
            ImRenderUtils::drawText(distDrawPos, distPreview, previewTextDim, distFont, previewTextAlpha, false, 0, drawList);
        }

        if (showHealth)
        {
            float barWidth = 5.0f * inScale;
            float barHeight = boxHeight;
            int healthAnchor = espModule ? espModule->mHealthAnchor.mValue : 0;
            ImVec2 barMinBase = ImVec2(boxMin.x - padText - barWidth, boxMin.y);
            if (healthAnchor == 1)
                barMinBase = ImVec2(boxMax.x + padText, boxMin.y);

            ImVec2 healthOffsetDraw = (dragActive == DragHealth) ? healthOffsetTarget : healthOffsetAnim;
            ImVec2 barMin = ImVec2(barMinBase.x + healthOffsetDraw.x, barMinBase.y + healthOffsetDraw.y);
            barMin = animatePos2(DragHealth, barMin, healthPosAnim, healthPosInit, 26.0f);
            ImVec2 barMax = ImVec2(barMin.x + barWidth, barMin.y + barHeight);

            beginDragIfHit(DragHealth, barMin, ImVec2(barWidth, barHeight), healthAnchor);
            if (espModule)
            {
                driveDrag(
                    DragHealth,
                    ImVec2(barWidth, barHeight),
                    espModule->mHealthAnchor.mValue,
                    espModule->mHealthOffsetX,
                    espModule->mHealthOffsetY,
                    false,
                    [&](int a) -> ImVec2
                    {
                        ImVec2 base = ImVec2(boxMin.x - padText - barWidth, boxMin.y);
                        if (a == 1) base = ImVec2(boxMax.x + padText, boxMin.y);
                        return base;
                    }
                );
                healthAnchor = espModule->mHealthAnchor.mValue;
            }
            if (espModule && dragActive == DragHealth)
            {
                barMinBase = ImVec2(boxMin.x - padText - barWidth, boxMin.y);
                if (healthAnchor == 1)
                    barMinBase = ImVec2(boxMax.x + padText, boxMin.y);

                barMin = ImVec2(barMinBase.x + healthOffsetTarget.x, barMinBase.y + healthOffsetTarget.y);
                barMax = ImVec2(barMin.x + barWidth, barMin.y + barHeight);
            }

            float t = ImGui::GetTime();
            float wave = (sinf(t * 1.7f) + 1.0f) * 0.5f;
            float ratio = 0.15f + wave * 0.75f;

            float filledHeight = barHeight * ratio;
            ImVec2 fillMin = ImVec2(barMin.x, barMax.y - filledHeight);
            ImVec2 fillMax = ImVec2(barMax.x, barMax.y);

            ImColor topColor = ImColor(accentColor.Value.x, accentColor.Value.y, accentColor.Value.z, std::clamp(previewAlpha, 0.0f, 1.0f));
            ImColor bottomColor = ImColor(0.0f, 0.0f,  0.0f, std::clamp(previewAlpha, 0.0f, 1.0f));

            drawList->AddRectFilledMultiColor(
                fillMin,
                fillMax,
                topColor,
                topColor,
                bottomColor,
                bottomColor
            );
        }

        ImGui::End();
        ImGui::PopStyleVar(4);
        drawList->PopClipRect();
        }
    }

    drawList->PushClipRect(ImVec2(contentRect.x, contentRect.y), ImVec2(contentRect.z, contentRect.w), true);
    if (loadingFade > 0.01f)
    {
        size_t total = 0;
        size_t done = 0;
        if (gPreloadReady.load())
        {
            std::lock_guard<std::mutex> lock(gPreloadMutex);
            total = gPreloadKeys.size();
            done = gPreloadIndex.load();
        }
        std::string progress;
        if (total > 0)
        {
            progress = std::to_string(done) + " / " + std::to_string(total);
        }

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec4 blurRect = ImVec4(mainRect.x, mainRect.y, mainRect.z, mainRect.w);
        ImRenderUtils::addBlurAlpha(blurRect, 12.0f, loadingFade, 16.0f, fg, true);
        fg->AddRectFilled(ImVec2(blurRect.x, blurRect.y), ImVec2(blurRect.z, blurRect.w), ImColor(0, 0, 0, (int)(90.0f * loadingFade)), 16.0f);

        ensureLoadingGif();
        ID3D11ShaderResourceView* gifFrame = nullptr;
        ImVec2 gifSize = ImVec2(0.0f, 0.0f);
        int tMs = (int)(ImGui::GetTime() * 1000.0);
        if (gLoadingGifReady.load())
        {
            std::lock_guard<std::mutex> lock(gLoadingGifMutex);
            if (gLoadingGif.w > 0 && gLoadingGif.h > 0 && gLoadingGif.total > 0 && gLoadingGif.frameCount > 0)
            {
                int totalMs = gLoadingGif.total;
                int t = totalMs > 0 ? (tMs % totalMs) : 0;
                int frameIndex = 0;
                for (int i = 0; i < (int)gLoadingGif.cumulative.size(); ++i)
                {
                    if (t < gLoadingGif.cumulative[i]) { frameIndex = i; break; }
                }
                float maxDim = 160.0f * inScale;
                float scale = maxDim / (float)std::max(gLoadingGif.w, gLoadingGif.h);
                gifSize = ImVec2(gLoadingGif.w * scale, gLoadingGif.h * scale);
                if (frameIndex >= 0 && frameIndex < (int)gLoadingGif.frames.size())
                {
                    if (!gLoadingGif.frames[frameIndex] && !gLoadingGif.pixels.empty())
                    {
                        size_t stride = (size_t)gLoadingGif.w * gLoadingGif.h * 4;
                        size_t offset = stride * (size_t)frameIndex;
                        if (offset + stride <= gLoadingGif.pixels.size())
                        {
                            const uint8_t* frameData = gLoadingGif.pixels.data() + offset;
                            ID3D11ShaderResourceView* srv = nullptr;
                            if (D3DHook::createTextureFromData(frameData, gLoadingGif.w, gLoadingGif.h, &srv))
                            {
                                gLoadingGif.frames[frameIndex] = srv;
                            }
                        }
                    }
                    gifFrame = gLoadingGif.frames[frameIndex];
                }
            }
        }

        std::string msg = "Загрузка, пожалуйста подождите...";
        float font = 1.05f * inScale;
        float tw = ImRenderUtils::getTextWidth(&msg, font);
        float th = ImRenderUtils::getTextHeightStr(&msg, font);
        float blurCx = (blurRect.x + blurRect.z) * 0.5f;
        float blurCy = (blurRect.y + blurRect.w) * 0.5f;
        float gifGap = 10.0f * inScale;
        if (gifFrame && gifSize.x > 0.0f && gifSize.y > 0.0f)
        {
            ImVec2 gifMin = ImVec2(blurCx - gifSize.x * 0.5f, blurCy - gifSize.y * 0.5f - 12.0f * inScale);
            ImVec2 gifMax = ImVec2(gifMin.x + gifSize.x, gifMin.y + gifSize.y);
            fg->AddImage(gifFrame, gifMin, gifMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, (int)(255.0f * loadingFade)));
            float tx = blurCx - tw * 0.5f;
            float ty = gifMax.y + gifGap;
            ImRenderUtils::drawText(ImVec2(tx, ty), msg, textDim, font, loadingFade, true, 0, fg);
            if (!progress.empty())
            {
                float pFont = 0.92f * inScale;
                float ptw = ImRenderUtils::getTextWidth(&progress, pFont);
                float ptx = blurCx - ptw * 0.5f;
                float pty = ty + th + 6.0f * inScale;
                ImRenderUtils::drawText(ImVec2(ptx, pty), progress, textDim, pFont, loadingFade, true, 0, fg);
            }
        }
        else
        {
            float tx = blurCx - tw * 0.5f;
            float ty = blurCy - th * 0.5f - (progress.empty() ? 0.0f : 10.0f * inScale);
            ImRenderUtils::drawText(ImVec2(tx, ty), msg, textDim, font, loadingFade, true, 0, fg);
            if (!progress.empty())
            {
                float pFont = 0.92f * inScale;
                float ptw = ImRenderUtils::getTextWidth(&progress, pFont);
                float ptx = blurCx - ptw * 0.5f;
                float pty = ty + th + 6.0f * inScale;
                ImRenderUtils::drawText(ImVec2(ptx, pty), progress, textDim, pFont, loadingFade, true, 0, fg);
            }
        }
        if (isInitialLoading)
        {
            drawList->PopClipRect();
            if (pushedComfortaa) ImGui::PopFont();
            return;
        }
    }

    const int moduleCategoryCount = (int)ModuleCategoryNames.size();
    const int autobuyCategoryIndex = moduleCategoryCount;
    const bool isAutobuyCategory = currentCategoryIndex == autobuyCategoryIndex;

    static std::vector<std::shared_ptr<Module>> emptyMods;
    static std::vector<std::shared_ptr<Module>> cachedCategoryMods;
    static int cachedCategoryIndex = -1;
    static size_t cachedModuleCount = 0;
    if (cachedCategoryIndex != currentCategoryIndex || cachedModuleCount != allModsRef.size()) {
        cachedCategoryIndex = currentCategoryIndex;
        cachedModuleCount = allModsRef.size();
        if (currentCategoryIndex < moduleCategoryCount) {
            const auto& catMods = moduleManager->getModulesInCategory(currentCategoryIndex);
            cachedCategoryMods.assign(catMods.begin(), catMods.end());
        } else {
            cachedCategoryMods.clear();
        }
    }
    const auto& mods = (currentCategoryIndex < moduleCategoryCount) ? cachedCategoryMods : emptyMods;
    if (categoryChanged) {
        openEnumSetting = nullptr;
        enumDropdownAnims.clear();
        for (const auto& mod : mods) {
            mod->cAnim = 0.0f;
        }
    }
    std::string q;
    bool hasQuery = false;
    if (searchText[0] != 0) {
        q = std::string(StringUtils::trim(std::string_view(searchText)));
        hasQuery = !q.empty();
    }
    static std::string lastSearchQuery;
    static bool lastHasQuery = false;
    if (hasQuery && (!lastHasQuery || q != lastSearchQuery)) {
        mScrollTarget = 0.0f;
    }
    lastHasQuery = hasQuery;
    lastSearchQuery = q;
    static std::vector<std::shared_ptr<Module>> displayMods;
    static std::string displayModsQuery;
    static uint64_t displayModsAppliedReadyId = 0;
    if (hasQuery)
    {
        if (displayModsQuery != q)
        {
            displayModsQuery = q;
            displayModsAppliedReadyId = 0;

            const auto& all = gFeatureManager->mModuleManager->getModules();
            std::vector<std::shared_ptr<Module>> snapshot;
            snapshot.assign(all.begin(), all.end());
            requestModuleSearch(q, snapshot);
            displayMods = std::move(snapshot);
        }
        else
        {
            uint64_t readyId = 0;
            std::vector<std::shared_ptr<Module>> ready;
            if (tryConsumeModuleSearch(q, readyId, ready) && readyId != displayModsAppliedReadyId)
            {
                displayModsAppliedReadyId = readyId;
                displayMods = std::move(ready);
            }
        }
    }
    else
    {
        displayModsQuery.clear();
        displayModsAppliedReadyId = 0;
    }
    const auto& modsToDraw = hasQuery ? displayMods : mods;

    float columnGap = 15.0f * inScale;
    float columnWidth = (contentW - (columnGap * 2.0f)) / 3.0f;

    if (!isAutobuyCategory && isMouseOver(mainRect) && isEnabled)
    {
        mScrollTarget += scrollDirection * 60.0f;
        scrollDirection = 0;
    }

    if (isAutobuyCategory)
    {
        mScrollTarget = 0.0f;
        mScrollY = 0.0f;
        maxScrollY = 0.0f;
    }

    if (mScrollTarget < 0.0f) mScrollTarget = 0.0f;
    if (mScrollTarget > maxScrollY) mScrollTarget = maxScrollY;

    mScrollY = MathUtils::animate(mScrollTarget, mScrollY, deltaTime * 12.0f);

    float col1Y = contentY - mScrollY;
    float col2Y = contentY - mScrollY;
    float col3Y = contentY - mScrollY;

    float heightCol1 = 0.0f;
    float heightCol2 = 0.0f;
    float heightCol3 = 0.0f;
    bool enumClickConsumed = false;
    bool colorClickConsumed = false;
    Setting* pendingEnumSetting = nullptr;
    EnumSettingBase* pendingEnum = nullptr;
    ImVec4 pendingEnumDropRect = ImVec4(0, 0, 0, 0);
    ImColor pendingEnumDropBorder = ImColor(0, 0, 0, 0);
    float pendingEnumAnim = 0.0f;
    float pendingEnumSettingsAlpha = 0.0f;
    float pendingEnumDropW = 0.0f;
    ImVec4 openColorButtonRect = ImVec4(0, 0, 0, 0);
    ImVec4 openColorPickerRect = ImVec4(0, 0, 0, 0);
    Module* pendingModuleDescModule = nullptr;
    std::string pendingModuleDescText;
    ImVec4 pendingModuleDescIconRect = ImVec4(0, 0, 0, 0);
    float pendingModuleDescAnim = 0.0f;
    float pendingModuleDescScale = 1.0f;
    float pendingModuleDescFade = 0.0f;
    float pendingModuleDescAlpha = 0.0f;

    if (isAutobuyCategory)
    {
        auto& autobuy = GetAutobuyController();
        static std::unordered_map<size_t, float> autobuyItemToggleAnims;
        static std::unordered_map<size_t, float> autobuyItemSelectAnims;
        static float autobuyItemsScroll = 0.0f;
        static float autobuyItemsScrollTarget = 0.0f;
        static float autobuySettingsFadeT = 1.0f;
        static bool autobuySettingsFadingOut = false;
        static int autobuySettingsPendingIndex = -1;
        static std::vector<AutobuyController::ItemDefinition> autobuyDefs;
        static std::vector<AutobuyController::ItemSettings> autobuyCfgs;
        struct AutobuyIconTex {
            ID3D11ShaderResourceView* srv = nullptr;
            int w = 0;
            int h = 0;
            bool loaded = false;
        };
        static std::unordered_map<std::string, AutobuyIconTex> autobuyIconCache;

        if (categoryChanged) {
            autobuyItemToggleAnims.clear();
            autobuyItemSelectAnims.clear();
            autobuyItemsScroll = 0.0f;
            autobuyItemsScrollTarget = 0.0f;
            autobuyLastSelectedIndex = -1;
            autobuySettingsFadeT = 1.0f;
            autobuySettingsFadingOut = false;
            autobuySettingsPendingIndex = -1;
        }

        ImColor autobuyBaseBg = bgColor;
        if (uiTheme == 1)
        {
            autobuyBaseBg = ImColor(229, 229, 233, (int)(255.0f * std::clamp(bgColor.Value.w, 0.0f, 1.0f)));

            float bgPad = 14.0f * inScale;
            ImVec2 bgMin = ImVec2(contentRect.x - bgPad, contentRect.y + contentSlideY - bgPad);
            ImVec2 bgMax = ImVec2(contentRect.z + bgPad, contentRect.w + contentSlideY + bgPad);
            bgMin.x = std::max(bgMin.x, mainRect.x);
            bgMin.y = std::max(bgMin.y, mainRect.y);
            bgMax.x = std::min(bgMax.x, mainRect.z);
            bgMax.y = std::min(bgMax.y, mainRect.w);
            drawList->AddRectFilled(
                bgMin,
                bgMax,
                autobuyBaseBg,
                0.0f
            );
        }

        float outerPad = 8.0f * inScale;
        float panelGap = 14.0f * inScale;
        float leftW = contentW * 0.62f;

        ImVec4 leftRect = ImVec4(
            contentRect.x + outerPad,
            contentRect.y + outerPad + contentSlideY,
            contentRect.x + outerPad + leftW,
            contentRect.w - outerPad + contentSlideY
        );

        ImVec4 rightRect = ImVec4(
            leftRect.z + panelGap,
            contentRect.y + outerPad + contentSlideY,
            contentRect.z - outerPad,
            contentRect.w - outerPad + contentSlideY
        );

        ImColor panelBg = ColorUtils::getUiCardColor(1.0f);
        ImColor panelBorder = outlineColor;
        if (uiTheme == 1)
        {
            panelBg = autobuyBaseBg;
        }
        drawList->AddRectFilled(ImVec2(leftRect.x, leftRect.y), ImVec2(leftRect.z, leftRect.w), panelBg, 14.0f * inScale);
        drawList->AddRect(ImVec2(leftRect.x, leftRect.y), ImVec2(leftRect.z, leftRect.w), panelBorder, 14.0f * inScale, 0, 1.2f);
        drawList->AddRectFilled(ImVec2(rightRect.x, rightRect.y), ImVec2(rightRect.z, rightRect.w), panelBg, 14.0f * inScale);
        drawList->AddRect(ImVec2(rightRect.x, rightRect.y), ImVec2(rightRect.z, rightRect.w), panelBorder, 14.0f * inScale, 0, 1.2f);

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::SetNextWindowPos(ImVec2(contentRect.x, contentRect.y));
        ImGui::SetNextWindowSize(ImVec2(contentW, contentH));
        ImGui::Begin("##autobuy_ui", nullptr, winFlags);

        autobuy.getConfigSnapshot(autobuyDefs, autobuyCfgs);
        const size_t itemCount = autobuyDefs.size();
        if (itemCount == 0)
        {
            ImGui::End();
            drawList->PopClipRect();
            if (pushedComfortaa) ImGui::PopFont();
            return;
        }

        autobuySelectedIndex = std::clamp(autobuySelectedIndex, 0, (int)itemCount - 1);

        auto loadAutobuyBuffersForIndex = [&](int idx) {
            idx = std::clamp(idx, 0, (int)itemCount - 1);
            const auto& cfg = autobuyCfgs[(size_t)idx];
            std::memset(autobuyPriceInput, 0, sizeof(autobuyPriceInput));
            if (!cfg.price.empty())
            {
                std::strncpy(autobuyPriceInput, cfg.price.c_str(), sizeof(autobuyPriceInput) - 1);
            }
            autobuyQuantityInput = cfg.quantity;
            std::memset(autobuyQuantityInputBuf, 0, sizeof(autobuyQuantityInputBuf));
            std::snprintf(autobuyQuantityInputBuf, sizeof(autobuyQuantityInputBuf), "%d", autobuyQuantityInput);
        };

        if (autobuyLastSelectedIndex < 0 || autobuyLastSelectedIndex >= (int)itemCount)
        {
            autobuyLastSelectedIndex = autobuySelectedIndex;
            loadAutobuyBuffersForIndex(autobuyLastSelectedIndex);
        }

        float innerPad = 14.0f * inScale;
        float titleFont = 0.90f * inScale;
        ImRenderUtils::drawText(ImVec2(leftRect.x + innerPad, leftRect.y + innerPad), "Items", textMain, titleFont, 1.0f, true, 0, drawList);
        if (autobuySelectedIndex != autobuyLastSelectedIndex && autobuySettingsPendingIndex != autobuySelectedIndex)
        {
            autobuySettingsPendingIndex = autobuySelectedIndex;
            autobuySettingsFadingOut = true;
        }

        float fadeTarget = autobuySettingsFadingOut ? 0.0f : 1.0f;
        autobuySettingsFadeT = MathUtils::animate(fadeTarget, autobuySettingsFadeT, deltaTime * 12.0f);
        if (autobuySettingsFadingOut && autobuySettingsFadeT < 0.02f)
        {
            autobuyLastSelectedIndex = std::clamp(autobuySettingsPendingIndex, 0, (int)itemCount - 1);
            autobuySettingsPendingIndex = -1;
            autobuySettingsFadingOut = false;
            autobuySettingsFadeT = 0.0f;
            loadAutobuyBuffersForIndex(autobuyLastSelectedIndex);
        }
        float settingsAlpha = autobuySettingsFadeT * autobuySettingsFadeT * (3.0f - 2.0f * autobuySettingsFadeT);

        ImRenderUtils::drawText(ImVec2(rightRect.x + innerPad, rightRect.y + innerPad), "Settings", textMain, titleFont, settingsAlpha, true, 0, drawList);

        float cardsTop = leftRect.y + innerPad + 26.0f * inScale;
        int cols = 3;
        float cardGap = 8.0f * inScale;
        float cardsX0 = leftRect.x + innerPad;
        float cardsX1 = leftRect.z - innerPad;
        float cardsAvailW = std::max(0.0f, cardsX1 - cardsX0);
        float cardWFull = (cardsAvailW - cardGap * (cols - 1)) / (float)cols;
        float cardW = cardWFull * 0.95f;
        float gridW = cardW * (float)cols + cardGap * (float)(cols - 1);
        float cardsLeft = cardsX0 + std::max(0.0f, (cardsAvailW - gridW) / 2.0f);
        float cardH = 64.0f * inScale;

        int rows = (int)((itemCount + (size_t)cols - 1) / (size_t)cols);
        float totalCardsH = (float)rows * cardH + (float)std::max(0, rows - 1) * cardGap;
        float viewCardsH = (leftRect.w - innerPad) - cardsTop;
        float maxCardsScroll = std::max(0.0f, totalCardsH - viewCardsH);
        float wheel = ImGui::GetIO().MouseWheel;
        bool canScrollCards = isEnabled && isMouseOver(contentRect) && isMouseOver(leftRect) && isMouseOver(ImVec4(cardsX0, cardsTop, cardsX1, leftRect.w - innerPad));
        if (canScrollCards && wheel != 0.0f) {
            autobuyItemsScrollTarget -= wheel * (60.0f * inScale);
        }
        autobuyItemsScrollTarget = std::clamp(autobuyItemsScrollTarget, 0.0f, maxCardsScroll);
        autobuyItemsScroll = MathUtils::animate(autobuyItemsScrollTarget, autobuyItemsScroll, deltaTime * 14.0f);

        ImFont* itemIconFont = nullptr;
        {
            auto itFont = FontHelper::Fonts.find("essence.ttf_large");
            if (itFont != FontHelper::Fonts.end() && itFont->second) itemIconFont = itFont->second;
        }

        ImVec2 cardsClipMin = ImVec2(cardsX0, cardsTop);
        ImVec2 cardsClipMax = ImVec2(cardsX1, leftRect.w - innerPad);
        drawList->PushClipRect(cardsClipMin, cardsClipMax, true);
        ImGui::PushClipRect(cardsClipMin, cardsClipMax, true);

        int remainingTextureLoads = 2;
        const float rowStride = cardH + cardGap;
        int startRow = (rowStride > 0.0f) ? (int)std::floor(autobuyItemsScroll / rowStride) : 0;
        int endRow = (rowStride > 0.0f) ? (int)std::floor((autobuyItemsScroll + viewCardsH) / rowStride) : (rows - 1);
        startRow = std::clamp(startRow - 1, 0, std::max(0, rows - 1));
        endRow = std::clamp(endRow + 1, 0, std::max(0, rows - 1));
        size_t startIndex = (size_t)startRow * (size_t)cols;
        size_t endIndex = std::min(itemCount, ((size_t)endRow + 1u) * (size_t)cols);

        for (size_t i = startIndex; i < endIndex; i++)
        {
            int row = (int)i / cols;
            int col = (int)i % cols;
            float x = cardsLeft + col * (cardW + cardGap);
            float y = (cardsTop + row * (cardH + cardGap)) - autobuyItemsScroll;

            ImVec4 cardRect = ImVec4(x, y, x + cardW, y + cardH);
            bool selected = (int)i == autobuySelectedIndex;

            const auto& def = autobuyDefs[i];
            const auto& cfg = autobuyCfgs[i];

            float& selT = autobuyItemSelectAnims[i];
            float selTarget = selected ? 1.0f : 0.0f;
            selT = MathUtils::animate(selTarget, selT, deltaTime * 12.0f);
            float selEased = selT * selT * (3.0f - 2.0f * selT);

            ImColor cardBg = (uiTheme == 1) ? panelBg : cardColor;
            ImColor borderOff = (uiTheme == 1) ? ImColor(218, 218, 230, 255) : ImColor(26, 26, 34, 255);
            ImColor border = MathUtils::lerpImColor(borderOff, accentColor, selEased);
            float borderThick = MathUtils::lerp(1.2f, 2.0f, selEased);
            float radius = 10.0f * inScale;
            drawList->AddRectFilled(ImVec2(cardRect.x, cardRect.y), ImVec2(cardRect.z, cardRect.w), cardBg, radius);
            if (selEased > 0.01f) {
                ImColor glow = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(24.0f * selEased));
                drawList->AddRectFilled(ImVec2(cardRect.x, cardRect.y), ImVec2(cardRect.z, cardRect.w), glow, radius);
            }
            drawList->AddRect(ImVec2(cardRect.x, cardRect.y), ImVec2(cardRect.z, cardRect.w), border, radius, 0, borderThick);

            float& itemT = autobuyItemToggleAnims[i];
            float itemTarget = cfg.enabled ? 1.0f : 0.0f;
            itemT = MathUtils::animate(itemTarget, itemT, deltaTime * 15.0f);
            float itemEased = itemT * itemT * (3.0f - 2.0f * itemT);

            float switchW = 25.0f * inScale;
            float switchH = 14.0f * inScale;
            ImVec4 monitorRect = ImVec4(
                cardRect.z - 10.0f * inScale - switchW,
                cardRect.w - 10.0f * inScale - switchH,
                cardRect.z - 10.0f * inScale,
                cardRect.w - 10.0f * inScale
            );

            bool monitorHovered = isMouseOver(monitorRect) && isEnabled && isMouseOver(ImVec4(cardsClipMin.x, cardsClipMin.y, cardsClipMax.x, cardsClipMax.y));
            float monitorRadius = (monitorRect.w - monitorRect.y) / 2.0f;
            ImColor monitorTrackOff = (uiTheme == 1) ? ImColor(214, 214, 224, 255) : ImColor(22, 22, 28, 255);
            ImColor monitorTrackOn = accentColor;
            ImColor monitorTrack = MathUtils::lerpImColor(monitorTrackOff, monitorTrackOn, itemEased);
            ImColor monitorBorderOff = (uiTheme == 1) ? ImColor(206, 206, 218, 255) : ImColor(48, 48, 60, 255);
            ImColor monitorBorder = MathUtils::lerpImColor(monitorBorderOff, accentColor, itemEased);

            drawList->AddRectFilled(ImVec2(monitorRect.x, monitorRect.y), ImVec2(monitorRect.z, monitorRect.w), monitorTrack, monitorRadius);
            drawList->AddRect(ImVec2(monitorRect.x, monitorRect.y), ImVec2(monitorRect.z, monitorRect.w), monitorBorder, monitorRadius, 0, 1.4f);

            float knobInset = 2.0f * inScale;
            float knobRadius = monitorRadius - knobInset;
            float knobMinX = monitorRect.x + monitorRadius;
            float knobMaxX = monitorRect.z - monitorRadius;
            float knobX = knobMinX + (knobMaxX - knobMinX) * itemEased;
            float knobY = monitorRect.y + monitorRadius;
            drawList->AddCircleFilled(ImVec2(knobX, knobY + 0.7f * inScale), knobRadius, ImColor(0, 0, 0, 55));
            drawList->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, ImColor(250, 250, 255, 255));
            drawList->AddCircle(ImVec2(knobX, knobY), knobRadius, ImColor(0, 0, 0, 55), 0, 1.0f);

            float iconSize = 18.0f * inScale;
            float iconX = cardRect.x + 11.0f * inScale;
            float iconY = cardRect.y + 13.0f * inScale;
            ImVec2 iconMin = ImVec2(iconX, iconY);
            ImVec2 iconMax = ImVec2(iconX + iconSize, iconY + iconSize);

            ID3D11ShaderResourceView* itemSrv = nullptr;
            {
                auto& cached = autobuyIconCache[def.key];
                if (!cached.loaded && remainingTextureLoads > 0) {
                    cached.loaded = true;
                    remainingTextureLoads--;
                    auto tryLoad = [&](const std::string& name) -> bool {
                        auto it = ResourceLoader::Resources.find(name);
                        if (it == ResourceLoader::Resources.end()) return false;
                        if (it->second.data() == nullptr) return false;
                        return D3DHook::loadTextureFromEmbeddedResource(name.c_str(), &cached.srv, &cached.w, &cached.h);
                    };

                    const std::string name1 = def.key + ".png";
                    const std::string name2 = std::string("items/") + def.key + ".png";
                    const std::string name3 = def.key;
                    const std::string name4 = std::string("items/") + def.key;

                    std::string altKey;
                    if (def.key.rfind("nether_", 0) == 0) {
                        altKey = std::string("netherite_") + def.key.substr(7);
                    }

                    if (!tryLoad(name1)) {
                        if (!tryLoad(name2)) {
                            if (!tryLoad(name3)) {
                                if (!tryLoad(name4) && !altKey.empty()) {
                                    const std::string n1 = altKey + ".png";
                                    const std::string n2 = std::string("items/") + altKey + ".png";
                                    const std::string n3 = altKey;
                                    const std::string n4 = std::string("items/") + altKey;
                                    if (!tryLoad(n1)) {
                                        if (!tryLoad(n2)) {
                                            if (!tryLoad(n3)) {
                                                tryLoad(n4);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (cached.srv == nullptr && !def.icon.empty())
                    {
                        const std::string& icon = def.icon;
                        const bool hasDot = (icon.find('.') != std::string::npos);

                        const std::string i1 = hasDot ? icon : (icon + ".png");
                        const std::string i2 = std::string("items/") + i1;
                        const std::string i3 = icon;
                        const std::string i4 = std::string("items/") + icon;

                        if (!tryLoad(i1))
                        {
                            if (!tryLoad(i2))
                            {
                                if (!tryLoad(i3))
                                {
                                    tryLoad(i4);
                                }
                            }
                        }
                    }
                }
                itemSrv = cached.srv;
            }

            if (itemSrv != nullptr)
            {
                drawList->AddImage(itemSrv, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
            }
            else if (itemIconFont)
            {
                ImGui::PushFont(itemIconFont);
                ImRenderUtils::drawText(ImVec2(iconMin.x, iconMin.y), def.icon, textDim, 0.86f * inScale, 1.0f, true, 0, drawList);
                ImGui::PopFont();
            }

            ImRenderUtils::drawText(ImVec2(cardRect.x + 11.0f * inScale + 26.0f * inScale, cardRect.y + 13.0f * inScale), def.name, textMain, 0.80f * inScale, 1.0f, true, 0, drawList);

            std::string priceText = cfg.price.empty() ? "price: -" : ("price: " + cfg.price);
            ImRenderUtils::drawText(ImVec2(iconMin.x, iconMax.y + 6.0f * inScale), priceText, textDim, 0.75f * inScale, 1.0f, true, 0, drawList);

            ImGui::SetCursorScreenPos(ImVec2(cardRect.x, cardRect.y));
            ImGui::PushID((int)i);
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##autobuy_item", ImVec2(cardW, cardH));
            bool cardClicked = ImGui::IsItemClicked(0) && isEnabled;
            if (cardClicked && !monitorHovered)
            {
                autobuySelectedIndex = (int)i;
            }

            ImGui::SetCursorScreenPos(ImVec2(monitorRect.x, monitorRect.y));
            ImGui::InvisibleButton("##autobuy_toggle", ImVec2(monitorRect.z - monitorRect.x, monitorRect.w - monitorRect.y));
            if (ImGui::IsItemClicked(0) && isEnabled) autobuy.setItemEnabled(i, !cfg.enabled);
            ImGui::PopID();
        }

        ImGui::PopClipRect();
        drawList->PopClipRect();

        {
            auto def = autobuy.getItemDefinition((size_t)autobuyLastSelectedIndex);
            ImRenderUtils::drawText(ImVec2(rightRect.x + innerPad, rightRect.y + innerPad + 26.0f * inScale), def.name, textMain, 0.88f * inScale, settingsAlpha, true, 0, drawList);

            float formX = rightRect.x + innerPad;
            float formY = rightRect.y + innerPad + 120.0f * inScale;
            float formW = (rightRect.z - innerPad) - formX;
            float fieldH = 32.0f * inScale;
            float fieldR = fieldH / 2.0f;
            float fieldPadX = 12.0f * inScale;
            float labelFont = 0.76f * inScale;
            float valueFont = 0.92f * inScale;

            auto drawInputBox = [&](const char* hitId, const char* inputId, const ImVec4& rect, char* buf, size_t bufSize, ImGuiInputTextFlags flags, const char* placeholder, bool& outActive) -> bool {
                float a = std::clamp(settingsAlpha, 0.0f, 1.0f);
                int alpha255 = (int)(255.0f * a);
                bool hovered = isMouseOver(rect) && isEnabled;
                ImColor bg = hovered
                    ? ((uiTheme == 1) ? ImColor(244, 244, 250, alpha255) : ImColor(26, 26, 34, alpha255))
                    : ((uiTheme == 1) ? ImColor(236, 236, 242, alpha255) : ImColor(22, 22, 28, alpha255));
                drawList->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.z, rect.w), bg, fieldR);

                ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
                ImGui::PushID(hitId);
                ImGui::InvisibleButton("##hit", ImVec2(rect.z - rect.x, rect.w - rect.y));
                bool clicked = ImGui::IsItemClicked(0);
                ImGui::PopID();

                float inputW = std::max(0.0f, (rect.z - rect.x) - fieldPadX * 2.0f);
                float lineH = ImGui::GetTextLineHeight();
                float cursorY = std::floor((fieldH - lineH) / 2.0f);

                ImGui::SetCursorScreenPos(ImVec2(rect.x + fieldPadX, rect.y + cursorY));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(accentColor.Value.x, accentColor.Value.y, accentColor.Value.z, 0.35f));
                ImGui::SetNextItemWidth(inputW);
                if (clicked) ImGui::SetKeyboardFocusHere();
                ImGui::InputText(inputId, buf, bufSize, flags);
                outActive = ImGui::IsItemActive();
                bool editedAfter = ImGui::IsItemDeactivatedAfterEdit();
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();

                bool focused = ImGui::IsItemFocused();
                ImColor borderOff = (uiTheme == 1) ? ImColor(206, 206, 218, alpha255) : ImColor(44, 44, 54, alpha255);
                ImColor borderOn = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), alpha255);
                ImColor border = focused ? borderOn : borderOff;
                drawList->AddRect(ImVec2(rect.x, rect.y), ImVec2(rect.z, rect.w), border, fieldR, 0, 1.4f);

                std::string value = std::string(buf);
                bool showPlaceholder = value.empty() && !outActive;
                std::string shown = showPlaceholder ? std::string(placeholder) : value;
                ImColor shownCol = showPlaceholder ? textDim : textMain;

                float textY = std::floor(rect.y + (fieldH - ImRenderUtils::getTextHeightStr(&shown, valueFont)) / 2.0f);
                ImVec2 clipMin = ImVec2(rect.x + fieldPadX, rect.y);
                ImVec2 clipMax = ImVec2(rect.z - fieldPadX, rect.w);
                drawList->PushClipRect(clipMin, clipMax, true);
                ImRenderUtils::drawText(ImVec2(rect.x + fieldPadX, textY), shown, shownCol, valueFont, a, true, 0, drawList);
                drawList->PopClipRect();

                if (outActive) {
                    float caretW = 1.4f * inScale;
                    float caretH = lineH * 0.92f;
                    float textW = ImRenderUtils::getTextWidth(&shown, valueFont);
                    float clipW = std::min(textW, clipMax.x - clipMin.x);
                    float caretX = rect.x + fieldPadX + clipW + 0.8f * inScale;
                    float blink = std::fmod((float)ImGui::GetTime(), 1.0f);
                    if (blink < 0.55f) {
                        drawList->AddRectFilled(ImVec2(caretX, rect.y + (fieldH - caretH) / 2.0f), ImVec2(caretX + caretW, rect.y + (fieldH + caretH) / 2.0f), ImColor(255, 255, 255, alpha255), 1.0f);
                    }
                }
                return editedAfter;
            };

            ImRenderUtils::drawText(ImVec2(formX, formY - 18.0f * inScale), "Price", textDim, labelFont, settingsAlpha, true, 0, drawList);
            ImVec4 priceRect = ImVec4(formX, formY, formX + formW, formY + fieldH);
            bool priceActive = false;
            bool priceEdited = drawInputBox("autobuy_price", "##autobuy_price", priceRect, autobuyPriceInput, sizeof(autobuyPriceInput), 0, "190,000,000.00", priceActive);
            if (priceEdited) {
                autobuy.setItemPrice((size_t)autobuyLastSelectedIndex, std::string(autobuyPriceInput));
            }

            float qtyLabelY = formY + fieldH + 22.0f * inScale;
            float qtyFieldY = qtyLabelY + 18.0f * inScale;
            ImRenderUtils::drawText(ImVec2(formX, qtyLabelY), "Quantity", textDim, labelFont, settingsAlpha, true, 0, drawList);
            ImVec4 qtyRect = ImVec4(formX, qtyFieldY, formX + formW, qtyFieldY + fieldH);
            bool qtyActive = false;
            bool qtyEdited = drawInputBox("autobuy_qty", "##autobuy_qty", qtyRect, autobuyQuantityInputBuf, sizeof(autobuyQuantityInputBuf), ImGuiInputTextFlags_CharsDecimal, "1", qtyActive);

            if (!qtyActive && autobuyQuantityInputBuf[0] == 0) {
                std::snprintf(autobuyQuantityInputBuf, sizeof(autobuyQuantityInputBuf), "%d", autobuyQuantityInput);
            }

            if (qtyEdited) {
                int q = std::atoi(autobuyQuantityInputBuf);
                if (q < 1) q = 1;
                if (q > 9999) q = 9999;
                if (q != autobuyQuantityInput) {
                    autobuyQuantityInput = q;
                    autobuy.setItemQuantity((size_t)autobuyLastSelectedIndex, autobuyQuantityInput);
                }
                std::snprintf(autobuyQuantityInputBuf, sizeof(autobuyQuantityInputBuf), "%d", autobuyQuantityInput);
            }

            ImRenderUtils::drawText(ImVec2(formX + 64.0f * inScale, formY - 18.0f * inScale), "(example: 190,000,000.00)", textDim, labelFont, settingsAlpha, true, 0, drawList);
        }

        float btnW = std::min(220.0f * inScale, (rightRect.z - rightRect.x) - innerPad * 2.0f);
        float btnH = 36.0f * inScale;
        float btnX = rightRect.x + ((rightRect.z - rightRect.x) - btnW) / 2.0f;
        float btnY = rightRect.w - innerPad - btnH;

        ImVec4 btnRectBase = ImVec4(btnX, btnY, btnX + btnW, btnY + btnH);
        bool hoveredBase = isMouseOver(btnRectBase) && isEnabled;

        static float autobuyBtnScaleAnim = 1.0f;
        float btnScaleTarget = hoveredBase ? 1.05f : 1.0f;
        autobuyBtnScaleAnim = MathUtils::animate(btnScaleTarget, autobuyBtnScaleAnim, deltaTime * 12.0f);
        float btnScale = autobuyBtnScaleAnim;

        float btnCx = (btnRectBase.x + btnRectBase.z) / 2.0f;
        float btnCy = (btnRectBase.y + btnRectBase.w) / 2.0f;
        float btnHalfW = ((btnRectBase.z - btnRectBase.x) / 2.0f) * btnScale;
        float btnHalfH = ((btnRectBase.w - btnRectBase.y) / 2.0f) * btnScale;
        ImVec4 btnRect = ImVec4(btnCx - btnHalfW, btnCy - btnHalfH, btnCx + btnHalfW, btnCy + btnHalfH);
        bool hovered = isMouseOver(btnRect) && isEnabled;

        float runEased = autobuy.isRunning() ? 1.0f : 0.0f;

        int btnA = (int)(255.0f * std::clamp(settingsAlpha, 0.0f, 1.0f));
        ImColor baseBg = ColorUtils::getUiCardColor((float)btnA / 255.0f);
        ImColor runBg = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(40.0f * (float)btnA / 255.0f));
        ImColor bg = MathUtils::lerpImColor(baseBg, runBg, runEased);
        if (hovered) {
            ImColor hoverTint = ImColor(255, 255, 255, (int)(16.0f * (float)btnA / 255.0f));
            bg = MathUtils::lerpImColor(bg, hoverTint, 1.0f);
        }

        ImColor borderOff = (uiTheme == 1) ? ImColor(206, 206, 218, btnA) : ImColor(44, 44, 54, btnA);
        ImColor borderOn = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), btnA);
        ImColor border = MathUtils::lerpImColor(borderOff, borderOn, runEased);
        float drawW = btnRect.z - btnRect.x;
        float drawH = btnRect.w - btnRect.y;
        float r = drawH / 2.0f;

        drawList->AddRectFilled(ImVec2(btnRect.x, btnRect.y), ImVec2(btnRect.z, btnRect.w), bg, r);
        drawList->AddRect(ImVec2(btnRect.x, btnRect.y), ImVec2(btnRect.z, btnRect.w), border, r, 0, 1.6f);

        std::string label = autobuy.isRunning() ? "Stop" : "Start";
        float font = 0.92f * inScale;
        float tw = ImRenderUtils::getTextWidth(&label, font);
        float th = ImRenderUtils::getTextHeightStr(&label, font);
        float tx = btnRect.x + (drawW - tw) / 2.0f;
        float ty = btnRect.y + (drawH - th) / 2.0f;
        ImRenderUtils::drawText(ImVec2(tx, ty), label, textMain, font, settingsAlpha, true, 0, drawList);

        ImGui::SetCursorScreenPos(ImVec2(btnRect.x, btnRect.y));
        ImGui::InvisibleButton("##autobuy_start_btn", ImVec2(drawW, drawH));
        if (ImGui::IsItemClicked(0) && isEnabled)
        {
            if (autobuy.isRunning()) autobuy.stop();
            else autobuy.start();
            ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
        }

        ImGui::End();
    }
    else if (ircPanelAnim > 0.05f)
    {
        float t = std::clamp(ircPanelAnim, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float panelAlpha = eased * categoryFade;

        float outerPad = 8.0f * inScale;
        ImVec4 panelRect = ImVec4(
            contentRect.x + outerPad,
            contentRect.y + outerPad + contentSlideY,
            contentRect.z - outerPad,
            contentRect.w - outerPad + contentSlideY
        );
        float openSlideY = (1.0f - eased) * 18.0f * inScale;
        panelRect.y += openSlideY;
        panelRect.w += openSlideY;

        ImColor panelBg = ColorUtils::getUiCardColor(panelAlpha);
        ImColor panelBorder = ColorUtils::getUiBorderColor(panelAlpha * (25.0f / 255.0f));
        float radius = 14.0f * inScale;
        drawList->AddRectFilled(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), panelBg, radius);
        drawList->AddRect(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), panelBorder, radius, 0, 1.2f);

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::SetNextWindowPos(ImVec2(contentRect.x, contentRect.y));
        ImGui::SetNextWindowSize(ImVec2(contentW, contentH));
        ImGui::Begin("##irc_panel", nullptr, winFlags);

        float innerPad = 14.0f * inScale;
        float headerH = 58.0f * inScale;
        float footerH = 62.0f * inScale;
        ImVec4 headerRect = ImVec4(panelRect.x, panelRect.y, panelRect.z, panelRect.y + headerH);
        ImVec4 footerRect = ImVec4(panelRect.x, panelRect.w - footerH, panelRect.z, panelRect.w);

        float listFullW = (panelRect.z - panelRect.x) - innerPad * 2.0f;
        const float dmGapX = 10.0f * inScale;
        float dmW = std::clamp(listFullW * 0.36f, 190.0f * inScale, 300.0f * inScale);
        dmW = std::max(120.0f * inScale, dmW - 70.0f * inScale);
        float listW = std::max(80.0f * inScale, listFullW - dmW - dmGapX);
        float chatColL = panelRect.x + innerPad + dmW + dmGapX;
        float chatColR = chatColL + listW;

        {
            static bool bgTried = false;
            static ID3D11ShaderResourceView* bgSrv = nullptr;
            static int bgW = 0;
            static int bgH = 0;
            if (!bgTried)
            {
                bgTried = true;
                D3DHook::loadTextureFromEmbeddedResource("bg.png", &bgSrv, &bgW, &bgH);
            }

            if (bgSrv)
            {
                ImVec2 bgMin = ImVec2(chatColL, panelRect.y);
                ImVec2 bgMax = ImVec2(panelRect.z, panelRect.w);
                drawList->PushClipRect(bgMin, bgMax, true);
                int imgA = (int)(255.0f * panelAlpha);
                ImDrawFlags bgFlags = ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomRight;
                drawList->AddImageRounded((ImTextureID)bgSrv, bgMin, bgMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, imgA), radius, bgFlags);
                ImColor tint = (uiTheme == 1) ? ImColor(255, 255, 255, (int)(80.0f * panelAlpha)) : ImColor(10, 10, 14, (int)(110.0f * panelAlpha));
                drawList->AddRectFilled(bgMin, bgMax, tint, radius, bgFlags);
                drawList->PopClipRect();
            }
        }

        

        auto* ircModule = gFeatureManager->mModuleManager->getModule<IRC>();
        bool ircEnabled = ircModule && ircModule->mEnabled;

        ConnectionState state = ConnectionState::Disconnected;
        if (IrcManager::mClient) state = IrcManager::mClient->mConnectionState.load();

        int onlineCount = 0;
        if (IrcManager::mClient)
        {
            onlineCount = (int)IrcManager::mClient->getConnectedUsers().size();
        }

        static uint64_t lastSelfAvatarCheck = 0;
        static std::string selfAvatarB64;
        static ID3D11ShaderResourceView* selfAvatarSrv = nullptr;
        if (NOW - lastSelfAvatarCheck > 2000)
        {
            lastSelfAvatarCheck = NOW;
            std::string solDir = FileUtils::getSolsticeDir();
            std::string pathPng = solDir + "logo.png";
            std::string pathB64 = solDir + "logo.b64";

            std::vector<uint8_t> bytes;
            if (FileUtils::fileExists(pathPng))
            {
                auto fileBytesU8 = FileUtils::readFile(pathPng);
                if (!fileBytesU8.empty() && fileBytesU8.size() <= (256ull * 1024ull))
                {
                    bytes.reserve(fileBytesU8.size());
                    for (unsigned char c : fileBytesU8) bytes.push_back(static_cast<uint8_t>(c));
                }
            }
            else if (FileUtils::fileExists(pathB64))
            {
                std::ifstream in(pathB64, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    const std::string raw = ss.str();
                    std::string b64 = std::string(StringUtils::trim(raw));
                    if (!b64.empty() && b64.size() <= 400000)
                    {
                        bytes = Base64::decodeBytes(b64);
                    }
                }
            }

            if (!bytes.empty())
            {
                std::string b64 = Base64::encodeBytes(bytes);
                if (!b64.empty() && b64 != selfAvatarB64)
                {
                    int w = 0, h = 0, ch = 0;
                    unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4);
                    if (rgba && w > 0 && h > 0)
                    {
                        ID3D11ShaderResourceView* srv = nullptr;
                        D3DHook::createTextureFromData(rgba, w, h, &srv);
                        if (srv)
                        {
                            if (selfAvatarSrv) selfAvatarSrv->Release();
                            selfAvatarSrv = srv;
                            selfAvatarB64 = b64;
                        }
                    }
                    if (rgba) stbi_image_free(rgba);
                }
            }
        }

        const bool isGlobalSelected = ircDmTargetKey.empty();
        bool peerOnline = false;
        if (!isGlobalSelected && IrcManager::mClient)
        {
            auto users = IrcManager::mClient->getConnectedUsers();
            std::string mappedKey;
            std::string mappedLabel;
            for (const auto& u : users)
            {
                const std::string displayName = u.prefix.empty() ? u.username : (u.prefix + " " + u.username);
                if (
                    StringUtils::equalsIgnoreCase(u.username, ircDmTargetKey) ||
                    StringUtils::equalsIgnoreCase(u.playerName, ircDmTargetKey) ||
                    StringUtils::equalsIgnoreCase(displayName, ircDmTargetKey)
                )
                {
                    peerOnline = true;
                    mappedKey = u.username;
                    mappedLabel = u.playerName;
                    break;
                }
            }
            if (peerOnline && !mappedKey.empty() && !StringUtils::equalsIgnoreCase(ircDmTargetKey, mappedKey))
            {
                ircDmTargetKey = mappedKey;
                if (!mappedLabel.empty()) ircDmTargetLabel = mappedLabel;
            }
        }

        ImVec2 avatarC = ImVec2(chatColL + 28.0f * inScale, headerRect.y + headerH * 0.5f);
        float avatarR = 16.5f * inScale;

        static uint64_t lastPeerAvatarCheck = 0;
        static std::string peerAvatarKey;
        static std::string peerAvatarB64;
        static ID3D11ShaderResourceView* peerAvatarSrv = nullptr;
        const bool peerKeyChanged = (peerAvatarKey != ircDmTargetKey);
        if (!isGlobalSelected && IrcManager::mClient && (peerKeyChanged || (NOW - lastPeerAvatarCheck > 1500)))
        {
            lastPeerAvatarCheck = NOW;
            peerAvatarKey = ircDmTargetKey;
            std::string b64 = IrcManager::mClient->getUserAvatarB64(ircDmTargetKey);
            if (peerKeyChanged)
            {
                peerAvatarB64.clear();
                if (peerAvatarSrv)
                {
                    peerAvatarSrv->Release();
                    peerAvatarSrv = nullptr;
                }
            }
            if (b64.empty())
            {
                peerAvatarB64.clear();
                if (peerAvatarSrv)
                {
                    peerAvatarSrv->Release();
                    peerAvatarSrv = nullptr;
                }
            }
            else if (b64 != peerAvatarB64)
            {
                auto bytes = Base64::decodeBytes(b64);
                if (bytes.empty())
                {
                    peerAvatarB64.clear();
                    if (peerAvatarSrv)
                    {
                        peerAvatarSrv->Release();
                        peerAvatarSrv = nullptr;
                    }
                }
                else
                {
                    int w = 0, h = 0, ch = 0;
                    unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4);
                    if (!rgba || w <= 0 || h <= 0)
                    {
                        peerAvatarB64.clear();
                        if (peerAvatarSrv)
                        {
                            peerAvatarSrv->Release();
                            peerAvatarSrv = nullptr;
                        }
                    }
                    else
                    {
                        ID3D11ShaderResourceView* srv = nullptr;
                        D3DHook::createTextureFromData(rgba, w, h, &srv);
                        if (srv)
                        {
                            if (peerAvatarSrv) peerAvatarSrv->Release();
                            peerAvatarSrv = srv;
                            peerAvatarB64 = b64;
                        }
                    }
                    if (rgba) stbi_image_free(rgba);
                }
            }
        }

        if (isGlobalSelected)
        {
            ImColor avatarBg = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(120.0f * panelAlpha));
            drawList->AddCircleFilled(avatarC, avatarR, avatarBg, 32);
            drawList->AddCircle(avatarC, avatarR, ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(200.0f * panelAlpha)), 32, 1.4f);

            static ImFont* energyFont = nullptr;
            if (!energyFont)
            {
                auto it = FontHelper::Fonts.find("essence.ttf");
                if (it != FontHelper::Fonts.end() && it->second) energyFont = it->second;
            }
            std::string icon = "j";
            float iconFont = 1.10f * inScale;
            if (energyFont)
            {
                ImGui::PushFont(energyFont);
                float iw = ImRenderUtils::getTextWidth(&icon, iconFont);
                float ih = ImRenderUtils::getTextHeightStr(&icon, iconFont);
                ImRenderUtils::drawText(ImVec2(avatarC.x - iw * 0.5f, avatarC.y - ih * 0.5f), icon, textMain, iconFont, panelAlpha, true, 0, drawList);
                ImGui::PopFont();
            }
            else
            {
                std::string avatarLetter = "G";
                float avatarFont = 1.05f * inScale;
                float aw = ImRenderUtils::getTextWidth(&avatarLetter, avatarFont);
                float ah = ImRenderUtils::getTextHeightStr(&avatarLetter, avatarFont);
                ImRenderUtils::drawText(ImVec2(avatarC.x - aw * 0.5f, avatarC.y - ah * 0.5f), avatarLetter, textMain, avatarFont, panelAlpha, true, 0, drawList);
            }
        }
        else
        {
            ImVec2 min = ImVec2(avatarC.x - avatarR, avatarC.y - avatarR);
            ImVec2 max = ImVec2(avatarC.x + avatarR, avatarC.y + avatarR);
            if (peerAvatarSrv)
            {
                const int imgA = (int)(255.0f * panelAlpha);
                drawList->AddImageRounded((ImTextureID)peerAvatarSrv, min, max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, imgA), avatarR, 0);
            }
            else
            {
            ImColor avBg = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(200.0f * panelAlpha)) : ImColor(30, 30, 38, (int)(200.0f * panelAlpha));
                drawList->AddCircleFilled(avatarC, avatarR, avBg, 32);
                std::string initials = ircDmTargetLabel.empty() ? (ircDmTargetKey.empty() ? "?" : std::string(1, (char)std::toupper(ircDmTargetKey[0]))) : std::string(1, (char)std::toupper(ircDmTargetLabel[0]));
                float f = 0.90f * inScale;
                float iw = ImRenderUtils::getTextWidth(&initials, f);
                float ih = ImRenderUtils::getTextHeightStr(&initials, f);
                ImColor ic = textMain;
                ic.Value.w = std::clamp(ic.Value.w * panelAlpha, 0.0f, 1.0f);
                ImRenderUtils::drawText(ImVec2(avatarC.x - iw * 0.5f, avatarC.y - ih * 0.5f + 0.6f * inScale), initials, ic, f, panelAlpha, true, 0, drawList);
            }
            drawList->AddCircle(avatarC, avatarR, ImColor(255, 255, 255, (int)(22.0f * panelAlpha)), 32, 1.2f);
        }

        std::string titleStr = isGlobalSelected ? std::string("Global Chat") : (ircDmTargetLabel.empty() ? ircDmTargetKey : ircDmTargetLabel);
        float titleFont = 0.98f * inScale;
        float subFont = 0.78f * inScale;
        float textX = avatarC.x + avatarR + 12.0f * inScale;
        float titleY = headerRect.y + 14.0f * inScale;
        ImRenderUtils::drawText(ImVec2(textX, titleY), titleStr, textMain, titleFont, panelAlpha, true, 0, drawList);

        std::string status;
        if (!ircEnabled) status = "Disabled";
        else if (state == ConnectionState::Connected) status = "Online";
        else if (state == ConnectionState::Connecting) status = "Connecting";
        else status = "Offline";
        bool isOnline = ircEnabled && state == ConnectionState::Connected;
        std::string subtitle = isGlobalSelected ? (isOnline ? (std::to_string(onlineCount) + " online") : status) : (peerOnline ? std::string("Online") : std::string("Offline"));
        float subY = titleY + 20.0f * inScale;
        ImRenderUtils::drawText(ImVec2(textX + 14.0f * inScale, subY), subtitle, textDim, subFont, panelAlpha, true, 0, drawList);
        if (isGlobalSelected ? isOnline : peerOnline)
        {
            ImVec2 dotC = ImVec2(textX + 6.0f * inScale, subY + 6.5f * inScale);
            float dotR = 3.6f * inScale;
            ImColor dotOuter = ImColor(0, 0, 0, (int)(120.0f * panelAlpha));
            ImColor dot = ImColor(70, 220, 120, (int)(255.0f * panelAlpha));
            drawList->AddCircleFilled(dotC, dotR + 1.2f * inScale, dotOuter, 16);
            drawList->AddCircleFilled(dotC, dotR, dot, 16);
            drawList->AddShadowCircle(dotC, dotR * 1.1f, ImColor(70, 220, 120, (int)(140.0f * panelAlpha)), 18, ImVec2(0.f, 0.f), 0, 32);
        }

        float closeSize = 30.0f * inScale;
        ImVec4 closeRect = ImVec4(headerRect.z - innerPad - closeSize, headerRect.y + (headerH - closeSize) * 0.5f, headerRect.z - innerPad, headerRect.y + (headerH + closeSize) * 0.5f);
        bool closeHovered = isMouseOver(closeRect) && isEnabledRaw;
        ImColor closeBg = closeHovered ? ImColor(255, 255, 255, (int)(14.0f * panelAlpha)) : ImColor(0, 0, 0, 0);
        drawList->AddRectFilled(ImVec2(closeRect.x, closeRect.y), ImVec2(closeRect.z, closeRect.w), closeBg, closeSize * 0.35f);
        std::string closeLabel = "X";
        float cw = ImRenderUtils::getTextWidth(&closeLabel, 0.92f * inScale);
        float ch = ImRenderUtils::getTextHeightStr(&closeLabel, 0.92f * inScale);
        ImColor closeText = closeHovered ? MathUtils::lerpImColor(textDim, accentColor, 0.70f) : textDim;
        ImRenderUtils::drawText(ImVec2(closeRect.x + (closeSize - cw) * 0.5f, closeRect.y + (closeSize - ch) * 0.5f + 0.6f * inScale), closeLabel, closeText, 0.92f * inScale, panelAlpha, true, 0, drawList);
        ImGui::SetCursorScreenPos(ImVec2(closeRect.x, closeRect.y));
        ImGui::InvisibleButton("##irc_close", ImVec2(closeRect.z - closeRect.x, closeRect.w - closeRect.y));
        if (ImGui::IsItemClicked(0) && isEnabledRaw)
        {
            isIrcPanelOpen = false;
            ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
        }

        std::string actionLabel;
        if (!ircEnabled) actionLabel = "Enable";
        else if (state == ConnectionState::Connected) actionLabel = "Disconnect";
        else actionLabel = "Reconnect";

        float actionFont = 0.82f * inScale;
        float actionPadX = 12.0f * inScale;
        float actionH = 30.0f * inScale;
        float actionW = ImRenderUtils::getTextWidth(&actionLabel, actionFont) + actionPadX * 2.0f;
        ImVec4 actionRect = ImVec4(closeRect.x - 10.0f * inScale - actionW, headerRect.y + (headerH - actionH) * 0.5f, closeRect.x - 10.0f * inScale, headerRect.y + (headerH + actionH) * 0.5f);
        bool actionHovered = isMouseOver(actionRect) && isEnabledRaw;
        ImColor actionBg = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)((actionHovered ? 70.0f : 55.0f) * panelAlpha));
        drawList->AddRectFilled(ImVec2(actionRect.x, actionRect.y), ImVec2(actionRect.z, actionRect.w), actionBg, actionH * 0.5f);
        ImRenderUtils::drawText(ImVec2(actionRect.x + actionPadX, actionRect.y + (actionH - ImRenderUtils::getTextHeightStr(&actionLabel, actionFont)) * 0.5f + 0.6f * inScale), actionLabel, textMain, actionFont, panelAlpha, true, 0, drawList);
        ImGui::SetCursorScreenPos(ImVec2(actionRect.x, actionRect.y));
        ImGui::InvisibleButton("##irc_action", ImVec2(actionRect.z - actionRect.x, actionRect.w - actionRect.y));
        if (ImGui::IsItemClicked(0) && isEnabledRaw && ircModule)
        {
            if (!ircEnabled) ircModule->setEnabled(true);
            else if (state == ConnectionState::Connected) ircModule->setEnabled(false);
            else IrcManager::init();
            ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
        }

        float listTop = headerRect.w + 2.0f * inScale;
        float listBottom = footerRect.y - 10.0f * inScale;
        float listH = std::max(40.0f * inScale, listBottom - listTop);
        float dmTop = panelRect.y + innerPad;
        float dmH = std::max(40.0f * inScale, listBottom - dmTop);

        static size_t lastCount = 0;
        static std::string lastDmKey;
        static float ircScrollY = 0.0f;
        static float ircScrollTargetY = 0.0f;

        if (lastDmKey != ircDmTargetKey)
        {
            lastDmKey = ircDmTargetKey;
            lastCount = 0;
            ircAutoScroll = true;
            ircScrollTargetY = 1e9f;
            ircScrollY = 1e9f;
        }

        FontHelper::pushPrefFont();
        ImGui::SetCursorScreenPos(ImVec2(panelRect.x + innerPad + dmW + dmGapX, listTop));
        ImGui::BeginChild("##irc_msgs", ImVec2(listW, listH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        std::vector<IrcClient::ChatMessage> history;
        if (IrcManager::mClient) history = IrcManager::mClient->getChatHistorySnapshot(400);

        std::vector<IrcClient::ChatMessage> drawHistory;
        drawHistory.reserve(history.size());
        if (ircDmTargetKey.empty())
        {
            for (const auto& m : history)
            {
                if (!m.isDirect) drawHistory.push_back(m);
            }
        }
        else
        {
            for (const auto& m : history)
            {
                if (!m.isDirect) continue;
                if (StringUtils::equalsIgnoreCase(m.directPeer, ircDmTargetKey)) drawHistory.push_back(m);
            }
        }

        ImVec2 clipMin = ImGui::GetWindowPos();
        ImVec2 clipMax = ImVec2(clipMin.x + ImGui::GetWindowSize().x, clipMin.y + ImGui::GetWindowSize().y);
        drawList->PushClipRect(clipMin, clipMax, true);

        ImVec4 txt = textMain.Value;
        txt.w = std::clamp(txt.w * panelAlpha, 0.0f, 1.0f);
        ImVec4 dim = textDim.Value;
        dim.w = std::clamp(dim.w * panelAlpha, 0.0f, 1.0f);

        const float gapY = 10.0f * inScale;
        const float padX = 12.0f * inScale;
        const float padY = 9.0f * inScale;
        const float bubbleMinW = 50.0f * inScale;
        const float bubbleMaxW = std::max(160.0f * inScale, listW * 0.78f);
        const float authorFontScale = 0.75f;
        const float textFontScale = 1.08f;
        const float bubbleR = 12.0f * inScale;
        const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        auto formatTime = [&](uint64_t epochMs) -> std::string
        {
            std::time_t tt = static_cast<std::time_t>(epochMs / 1000ull);
            std::tm tm{};
            localtime_s(&tm, &tt);
            char buf[6] = {};
            std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
            return std::string(buf);
        };

        struct AvatarEntry
        {
            std::string b64;
            ID3D11ShaderResourceView* srv = nullptr;
            int w = 0;
            int h = 0;
        };
        static std::unordered_map<std::string, AvatarEntry> avatarCache;

        const float avatarD = 30.0f * inScale;
        const float msgAvatarR = avatarD * 0.5f;
        const float avatarGap = 10.0f * inScale;

        auto wrapUtf8MaxChars = [](const std::string& s, size_t maxChars) -> std::string
        {
            if (maxChars == 0 || s.empty()) return s;

            std::string out;
            out.reserve(s.size() + (s.size() / maxChars));

            size_t lineLen = 0;
            size_t lastBreakPos = std::string::npos;
            size_t lineLenAtLastBreak = 0;

            for (size_t i = 0; i < s.size();)
            {
                unsigned char c = static_cast<unsigned char>(s[i]);
                size_t len = 1;
                if (c < 0x80) len = 1;
                else if ((c & 0xE0) == 0xC0) len = 2;
                else if ((c & 0xF0) == 0xE0) len = 3;
                else if ((c & 0xF8) == 0xF0) len = 4;
                if (i + len > s.size()) len = 1;

                if (len == 1 && s[i] == '\n')
                {
                    out.push_back('\n');
                    i += 1;
                    lineLen = 0;
                    lastBreakPos = std::string::npos;
                    lineLenAtLastBreak = 0;
                    continue;
                }

                size_t cpStart = out.size();
                if (len == 1 && s[i] == '\t')
                {
                    out.push_back(' ');
                    lineLen += 1;
                    lastBreakPos = out.size() - 1;
                    lineLenAtLastBreak = lineLen;
                    i += 1;
                }
                else
                {
                    out.append(s.data() + i, len);
                    i += len;
                    lineLen += 1;
                    if (len == 1 && s[i - 1] == ' ')
                    {
                        lastBreakPos = out.size() - 1;
                        lineLenAtLastBreak = lineLen;
                    }
                }

                if (lineLen > maxChars)
                {
                    if (lastBreakPos != std::string::npos)
                    {
                        out[lastBreakPos] = '\n';
                        lineLen = lineLen - lineLenAtLastBreak;
                        lastBreakPos = std::string::npos;
                        lineLenAtLastBreak = 0;
                    }
                    else
                    {
                        out.insert(cpStart, "\n");
                        lineLen = 1;
                    }
                }
            }

            return out;
        };

        static ID3D11ShaderResourceView* ircOwnerNotchTexture = nullptr;
        static int ircOwnerNotchW = 0;
        static int ircOwnerNotchH = 0;
        static bool ircOwnerNotchLoaded = false;
        if (!ircOwnerNotchLoaded)
        {
            D3DHook::loadTextureFromEmbeddedResource("notch.png", &ircOwnerNotchTexture, &ircOwnerNotchW, &ircOwnerNotchH);
            ircOwnerNotchLoaded = true;
        }

        for (size_t i = 0; i < drawHistory.size(); i++)
        {
            const auto& msg = drawHistory[i];
            const auto* prev = (i > 0) ? &drawHistory[i - 1] : nullptr;
            const auto* next = (i + 1 < drawHistory.size()) ? &drawHistory[i + 1] : nullptr;

            const bool isSystem = msg.kind == IrcClient::ChatMessageKind::System;
            const bool isSelf = msg.isSelf;

            std::string author = msg.author;
            std::string text = msg.text;
            if (text.empty()) continue;
            text = wrapUtf8MaxChars(text, 32);

            auto sameGroup = [&](const IrcClient::ChatMessage* a, const IrcClient::ChatMessage& b) -> bool
            {
                if (!a) return false;
                if (a->kind != IrcClient::ChatMessageKind::UserMessage || b.kind != IrcClient::ChatMessageKind::UserMessage) return false;
                if (a->isSelf != b.isSelf) return false;
                if (a->author != b.author) return false;
                uint64_t dt = (a->timeMs > b.timeMs) ? (a->timeMs - b.timeMs) : (b.timeMs - a->timeMs);
                return dt <= 120000ull;
            };

            const bool prevSame = sameGroup(prev, msg);
            const bool nextSame = sameGroup(next, msg);
            const bool showAuthor = (!isSystem && !isSelf && !author.empty() && !prevSame);
            const bool drawTail = (!isSystem && !nextSame);
            const bool isOwner = (!isSystem && msg.isOwner);

            float lifeMs = static_cast<float>(nowMs - msg.timeMs);
            float life = std::clamp(lifeMs / 220.0f, 0.0f, 1.0f);
            float easedAppear = life * life * (3.0f - 2.0f * life);
            float msgAlpha = panelAlpha;

            std::string timeStr = formatTime(msg.timeMs);
            float timeFont = 0.74f * inScale;
            float timeW = ImRenderUtils::getTextWidth(&timeStr, timeFont);
            float timeH = ImRenderUtils::getTextHeightStr(&timeStr, timeFont);
            const float selfMetaAvatarD = 22.0f * inScale;
            const float selfMetaGapX = 6.0f * inScale;
            const float selfMetaGapY = 4.0f * inScale;
            std::string selfMetaName = "you";
            float selfMetaNameW = 0.0f;
            float selfMetaNameH = 0.0f;
            if (isSelf && !isSystem)
            {
                selfMetaNameW = ImRenderUtils::getTextWidth(&selfMetaName, timeFont);
                selfMetaNameH = ImRenderUtils::getTextHeightStr(&selfMetaName, timeFont);
            }
            const float selfMetaRowH = (isSelf && !isSystem) ? std::max(std::max(timeH, selfMetaNameH), selfMetaAvatarD) : 0.0f;

            float wrapW = bubbleMaxW - padX * 2.0f;
            float authorH = 0.0f;
            float authorW = 0.0f;
            float authorTextW = 0.0f;
            float authorExtraW = 0.0f;
            if (showAuthor)
            {
                authorTextW = ImGui::CalcTextSize(author.c_str()).x * authorFontScale;
                authorW = authorTextW;
                authorH = ImGui::GetTextLineHeight() * authorFontScale + 2.0f * inScale;
                if (isOwner)
                {
                    const float gapX = 6.0f * inScale;
                    const float iconD = std::clamp(ImGui::GetTextLineHeight() * authorFontScale * 0.92f, 12.0f * inScale, 18.0f * inScale);
                    const std::string ownerLabel = "Owner";
                    const float ownerTextW = ImGui::CalcTextSize(ownerLabel.c_str()).x * authorFontScale;
                    const float pillPadX = 6.0f * inScale;
                    const float pillW = ownerTextW + pillPadX * 2.0f;
                    authorExtraW = gapX + iconD + gapX + pillW;
                    authorW += authorExtraW;
                }
            }

            ImGui::SetWindowFontScale(textFontScale);
            ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapW);
            ImGui::SetWindowFontScale(1.0f);
            float bubbleW = std::clamp(textSize.x + padX * 2.0f, bubbleMinW, bubbleMaxW);
            if (!isSelf) bubbleW = std::max(bubbleW, std::clamp(timeW + padX * 2.0f, bubbleMinW, bubbleMaxW));
            else if (!isSystem)
            {
                float metaW = selfMetaNameW + selfMetaGapX + timeW + selfMetaGapX + selfMetaAvatarD;
                if (isOwner)
                {
                    std::string ownerLabel = "Owner";
                    const float iconD = std::clamp(selfMetaAvatarD * 0.70f, 12.0f * inScale, 16.0f * inScale);
                    const float ownerTextW = ImRenderUtils::getTextWidth(&ownerLabel, timeFont);
                    const float pillPadX = 6.0f * inScale;
                    const float pillW = ownerTextW + pillPadX * 2.0f;
                    metaW += (iconD + selfMetaGapX + pillW + selfMetaGapX);
                }
                bubbleW = std::max(bubbleW, std::clamp(metaW + padX * 2.0f, bubbleMinW, bubbleMaxW));
            }
            if (showAuthor)
            {
                bubbleW = std::max(bubbleW, std::clamp(authorW + padX * 2.0f, bubbleMinW, bubbleMaxW));
            }
            float bubbleH = authorH + textSize.y + padY * 2.0f;
            if (!isSelf) bubbleH += timeH + 3.0f * inScale;

            ImVec2 cur = ImGui::GetCursorScreenPos();
            float startX = cur.x;
            float startY = cur.y;
            float x = 0.0f;
            if (isSystem) x = (listW - bubbleW) * 0.5f;
            else if (isSelf) x = listW - bubbleW;
            else x = avatarD + avatarGap;

            float slideY = (1.0f - easedAppear) * 10.0f * inScale;
            float stackGapY = prevSame ? (5.0f * inScale) : gapY;

            const float selfMetaOffY = (isSelf && !isSystem) ? (selfMetaRowH + selfMetaGapY) : 0.0f;
            ImVec2 bubbleMin = ImVec2(startX + x, startY + slideY + selfMetaOffY);
            ImVec2 bubbleMax = ImVec2(bubbleMin.x + bubbleW, bubbleMin.y + bubbleH);

            if (!isSystem && !isSelf && drawTail)
            {
                ImVec2 avC = ImVec2(startX + msgAvatarR, bubbleMax.y - msgAvatarR);
                ImVec2 avMin = ImVec2(avC.x - msgAvatarR, avC.y - msgAvatarR);
                ImVec2 avMax = ImVec2(avC.x + msgAvatarR, avC.y + msgAvatarR);

                std::string avatarKey = msg.senderKey.empty() ? author : msg.senderKey;
                std::string avatarB64;
                if (IrcManager::mClient) avatarB64 = IrcManager::mClient->getUserAvatarB64(avatarKey);

                auto& entry = avatarCache[avatarKey];
                if (avatarB64.empty())
                {
                    entry.b64.clear();
                    entry.w = 0;
                    entry.h = 0;
                    if (entry.srv)
                    {
                        entry.srv->Release();
                        entry.srv = nullptr;
                    }
                }
                else if (avatarB64 != entry.b64)
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

                ImColor avBg = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(255.0f * msgAlpha)) : ImColor(30, 30, 38, (int)(255.0f * msgAlpha));
                ImColor avBorder = ImColor(255, 255, 255, (int)(22.0f * msgAlpha));
                drawList->AddCircleFilled(avC, msgAvatarR, avBg, 32);

                if (entry.srv)
                {
                    drawList->AddImageRounded((ImTextureID)entry.srv, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, (int)(255.0f * msgAlpha)), msgAvatarR);
                }
                else
                {
                    std::string initials = author.empty() ? "?" : std::string(1, (char)std::toupper(author[0]));
                    float f = 0.78f * inScale;
                    float iw = ImRenderUtils::getTextWidth(&initials, f);
                    float ih = ImRenderUtils::getTextHeightStr(&initials, f);
                    ImColor ic = textMain;
                    ic.Value.w = std::clamp(ic.Value.w * msgAlpha, 0.0f, 1.0f);
                    ImRenderUtils::drawText(ImVec2(avC.x - iw * 0.5f, avC.y - ih * 0.5f + 0.6f * inScale), initials, ic, f, msgAlpha, true, 0, drawList);
                }

                drawList->AddCircle(avC, msgAvatarR, avBorder, 32, 1.2f);

                if (isOwner)
                {
                    std::string ownerLabel = "Owner";
                    const float gapX = 6.0f * inScale;
                    const float iconD = std::clamp(msgAvatarR * 0.95f, 12.0f * inScale, 18.0f * inScale);
                    const float y = avC.y - iconD * 0.5f;
                    float bx = avMax.x + gapX;

                    if (ircOwnerNotchTexture && ircOwnerNotchW > 0 && ircOwnerNotchH > 0)
                    {
                        ImVec2 iconMin = ImVec2(bx, y);
                        ImVec2 iconMax = ImVec2(bx + iconD, y + iconD);
                        ImColor tint = ImColor(255, 255, 255, (int)(235.0f * msgAlpha));
                        drawList->AddImageRounded((ImTextureID)ircOwnerNotchTexture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tint, 4.0f * inScale);
                    }
                    bx += iconD + gapX;

                    const float ownerScale = 0.72f * inScale;
                    const float ownerTextW = ImRenderUtils::getTextWidth(&ownerLabel, ownerScale);
                    const float ownerTextH = ImRenderUtils::getTextHeightStr(&ownerLabel, ownerScale);
                    const float pillPadX = 6.0f * inScale;
                    const float pillW = ownerTextW + pillPadX * 2.0f;
                    const float pillH = iconD;
                    ImVec2 pillMin = ImVec2(bx, y);
                    ImVec2 pillMax = ImVec2(bx + pillW, y + pillH);

                    const float time = (float)ImGui::GetTime();
                    const float pulse = 0.5f + 0.5f * std::sin(time * 4.0f);
                    const int glowA = (int)((70.0f + 85.0f * pulse) * msgAlpha);
                    const int fillA = (int)((95.0f + 90.0f * pulse) * msgAlpha);
                    ImColor fill = ImColor(255, 0, 0, fillA);
                    ImColor border = ImColor(255, 90, 90, (int)(160.0f * msgAlpha));
                    ImColor glow = ImColor(255, 0, 0, glowA);
                    drawList->AddShadowRect(pillMin, pillMax, glow, 28.0f * inScale, ImVec2(0.f, 0.f), 0, pillH * 0.5f);
                    drawList->AddRectFilled(pillMin, pillMax, fill, pillH * 0.5f);
                    drawList->AddRect(pillMin, pillMax, border, pillH * 0.5f, 0, 1.1f);

                    ImColor ownerCol = ImColor(255, 255, 255, (int)(255.0f * msgAlpha));
                    ImRenderUtils::drawText(ImVec2(bx + pillPadX, y + (pillH - ownerTextH) * 0.5f + 0.6f * inScale), ownerLabel, ownerCol, ownerScale, msgAlpha, true, 0, drawList);
                }
            }

            ImColor inBg = (uiTheme == 1) ? ImColor(238, 238, 246, (int)(255.0f * msgAlpha)) : ImColor(30, 30, 38, (int)(255.0f * msgAlpha));
            ImColor outBg = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(255.0f * msgAlpha));
            ImColor sysBg = (uiTheme == 1) ? ImColor(230, 230, 238, (int)(255.0f * msgAlpha)) : ImColor(22, 22, 28, (int)(255.0f * msgAlpha));

            ImColor txtCol = ImColor(txt);
            txtCol.Value.w = std::clamp(txtCol.Value.w * msgAlpha, 0.0f, 1.0f);
            ImColor dimCol = ImColor(dim);
            dimCol.Value.w = std::clamp(dimCol.Value.w * msgAlpha, 0.0f, 1.0f);

            ImColor bg = isSystem ? sysBg : (isSelf ? outBg : inBg);

            ImDrawFlags round = ImDrawFlags_RoundCornersAll;
            if (!isSystem)
            {
                if (isSelf)
                {
                    round = ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft | ImDrawFlags_RoundCornersBottomRight;
                }
                else
                {
                    round = ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomLeft | ImDrawFlags_RoundCornersBottomRight;
                }
            }
            drawList->AddRectFilled(ImVec2(bubbleMin.x, bubbleMin.y), ImVec2(bubbleMax.x, bubbleMax.y), bg, bubbleR, round);

            if (drawTail)
            {
                float tailW = 8.0f * inScale;
                float tailH = 10.0f * inScale;
                float overlap = 2.2f * inScale;

                float yMid = bubbleMax.y - std::max(bubbleR * 0.75f, tailH * 0.85f);
                auto snapY = [&](ImVec2 p) -> ImVec2
                {
                    return ImVec2(p.x, std::floor(p.y + 0.5f));
                };

                if (isSelf)
                {
                    float baseX = bubbleMax.x - overlap;
                    ImVec2 p1 = snapY(ImVec2(baseX, yMid - tailH * 0.5f));
                    ImVec2 p2 = snapY(ImVec2(baseX + tailW, yMid));
                    ImVec2 p3 = snapY(ImVec2(baseX, yMid + tailH * 0.5f));
                    drawList->AddTriangleFilled(p1, p2, p3, bg);
                }
                else
                {
                    float baseX = bubbleMin.x + overlap;
                    ImVec2 p1 = snapY(ImVec2(baseX, yMid - tailH * 0.5f));
                    ImVec2 p2 = snapY(ImVec2(baseX - tailW, yMid));
                    ImVec2 p3 = snapY(ImVec2(baseX, yMid + tailH * 0.5f));
                    drawList->AddTriangleFilled(p1, p2, p3, bg);
                }
            }

            ImVec2 textPos = ImVec2(bubbleMin.x + padX, bubbleMin.y + padY);
            if (showAuthor)
            {
                ImColor authorCol = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(255.0f * msgAlpha));
                ImRenderUtils::drawText(textPos, author, authorCol, authorFontScale * inScale, msgAlpha, true, 0, drawList);
                if (isOwner)
                {
                    const float gapX = 6.0f * inScale;
                    const float iconD = std::clamp(ImGui::GetTextLineHeight() * authorFontScale * 0.92f, 12.0f * inScale, 18.0f * inScale);
                    const float iconY = textPos.y + (authorH - iconD) * 0.5f;
                    float x = textPos.x + authorTextW + gapX;

                    if (ircOwnerNotchTexture && ircOwnerNotchW > 0 && ircOwnerNotchH > 0)
                    {
                        ImVec2 iconMin = ImVec2(x, iconY);
                        ImVec2 iconMax = ImVec2(x + iconD, iconY + iconD);
                        ImColor tint = ImColor(255, 255, 255, (int)(235.0f * msgAlpha));
                        drawList->AddImageRounded((ImTextureID)ircOwnerNotchTexture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tint, 4.0f * inScale);
                    }
                    x += iconD + gapX;

                    const std::string ownerLabel = "Owner";
                    const float ownerTextW = ImGui::CalcTextSize(ownerLabel.c_str()).x * authorFontScale;
                    const float pillPadX = 6.0f * inScale;
                    const float pillW = ownerTextW + pillPadX * 2.0f;
                    const float pillH = iconD;
                    const float pillY = textPos.y + (authorH - pillH) * 0.5f;
                    ImVec2 pillMin = ImVec2(x, pillY);
                    ImVec2 pillMax = ImVec2(x + pillW, pillY + pillH);

                    const float time = (float)ImGui::GetTime();
                    const float pulse = 0.5f + 0.5f * std::sin(time * 4.0f);
                    const int glowA = (int)((70.0f + 85.0f * pulse) * msgAlpha);
                    const int fillA = (int)((95.0f + 90.0f * pulse) * msgAlpha);
                    ImColor fill = ImColor(255, 0, 0, fillA);
                    ImColor border = ImColor(255, 90, 90, (int)(160.0f * msgAlpha));
                    ImColor glow = ImColor(255, 0, 0, glowA);
                    drawList->AddShadowRect(pillMin, pillMax, glow, 28.0f * inScale, ImVec2(0.f, 0.f), 0, pillH * 0.5f);
                    drawList->AddRectFilled(pillMin, pillMax, fill, pillH * 0.5f);
                    drawList->AddRect(pillMin, pillMax, border, pillH * 0.5f, 0, 1.1f);

                    ImColor ownerCol = ImColor(255, 255, 255, (int)(255.0f * msgAlpha));
                    const float ownerH = ImGui::GetTextLineHeight() * authorFontScale;
                    const float ownerY = pillY + (pillH - ownerH) * 0.5f;
                    ImRenderUtils::drawText(ImVec2(x + pillPadX, ownerY), ownerLabel, ownerCol, authorFontScale * inScale, msgAlpha, true, 0, drawList);
                }
                textPos.y += authorH;
            }

            if (isSelf && !isSystem)
            {
                ImColor youCol = textMain;
                ImColor timeCol = textDim;
                youCol.Value.w = std::clamp(panelAlpha, 0.0f, 1.0f);
                timeCol.Value.w = std::clamp(panelAlpha, 0.0f, 1.0f);
                ImColor avatarTint = ImColor(255, 255, 255, (int)(255.0f * msgAlpha));

                const float metaBaseY = bubbleMin.y - selfMetaOffY;
                const float metaY = metaBaseY;
                const float avatarY = metaY + (selfMetaRowH - selfMetaAvatarD) * 0.5f;
                const float timeY = metaY + (selfMetaRowH - timeH) * 0.5f;
                const float nameY = metaY + (selfMetaRowH - selfMetaNameH) * 0.5f;

                float iconD = 0.0f;
                float pillW = 0.0f;
                float badgeW = 0.0f;
                std::string ownerLabel = "Owner";
                float ownerTextH = 0.0f;
                if (isOwner)
                {
                    iconD = std::clamp(selfMetaAvatarD * 0.70f, 12.0f * inScale, 16.0f * inScale);
                    const float ownerTextW = ImRenderUtils::getTextWidth(&ownerLabel, timeFont);
                    ownerTextH = ImRenderUtils::getTextHeightStr(&ownerLabel, timeFont);
                    const float pillPadX = 6.0f * inScale;
                    pillW = ownerTextW + pillPadX * 2.0f;
                    badgeW = iconD + selfMetaGapX + pillW;
                }

                const float metaGroupW = selfMetaNameW + selfMetaGapX + timeW + selfMetaGapX + badgeW + (isOwner ? selfMetaGapX : 0.0f) + selfMetaAvatarD;
                const float metaGroupX = bubbleMin.x + padX;
                const float nameX = metaGroupX;
                const float timeX = nameX + selfMetaNameW + selfMetaGapX;
                float avatarX = timeX + timeW + selfMetaGapX;
                float badgeX = avatarX;
                if (isOwner) avatarX = badgeX + badgeW + selfMetaGapX;

                ImRenderUtils::drawText(ImVec2(nameX, nameY), selfMetaName, youCol, timeFont, msgAlpha, true, 0, drawList);
                ImRenderUtils::drawText(ImVec2(timeX, timeY), timeStr, timeCol, timeFont, msgAlpha, true, 0, drawList);

                if (isOwner)
                {
                    const float iconY = metaY + (selfMetaRowH - iconD) * 0.5f;
                    if (ircOwnerNotchTexture && ircOwnerNotchW > 0 && ircOwnerNotchH > 0)
                    {
                        ImVec2 iconMin = ImVec2(badgeX, iconY);
                        ImVec2 iconMax = ImVec2(badgeX + iconD, iconY + iconD);
                        ImColor tint = ImColor(255, 255, 255, (int)(235.0f * msgAlpha));
                        drawList->AddImageRounded((ImTextureID)ircOwnerNotchTexture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tint, 4.0f * inScale);
                    }

                    const float pillPadX = 6.0f * inScale;
                    const float pillH = iconD;
                    ImVec2 pillMin = ImVec2(badgeX + iconD + selfMetaGapX, iconY);
                    ImVec2 pillMax = ImVec2(pillMin.x + pillW, iconY + pillH);

                    const float time = (float)ImGui::GetTime();
                    const float pulse = 0.5f + 0.5f * std::sin(time * 4.0f);
                    const int glowA = (int)((70.0f + 85.0f * pulse) * msgAlpha);
                    const int fillA = (int)((95.0f + 90.0f * pulse) * msgAlpha);
                    ImColor fill = ImColor(255, 0, 0, fillA);
                    ImColor border = ImColor(255, 90, 90, (int)(160.0f * msgAlpha));
                    ImColor glow = ImColor(255, 0, 0, glowA);
                    drawList->AddShadowRect(pillMin, pillMax, glow, 28.0f * inScale, ImVec2(0.f, 0.f), 0, pillH * 0.5f);
                    drawList->AddRectFilled(pillMin, pillMax, fill, pillH * 0.5f);
                    drawList->AddRect(pillMin, pillMax, border, pillH * 0.5f, 0, 1.1f);

                    ImColor ownerCol = ImColor(255, 255, 255, (int)(255.0f * msgAlpha));
                    ImRenderUtils::drawText(ImVec2(pillMin.x + pillPadX, iconY + (pillH - ownerTextH) * 0.5f + 0.6f * inScale), ownerLabel, ownerCol, timeFont, msgAlpha, true, 0, drawList);
                }

                ImVec2 avMin = ImVec2(avatarX, avatarY);
                ImVec2 avMax = ImVec2(avatarX + selfMetaAvatarD, avatarY + selfMetaAvatarD);
                ImVec2 avC = ImVec2(avatarX + selfMetaAvatarD * 0.5f, avatarY + selfMetaAvatarD * 0.5f);
                float avR = selfMetaAvatarD * 0.5f;
                if (selfAvatarSrv) drawList->AddImageRounded((ImTextureID)selfAvatarSrv, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), avatarTint, avR);
                else
                {
                    std::string initials = "Y";
                    float f = 0.72f * inScale;
                    float iw = ImRenderUtils::getTextWidth(&initials, f);
                    float ih = ImRenderUtils::getTextHeightStr(&initials, f);
                    ImRenderUtils::drawText(ImVec2(avC.x - iw * 0.5f, avC.y - ih * 0.5f + 0.6f * inScale), initials, youCol, f, msgAlpha, true, 0, drawList);
                }
            }

            ImGui::SetCursorScreenPos(textPos);
            ImGui::PushTextWrapPos(textPos.x + wrapW);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(txtCol.Value.x, txtCol.Value.y, txtCol.Value.z, txtCol.Value.w));
            ImGui::SetWindowFontScale(textFontScale);
            ImGui::TextUnformatted(text.c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();

            if (!isSelf)
            {
                ImVec2 timePos = ImVec2(bubbleMax.x - padX - timeW, bubbleMax.y - padY - timeH);
                ImColor timeCol = isSystem ? dimCol : ImColor(100, 100, 100, (int)(255.0f * msgAlpha));
                ImRenderUtils::drawText(timePos, timeStr, timeCol, timeFont, msgAlpha, true, 0, drawList);
            }

            ImGui::SetCursorScreenPos(ImVec2(startX, startY + slideY + selfMetaOffY + bubbleH + stackGapY));
        }

        ImGui::Dummy(ImVec2(1.0f, gapY));

        float maxY = ImGui::GetScrollMaxY();
        if (maxY < 0.0f) maxY = 0.0f;

        if (drawHistory.size() != lastCount)
        {
            lastCount = drawHistory.size();
            if (ircAutoScroll)
            {
                ircScrollTargetY = maxY;
                ircScrollY = maxY;
            }
        }

        if (ImGui::IsWindowHovered())
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                float scrollStep = 220.0f * inScale;
                ircScrollTargetY = std::clamp(ircScrollTargetY - (wheel * scrollStep), 0.0f, maxY);
                ircAutoScroll = (ircScrollTargetY >= maxY - 2.0f);
            }
        }

        if (ircAutoScroll)
        {
            ircScrollTargetY = maxY;
            ircScrollY = maxY;
            ImGui::SetScrollY(maxY);
        }
        else
        {
            ircScrollTargetY = std::clamp(ircScrollTargetY, 0.0f, maxY);
            ircScrollY = std::clamp(ircScrollY, 0.0f, maxY);
            ircScrollY = MathUtils::animate(ircScrollTargetY, ircScrollY, uiDelta * 18.0f);
            ImGui::SetScrollY(ircScrollY);
        }

        drawList->PopClipRect();
        ImGui::EndChild();
        FontHelper::popPrefFont();

        ImGui::SetCursorScreenPos(ImVec2(panelRect.x, dmTop));
        ImGui::BeginChild("##irc_dm", ImVec2(dmW + innerPad + dmGapX, dmH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImDrawList* dmDl = ImGui::GetWindowDrawList();
            ImVec2 dmClipMin = ImGui::GetWindowPos();
            ImVec2 dmClipMax = ImVec2(dmClipMin.x + ImGui::GetWindowSize().x, dmClipMin.y + ImGui::GetWindowSize().y);
            dmDl->PushClipRect(dmClipMin, dmClipMax, true);

            const float itemH = 52.0f * inScale;
            const float itemPadX = 18.0f * inScale;
            const float dmAvatarD = 36.0f * inScale;
            const float dmAvatarR = dmAvatarD * 0.5f;
            const float dmAvatarGap = 10.0f * inScale;

            ImColor dmPanelBg = ColorUtils::getUiCardColor(panelAlpha);
            ImColor itemBgOff = ImColor(0, 0, 0, 0);
            ImColor itemBgHover = ImColor(0, 0, 0, 0);
            ImColor itemBgOn = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(75.0f * panelAlpha));
            ImColor itemSelBar = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(220.0f * panelAlpha));
            ImColor itemText = textMain;
            ImColor itemTextDim = textDim;
            itemText.Value.w = std::clamp(panelAlpha, 0.0f, 1.0f);
            itemTextDim.Value.w = std::clamp(panelAlpha, 0.0f, 1.0f);

            float dmPanelR = 12.0f * inScale;
            dmDl->AddRectFilled(dmClipMin, dmClipMax, dmPanelBg, dmPanelR, ImDrawFlags_RoundCornersLeft);
            ImGui::SetCursorScreenPos(dmClipMin);

            auto users = IrcManager::mClient ? IrcManager::mClient->getConnectedUsers() : std::vector<ConnectedIrcUser>{};

            auto resolveTargetKey = [&](const std::string& friendName, bool& outOnline) -> std::string
            {
                outOnline = false;
                for (const auto& u : users)
                {
                    if (StringUtils::equalsIgnoreCase(u.playerName, friendName) || StringUtils::equalsIgnoreCase(u.username, friendName))
                    {
                        outOnline = true;
                        return u.username;
                    }
                }
                return friendName;
            };

            static char dmQuery[64] = {};
            const float dmSearchH = 34.0f * inScale;
            const float dmSearchPadX = std::max(8.0f * inScale, itemPadX - 6.0f * inScale);
            ImVec2 searchMin = ImGui::GetCursorScreenPos();
            ImVec2 searchMax = ImVec2(dmClipMax.x, searchMin.y + dmSearchH);
            ImColor searchBg = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(200.0f * panelAlpha)) : ImColor(22, 22, 28, (int)(190.0f * panelAlpha));
            ImColor searchBorder = (uiTheme == 1) ? ImColor(206, 206, 218, (int)(140.0f * panelAlpha)) : ImColor(255, 255, 255, (int)(16.0f * panelAlpha));
            float searchR = dmSearchH * 0.35f;
            dmDl->AddRectFilled(ImVec2(dmClipMin.x + dmSearchPadX, searchMin.y), ImVec2(dmClipMax.x - dmSearchPadX, searchMax.y), searchBg, searchR);
            dmDl->AddRect(ImVec2(dmClipMin.x + dmSearchPadX, searchMin.y), ImVec2(dmClipMax.x - dmSearchPadX, searchMax.y), searchBorder, searchR, 0, 1.2f);

            static bool dmFindIconTried = false;
            static ID3D11ShaderResourceView* dmFindIconSrv = nullptr;
            static int dmFindIconW = 0;
            static int dmFindIconH = 0;
            if (!dmFindIconTried)
            {
                dmFindIconTried = true;
                D3DHook::loadTextureFromEmbeddedResource("find.png", &dmFindIconSrv, &dmFindIconW, &dmFindIconH);
            }

            const float findIconD = dmSearchH * 0.50f;
            const float findIconX = dmClipMin.x + dmSearchPadX + 10.0f * inScale;
            const float findIconY = searchMin.y + (dmSearchH - findIconD) * 0.5f;
            if (dmFindIconSrv)
            {
                dmDl->AddImage(
                    (ImTextureID)dmFindIconSrv,
                    ImVec2(findIconX, findIconY),
                    ImVec2(findIconX + findIconD, findIconY + findIconD),
                    ImVec2(0, 0),
                    ImVec2(1, 1),
                    (uiTheme == 1) ? ImColor(120, 120, 132, (int)(200.0f * panelAlpha)) : ImColor(170, 170, 170, (int)(185.0f * panelAlpha))
                );
            }

            ImGui::SetCursorScreenPos(ImVec2(findIconX + findIconD + 8.0f * inScale, searchMin.y + (dmSearchH - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::SetNextItemWidth(std::max(40.0f * inScale, (dmClipMax.x - dmClipMin.x) - (dmSearchPadX * 2.0f) - 20.0f * inScale - (findIconD + 8.0f * inScale)));
            bool dmEnter = ImGui::InputText("##irc_dm_query", dmQuery, IM_ARRAYSIZE(dmQuery), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            std::string dmQ = std::string(StringUtils::trim(std::string_view(dmQuery)));
            bool hasDmQ = !dmQ.empty();

            auto tryOpenDmByQuery = [&](const std::string& q) -> bool
            {
                if (q.empty()) return false;
                for (const auto& u : users)
                {
                    const std::string display = u.prefix.empty() ? u.username : (u.prefix + " " + u.username);
                    if (
                        StringUtils::equalsIgnoreCase(u.playerName, q) ||
                        StringUtils::equalsIgnoreCase(u.username, q) ||
                        StringUtils::equalsIgnoreCase(display, q)
                    )
                    {
                        ircDmTargetKey = u.username;
                        ircDmTargetLabel = u.playerName.empty() ? u.username : u.playerName;
                        ircAutoScroll = true;
                        ircScrollTargetY = 1e9f;
                        ircScrollY = 1e9f;
                        dmQuery[0] = 0;
                        return true;
                    }
                }
                ircDmTargetKey = q;
                ircDmTargetLabel = q;
                ircAutoScroll = true;
                ircScrollTargetY = 1e9f;
                ircScrollY = 1e9f;
                dmQuery[0] = 0;
                return true;
            };

            if (dmEnter && isEnabledRaw)
            {
                tryOpenDmByQuery(dmQ);
            }

            const float listTopY = searchMax.y + 14.0f * inScale;
            ImGui::SetCursorScreenPos(ImVec2(dmClipMin.x, listTopY));
            ImGui::BeginChild("##irc_dm_list", ImVec2(dmClipMax.x - dmClipMin.x, std::max(1.0f, dmClipMax.y - listTopY)), false);
            {
                ImDrawList* listDl = ImGui::GetWindowDrawList();
                ImVec2 listClipMin = ImGui::GetWindowPos();
                ImVec2 listClipMax = ImVec2(listClipMin.x + ImGui::GetWindowSize().x, listClipMin.y + ImGui::GetWindowSize().y);
                listDl->PushClipRect(listClipMin, listClipMax, true);

                ImGui::SetCursorScreenPos(listClipMin);

                auto addEntry = [&](const std::string& label, const std::string& targetKey, bool online) -> void
                {
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 p1 = ImVec2(listClipMax.x, p0.y + itemH);
                    const bool selected = (targetKey.empty() ? ircDmTargetKey.empty() : StringUtils::equalsIgnoreCase(ircDmTargetKey, targetKey));
                    const bool hovered = ImGui::IsMouseHoveringRect(p0, p1, false) && isEnabledRaw;

                    ImColor bg = selected ? itemBgOn : itemBgOff;
                    if (selected)
                    {
                        ImVec2 bgMin = ImVec2(listClipMin.x, p0.y);
                        ImVec2 bgMax = ImVec2(listClipMax.x, p1.y);
                        listDl->AddRectFilled(bgMin, bgMax, bg, 0.0f);
                    }

                    ImColor tcol = online ? itemText : itemTextDim;
                    float textX = p0.x + itemPadX;

                    ImVec2 avC = ImVec2(textX + dmAvatarR, p0.y + itemH * 0.5f);
                    if (targetKey.empty()) avC = ImVec2(avC.x + 2.0f * inScale, avC.y - 1.0f * inScale);
                    ImVec2 avMin = ImVec2(avC.x - dmAvatarR, avC.y - dmAvatarR);
                    ImVec2 avMax = ImVec2(avC.x + dmAvatarR, avC.y + dmAvatarR);

                    std::string avatarB64;
                    if (!targetKey.empty() && IrcManager::mClient) avatarB64 = IrcManager::mClient->getUserAvatarB64(targetKey);

                    auto& entry = avatarCache[targetKey];
                    if (!targetKey.empty() && avatarB64.empty())
                    {
                        entry.b64.clear();
                        entry.w = 0;
                        entry.h = 0;
                        if (entry.srv)
                        {
                            entry.srv->Release();
                            entry.srv = nullptr;
                        }
                    }
                    else if (!targetKey.empty() && avatarB64 != entry.b64)
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

                    ImColor avBorder = ImColor(255, 255, 255, (int)(22.0f * panelAlpha));
                    if (targetKey.empty())
                    {
                        ImColor avatarBg = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(120.0f * panelAlpha));
                        listDl->AddCircleFilled(avC, dmAvatarR, avatarBg, 32);
                        listDl->AddCircle(avC, dmAvatarR, ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(200.0f * panelAlpha)), 32, 1.2f);

                        static ImFont* globalIconFont = nullptr;
                        if (!globalIconFont)
                        {
                            auto it = FontHelper::Fonts.find("essence.ttf");
                            if (it != FontHelper::Fonts.end() && it->second) globalIconFont = it->second;
                        }
                        std::string icon = "j";
                        float iconFont = 0.98f * inScale;
                        if (globalIconFont)
                        {
                            ImGui::PushFont(globalIconFont);
                            float iw = ImRenderUtils::getTextWidth(&icon, iconFont);
                            float ih = ImRenderUtils::getTextHeightStr(&icon, iconFont);
                            ImRenderUtils::drawText(ImVec2(avC.x - iw * 0.5f, avC.y - ih * 0.5f), icon, textMain, iconFont, panelAlpha, true, 0, listDl);
                            ImGui::PopFont();
                        }
                        else
                        {
                            std::string avatarLetter = "G";
                            float f = 0.86f * inScale;
                            float iw = ImRenderUtils::getTextWidth(&avatarLetter, f);
                            float ih = ImRenderUtils::getTextHeightStr(&avatarLetter, f);
                            ImRenderUtils::drawText(ImVec2(avC.x - iw * 0.5f, avC.y - ih * 0.5f), avatarLetter, textMain, f, panelAlpha, true, 0, listDl);
                        }
                    }
                    else
                    {
                        ImColor avBg = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(255.0f * panelAlpha)) : ImColor(30, 30, 38, (int)(255.0f * panelAlpha));
                        listDl->AddCircleFilled(avC, dmAvatarR, avBg, 32);
                        if (entry.srv)
                        {
                            listDl->AddImageRounded((ImTextureID)entry.srv, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, (int)(255.0f * panelAlpha)), dmAvatarR);
                        }
                        else
                        {
                            std::string initials = label.empty() ? "?" : std::string(1, (char)std::toupper(label[0]));
                            float f = 0.78f * inScale;
                            float iw = ImRenderUtils::getTextWidth(&initials, f);
                            float ih = ImRenderUtils::getTextHeightStr(&initials, f);
                            ImColor ic = textMain;
                            ic.Value.w = std::clamp(ic.Value.w * panelAlpha, 0.0f, 1.0f);
                            ImRenderUtils::drawText(ImVec2(avC.x - iw * 0.5f, avC.y - ih * 0.5f + 0.6f * inScale), initials, ic, f, panelAlpha, true, 0, listDl);
                        }
                        listDl->AddCircle(avC, dmAvatarR, avBorder, 32, 1.1f);
                    }

                    if (!targetKey.empty() && online)
                    {
                        ImVec2 dotC = ImVec2(avMax.x - 2.0f * inScale, avMax.y - 2.0f * inScale);
                        float dotR = 3.2f * inScale;
                        listDl->AddCircleFilled(dotC, dotR, ImColor(70, 220, 120, (int)(235.0f * panelAlpha)), 16);
                        listDl->AddCircle(dotC, dotR, ImColor(0, 0, 0, (int)(130.0f * panelAlpha)), 16, 1.0f);
                    }

                    textX = avMax.x + dmAvatarGap;

                    float nameFont = 0.82f * inScale;
                    float subFont = 0.72f * inScale;
                    float nameY = p0.y + 9.0f * inScale;
                    float subY = p0.y + 30.0f * inScale;

                    ImRenderUtils::drawText(ImVec2(textX, nameY), label, itemText, nameFont, panelAlpha, true, 0, listDl);

                    std::string sub = targetKey.empty() ? std::string("Глобальный чат") : (online ? std::string("в сети") : std::string("не в сети"));
                    ImRenderUtils::drawText(ImVec2(textX, subY), sub, itemTextDim, subFont, panelAlpha, true, 0, listDl);

                    ImGui::SetCursorScreenPos(p0);
                    ImGui::InvisibleButton(("##irc_dm_" + label).c_str(), ImVec2(p1.x - p0.x, itemH));
                    if (ImGui::IsItemClicked(0))
                    {
                        ircDmTargetKey = targetKey;
                        ircDmTargetLabel = label;
                        ircAutoScroll = true;
                        ircScrollTargetY = 1e9f;
                        ircScrollY = 1e9f;
                    }
                };

                addEntry("Общий чат", "", false);

                if (hasDmQ)
                {
                    int shown = 0;
                    for (const auto& u : users)
                    {
                        const std::string display = u.prefix.empty() ? u.username : (u.prefix + " " + u.username);
                        if (
                            StringUtils::containsIgnoreCase(u.playerName, dmQ) ||
                            StringUtils::containsIgnoreCase(u.username, dmQ) ||
                            StringUtils::containsIgnoreCase(display, dmQ)
                        )
                        {
                            std::string label = u.playerName.empty() ? u.username : u.playerName;
                            addEntry(label, u.username, true);
                            shown++;
                            if (shown >= 30) break;
                        }
                    }

                    if (shown == 0)
                    {
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        ImRenderUtils::drawText(ImVec2(p0.x + itemPadX, p0.y + 6.0f * inScale), std::string("Не найдено нахуй"), itemTextDim, 0.80f * inScale, panelAlpha, true, 0, listDl);
                    }
                }
                else
                {
                    if (IrcManager::mClient)
                    {
                        auto history = IrcManager::mClient->getChatHistorySnapshot(250);
                        std::vector<std::string> recentPeers;
                        recentPeers.reserve(24);
                        for (auto it = history.rbegin(); it != history.rend(); ++it)
                        {
                            if (!it->isDirect) continue;
                            if (it->directPeer.empty()) continue;

                            bool exists = false;
                            for (const auto& p : recentPeers)
                            {
                                if (StringUtils::equalsIgnoreCase(p, it->directPeer))
                                {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists)
                            {
                                recentPeers.push_back(it->directPeer);
                                if (recentPeers.size() >= 12) break;
                            }
                        }

                        for (const auto& peer : recentPeers)
                        {
                            bool online = false;
                            std::string label = peer;
                            for (const auto& u : users)
                            {
                                if (StringUtils::equalsIgnoreCase(u.username, peer))
                                {
                                    online = true;
                                    if (!u.playerName.empty()) label = u.playerName;
                                    break;
                                }
                            }
                            addEntry(label, peer, online);
                        }
                    }

                    if (gFriendManager && !gFriendManager->mFriends.empty())
                    {
                        for (const auto& f : gFriendManager->mFriends)
                        {
                            bool online = false;
                            std::string targetKey = resolveTargetKey(f, online);
                            addEntry(f, targetKey, online);
                        }
                    }
                    else
                    {
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        ImRenderUtils::drawText(ImVec2(p0.x + itemPadX, p0.y + 6.0f * inScale), std::string("Нет друзей"), itemTextDim, 0.80f * inScale, panelAlpha, true, 0, listDl);
                    }
                }

                listDl->PopClipRect();
            }
            ImGui::EndChild();

            dmDl->PopClipRect();
        }
        ImGui::EndChild();

        float footerPad = 12.0f * inScale;
        float inputH = 42.0f * inScale;
        float inputY = footerRect.y + (footerH - inputH) * 0.5f;
        static float ircSendAnim = 0.0f;
        bool hasText = (StringUtils::trim(std::string(ircInput)).size() > 0);
        ircSendAnim = MathUtils::animate(hasText ? 1.0f : 0.0f, ircSendAnim, uiDelta * 14.0f);
        float sendT = std::clamp(ircSendAnim, 0.0f, 1.0f);

        float chatLeft = panelRect.x + innerPad + dmW + dmGapX;
        float chatRight = chatLeft + listW;
        float inputBoundL = chatLeft + footerPad;
        float inputBoundR = chatRight - footerPad;
        float inputAvailW = std::max(40.0f * inScale, inputBoundR - inputBoundL);
        float inputW = inputAvailW * 0.84f;
        float inputAvatarD = inputH * 0.74f;
        float inputAvatarGap = 10.0f * inScale;
        float groupW = inputAvatarD + inputAvatarGap + inputW;
        float groupX = inputBoundL + (inputAvailW - groupW) * 0.5f;
        groupX = std::clamp(groupX, inputBoundL, std::max(inputBoundL, inputBoundR - groupW));
        float avatarY = inputY + (inputH - inputAvatarD) * 0.5f;
        ImVec4 avatarRect = ImVec4(groupX, avatarY, groupX + inputAvatarD, avatarY + inputAvatarD);
        ImVec4 inputRect = ImVec4(avatarRect.z + inputAvatarGap, inputY, avatarRect.z + inputAvatarGap + inputW, inputY + inputH);
        float sendBtnInset = 6.0f * inScale;
        float sendBtnD = inputH - sendBtnInset * 2.0f;
        ImVec4 sendRect = ImVec4(inputRect.z - sendBtnInset - sendBtnD, inputRect.y + sendBtnInset, inputRect.z - sendBtnInset, inputRect.w - sendBtnInset);
        bool sendHovered = isMouseOver(sendRect) && isEnabledRaw;

        static float ircInputAnim = 0.0f;
        float inputTarget = (ircInput[0] != 0) ? 1.0f : 0.0f;
        ircInputAnim = MathUtils::animate(inputTarget, ircInputAnim, uiDelta * 10.0f);
        float inputGlowT = std::clamp(ircInputAnim, 0.0f, 1.0f);

        float inputR = inputH * 0.25f;
        ImColor inputBg = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(200.0f * panelAlpha)) : ImColor(22, 22, 28, (int)(185.0f * panelAlpha));
        drawList->AddRectFilled(ImVec2(inputRect.x, inputRect.y), ImVec2(inputRect.z, inputRect.w), inputBg, inputR);
        ImColor inputBorderOff = (uiTheme == 1) ? ImColor(206, 206, 218, (int)(140.0f * panelAlpha)) : ImColor(255, 255, 255, (int)(16.0f * panelAlpha));
        ImColor inputBorder = MathUtils::lerpImColor(inputBorderOff, accentColor, inputGlowT * 0.55f);
        drawList->AddRect(ImVec2(inputRect.x, inputRect.y), ImVec2(inputRect.z, inputRect.w), inputBorder, inputR, 0, 1.2f);

        {
            ImVec2 avMin = ImVec2(avatarRect.x, avatarRect.y);
            ImVec2 avMax = ImVec2(avatarRect.z, avatarRect.w);
            ImVec2 avC = ImVec2((avatarRect.x + avatarRect.z) * 0.5f, (avatarRect.y + avatarRect.w) * 0.5f);
            float avR = inputAvatarD * 0.5f;
            ImColor avBorder = ImColor(255, 255, 255, (int)(22.0f * panelAlpha));
            if (selfAvatarSrv) drawList->AddImageRounded((ImTextureID)selfAvatarSrv, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, (int)(255.0f * panelAlpha)), avR);
            else
            {
                std::string initials = "Y";
                float f = 0.82f * inScale;
                float iw = ImRenderUtils::getTextWidth(&initials, f);
                float ih = ImRenderUtils::getTextHeightStr(&initials, f);
                ImColor ic = textMain;
                ic.Value.w = std::clamp(ic.Value.w * panelAlpha, 0.0f, 1.0f);
                ImRenderUtils::drawText(ImVec2(avC.x - iw * 0.5f, avC.y - ih * 0.5f + 0.6f * inScale), initials, ic, f, panelAlpha, true, 0, drawList);
            }
            drawList->AddCircle(avC, avR, avBorder, 32, 1.2f);
        }

        float textPadL = 12.0f * inScale;
        float textPadR = 10.0f * inScale;
        float textMaxW = (inputRect.z - inputRect.x) - textPadL - textPadR - (sendBtnD + sendBtnInset);
        ImGui::SetCursorScreenPos(ImVec2(inputRect.x + textPadL, inputRect.y + (inputH - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::SetNextItemWidth(std::max(40.0f * inScale, textMaxW));
        bool sendNow = ImGui::InputText("##irc_input", ircInput, IM_ARRAYSIZE(ircInput), ImGuiInputTextFlags_EnterReturnsTrue);
        bool inputActive = ImGui::IsItemActive();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);

        float focusTarget = (inputActive || ircInput[0] != 0) ? 1.0f : 0.0f;
        ircInputAnim = MathUtils::animate(focusTarget, ircInputAnim, uiDelta * 14.0f);

        if (ircInput[0] == 0 && !inputActive)
        {
            std::string placeholder = ircDmTargetKey.empty()
                ? "Сообщение..."
                : ("лс: " + (ircDmTargetLabel.empty() ? ircDmTargetKey : ircDmTargetLabel));
            float phFont = 0.86f * inScale;
            ImColor phCol = ImColor(textDim);
            phCol.Value.w = std::clamp(phCol.Value.w * (0.55f * panelAlpha), 0.0f, 1.0f);
            ImVec2 phPos = ImVec2(inputRect.x + 14.0f * inScale, inputRect.y + (inputH - ImRenderUtils::getTextHeightStr(&placeholder, phFont)) * 0.5f + 0.8f * inScale);
            ImRenderUtils::drawText(phPos, placeholder, phCol, phFont, panelAlpha, true, 0, drawList);
        }

        bool sendClicked = false;
        {
            static bool sendIconTried = false;
            static ID3D11ShaderResourceView* sendIconSrv = nullptr;
            static int sendIconW = 0;
            static int sendIconH = 0;
            if (!sendIconTried)
            {
                sendIconTried = true;
                D3DHook::loadTextureFromEmbeddedResource("keybinds.png", &sendIconSrv, &sendIconW, &sendIconH);
            }

            float cx = (sendRect.x + sendRect.z) * 0.5f;
            float cy = (sendRect.y + sendRect.w) * 0.5f;
            float r = (sendBtnD * 0.5f) * (0.92f + 0.08f * sendT);
            float aMul = hasText ? (0.75f + 0.25f * sendT) : 0.35f;
            int sendA = (int)(165.0f * panelAlpha * aMul);
            ImColor sendBg = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), sendA);
            drawList->AddCircleFilled(ImVec2(cx, cy), r, sendBg, 32);
            drawList->AddShadowCircle(ImVec2(cx, cy), r * 0.70f, ImColor(0, 0, 0, (int)(90.0f * panelAlpha * aMul)), 25, ImVec2(0.f, 0.f), 0, 64);
            if (sendIconSrv)
            {
                float iconD = sendBtnD * 0.62f;
                ImVec2 iconMin = ImVec2(cx - iconD * 0.5f, cy - iconD * 0.5f);
                ImVec2 iconMax = ImVec2(iconMin.x + iconD, iconMin.y + iconD);
                ImColor tint = ImColor(255, 255, 255, (int)(255.0f * panelAlpha * aMul));
                drawList->AddImage((ImTextureID)sendIconSrv, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tint);
            }
            ImGui::SetCursorScreenPos(ImVec2(sendRect.x, sendRect.y));
            sendClicked = ImGui::InvisibleButton("##irc_send", ImVec2(sendRect.z - sendRect.x, sendRect.w - sendRect.y));
        }

        if ((sendNow || sendClicked) && isEnabledRaw)
        {
            std::string msg = std::string(ircInput);
            msg = StringUtils::trim(msg);
            if (!msg.empty())
            {
                if (ircDmTargetKey.empty()) IrcManager::sendMessage(msg);
                else IrcManager::sendDirectMessage(ircDmTargetKey, msg);
                ircInput[0] = 0;
                ircAutoScroll = true;
                ircScrollTargetY = 1e9f;
                ircScrollY = 1e9f;
            }
        }

        ImGui::End();
    }
    else if (animationsPanelAnim > 0.05f)
    {
        auto* animations = gFeatureManager->mModuleManager->getModule<Animations>();
        float t = std::clamp(animationsPanelAnim, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float panelAlpha = eased * categoryFade;
        int a = (int)(255.0f * std::clamp(panelAlpha, 0.0f, 1.0f));

        float outerPad = 8.0f * inScale;
        float panelGap = 14.0f * inScale;
        float rightW = contentW * 0.34f;
        ImVec4 leftRect = ImVec4(
            contentRect.x + outerPad,
            contentRect.y + outerPad + contentSlideY,
            contentRect.z - outerPad - rightW - panelGap,
            contentRect.w - outerPad + contentSlideY
        );
        ImVec4 rightRect = ImVec4(
            leftRect.z + panelGap,
            contentRect.y + outerPad + contentSlideY,
            contentRect.z - outerPad,
            contentRect.w - outerPad + contentSlideY
        );

        float openSlideX = (1.0f - eased) * 42.0f * inScale;
        leftRect.x += openSlideX;
        leftRect.z += openSlideX;
        rightRect.x += openSlideX;
        rightRect.z += openSlideX;

        auto alphaColor = [&](ImColor c, float aMul) -> ImColor
        {
            ImVec4 v = c.Value;
            v.w = std::clamp(v.w * aMul, 0.0f, 1.0f);
            return ImColor(v);
        };

        ImColor panelBg = ColorUtils::getUiCardColor(panelAlpha);
        ImColor panelBorder = ColorUtils::getUiBorderColor(panelAlpha * (25.0f / 255.0f));
        if (uiTheme == 1)
        {
            ImColor baseBg = ImColor(229, 229, 233, (int)(255.0f * std::clamp(panelAlpha, 0.0f, 1.0f)));
            panelBg = baseBg;
            panelBorder = outlineColor;
            panelBorder.Value.w = std::clamp(panelAlpha, 0.0f, 1.0f);
        }
        else
        {
            panelBg = ImColor(12, 12, 16, (int)(255.0f * std::clamp(panelAlpha, 0.0f, 1.0f)));
            panelBorder = ImColor(0, 0, 0, 0);
        }
        float radius = 14.0f * inScale;
        drawList->AddRectFilled(ImVec2(leftRect.x, leftRect.y), ImVec2(leftRect.z, leftRect.w), panelBg, radius);
        drawList->AddRect(ImVec2(leftRect.x, leftRect.y), ImVec2(leftRect.z, leftRect.w), panelBorder, radius, 0, 1.2f);
        drawList->AddRectFilled(ImVec2(rightRect.x, rightRect.y), ImVec2(rightRect.z, rightRect.w), panelBg, radius);
        drawList->AddRect(ImVec2(rightRect.x, rightRect.y), ImVec2(rightRect.z, rightRect.w), panelBorder, radius, 0, 1.2f);

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::SetNextWindowPos(ImVec2(contentRect.x, contentRect.y));
        ImGui::SetNextWindowSize(ImVec2(contentW, contentH));
        ImGui::Begin("##animations_panel", nullptr, winFlags);

        float innerPad = 16.0f * inScale;
        float titleFont = 0.92f * inScale;
        ImRenderUtils::drawText(ImVec2(rightRect.x + innerPad, rightRect.y + innerPad), "Animations", textMain, titleFont, panelAlpha, true, 0, drawList);

        float closeSize = 28.0f * inScale;
        ImVec4 closeRect = ImVec4(rightRect.z - innerPad - closeSize, rightRect.y + innerPad - 6.0f * inScale, rightRect.z - innerPad, rightRect.y + innerPad - 6.0f * inScale + closeSize);
        bool closeHovered = isMouseOver(closeRect) && isEnabledRaw;
        ImColor closeBorderOff = (uiTheme == 1) ? ImColor(206, 206, 218, a) : ImColor(44, 44, 54, a);
        ImColor closeBorder = closeHovered ? alphaColor(accentColor, panelAlpha) : closeBorderOff;
        drawList->AddRect(ImVec2(closeRect.x, closeRect.y), ImVec2(closeRect.z, closeRect.w), closeBorder, closeSize * 0.35f, 0, 1.4f);
        std::string closeLabel = "X";
        float cw = ImRenderUtils::getTextWidth(&closeLabel, 0.95f * inScale);
        float ch = ImRenderUtils::getTextHeightStr(&closeLabel, 0.95f * inScale);
        ImColor closeText = closeHovered ? MathUtils::lerpImColor(textDim, accentColor, 0.65f) : textDim;
        ImRenderUtils::drawText(ImVec2(closeRect.x + (closeSize - cw) * 0.5f, closeRect.y + (closeSize - ch) * 0.5f), closeLabel, closeText, 0.95f * inScale, panelAlpha, true, 0, drawList);
        ImGui::SetCursorScreenPos(ImVec2(closeRect.x, closeRect.y));
        ImGui::InvisibleButton("##animations_close", ImVec2(closeRect.z - closeRect.x, closeRect.w - closeRect.y));
        if (ImGui::IsItemClicked(0) && isEnabledRaw) {
            isAnimationsPanelOpen = false;
            ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
        }

        ImVec4 graphRect = ImVec4(
            leftRect.x + innerPad,
            leftRect.y + innerPad + 26.0f * inScale,
            leftRect.z - innerPad,
            leftRect.w - innerPad
        );

        ImColor graphBg = (uiTheme == 1) ? ImColor(242, 242, 248, a) : ImColor(18, 18, 24, a);
        ImColor graphBorder = (uiTheme == 1) ? ImColor(206, 206, 218, a) : ImColor(44, 44, 56, a);
        drawList->AddRectFilled(ImVec2(graphRect.x, graphRect.y), ImVec2(graphRect.z, graphRect.w), graphBg, 12.0f * inScale);
        drawList->AddRect(ImVec2(graphRect.x, graphRect.y), ImVec2(graphRect.z, graphRect.w), graphBorder, 12.0f * inScale, 0, 1.2f);

        ImColor gridCol = (uiTheme == 1) ? ImColor(210, 210, 220, (int)(140.0f * panelAlpha)) : ImColor(60, 60, 74, (int)(120.0f * panelAlpha));
        int gridCount = 5;
        for (int i = 1; i < gridCount; i++)
        {
            float tLine = (float)i / (float)gridCount;
            float gx = MathUtils::lerp(graphRect.x, graphRect.z, tLine);
            float gy = MathUtils::lerp(graphRect.y, graphRect.w, tLine);
            drawList->AddLine(ImVec2(gx, graphRect.y), ImVec2(gx, graphRect.w), gridCol, 1.0f);
            drawList->AddLine(ImVec2(graphRect.x, gy), ImVec2(graphRect.z, gy), gridCol, 1.0f);
        }

        float p1x = animations ? std::clamp(finiteOr(animations->mInterpP1X.mValue, 0.25f), 0.0f, 1.0f) : 0.25f;
        float p1y = animations ? std::clamp(finiteOr(animations->mInterpP1Y.mValue, 0.10f), 0.0f, 1.0f) : 0.10f;
        float p2x = animations ? std::clamp(finiteOr(animations->mInterpP2X.mValue, 0.25f), 0.0f, 1.0f) : 0.25f;
        float p2y = animations ? std::clamp(finiteOr(animations->mInterpP2Y.mValue, 1.00f), 0.0f, 1.0f) : 1.00f;

        auto toScreen = [&](float x, float y) -> ImVec2
        {
            float sx = MathUtils::lerp(graphRect.x, graphRect.z, x);
            float sy = MathUtils::lerp(graphRect.w, graphRect.y, y);
            return ImVec2(sx, sy);
        };

        ImVec2 p0 = toScreen(0.0f, 0.0f);
        ImVec2 p1 = toScreen(p1x, p1y);
        ImVec2 p2 = toScreen(p2x, p2y);
        ImVec2 p3 = toScreen(1.0f, 1.0f);

        ImColor curveCol = alphaColor(accentColor, panelAlpha);
        curveCol.Value.w = std::clamp(panelAlpha, 0.0f, 1.0f);
        ImColor lineCol = (uiTheme == 1) ? ImColor(140, 140, 160, (int)(200.0f * panelAlpha)) : ImColor(90, 90, 120, (int)(200.0f * panelAlpha));
        drawList->AddLine(p0, p1, lineCol, 1.2f * inScale);
        drawList->AddLine(p2, p3, lineCol, 1.2f * inScale);
        drawList->AddBezierCubic(p0, p1, p2, p3, ImGui::ColorConvertFloat4ToU32(curveCol.Value), 2.6f * inScale);

        float handleR = 6.5f * inScale;
        auto dragHandle = [&](const char* id, ImVec2& pos, float& vx, float& vy)
        {
            ImVec2 min = ImVec2(pos.x - handleR, pos.y - handleR);
            ImGui::SetCursorScreenPos(min);
            ImGui::InvisibleButton(id, ImVec2(handleR * 2.0f, handleR * 2.0f));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(0) && isEnabledRaw)
            {
                ImVec2 mp = ImGui::GetIO().MousePos;
                mp.x = std::clamp(mp.x, graphRect.x, graphRect.z);
                mp.y = std::clamp(mp.y, graphRect.y, graphRect.w);
                float w = std::max(1.0f, graphRect.z - graphRect.x);
                float h = std::max(1.0f, graphRect.w - graphRect.y);
                vx = std::clamp((mp.x - graphRect.x) / w, 0.0f, 1.0f);
                vy = std::clamp((graphRect.w - mp.y) / h, 0.0f, 1.0f);
                pos = toScreen(vx, vy);
            }
        };

        dragHandle("##anim_p1", p1, p1x, p1y);
        dragHandle("##anim_p2", p2, p2x, p2y);

        ImColor handleFill = (uiTheme == 1) ? ImColor(255, 255, 255, (int)(220.0f * panelAlpha)) : ImColor(22, 22, 28, (int)(220.0f * panelAlpha));
        ImColor handleBorder = alphaColor(accentColor, panelAlpha);
        drawList->AddCircleFilled(p1, handleR, handleFill, 24);
        drawList->AddCircle(p1, handleR, handleBorder, 24, 1.8f * inScale);
        drawList->AddCircleFilled(p2, handleR, handleFill, 24);
        drawList->AddCircle(p2, handleR, handleBorder, 24, 1.8f * inScale);

        if (animations)
        {
            animations->mInterpP1X.mValue = p1x;
            animations->mInterpP1Y.mValue = p1y;
            animations->mInterpP2X.mValue = p2x;
            animations->mInterpP2Y.mValue = p2y;
        }

        float controlsAlpha = panelAlpha;
        int controlsA = a;

        auto drawToggle = [&](const char* id, const char* name, bool& v, ImVec2 pos, float w)
        {
            float h = 28.0f * inScale;
            ImVec4 r = ImVec4(pos.x, pos.y, pos.x + w, pos.y + h);
            ImRenderUtils::drawText(ImVec2(r.x, r.y + 6.0f * inScale), name, textMain, 0.92f * inScale, controlsAlpha, true, 0, drawList);

            float box = 17.0f * inScale;
            ImVec4 cb = ImVec4(r.z - box, r.y + (h - box) * 0.5f, r.z, r.y + (h + box) * 0.5f);
            ImColor bgOff = (uiTheme == 1) ? ImColor(236, 236, 242, controlsA) : ImColor(18, 18, 22, controlsA);
            ImColor borderOff = (uiTheme == 1) ? ImColor(206, 206, 218, controlsA) : ImColor(44, 44, 56, controlsA);
            static std::unordered_map<std::string, float> boolAnims;
            float& anim = boolAnims[id];
            anim = MathUtils::animate(v ? 1.0f : 0.0f, anim, deltaTime * 15.0f);
            ImColor border = borderOff;
            ImColor bg = MathUtils::lerpImColor(bgOff, accentColor, anim);
            float rr = 4.0f * inScale;
            drawList->AddRectFilled(ImVec2(cb.x, cb.y), ImVec2(cb.z, cb.w), bg, rr);
            drawList->AddRect(ImVec2(cb.x, cb.y), ImVec2(cb.z, cb.w), border, rr, 0, 1.6f);
            if (anim > 0.01f) {
                float pad = 4.6f * inScale;
                float x0 = cb.x + pad;
                float y0 = cb.y + pad;
                float x1 = cb.z - pad;
                float y1 = cb.w - pad;
                ImVec2 p1c = ImVec2(x0 + (x1 - x0) * 0.10f, y0 + (y1 - y0) * 0.55f);
                ImVec2 p2c = ImVec2(x0 + (x1 - x0) * 0.38f, y0 + (y1 - y0) * 0.82f);
                ImVec2 p3c = ImVec2(x0 + (x1 - x0) * 0.92f, y0 + (y1 - y0) * 0.20f);
                ImVec2 pts[3] = { p1c, p2c, p3c };
                ImColor check = ImColor(255, 255, 255, (int)(255.0f * anim * controlsAlpha));
                drawList->AddPolyline(pts, 3, check, 0, 1.55f * inScale);
            }

            ImGui::SetCursorScreenPos(ImVec2(r.x, r.y));
            ImGui::InvisibleButton(id, ImVec2(r.z - r.x, r.w - r.y));
            if (ImGui::IsItemClicked(0) && isEnabledRaw) v = !v;
        };

        auto drawSlider = [&](const char* id, const char* name, float& v, float vMin, float vMax)
        {
            float rowH = 48.0f * inScale;
            float labelFont = 0.92f * inScale;
            float valueFont = 0.88f * inScale;

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec4 r = ImVec4(cursor.x, cursor.y, rightRect.z - innerPad, cursor.y + rowH);

            char valBuf[32];
            std::snprintf(valBuf, sizeof(valBuf), "%.2f", v);
            std::string valStr = std::string(valBuf);

            ImRenderUtils::drawText(ImVec2(r.x, r.y + 5.0f * inScale), name, textMain, labelFont, controlsAlpha, true, 0, drawList);
            float valW = ImRenderUtils::getTextWidth(&valStr, valueFont);
            ImRenderUtils::drawText(ImVec2(r.z - valW, r.y + 6.0f * inScale), valStr, accentColor, valueFont, controlsAlpha, true, 0, drawList);

            float sliderH = 6.0f * inScale;
            float sliderY = r.y + 28.0f * inScale;
            ImVec4 track = ImVec4(r.x, sliderY, r.z, sliderY + sliderH);
            float sliderLen = std::max(1.0f, track.z - track.x);

            float ratio = (v - vMin) / (vMax - vMin);
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            static std::unordered_map<std::string, float> sliderAnims;
            float& anim = sliderAnims[id];
            anim = MathUtils::animate(ratio, anim, deltaTime * 15.0f);

            float sliderR = sliderH * 0.5f;
            ImColor trackBg = (uiTheme == 1) ? ImColor(236, 236, 242, controlsA) : ImColor(24, 24, 29, controlsA);
            drawList->AddRectFilled(ImVec2(track.x, track.y), ImVec2(track.z, track.w), trackBg, sliderR);
            ImColor accentFade = accentColor;
            accentFade.Value.w = std::clamp(controlsAlpha, 0.0f, 1.0f);
            drawList->AddRectFilled(ImVec2(track.x, track.y), ImVec2(track.x + (sliderLen * anim), track.w), accentFade, sliderR);

            ImGui::SetCursorScreenPos(ImVec2(r.x, r.y + 15.0f * inScale));
            ImGui::InvisibleButton(id, ImVec2(r.z - r.x, r.w - r.y - 15.0f * inScale));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(0) && isEnabledRaw)
            {
                float mouseX = ImGui::GetIO().MousePos.x;
                float newRatio = (mouseX - track.x) / sliderLen;
                newRatio = std::clamp(newRatio, 0.0f, 1.0f);
                v = vMin + newRatio * (vMax - vMin);
            }
        };

        float startY = rightRect.y + innerPad + 32.0f * inScale;
        float x = rightRect.x + innerPad;
        float w = (rightRect.z - rightRect.x) - innerPad * 2.0f;

        if (animations)
        {
            std::string hint = "Скорость удара дефолтная, форма зависит от интры";
            ImRenderUtils::drawText(ImVec2(x, startY - 16.0f * inScale), hint, textDim, 0.82f * inScale, panelAlpha, true, 0, drawList);

            ImVec2 togglePos = ImVec2(x, startY + 6.0f * inScale);
            drawToggle("##anim_custom_interp", "Custom Interpolation", animations->mCustomInterpolation.mValue, togglePos, w);

            ImGui::SetCursorScreenPos(ImVec2(x, startY + 40.0f * inScale));
            drawSlider("##anim_swing_speed", "Swing Speed", animations->mSwingSpeed.mValue, 0.20f, 3.00f);
        }

        ImGui::End();
    }
    else if (targetEspPanelAnim > 0.05f)
    {
        auto* targetEsp = gFeatureManager->mModuleManager->getModule<TargetESP>();
        if (targetEsp)
        {
            int desiredMode = (targetEsp->mMode.mValue == 1) ? 1 : 0;
            if (targetEspModeShown == -1) targetEspModeShown = desiredMode;

            if (!isTargetEspModeSwitching && desiredMode != targetEspModeShown) {
                isTargetEspModeSwitching = true;
                isTargetEspModeFadingOut = true;
                targetEspModeTarget = desiredMode;
            }

            if (isTargetEspModeSwitching) {
                if (isTargetEspModeFadingOut) {
                    targetEspModeFade = MathUtils::animate(0.0f, targetEspModeFade, uiDelta * 12.0f);
                    if (targetEspModeFade < 0.02f) {
                        targetEspModeShown = (targetEspModeTarget != -1) ? targetEspModeTarget : targetEspModeShown;
                        targetEspModeTarget = -1;
                        isTargetEspModeFadingOut = false;
                    }
                }
                else {
                    targetEspModeFade = MathUtils::animate(1.0f, targetEspModeFade, uiDelta * 12.0f);
                    if (targetEspModeFade > 0.98f) {
                        isTargetEspModeSwitching = false;
                        targetEspModeFade = 1.0f;
                    }
                }
            }
            else {
                targetEspModeFade = 1.0f;
            }
        }
        float t = std::clamp(targetEspPanelAnim, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float panelAlpha = eased * categoryFade;
        int a = (int)(255.0f * std::clamp(panelAlpha, 0.0f, 1.0f));

        float outerPad = 8.0f * inScale;
        float panelGap = 14.0f * inScale;
        float rightW = contentW * 0.34f;
        ImVec4 leftRect = ImVec4(
            contentRect.x + outerPad,
            contentRect.y + outerPad + contentSlideY,
            contentRect.z - outerPad - rightW - panelGap,
            contentRect.w - outerPad + contentSlideY
        );
        ImVec4 rightRect = ImVec4(
            leftRect.z + panelGap,
            contentRect.y + outerPad + contentSlideY,
            contentRect.z - outerPad,
            contentRect.w - outerPad + contentSlideY
        );

        float openSlideX = (1.0f - eased) * 42.0f * inScale;
        leftRect.x += openSlideX;
        leftRect.z += openSlideX;
        rightRect.x += openSlideX;
        rightRect.z += openSlideX;

        auto alphaColor = [&](ImColor c, float aMul) -> ImColor
        {
            ImVec4 v = c.Value;
            v.w = std::clamp(v.w * aMul, 0.0f, 1.0f);
            return ImColor(v);
        };

        ImColor panelBg = ColorUtils::getUiCardColor(panelAlpha);
        ImColor panelBorder = ColorUtils::getUiBorderColor(panelAlpha * (25.0f / 255.0f));
        if (uiTheme == 1)
        {
            ImColor baseBg = ImColor(229, 229, 233, (int)(255.0f * std::clamp(panelAlpha, 0.0f, 1.0f)));
            ImVec2 bgMin = ImVec2(contentRect.x, contentRect.y + contentSlideY);
            ImVec2 bgMax = ImVec2(contentRect.z, contentRect.w + contentSlideY);
            panelBg = baseBg;
            panelBorder = outlineColor;
            panelBorder.Value.w = std::clamp(panelAlpha, 0.0f, 1.0f);
        }
        else
        {
            panelBg = ImColor(12, 12, 16, (int)(255.0f * std::clamp(panelAlpha, 0.0f, 1.0f)));
            panelBorder = ImColor(0, 0, 0, 0);
        }
        float radius = 14.0f * inScale;
        drawList->AddRectFilled(ImVec2(leftRect.x, leftRect.y), ImVec2(leftRect.z, leftRect.w), panelBg, radius);
        drawList->AddRect(ImVec2(leftRect.x, leftRect.y), ImVec2(leftRect.z, leftRect.w), panelBorder, radius, 0, 1.2f);
        drawList->AddRectFilled(ImVec2(rightRect.x, rightRect.y), ImVec2(rightRect.z, rightRect.w), panelBg, radius);
        drawList->AddRect(ImVec2(rightRect.x, rightRect.y), ImVec2(rightRect.z, rightRect.w), panelBorder, radius, 0, 1.2f);

        struct V3 { float x, y, z; };
        auto v3 = [](float x, float y, float z) -> V3 { return V3{x, y, z}; };
        auto add3 = [](V3 a, V3 b) -> V3 { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; };
        auto sub3 = [](V3 a, V3 b) -> V3 { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; };
        auto mul3 = [](V3 a, float s) -> V3 { return V3{a.x * s, a.y * s, a.z * s}; };
        auto dot3 = [](V3 a, V3 b) -> float { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); };
        auto cross3 = [](V3 a, V3 b) -> V3 { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
        auto len3 = [&](V3 a) -> float { return std::sqrt(dot3(a, a)); };
        auto norm3 = [&](V3 a) -> V3 { float l = len3(a); return (l > 0.00001f) ? mul3(a, 1.0f / l) : V3{0.f, 0.f, 0.f}; };

        auto rotateY = [&](V3 v, float angle) -> V3
        {
            const float s = std::sin(angle);
            const float c = std::cos(angle);
            return V3{v.x * c - v.z * s, v.y, v.x * s + v.z * c};
        };

        auto rotateX = [&](V3 v, float angle) -> V3
        {
            const float s = std::sin(angle);
            const float c = std::cos(angle);
            return V3{v.x, v.y * c - v.z * s, v.y * s + v.z * c};
        };

        auto lerpColor = [](ImColor a, ImColor b, float t) -> ImColor
        {
            t = std::clamp(t, 0.0f, 1.0f);
            ImVec4 av = a.Value;
            ImVec4 bv = b.Value;
            return ImColor(
                av.x + (bv.x - av.x) * t,
                av.y + (bv.y - av.y) * t,
                av.z + (bv.z - av.z) * t,
                av.w + (bv.w - av.w) * t
            );
        };

        auto scaleRgb = [](ImColor c, float s) -> ImColor
        {
            ImVec4 v = c.Value;
            v.x = std::clamp(v.x * s, 0.0f, 1.0f);
            v.y = std::clamp(v.y * s, 0.0f, 1.0f);
            v.z = std::clamp(v.z * s, 0.0f, 1.0f);
            return ImColor(v);
        };

        struct CrystalFace2
        {
            int a, b, c;
            float depth;
            float shade;
            float grad;
        };

        float innerPad = 16.0f * inScale;
        ImVec2 clipMin = ImVec2(leftRect.x + 1.0f, leftRect.y + 1.0f);
        ImVec2 clipMax = ImVec2(leftRect.z - 1.0f, leftRect.w - 1.0f);
        ImVec2 panelCenter = ImVec2((leftRect.x + leftRect.z) * 0.5f, (leftRect.y + leftRect.w) * 0.5f);
        float availW = std::max(0.0f, (leftRect.z - leftRect.x) - innerPad * 2.0f);
        float availH = std::max(0.0f, (leftRect.w - leftRect.y) - innerPad * 2.0f);
        float minDim = std::max(1.0f, std::min(availW, availH));

        const float time = (float)ImGui::GetTime();
        int count = 8;
        float radiusSetting = 0.9f;
        float sizeSetting = 0.55f;
        float spinSetting = 1.25f;
        bool filledSetting = true;
        if (targetEsp)
        {
            float countF = std::clamp(finiteOr(targetEsp->mCrystalCount.mValue, 8.0f), 1.0f, 20.0f);
            count = std::clamp((int)std::round(countF), 1, 20);
            radiusSetting = std::clamp(finiteOr(targetEsp->mRadius.mValue, 0.9f), 0.1f, 3.0f);
            sizeSetting = std::clamp(finiteOr(targetEsp->mSize.mValue, 0.55f), 0.15f, 2.0f);
            spinSetting = std::clamp(finiteOr(targetEsp->mSpinSpeed.mValue, 1.25f), 0.0f, 6.0f);
            filledSetting = targetEsp->mFilled.mValue;
        }

        float baseRadius = radiusSetting * 1.10f;
        float baseSize = sizeSetting * 0.60f;
        float extent = std::max(1.0f, baseRadius * 1.55f + baseSize * 1.10f);
        float worldToPx = ((minDim * 0.62f) / extent) * 3.0f;
        float worldToPxY = worldToPx * 1.18f;
        float camZ = 3.6f;
        V3 viewPos = v3(0.f, 0.f, camZ);

        auto project = [&](V3 p) -> ImVec2
        {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return panelCenter;
            float z = p.z + camZ;
            if (!std::isfinite(z)) z = camZ;
            float inv = 1.0f / std::max(0.12f, z);
            float sx = panelCenter.x + (p.x * worldToPx * inv);
            float sy = panelCenter.y + (p.y * worldToPxY * inv);
            if (!std::isfinite(sx) || !std::isfinite(sy)) return panelCenter;
            return ImVec2(sx, sy);
        };

        auto drawCrystal3D = [&](ImDrawList* dl, V3 center, float size, float yaw, float tilt, ImColor top, ImColor bottom, bool filled, float alpha)
        {
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) return;
            if (!std::isfinite(size) || size <= 0.0001f) return;
            if (!std::isfinite(yaw) || !std::isfinite(tilt)) return;
            const float halfH = size * 0.60f;
            const float halfW = size * 0.55f;

            V3 verts[6] = {
                v3(0.f, +halfH, 0.f),
                v3(0.f, -halfH, 0.f),
                v3(+halfW, 0.f, 0.f),
                v3(-halfW, 0.f, 0.f),
                v3(0.f, 0.f, +halfW),
                v3(0.f, 0.f, -halfW),
            };

            for (auto& v : verts)
            {
                v = rotateX(v, tilt);
                v = rotateY(v, yaw);
                v = add3(v, center);
            }

            CrystalFace2 faces[8] = {
                {0, 2, 4, 0.f, 1.f, 0.5f},
                {0, 4, 3, 0.f, 1.f, 0.5f},
                {0, 3, 5, 0.f, 1.f, 0.5f},
                {0, 5, 2, 0.f, 1.f, 0.5f},
                {1, 4, 2, 0.f, 1.f, 0.5f},
                {1, 3, 4, 0.f, 1.f, 0.5f},
                {1, 5, 3, 0.f, 1.f, 0.5f},
                {1, 2, 5, 0.f, 1.f, 0.5f},
            };

            for (auto& f : faces)
            {
                V3 a3 = verts[f.a];
                V3 b3 = verts[f.b];
                V3 c3 = verts[f.c];
                V3 center3 = mul3(add3(add3(a3, b3), c3), 1.0f / 3.0f);
                f.depth = len3(sub3(viewPos, center3));
                if (!std::isfinite(f.depth)) f.depth = -1e30f;

                V3 ab = sub3(b3, a3);
                V3 ac = sub3(c3, a3);
                V3 n = norm3(cross3(ab, ac));
                V3 toCam = norm3(sub3(viewPos, center3));
                float ndot = dot3(n, toCam);
                if (!std::isfinite(ndot)) ndot = 0.0f;
                ndot = std::clamp(ndot, -1.0f, 1.0f);
                f.shade = 0.55f + 0.45f * (0.5f + 0.5f * ndot);
                if (!std::isfinite(f.shade)) f.shade = 1.0f;

                float yAvg = (a3.y + b3.y + c3.y) * (1.0f / 3.0f);
                f.grad = std::clamp((yAvg - (center.y - halfH)) / (halfH * 2.0f), 0.0f, 1.0f);
                if (!std::isfinite(f.grad)) f.grad = 0.5f;
            }

            std::sort(std::begin(faces), std::end(faces), [](const CrystalFace2& a, const CrystalFace2& b) {
                const float da = std::isfinite(a.depth) ? a.depth : -1e30f;
                const float db = std::isfinite(b.depth) ? b.depth : -1e30f;
                return da > db;
            });

            ImVec2 proj[6];
            for (int i = 0; i < 6; i++) proj[i] = project(verts[i]);

            ImColor line = lerpColor(bottom, top, 0.60f);
            line.Value.w = std::clamp(alpha, 0.0f, 1.0f);

            ImVec2 center2d = project(center);
            {
                const float dx = proj[0].x - proj[1].x;
                const float dy = proj[0].y - proj[1].y;
                const float r = std::max(6.0f * inScale, std::sqrt((dx * dx) + (dy * dy)) * 0.45f);

                ImColor glowTheme = line;
                glowTheme.Value.w = 0.48f * std::clamp(alpha, 0.0f, 1.0f);
                dl->AddShadowCircle(center2d, r * 1.10f, glowTheme, 38.0f * inScale, ImVec2(0.f, 0.f), 0, 64);

                ImColor glowWhite = ImColor(1.f, 1.f, 1.f, 0.40f * std::clamp(alpha, 0.0f, 1.0f));
                dl->AddShadowCircle(center2d, r * 0.85f, glowWhite, 26.0f * inScale, ImVec2(0.f, 0.f), 0, 64);
            }

            if (filled)
            {
                for (const auto& f : faces)
                {
                    ImVec2 tri[3] = {proj[f.a], proj[f.b], proj[f.c]};
                    ImColor col = lerpColor(bottom, top, f.grad);
                    col = scaleRgb(col, 0.82f + 0.18f * f.shade);
                    col.Value.w = std::clamp(alpha, 0.0f, 1.0f);
                    dl->AddConvexPolyFilled(tri, 3, col);
                }
            }

            const float thickness = 2.2f * inScale;
            auto addEdge = [&](int a, int b)
            {
                dl->AddLine(proj[a], proj[b], line, thickness);
            };

            addEdge(0, 2);
            addEdge(0, 3);
            addEdge(0, 4);
            addEdge(0, 5);
            addEdge(1, 2);
            addEdge(1, 3);
            addEdge(1, 4);
            addEdge(1, 5);
            addEdge(2, 4);
            addEdge(2, 5);
            addEdge(3, 4);
            addEdge(3, 5);
        };

        ImColor accent = accentColor;
        accent.Value.w = 1.0f;
        ImColor top = lerpColor(accent, IM_COL32(255, 255, 255, 255), 0.78f);
        ImColor bottom = lerpColor(accent, IM_COL32(255, 255, 255, 255), 0.18f);
        const float pulse = 0.86f + 0.14f * (0.5f + 0.5f * std::sin(time * 2.25f));
        top = scaleRgb(top, pulse);
        bottom = scaleRgb(bottom, pulse * 0.98f);
        top.Value.w = 1.0f;
        bottom.Value.w = 1.0f;

        const float golden = 2.39996322972865332f;
        float easedCrystals = eased;
        float alpha = std::clamp(panelAlpha, 0.0f, 1.0f);
        float height = 1.85f;
        float mtMode = std::clamp(targetEspModeFade, 0.0f, 1.0f);
        float modeEased = mtMode * mtMode * (3.0f - 2.0f * mtMode);

        {
            static ID3D11ShaderResourceView* steveTexture = nullptr;
            static int steveW = 0;
            static int steveH = 0;
            static bool steveLoaded = false;
            if (!steveLoaded) {
                D3DHook::loadTextureFromEmbeddedResource("steve.png", &steveTexture, &steveW, &steveH);
                steveLoaded = true;
            }
            static ID3D11ShaderResourceView* notchTexture = nullptr;
            static int notchW = 0;
            static int notchH = 0;
            static bool notchLoaded = false;
            if (!notchLoaded) {
                D3DHook::loadTextureFromEmbeddedResource("nur.png", &notchTexture, &notchW, &notchH);
                notchLoaded = true;
            }
            float imageAlpha = (targetEspModeShown == 1) ? (alpha * modeEased) : 0.0f;

            if (steveTexture && steveW > 0 && steveH > 0)
            {
                float baseSteveH = (minDim * 0.68f);
                baseSteveH = std::max(12.0f, baseSteveH);
                float steveAspect = (float)steveW / (float)steveH;
                float baseSteveW = baseSteveH * steveAspect;

                ImVec2 steveMin = ImVec2(panelCenter.x - baseSteveW * 0.5f, panelCenter.y - baseSteveH * 0.5f);
                ImVec2 steveMax = ImVec2(steveMin.x + baseSteveW, steveMin.y + baseSteveH);

                drawList->PushClipRect(clipMin, clipMax, true);
                drawList->AddImage(steveTexture, steveMin, steveMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, (int)(255.0f * alpha)));
                drawList->PopClipRect();

                ID3D11ShaderResourceView* overlayTex = notchTexture;
                int overlayW = notchW;
                int overlayH = notchH;

                if (imageAlpha > 0.001f && overlayTex && overlayW > 0 && overlayH > 0)
                {
                    float userScale = std::clamp(finiteOr(targetEsp ? targetEsp->mImageScale.mValue : 1.0f, 1.0f), 0.01f, 10.0f);
                    float userSpin = std::clamp(finiteOr(targetEsp ? targetEsp->mImageSpin.mValue : 2.0f, 2.0f), 0.0f, 50.0f);
                    float reverseDeg = std::clamp(finiteOr(targetEsp ? targetEsp->mImageReverseDegrees.mValue : 0.0f, 0.0f), 0.0f, 10000.0f);
                    bool hitFx = targetEsp ? targetEsp->mImageHitEffect.mValue : false;
                    bool pulseFx = targetEsp ? targetEsp->mImagePulse.mValue : false;

                    float hitT = 0.0f;
                    if (hitFx)
                    {
                        const float period = 1.35f;
                        const float hitFrac = 0.32f;
                        float p = std::fmod(time, period) / period;
                        float u = std::clamp(p / hitFrac, 0.0f, 1.0f);
                        float wave = std::sin(u * 3.14159265358979323846f);
                        hitT = wave * wave;
                        hitT = hitT * hitT * (3.0f - 2.0f * hitT);
                    }

                    float appear = 0.75f + 0.25f * modeEased;
                    float scaleMul = userScale * appear;
                    if (pulseFx)
                    {
                        float p = 0.5f + 0.5f * std::sin(time * 4.0f);
                        float easedP = p * p * (3.0f - 2.0f * p);
                        scaleMul *= (0.92f + 0.16f * easedP);
                    }
                    if (hitFx) scaleMul *= (1.0f - 0.22f * hitT);

                    float baseH = (baseSteveH * 0.36f) * scaleMul;
                    baseH = std::max(10.0f, baseH);
                    float aspect = (float)overlayW / (float)overlayH;
                    float baseW = baseH * aspect;

                    ImVec2 center = ImVec2(panelCenter.x, panelCenter.y + baseSteveH * 0.07f);
                    center.x += 1.0f * inScale;
                    center.y += -1.0f * inScale;
                    float ang = 0.0f;
                    if (reverseDeg > 0.0f)
                    {
                        float maxDeg = (std::max)(0.0f, reverseDeg);
                        float deg = maxDeg * std::sin(time * userSpin);
                        ang = deg * (3.14159265358979323846f / 180.0f);
                    }
                    else
                    {
                        ang = time * userSpin;
                    }
                    float cs = std::cos(ang);
                    float sn = std::sin(ang);
                    float hx = baseW * 0.5f;
                    float hy = baseH * 0.5f;
                    auto rot = [&](float x, float y) -> ImVec2
                    {
                        return ImVec2(center.x + (x * cs - y * sn), center.y + (x * sn + y * cs));
                    };

                    ImVec2 p1 = rot(-hx, -hy);
                    ImVec2 p2 = rot(+hx, -hy);
                    ImVec2 p3 = rot(+hx, +hy);
                    ImVec2 p4 = rot(-hx, +hy);

                    drawList->PushClipRect(clipMin, clipMax, true);
                    ImColor glow = accentColor;
                    glow.Value.w = 0.35f * imageAlpha;
                    drawList->AddShadowCircle(center, baseH * 0.26f, glow, 62.0f * inScale, ImVec2(0.f, 0.f), 0, 64);
                    ImColor imgCol = accentColor;
                    if (hitFx)
                    {
                        imgCol = lerpColor(accentColor, IM_COL32(255, 64, 64, 255), hitT);
                    }
                    imgCol.Value.w = std::clamp(imageAlpha, 0.0f, 1.0f);
                    drawList->AddImageQuad(
                        overlayTex,
                        p1, p2, p3, p4,
                        ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                        ImGui::ColorConvertFloat4ToU32(imgCol.Value)
                    );
                    drawList->PopClipRect();
                }
            }
        }

        float crystalsAlpha2 = (targetEspModeShown == 0) ? (alpha * modeEased) : 0.0f;

        if (crystalsAlpha2 > 0.001f)
        {
            drawList->PushClipRect(clipMin, clipMax, true);
            float radiusMul = 1.0f + (1.0f - easedCrystals) * 0.35f;
            float ySpread = 1.0f + (1.0f - easedCrystals) * 0.12f;
            for (int i = 0; i < count; i++)
            {
                const float idx = (float)i;
                const float u = (idx + 0.5f) / (float)count;
                const float phi = std::acos(1.0f - 2.0f * u);
                const float theta = golden * idx;
                V3 dir = v3(
                    std::cos(theta) * std::sin(phi),
                    std::cos(phi),
                    std::sin(theta) * std::sin(phi)
                );

                const float orbit = time * spinSetting * 0.85f;
                dir = rotateY(dir, orbit + idx * 0.25f);
                dir = rotateX(dir, 0.35f * std::sin(time * 0.9f + idx * 0.7f));

                V3 sideDir = v3(std::cos(theta), dir.y, std::sin(theta));
                float sideLen = len3(sideDir);
                if (sideLen > 0.0001f) sideDir = mul3(sideDir, 1.0f / sideLen);
                dir = add3(mul3(sideDir, 1.0f - easedCrystals), mul3(dir, easedCrystals));

                V3 scatter = v3(
                    std::sin(idx * 12.9898f) * 0.75f,
                    std::sin(idx * 78.2330f) * 0.65f,
                    std::sin(idx * 37.7190f) * 0.75f
                );
                float scLen = len3(scatter);
                if (scLen > 0.0001f) scatter = mul3(scatter, 1.0f / scLen);

                const float radialJitter = 0.72f + 0.28f * (0.5f + 0.5f * std::sin(idx * 4.13f));
                V3 offset = v3(
                    dir.x * (baseRadius * radialJitter * radiusMul),
                    dir.y * (height * 0.70f * ySpread),
                    dir.z * (baseRadius * radialJitter * radiusMul)
                );
                offset = add3(offset, mul3(scatter, ((1.0f - easedCrystals) * baseRadius * 0.35f)));
                offset.y += scatter.y * ((1.0f - easedCrystals) * height * 0.22f);

                const float yBob = 0.10f * std::sin(time * 3.1f + idx * 1.7f);
                V3 c = offset;
                c.y += yBob;

                const float s = baseSize * (0.90f + 0.10f * std::sin(time * 2.6f + idx)) * (0.75f + 0.25f * easedCrystals);
                const float yaw = orbit * 1.6f + idx * 0.9f;
                const float tilt = 0.55f * std::sin(time * 1.25f + idx * 0.8f);

                drawCrystal3D(drawList, c, s, yaw, tilt, top, bottom, filledSetting, crystalsAlpha2);
            }
            drawList->PopClipRect();
        }

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::SetNextWindowPos(ImVec2(contentRect.x, contentRect.y));
        ImGui::SetNextWindowSize(ImVec2(contentW, contentH));
        ImGui::Begin("##targetesp_panel", nullptr, winFlags);

        float titleFont = 0.92f * inScale;
        ImRenderUtils::drawText(ImVec2(rightRect.x + innerPad, rightRect.y + innerPad), "TargetESP", textMain, titleFont, panelAlpha, true, 0, drawList);

        float closeSize = 28.0f * inScale;
        ImVec4 closeRect = ImVec4(rightRect.z - innerPad - closeSize, rightRect.y + innerPad - 6.0f * inScale, rightRect.z - innerPad, rightRect.y + innerPad - 6.0f * inScale + closeSize);
        bool closeHovered = isMouseOver(closeRect) && isEnabledRaw;
        ImColor closeBorderOff = (uiTheme == 1) ? ImColor(206, 206, 218, a) : ImColor(44, 44, 54, a);
        ImColor closeBorder = closeHovered ? alphaColor(accentColor, panelAlpha) : closeBorderOff;
        drawList->AddRect(ImVec2(closeRect.x, closeRect.y), ImVec2(closeRect.z, closeRect.w), closeBorder, closeSize * 0.35f, 0, 1.4f);
        std::string closeLabel = "X";
        float cw = ImRenderUtils::getTextWidth(&closeLabel, 0.95f * inScale);
        float ch = ImRenderUtils::getTextHeightStr(&closeLabel, 0.95f * inScale);
        ImColor closeText = closeHovered ? MathUtils::lerpImColor(textDim, accentColor, 0.65f) : textDim;
        ImRenderUtils::drawText(ImVec2(closeRect.x + (closeSize - cw) * 0.5f, closeRect.y + (closeSize - ch) * 0.5f), closeLabel, closeText, 0.95f * inScale, panelAlpha, true, 0, drawList);
        ImGui::SetCursorScreenPos(ImVec2(closeRect.x, closeRect.y));
        ImGui::InvisibleButton("##targetesp_close", ImVec2(closeRect.z - closeRect.x, closeRect.w - closeRect.y));
        if (ImGui::IsItemClicked(0) && isEnabledRaw) {
            isTargetEspPanelOpen = false;
            ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
        }

        float controlsAlpha = panelAlpha;
        int controlsA = a;
        float settingsOffsetY = 0.0f;
        int modeShown = targetEspModeShown;
        if (targetEsp)
        {
            if (modeShown == -1) modeShown = (targetEsp->mMode.mValue == 1) ? 1 : 0;
            float mt = std::clamp(targetEspModeFade, 0.0f, 1.0f);
            float modeEased = mt * mt * (3.0f - 2.0f * mt);
            controlsAlpha = panelAlpha * modeEased;
            controlsA = (int)(255.0f * std::clamp(controlsAlpha, 0.0f, 1.0f));
            settingsOffsetY = 0.0f;
        }

        auto drawToggle = [&](const char* id, const char* name, bool& v, ImVec2 pos, float w)
        {
            float h = 28.0f * inScale;
            ImVec4 r = ImVec4(pos.x, pos.y, pos.x + w, pos.y + h);
            ImRenderUtils::drawText(ImVec2(r.x, r.y + 6.0f * inScale), name, textMain, 0.92f * inScale, controlsAlpha, true, 0, drawList);

            float box = 17.0f * inScale;
            ImVec4 cb = ImVec4(r.z - box, r.y + (h - box) * 0.5f, r.z, r.y + (h + box) * 0.5f);
            ImColor bgOff = (uiTheme == 1) ? ImColor(236, 236, 242, controlsA) : ImColor(18, 18, 22, controlsA);
            ImColor borderOff = (uiTheme == 1) ? ImColor(206, 206, 218, controlsA) : ImColor(44, 44, 56, controlsA);
            static std::unordered_map<std::string, float> boolAnims;
            float& anim = boolAnims[id];
            anim = MathUtils::animate(v ? 1.0f : 0.0f, anim, deltaTime * 15.0f);
            ImColor border = borderOff;
            ImColor bg = MathUtils::lerpImColor(bgOff, accentColor, anim);
            float rr = 4.0f * inScale;
            drawList->AddRectFilled(ImVec2(cb.x, cb.y), ImVec2(cb.z, cb.w), bg, rr);
            drawList->AddRect(ImVec2(cb.x, cb.y), ImVec2(cb.z, cb.w), border, rr, 0, 1.6f);
            if (anim > 0.01f) {
                float pad = 4.6f * inScale;
                float x0 = cb.x + pad;
                float y0 = cb.y + pad;
                float x1 = cb.z - pad;
                float y1 = cb.w - pad;
                ImVec2 p1 = ImVec2(x0 + (x1 - x0) * 0.10f, y0 + (y1 - y0) * 0.55f);
                ImVec2 p2 = ImVec2(x0 + (x1 - x0) * 0.38f, y0 + (y1 - y0) * 0.82f);
                ImVec2 p3 = ImVec2(x0 + (x1 - x0) * 0.92f, y0 + (y1 - y0) * 0.20f);
                ImVec2 pts[3] = { p1, p2, p3 };
                ImColor check = ImColor(255, 255, 255, (int)(255.0f * anim * controlsAlpha));
                drawList->AddPolyline(pts, 3, check, 0, 1.55f * inScale);
            }

            ImGui::SetCursorScreenPos(ImVec2(r.x, r.y));
            ImGui::InvisibleButton(id, ImVec2(r.z - r.x, r.w - r.y));
            if (ImGui::IsItemClicked(0) && isEnabledRaw) v = !v;
        };

        auto drawSlider = [&](const char* id, const char* name, float& v, float vMin, float vMax)
        {
            float rowH = 48.0f * inScale;
            float labelFont = 0.92f * inScale;
            float valueFont = 0.88f * inScale;

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec4 r = ImVec4(cursor.x, cursor.y, rightRect.z - innerPad, cursor.y + rowH);

            char valBuf[32];
            std::snprintf(valBuf, sizeof(valBuf), "%.2f", v);
            std::string valStr = std::string(valBuf);

            ImRenderUtils::drawText(ImVec2(r.x, r.y + 5.0f * inScale), name, textMain, labelFont, controlsAlpha, true, 0, drawList);
            float valW = ImRenderUtils::getTextWidth(&valStr, valueFont);
            ImRenderUtils::drawText(ImVec2(r.z - valW, r.y + 6.0f * inScale), valStr, accentColor, valueFont, controlsAlpha, true, 0, drawList);

            float sliderH = 6.0f * inScale;
            float sliderY = r.y + 28.0f * inScale;
            ImVec4 track = ImVec4(r.x, sliderY, r.z, sliderY + sliderH);
            float sliderLen = std::max(1.0f, track.z - track.x);

            float ratio = (v - vMin) / (vMax - vMin);
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            static std::unordered_map<std::string, float> sliderAnims;
            float& anim = sliderAnims[id];
            anim = MathUtils::animate(ratio, anim, deltaTime * 15.0f);

            float sliderR = sliderH * 0.5f;
            ImColor trackBg = (uiTheme == 1) ? ImColor(236, 236, 242, controlsA) : ImColor(24, 24, 29, controlsA);
            drawList->AddRectFilled(ImVec2(track.x, track.y), ImVec2(track.z, track.w), trackBg, sliderR);
            ImColor accentFade = accentColor;
            accentFade.Value.w = std::clamp(controlsAlpha, 0.0f, 1.0f);
            drawList->AddRectFilled(ImVec2(track.x, track.y), ImVec2(track.x + (sliderLen * anim), track.w), accentFade, sliderR);

            ImGui::SetCursorScreenPos(ImVec2(r.x, r.y + 15.0f * inScale));
            ImGui::InvisibleButton(id, ImVec2(r.z - r.x, r.w - r.y - 15.0f * inScale));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(0) && isEnabledRaw)
            {
                float mouseX = ImGui::GetIO().MousePos.x;
                float newRatio = (mouseX - track.x) / sliderLen;
                newRatio = std::clamp(newRatio, 0.0f, 1.0f);
                v = vMin + newRatio * (vMax - vMin);
            }
        };

        auto drawSliderInt = [&](const char* id, const char* name, int& v, int vMin, int vMax)
        {
            float vf = (float)v;
            drawSlider(id, name, vf, (float)vMin, (float)vMax);
            int vi = (int)std::round(vf);
            v = std::clamp(vi, vMin, vMax);
        };

        if (targetEsp)
        {
            float modeTargetUi = (targetEsp->mMode.mValue == 1) ? 1.0f : 0.0f;
            static float modeUiAnim = 0.0f;
            modeUiAnim = MathUtils::animate(modeTargetUi, modeUiAnim, deltaTime * 14.0f);
            float mtUi = std::clamp(modeUiAnim, 0.0f, 1.0f);
            float modeUiEased = mtUi * mtUi * (3.0f - 2.0f * mtUi);

            float arrowW = 28.0f * inScale;
            float arrowH = 44.0f * inScale;
            float arrowPad = 10.0f * inScale;

            float leftArrowAlpha = panelAlpha * modeUiEased;
            float rightArrowAlpha = panelAlpha * (1.0f - modeUiEased);

            ImVec4 leftArrowRect = ImVec4(leftRect.x + arrowPad, panelCenter.y - arrowH * 0.5f, leftRect.x + arrowPad + arrowW, panelCenter.y + arrowH * 0.5f);
            ImVec4 rightArrowRect = ImVec4(leftRect.z - arrowPad - arrowW, panelCenter.y - arrowH * 0.5f, leftRect.z - arrowPad, panelCenter.y + arrowH * 0.5f);

            auto drawArrowButton = [&](ImVec4 r, bool left, float alphaBtn, const char* btnId)
            {
                if (alphaBtn <= 0.02f) return false;
                bool hovered = isMouseOver(r) && isEnabledRaw;
                static std::unordered_map<std::string, float> arrowHoverAnims;
                float& hAnim = arrowHoverAnims[btnId];
                hAnim = MathUtils::animate(hovered ? 1.0f : 0.0f, hAnim, deltaTime * 18.0f);
                ImColor accent = accentColor;

                ImVec2 c = ImVec2((r.x + r.z) * 0.5f, (r.y + r.w) * 0.5f);
                c.x += (left ? -1.0f : 1.0f) * (0.9f * inScale) * hAnim;
                float s = 8.8f * inScale;
                float d = 0.86f * s;
                ImVec2 a = left ? ImVec2(c.x + d, c.y - d) : ImVec2(c.x - d, c.y - d);
                ImVec2 b = ImVec2(c.x, c.y);
                ImVec2 c2 = left ? ImVec2(c.x + d, c.y + d) : ImVec2(c.x - d, c.y + d);
                ImVec2 pts[3] = { a, b, c2 };
                ImColor baseChevron = ImColor(255, 255, 255, (int)(255.0f * alphaBtn));
                ImColor chevron = MathUtils::lerpImColor(baseChevron, accent, hAnim * 0.35f);
                chevron.Value.w = std::clamp(alphaBtn, 0.0f, 1.0f);
                drawList->AddPolyline(pts, 3, ImGui::ColorConvertFloat4ToU32(chevron.Value), 0, 2.4f * inScale);

                ImGui::SetCursorScreenPos(ImVec2(r.x, r.y));
                ImGui::InvisibleButton(btnId, ImVec2(r.z - r.x, r.w - r.y));
                return ImGui::IsItemClicked(0);
            };

            bool leftClicked = drawArrowButton(leftArrowRect, true, leftArrowAlpha, "##tesp_mode_left");
            if (leftClicked && isEnabledRaw && targetEsp->mMode.mValue == 1)
            {
                targetEsp->mMode.mValue = 0;
                ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
            }

            bool rightClicked = drawArrowButton(rightArrowRect, false, rightArrowAlpha, "##tesp_mode_right");
            if (rightClicked && isEnabledRaw && targetEsp->mMode.mValue == 0)
            {
                targetEsp->mMode.mValue = 1;
                ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
            }

            float startY = rightRect.y + innerPad + 32.0f * inScale;
            float x = rightRect.x + innerPad;
            float w = (rightRect.z - rightRect.x) - innerPad * 2.0f;

            float rowGap = 8.0f * inScale;
            float rowH = 48.0f * inScale;

            float y = startY + settingsOffsetY;
            bool isImageMode = (modeShown == 1);

            if (controlsAlpha > 0.02f && !isImageMode)
            {
                int crystals = (int)std::round(targetEsp->mCrystalCount.mValue);
                crystals = std::clamp(crystals, 1, 20);
                ImGui::SetCursorScreenPos(ImVec2(x, y));
                drawSliderInt("##tesp_count", "Crystals", crystals, 1, 20);
                targetEsp->mCrystalCount.mValue = (float)crystals;
                y += rowH + rowGap;

                ImGui::SetCursorScreenPos(ImVec2(x, y));
                drawSlider("##tesp_radius", "Radius", targetEsp->mRadius.mValue, 0.1f, 3.0f);
                y += rowH + rowGap;

                ImGui::SetCursorScreenPos(ImVec2(x, y));
                drawSlider("##tesp_size", "Size", targetEsp->mSize.mValue, 0.15f, 2.0f);
                y += rowH + rowGap;

                ImGui::SetCursorScreenPos(ImVec2(x, y));
                drawSlider("##tesp_spin", "Spin Speed", targetEsp->mSpinSpeed.mValue, 0.0f, 6.0f);
                y += rowH + rowGap;

                ImVec2 filledPos = ImVec2(x, y + 6.0f * inScale);
                drawToggle("##tesp_filled", "Filled", targetEsp->mFilled.mValue, filledPos, w);
            }
            else if (controlsAlpha > 0.02f)
            {
                ImGui::SetCursorScreenPos(ImVec2(x, y));
                drawSlider("##tesp_img_scale", "Kvadratik Size", targetEsp->mImageScale.mValue, 0.2f, 2.5f);
                y += rowH + rowGap;

                ImGui::SetCursorScreenPos(ImVec2(x, y));
                drawSlider("##tesp_img_spin", "Kvadratik Spin", targetEsp->mImageSpin.mValue, 0.0f, 10.0f);
                y += rowH + rowGap;

                ImGui::SetCursorScreenPos(ImVec2(x, y));
                drawSlider("##tesp_img_reverse", "Kvadratik Reverse", targetEsp->mImageReverseDegrees.mValue, 0.0f, 720.0f);
                y += rowH + rowGap;

                ImVec2 hitPos = ImVec2(x, y + 6.0f * inScale);
                drawToggle("##tesp_img_hit", "Hit Effect", targetEsp->mImageHitEffect.mValue, hitPos, w);
                y += rowH * 0.72f + rowGap;

                ImVec2 pulsePos = ImVec2(x, y + 6.0f * inScale);
                drawToggle("##tesp_img_pulse", "Pulse", targetEsp->mImagePulse.mValue, pulsePos, w);
            }
        }

        ImGui::End();
    }
    else
    {
        static std::unordered_map<Module*, float> settingsHeightAnims;
        if (categoryChanged) {
            settingsHeightAnims.clear();
        }
        for (const auto& mod : modsToDraw)
        {
            Module* modPtr = mod.get();
            mod->showSettings = true;
            float modHeaderHeight = 45.0f * inScale;
            float settingsHeight = 0.0f;

            mod->cAnim = MathUtils::animate(1.0f, mod->cAnim, deltaTime * 10.0f);
            const float moduleFade = categoryFade * moduleListEased;
            const float settingsAlpha = mod->cAnim * moduleFade;
            auto alphaColor = [&](ImColor c, float aMul) -> ImColor
            {
                ImVec4 v = c.Value;
                v.w = std::clamp(v.w * aMul, 0.0f, 1.0f);
                return ImColor(v);
            };

            if (mod->cAnim > 0.01f) {
            float descFont = 0.80f * inScale;
            float descMaxWidth = (columnWidth - 24.0f) * 0.90f;
            float descLineSpacing = 2.0f * inScale;
            float descTopPad = 16.0f * inScale;
            for (auto& setting : mod->mSettings) {
                if (!setting->mIsVisible()) continue;
                float descH = wrappedDescriptionHeight(setting, setting->mDescription, descFont, descMaxWidth, descLineSpacing);
                float descExtra = (descH > 0.0f) ? (descTopPad + descH) : 0.0f;
                if (setting->mType == SettingType::Bool) {
                    float baseRowH = 28.0f * inScale;
                    float titlePad = 6.0f * inScale;
                    float bottomPad = 6.0f * inScale;
                    float boolH = baseRowH;
                    if (descH > 0.0f) {
                        boolH = std::max(baseRowH, titlePad + descTopPad + descH + bottomPad);
                    }
                    settingsHeight += boolH;
                }
                else if (setting->mType == SettingType::Number) settingsHeight += (48.0f * inScale) + descExtra;
                else if (setting->mType == SettingType::Enum) {
                    auto s = setting->asEnumSettingBase();
                    if (!s) {
                        settingsHeight += (30.0f * inScale) + descExtra;
                    } else {
                        float& anim = enumDropdownAnims[setting];
                        float target = (openEnumSetting == setting) ? 1.0f : 0.0f;
                        anim = MathUtils::animate(target, anim, deltaTime * 14.0f);

                        float headerH = 28.0f * inScale;
                        float descPadTop = 6.0f * inScale;
                        float descPadBottom = 6.0f * inScale;

                        float fullW = columnWidth - 24.0f;
                        float labelW = ImRenderUtils::getTextWidth(&s->mName, 0.95f * inScale);
                        float minGap = 12.0f * inScale;
                        float desiredW = fullW * 0.5f;
                        float dropW = std::clamp(std::max(128.0f * inScale, desiredW), 0.0f, fullW);
                        float dropX = fullW - dropW;
                        float minX = labelW + minGap;
                        if (dropX < minX) {
                            dropX = minX;
                        }

                        float descMaxWidthEnum = std::min(descMaxWidth, std::max(0.0f, dropX - 2.0f * inScale));
                        if (descMaxWidthEnum < 90.0f * inScale) descMaxWidthEnum = descMaxWidth;
                        float descHEnum = wrappedDescriptionHeight(setting, setting->mDescription, descFont, descMaxWidthEnum, descLineSpacing);
                        float baseH = headerH + ((descHEnum > 0.0f) ? (descPadTop + descHEnum + descPadBottom) : 0.0f);

                        settingsHeight += baseH;
                    }
                }
                else if (setting->mType == SettingType::Color)
                {
                    auto s = reinterpret_cast<ColorSetting*>(setting);
                    float target = (displayColorPicker && lastColorSetting == s) ? 1.0f : 0.0f;
                    s->mSlide = MathUtils::animate(target, s->mSlide, deltaTime * 14.0f);

                    float baseH = (52.0f * inScale) + descExtra;
                    float pickerGap = 8.0f * inScale;
                    float maxPickerW = columnWidth - 24.0f;
                    float pickerW = std::min(colorPickerWidth * inScale, maxPickerW);
                    float barsW = ImGui::GetFrameHeight();
                    float pickerH = ImMax(barsW, pickerW - 2.0f * (barsW + ImGui::GetStyle().ItemInnerSpacing.x));
                    settingsHeight += baseH + ((pickerH + pickerGap) * s->mSlide);
                }
                else settingsHeight += (30.0f * inScale) + descExtra;
            }
            if (settingsHeight > 0) settingsHeight += 10.0f * inScale;
        }

        auto [heightIt, heightInserted] = settingsHeightAnims.emplace(modPtr, settingsHeight);
        if (!heightInserted) {
            heightIt->second = MathUtils::animate(settingsHeight, heightIt->second, deltaTime * 12.0f);
        }
        float settingsHeightAnimated = heightIt->second;
        float currentModHeight = modHeaderHeight + (settingsHeightAnimated * mod->cAnim);

        int targetCol = 0;
        if (heightCol1 <= heightCol2 && heightCol1 <= heightCol3) targetCol = 0;
        else if (heightCol2 <= heightCol1 && heightCol2 <= heightCol3) targetCol = 1;
        else targetCol = 2;

        float xPos = contentRect.x + (targetCol * (columnWidth + columnGap));
        float yPos = 0.0f;

        if (targetCol == 0) yPos = col1Y;
        else if (targetCol == 1) yPos = col2Y;
        else yPos = col3Y;
        yPos += contentSlideY + moduleListSlideY;

        ImVec4 modRect = ImVec4(xPos, yPos, xPos + columnWidth, yPos + currentModHeight);

        if (modRect.y < contentRect.w && modRect.w > contentRect.y)
        {
            drawList->AddRectFilled(ImVec2(modRect.x, modRect.y), ImVec2(modRect.z, modRect.w), alphaColor(cardColor, moduleFade), 12.0f);

            std::string& moduleName = mod->getName();
            float moduleNameFont = 1.1f * inScale;
            float moduleNameY = modRect.y + 12.0f * inScale;
            float moduleNameH = ImRenderUtils::getTextHeightStr(&moduleName, moduleNameFont);
            float moduleNameCenterY = moduleNameY + (moduleNameH * 0.5f);

            ImColor iconCol = alphaColor(textDim, moduleFade);
            bool pushedModuleIconFont = false;
            std::string moduleIconLabel = "G";
            float moduleIconFont = 1.18f * inScale;
            float moduleIconW = 0.0f;
            float moduleIconH = 0.0f;
            {
                auto itFont = FontHelper::Fonts.find("icons3.ttf_large");
                if (itFont != FontHelper::Fonts.end() && itFont->second)
                {
                    ImGui::PushFont(itFont->second);
                    pushedModuleIconFont = true;
                }
            }
            moduleIconW = ImRenderUtils::getTextWidth(&moduleIconLabel, moduleIconFont);
            moduleIconH = ImRenderUtils::getTextHeightStr(&moduleIconLabel, moduleIconFont);
            ImVec2 moduleIconPos = ImVec2(modRect.x + 12.0f * inScale, moduleNameCenterY - (moduleIconH * 0.5f) + 2.3f * inScale);
            ImRenderUtils::drawText(moduleIconPos, moduleIconLabel, iconCol, moduleIconFont, moduleFade, false);
            if (pushedModuleIconFont) ImGui::PopFont();

            ImVec4 moduleIconRect = ImVec4(
                moduleIconPos.x - 2.0f * inScale,
                moduleIconPos.y - 2.0f * inScale,
                moduleIconPos.x + moduleIconW + 2.0f * inScale,
                moduleIconPos.y + moduleIconH + 2.0f * inScale
            );
            bool moduleIconHovered = isMouseOver(moduleIconRect) && isEnabled && isMouseOver(contentRect);
            static std::unordered_map<Module*, float> moduleDescAnims;
            static std::unordered_map<Module*, float> moduleDescScales;
            float& moduleDescAnim = moduleDescAnims[modPtr];
            float& moduleDescScale = moduleDescScales[modPtr];
            moduleDescAnim = MathUtils::animate(moduleIconHovered ? 1.0f : 0.0f, moduleDescAnim, deltaTime * 14.0f);
            moduleDescScale = MathUtils::animate(moduleIconHovered ? 1.03f : 0.97f, moduleDescScale, deltaTime * 12.0f);

            float moduleDescAlpha = moduleDescAnim * moduleFade;
            if (!mod->mDescription.empty() && moduleDescAlpha > pendingModuleDescAlpha)
            {
                pendingModuleDescModule = modPtr;
                pendingModuleDescText = mod->mDescription;
                pendingModuleDescIconRect = moduleIconRect;
                pendingModuleDescAnim = moduleDescAnim;
                pendingModuleDescScale = moduleDescScale;
                pendingModuleDescFade = moduleFade;
                pendingModuleDescAlpha = moduleDescAlpha;
            }

            ImRenderUtils::drawText(ImVec2(modRect.x + 35.0f * inScale, moduleNameY), moduleName, textMain, moduleNameFont, moduleFade, true);

            float switchW = 30.0f * inScale;
            float switchH = 18.0f * inScale;
            float headerCenterY = modRect.y + (modHeaderHeight / 2.0f);
            float switchY = headerCenterY - (switchH / 2.0f);
            ImVec4 switchRect = ImVec4(modRect.z - 12.0f - switchW, switchY, modRect.z - 12.0f, switchY + switchH);
            ImVec4 hitBox = ImVec4(modRect.x, modRect.y, modRect.z, modRect.y + modHeaderHeight);

            bool bindIsActive = isBinding && lastMod == mod;
            std::string bindStr = bindIsActive ? "..." : ((mod->mKey == 0) ? "None" : Keyboard::getKey(mod->mKey));
            float bindFont = 0.82f * inScale;
            float bindPadX = 5.0f * inScale;
            float bindGap = 8.0f * inScale;

            float iconPad = 1.5f * inScale;
            float iconGap = 3.0f * inScale;
            float iconH = switchH - iconPad * 2.0f;
            float iconW = iconH;

            bool bindIsShortKey = (bindStr.size() <= 2) && (bindStr != "None") && !bindIsActive;
            float bindMinW = (bindIsShortKey ? (22.0f * inScale) : (40.0f * inScale));
            float bindTextW = ImRenderUtils::getTextWidth(&bindStr, bindFont);
            float bindW = std::max(bindMinW, bindPadX + iconW + iconGap + bindTextW + bindPadX);
            static std::unordered_map<Module*, float> bindWidthAnims;
            static std::unordered_map<Module*, float> bindColorAnims;
            static std::unordered_map<Module*, int> bindLastKeys;
            static std::unordered_map<Module*, float> bindKeyFlashes;


            float bindWAnim = bindW;
            auto wIt = bindWidthAnims.find(modPtr);
            if (wIt == bindWidthAnims.end()) {
                bindWidthAnims.emplace(modPtr, bindWAnim);
            }
            else {
                wIt->second = MathUtils::animate(bindW, wIt->second, deltaTime * 18.0f);
                bindWAnim = wIt->second;
            }

            int currentKey = mod->mKey;
            auto lkIt = bindLastKeys.find(modPtr);
            if (lkIt == bindLastKeys.end()) {
                bindLastKeys.emplace(modPtr, currentKey);
            }
            else if (lkIt->second != currentKey) {
                if (currentKey != 0) {
                    bindKeyFlashes[modPtr] = 1.0f;
                }
                lkIt->second = currentKey;
            }

            float keyFlash = 0.0f;
            auto fIt = bindKeyFlashes.find(modPtr);
            if (fIt != bindKeyFlashes.end()) {
                fIt->second = MathUtils::animate(0.0f, fIt->second, deltaTime * 10.0f);
                keyFlash = fIt->second;
            }

            float bindColorTarget = bindIsActive ? 1.0f : keyFlash;
            float bindColorT = bindColorTarget;
            auto cIt = bindColorAnims.find(modPtr);
            if (cIt == bindColorAnims.end()) {
                bindColorAnims.emplace(modPtr, bindColorTarget);
            }
            else {
                cIt->second = MathUtils::animate(bindColorTarget, cIt->second, deltaTime * 14.0f);
                bindColorT = cIt->second;
            }

            auto* targetEspModule = gFeatureManager->mModuleManager->getModule<TargetESP>();
            auto* animationsModule = gFeatureManager->mModuleManager->getModule<Animations>();
            bool isTargetEspMod = (targetEspModule != nullptr) && (mod.get() == targetEspModule);
            bool isAnimationsMod = (animationsModule != nullptr) && (mod.get() == animationsModule);
            float panelBtnGap = 6.0f * inScale;
            float panelBtnW = switchH;
            bool hasPanelBtn = isTargetEspMod || isAnimationsMod;

            float bindRight = switchRect.x - bindGap;
            if (hasPanelBtn) bindRight -= (panelBtnW + panelBtnGap);
            ImVec4 bindRect = ImVec4(bindRight - bindWAnim, switchRect.y, bindRight, switchRect.w);
            ImVec4 panelBtnRect = ImVec4(switchRect.x - panelBtnGap - panelBtnW, bindRect.y, switchRect.x - panelBtnGap, bindRect.w);
            bool panelBtnHovered = hasPanelBtn && isMouseOver(panelBtnRect) && isEnabled && isMouseOver(contentRect);

            bool bindHovered = isMouseOver(bindRect) && isEnabled && isMouseOver(contentRect);
            ImColor bindBg = (uiTheme == 1) ? ImColor(236, 236, 242, 255) : ImColor(24, 24, 29, 255);
            ImColor bindBorderOff = (uiTheme == 1) ? ImColor(206, 206, 218, 255) : ImColor(46, 46, 58, 255);
            ImColor bindBorder = MathUtils::lerpImColor(bindBorderOff, accentColor, bindColorT);
            ImColor bindTextCol = MathUtils::lerpImColor(textDim, accentColor, bindColorT * 0.65f);
            float bindRadius = 5.0f * inScale;
            bindBg = alphaColor(bindBg, moduleFade);
            bindBorder = alphaColor(bindBorder, moduleFade);
            bindTextCol = alphaColor(bindTextCol, moduleFade);

            if (hasPanelBtn)
            {
                float panelOpen = isTargetEspMod ? (isTargetEspPanelOpen ? 1.0f : 0.0f) : (isAnimationsPanelOpen ? 1.0f : 0.0f);
                float target = panelOpen > 0.5f ? 1.0f : (panelBtnHovered ? 0.65f : 0.0f);
                static std::unordered_map<Module*, float> panelBtnAnims;
                float& panelBtnAnim = panelBtnAnims[modPtr];
                panelBtnAnim = MathUtils::animate(target, panelBtnAnim, deltaTime * 14.0f);
                ImColor panelBorder = alphaColor(MathUtils::lerpImColor(bindBorderOff, accentColor, panelBtnAnim), moduleFade);
                drawList->AddRectFilled(ImVec2(panelBtnRect.x, panelBtnRect.y), ImVec2(panelBtnRect.z, panelBtnRect.w), bindBg, bindRadius);
                drawList->AddRect(ImVec2(panelBtnRect.x, panelBtnRect.y), ImVec2(panelBtnRect.z, panelBtnRect.w), panelBorder, bindRadius, 0, 1.6f);

                std::string panelIcon = panelOpen > 0.5f ? "<" : ">";
                float iconFont = 0.95f * inScale;
                float iw = ImRenderUtils::getTextWidth(&panelIcon, iconFont);
                float ih = ImRenderUtils::getTextHeightStr(&panelIcon, iconFont);
                float ix = panelBtnRect.x + ((panelBtnRect.z - panelBtnRect.x) - iw) * 0.5f;
                float iy = panelBtnRect.y + ((panelBtnRect.w - panelBtnRect.y) - ih) * 0.5f + 0.6f * inScale;
                ImColor baseIconCol = (uiTheme == 1) ? textMain : textDim;
                ImColor iconCol = alphaColor(MathUtils::lerpImColor(baseIconCol, accentColor, panelBtnAnim), moduleFade);
                ImRenderUtils::drawText(ImVec2(ix, iy), panelIcon, iconCol, iconFont, moduleFade, true);
            }

            drawList->AddRectFilled(ImVec2(bindRect.x, bindRect.y), ImVec2(bindRect.z, bindRect.w), bindBg, bindRadius);
            drawList->AddRect(ImVec2(bindRect.x, bindRect.y), ImVec2(bindRect.z, bindRect.w), bindBorder, bindRadius, 0, 1.6f);
            static ID3D11ShaderResourceView* bindIconTexture = nullptr;
            static int bindIconW = 0;
            static int bindIconH = 0;
            static bool bindIconLoaded = false;
            if (!bindIconLoaded) {
                D3DHook::loadTextureFromEmbeddedResource("kbsmell.png", &bindIconTexture, &bindIconW, &bindIconH);
                bindIconLoaded = true;
            }
            float textAreaX0 = bindRect.x;
            float textAreaX1 = bindRect.z;
            if (bindIconTexture != nullptr) {
                ImColor bindIconTint = bindTextCol;
                ImVec2 iconMin = ImVec2(bindRect.x + iconPad + 2.0f * inScale, bindRect.y + iconPad);
                ImVec2 iconMax = ImVec2(iconMin.x + iconW, iconMin.y + iconH);
                drawList->AddImage(bindIconTexture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), bindIconTint);
                textAreaX0 = iconMax.x + 2.0f * inScale;
                textAreaX1 = bindRect.z - iconPad;
            }

            float textW = bindTextW;
            float bindTextX = textAreaX0 + (std::max(0.0f, (textAreaX1 - textAreaX0) - textW)) / 2.0f;
            float bindTextY = bindRect.y + (switchH - ImRenderUtils::getTextHeightStr(&bindStr, bindFont)) / 2.0f + 0.6f * inScale;
            ImRenderUtils::drawText(ImVec2(bindTextX, bindTextY), bindStr, bindTextCol, bindFont, moduleFade, true);

            if (isMouseOver(hitBox) && isEnabled && isMouseOver(contentRect) && ImGui::IsMouseClicked(0)) {
                if (panelBtnHovered) {
                    if (currentCategoryIndex != visualCategoryIndex) {
                        currentCategoryIndex = visualCategoryIndex;
                        targetCategoryIndex = -1;
                        isCategorySwitching = false;
                        isCategoryFadingOut = false;
                        categoryFade = 1.0f;
                        mScrollTarget = 0.0f;
                        mScrollY = 0.0f;
                        isIrcPanelOpen = false;
                    }
                    if (isTargetEspMod) {
                        isTargetEspPanelOpen = !isTargetEspPanelOpen;
                        if (isTargetEspPanelOpen) isAnimationsPanelOpen = false;
                    } else if (isAnimationsMod) {
                        isAnimationsPanelOpen = !isAnimationsPanelOpen;
                        if (isAnimationsPanelOpen) isTargetEspPanelOpen = false;
                    }
                    ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
                } else if (bindHovered) {
                    isBinding = true;
                    isBoolSettingBinding = false;
                    lastBoolSetting = nullptr;
                    lastMod = mod;
                    ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
                } else {
                    mod->toggle();
                    ClientInstance::get()->playUi("random.pop", 0.75f, 1.0f);
                }
            }

            mod->cScale = MathUtils::animate(mod->mEnabled ? 1.0f : 0.0f, mod->cScale, deltaTime * 12.0f);

            float t = mod->cScale;
            float eased = t * t * (3.0f - 2.0f * t);

            ImColor trackOff = (uiTheme == 1) ? ImColor(214, 214, 224, 255) : ImColor(22, 22, 28, 255);
            ImColor trackOn = accentColor;
            ImColor trackCol = alphaColor(MathUtils::lerpImColor(trackOff, trackOn, eased), moduleFade);

            ImColor borderOff = (uiTheme == 1) ? ImColor(206, 206, 218, 255) : ImColor(48, 48, 60, 255);
            ImColor borderCol = alphaColor(MathUtils::lerpImColor(borderOff, accentColor, eased), moduleFade);

            float radius = switchH / 2.0f;
            drawList->AddRectFilled(ImVec2(switchRect.x, switchRect.y), ImVec2(switchRect.z, switchRect.w), trackCol, radius);
            drawList->AddRect(ImVec2(switchRect.x, switchRect.y), ImVec2(switchRect.z, switchRect.w), borderCol, radius, 0, 1.6f);

            float knobInset = 2.2f * inScale;
            float knobRadius = radius - knobInset;
            float knobMinX = switchRect.x + radius;
            float knobMaxX = switchRect.z - radius;
            float knobX = knobMinX + (knobMaxX - knobMinX) * eased;
            float knobY = switchRect.y + radius;

            drawList->AddCircleFilled(ImVec2(knobX, knobY + 0.8f * inScale), knobRadius, alphaColor(ImColor(0, 0, 0, 55), moduleFade));
            drawList->AddCircleFilled(ImVec2(knobX, knobY), knobRadius, alphaColor(ImColor(250, 250, 255, 255), moduleFade));
            drawList->AddCircle(ImVec2(knobX, knobY), knobRadius, alphaColor(ImColor(0, 0, 0, 55), moduleFade), 0, 1.0f);
        }

            if (mod->cAnim > 0.01f && modRect.y + modHeaderHeight < contentRect.w && modRect.w > contentRect.y) {
                drawList->PushClipRect(ImVec2(modRect.x, std::max(modRect.y + modHeaderHeight, contentRect.y)), ImVec2(modRect.z, std::min(modRect.w, contentRect.w)), true);
                float setY = modRect.y + modHeaderHeight;

            for (auto& setting : mod->mSettings) {
                if (!setting->mIsVisible()) continue;

                float h = 0;
                if (setting->mType == SettingType::Bool) {
                    float descFont = 0.80f * inScale;
                    float descLineSpacing = 2.0f * inScale;
                    float descTopPad = 16.0f * inScale;
                    float descMaxWidth = ((modRect.z - 12.0f) - (modRect.x + 12.0f)) * 0.90f;
                    float descH = wrappedDescriptionHeight(setting, setting->mDescription, descFont, descMaxWidth, descLineSpacing);
                    float baseRowH = 28.0f * inScale;
                    float titlePad = 6.0f * inScale;
                    float bottomPad = 6.0f * inScale;
                    h = baseRowH;
                    if (descH > 0.0f) {
                        h = std::max(baseRowH, titlePad + descTopPad + descH + bottomPad);
                    }
                    auto s = reinterpret_cast<BoolSetting*>(setting);
                    ImVec4 sRect = ImVec4(modRect.x + 12, setY, modRect.z - 12, setY + h);

                    float titleY = sRect.y + 6.0f * inScale;
                    ImRenderUtils::drawText(ImVec2(sRect.x, titleY), s->mName, textMain, 0.95f * inScale, settingsAlpha, true);
                    if (!s->mDescription.empty()) {
                        float y = titleY + descTopPad;
                        const auto* wrap = getWrappedDescription(setting, s->mDescription, descFont, descMaxWidth);
                        for (const auto& ln : wrap->lines) {
                            ImRenderUtils::drawText(ImVec2(sRect.x, y), ln, textDim, descFont, settingsAlpha, true);
                            y += wrap->lineH + descLineSpacing;
                        }
                    }

                    if (isMouseOver(sRect) && isEnabled && isMouseOver(contentRect) && ImGui::IsMouseClicked(0)) {
                        s->mValue = !s->mValue;
                    }

                    setting->boolScale = MathUtils::animate(s->mValue ? 1.0f : 0.0f, setting->boolScale, deltaTime * 15.0f);

                    auto snap = [](float v) { return std::round(v * 2.0f) / 2.0f; };

                    float checkboxSize = 17.0f * inScale;
                    float centerY = sRect.y + (baseRowH / 2.0f);

                    ImVec4 checkboxRect = ImVec4(
                        sRect.z - checkboxSize,
                        centerY - checkboxSize / 2.0f,
                        sRect.z,
                        centerY + checkboxSize / 2.0f
                    );

                    checkboxRect.x = snap(checkboxRect.x);
                    checkboxRect.y = snap(checkboxRect.y);
                    checkboxRect.z = snap(checkboxRect.z);
                    checkboxRect.w = snap(checkboxRect.w);

                    ImColor checkboxBgOff = (uiTheme == 1) ? ImColor(236, 236, 242, 255) : ImColor(18, 18, 22, 255);
                    ImColor checkboxBorderOff = (uiTheme == 1) ? ImColor(206, 206, 218, 255) : ImColor(44, 44, 56, 255);
                    ImColor checkboxBorderOn = accentColor;
                    ImColor checkboxBorder = MathUtils::lerpImColor(checkboxBorderOff, checkboxBorderOn, setting->boolScale);
                    ImColor currentCheckboxBg = MathUtils::lerpImColor(checkboxBgOff, checkboxBorderOn, setting->boolScale);
                    int fadeAlpha = (int)(255 * settingsAlpha);
                    ImColor checkboxBorderF = ImColor((int)(checkboxBorder.Value.x * 255), (int)(checkboxBorder.Value.y * 255), (int)(checkboxBorder.Value.z * 255), fadeAlpha);
                    ImColor currentCheckboxBgF = ImColor((int)(currentCheckboxBg.Value.x * 255), (int)(currentCheckboxBg.Value.y * 255), (int)(currentCheckboxBg.Value.z * 255), fadeAlpha);

                    float rounding = 4.0f * inScale;
                    drawList->AddRectFilled(ImVec2(checkboxRect.x, checkboxRect.y), ImVec2(checkboxRect.z, checkboxRect.w), currentCheckboxBgF, rounding);
                    drawList->AddRect(ImVec2(checkboxRect.x, checkboxRect.y), ImVec2(checkboxRect.z, checkboxRect.w), checkboxBorderF, rounding, 0, 1.6f);

                    if (setting->boolScale > 0.01f) {
                        float pad = 4.6f * inScale;
                        float x0 = checkboxRect.x + pad;
                        float y0 = checkboxRect.y + pad;
                        float x1 = checkboxRect.z - pad;
                        float y1 = checkboxRect.w - pad;

                        ImVec2 p1 = ImVec2(snap(x0 + (x1 - x0) * 0.10f), snap(y0 + (y1 - y0) * 0.55f));
                        ImVec2 p2 = ImVec2(snap(x0 + (x1 - x0) * 0.38f), snap(y0 + (y1 - y0) * 0.82f));
                        ImVec2 p3 = ImVec2(snap(x0 + (x1 - x0) * 0.92f), snap(y0 + (y1 - y0) * 0.20f));

                        ImVec2 pts[3] = { p1, p2, p3 };
                        ImColor checkmarkColor = ImColor(255, 255, 255, (int)(255 * setting->boolScale * settingsAlpha));
                        drawList->AddPolyline(pts, 3, checkmarkColor, 0, 1.55f * inScale);
                    }
                }
                else if (setting->mType == SettingType::Number) {
                    float descFont = 0.80f * inScale;
                    float descLineSpacing = 2.0f * inScale;
                    float descTopPad = 16.0f * inScale;
                    float descMaxWidth = ((modRect.z - 12.0f) - (modRect.x + 12.0f)) * 0.90f;
                    float descH = wrappedDescriptionHeight(setting, setting->mDescription, descFont, descMaxWidth, descLineSpacing);
                    float descExtra = (descH > 0.0f) ? (descTopPad + descH) : 0.0f;
                    h = (48.0f * inScale) + descExtra;
                    auto s = reinterpret_cast<NumberSetting*>(setting);
                    ImVec4 sRect = ImVec4(modRect.x + 12, setY, modRect.z - 12, setY + h);

                    ImRenderUtils::drawText(ImVec2(sRect.x, sRect.y + 5), s->mName, textMain, 0.95f * inScale, settingsAlpha, true);
                    if (!s->mDescription.empty()) {
                        float y = (sRect.y + 5.0f * inScale) + descTopPad;
                        const auto* wrap = getWrappedDescription(setting, s->mDescription, descFont, descMaxWidth);
                        for (const auto& ln : wrap->lines) {
                            ImRenderUtils::drawText(ImVec2(sRect.x, y), ln, textDim, descFont, settingsAlpha, true);
                            y += wrap->lineH + descLineSpacing;
                        }
                    }

                    char valStr[32]; sprintf(valStr, "%.2f units", s->mValue);
                    std::string valString = std::string(valStr);
                    ImRenderUtils::drawText(ImVec2(sRect.z - ImRenderUtils::getTextWidth(&valString, 0.9f) - 2, sRect.y + 5), valStr, accentColor, 0.9f * inScale, settingsAlpha, true);

                    float sliderH = 6.0f * inScale;
                    float sliderY = sRect.y + 24.0f * inScale + descExtra;
                    ImVec4 track = ImVec4(sRect.x, sliderY, sRect.z, sliderY + sliderH);

                    float sliderR = sliderH * 0.5f;
                    ImColor trackBg = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(255 * settingsAlpha)) : ImColor(24, 24, 29, (int)(255 * settingsAlpha));
                    drawList->AddRectFilled(ImVec2(track.x, track.y), ImVec2(track.z, track.w), trackBg, sliderR);

                    float sliderLen = track.z - track.x;
                    float ratio = (s->mValue - s->mMin) / (s->mMax - s->mMin);
                    setting->sliderEase = MathUtils::animate(ratio, setting->sliderEase, deltaTime * 15.0f);

                    ImColor accentFade = ImColor((int)(accentColor.Value.x * 255), (int)(accentColor.Value.y * 255), (int)(accentColor.Value.z * 255), (int)(255 * settingsAlpha));
                    drawList->AddRectFilled(ImVec2(track.x, track.y), ImVec2(track.x + (sliderLen * setting->sliderEase), track.w), accentFade, sliderR);

                    if (isMouseOver(ImVec4(sRect.x, sRect.y + 15, sRect.z, sRect.w)) && isMouseOver(contentRect) && ImGui::IsMouseDown(0) && isEnabled) {
                        float mouseX = ImGui::GetIO().MousePos.x;
                        float newVal = s->mMin + ((mouseX - sRect.x) / sliderLen) * (s->mMax - s->mMin);
                        s->mValue = std::clamp(newVal, s->mMin, s->mMax);
                    }
                }
                else if (setting->mType == SettingType::Enum) {
                    auto s = setting->asEnumSettingBase();
                    if (!s) {
                        h = 30.0f * inScale;
                        ImVec4 sRect = ImVec4(modRect.x + 12, setY, modRect.z - 12, setY + h);
                        ImRenderUtils::drawText(ImVec2(sRect.x, sRect.y + 5), setting->mName, textMain, 0.95f * inScale, settingsAlpha, true);
                        setY += h;
                        continue;
                    }
                    const auto& enumValues = s->getValues();
                    float headerH = 28.0f * inScale;
                    float dropH = 24.0f * inScale;
                    float descFont = 0.80f * inScale;
                    float descLineSpacing = 2.0f * inScale;
                    float descPadTop = 6.0f * inScale;
                    float descPadBottom = 6.0f * inScale;

                    float& animRef = enumDropdownAnims[setting];
                    float target = (openEnumSetting == setting) ? 1.0f : 0.0f;
                    animRef = MathUtils::animate(target, animRef, deltaTime * 14.0f);
                    float anim = animRef;

                    float sX = modRect.x + 12;
                    float sY = setY;
                    float sZ = modRect.z - 12;
                    float fullW = sZ - sX;

                    float labelW = ImRenderUtils::getTextWidth(&s->mName, 0.95f * inScale);
                    float minGap = 12.0f * inScale;
                    float desiredW = fullW * 0.5f;
                    float dropW = std::clamp(std::max(128.0f * inScale, desiredW), 0.0f, fullW);
                    float dropX = sZ - dropW;
                    float minX = sX + labelW + minGap;
                    if (dropX < minX) {
                        dropX = minX;
                        dropW = sZ - dropX;
                    }

                    float dropY = sY + (headerH - dropH) * 0.5f;
                    ImVec4 dropRect = ImVec4(dropX, dropY, dropX + dropW, dropY + dropH);

                    float descMaxWidthFull = ((modRect.z - 12.0f) - (modRect.x + 12.0f)) * 0.90f;
                    float descMaxWidth = std::min(descMaxWidthFull, std::max(0.0f, (dropRect.x - sX) - 2.0f * inScale));
                    if (descMaxWidth < 90.0f * inScale) descMaxWidth = descMaxWidthFull;
                    float descH = wrappedDescriptionHeight(setting, setting->mDescription, descFont, descMaxWidth, descLineSpacing);

                    float baseH = headerH + ((descH > 0.0f) ? (descPadTop + descH + descPadBottom) : 0.0f);
                    h = baseH;
                    ImVec4 sRect = ImVec4(sX, sY, sZ, sY + h);

                    float titleY = sRect.y + 5.0f * inScale;
                    ImVec2 titleClipMin = ImVec2(sRect.x, sRect.y);
                    ImVec2 titleClipMax = ImVec2(std::max(sRect.x, dropRect.x - minGap), sRect.y + headerH);
                    if (titleClipMax.x > titleClipMin.x + 2.0f) {
                        drawList->PushClipRect(titleClipMin, titleClipMax, true);
                        ImRenderUtils::drawText(ImVec2(sRect.x, titleY), s->mName, textMain, 0.95f * inScale, settingsAlpha, true);
                        drawList->PopClipRect();
                    }

                    bool dropHovered = isMouseOver(dropRect) && isMouseOver(contentRect) && (isEnabled || enumModalActive);
                    float borderT = std::clamp(anim + (dropHovered ? 0.35f : 0.0f), 0.0f, 1.0f);
                    ImColor dropBgOff = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(255 * settingsAlpha)) : ImColor(18, 18, 22, (int)(255 * settingsAlpha));
                    ImColor dropBgHover = (uiTheme == 1) ? ImColor(228, 228, 238, (int)(255 * settingsAlpha)) : ImColor(22, 22, 28, (int)(255 * settingsAlpha));
                    ImColor dropBg = dropHovered ? dropBgHover : dropBgOff;
                    ImColor dropBorder = (uiTheme == 1) ? ImColor(206, 206, 218, (int)(255 * settingsAlpha)) : ImColor(46, 46, 58, (int)(255 * settingsAlpha));

                    float dropRounding = 7.0f * inScale;
                    drawList->AddRectFilled(
                        ImVec2(dropRect.x, dropRect.y + 2.0f * inScale),
                        ImVec2(dropRect.z, dropRect.w + 2.0f * inScale),
                        ImColor(0, 0, 0, (int)(70.0f * settingsAlpha)),
                        dropRounding
                    );
                    drawList->AddRectFilled(ImVec2(dropRect.x, dropRect.y), ImVec2(dropRect.z, dropRect.w), dropBg, dropRounding);
                    drawList->AddRect(ImVec2(dropRect.x, dropRect.y), ImVec2(dropRect.z, dropRect.w), dropBorder, dropRounding, 0, 1.2f);

                    int currentIndex = s->getIndex();
                    if (currentIndex < 0) currentIndex = 0;
                    if (!enumValues.empty() && currentIndex >= (int)enumValues.size()) currentIndex = (int)enumValues.size() - 1;
                    std::string currentValue = enumValues.empty() ? std::string() : enumValues[currentIndex];
                    float valueFont = 0.90f * inScale;
                    float valueY = std::floor(dropRect.y + (dropH - ImRenderUtils::getTextHeightStr(&currentValue, valueFont)) * 0.5f);
                    ImVec2 valueClipMin = ImVec2(dropRect.x + 10.0f * inScale, dropRect.y);
                    ImVec2 valueClipMax = ImVec2(dropRect.z - 26.0f * inScale, dropRect.w);
                    drawList->PushClipRect(valueClipMin, valueClipMax, true);
                    ImRenderUtils::drawText(ImVec2(dropRect.x + 10.0f * inScale, valueY), currentValue, textDim, valueFont, settingsAlpha, true);
                    drawList->PopClipRect();

                    float chevronCX = std::floor(dropRect.z - 14.0f * inScale) + 0.5f;
                    float chevronCY = std::floor(dropRect.y + dropH * 0.5f) + 0.5f;
                    ImVec2 chevronC = ImVec2(chevronCX, chevronCY);
                    float chevronW = 4.0f * inScale;
                    float chevronH = 2.2f * inScale;
                    ImVec2 pts[3] = {
                        ImVec2(chevronC.x - chevronW, chevronC.y - chevronH),
                        ImVec2(chevronC.x, chevronC.y + chevronH),
                        ImVec2(chevronC.x + chevronW, chevronC.y - chevronH)
                    };
                    ImColor chevronCol = ImColor(textDim.Value.x, textDim.Value.y, textDim.Value.z, std::clamp(0.65f + 0.35f * borderT, 0.0f, 1.0f) * settingsAlpha);
                    drawList->AddPolyline(pts, 3, ImGui::ColorConvertFloat4ToU32(chevronCol.Value), 0, 1.6f * inScale);

                    if (dropHovered && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
                        enumClickConsumed = true;
                        openEnumSetting = (openEnumSetting == setting) ? nullptr : setting;
                    }

                    float contentBottomY = sRect.y + headerH;
                    if (!s->mDescription.empty()) {
                        float y = (sRect.y + headerH) + descPadTop;
                        const auto* wrap = getWrappedDescription(setting, s->mDescription, descFont, descMaxWidth);
                        ImVec2 clipMin = ImVec2(sRect.x, y);
                        ImVec2 clipMax = ImVec2(sRect.x + descMaxWidth, y + descH + 2.0f * inScale);
                        drawList->PushClipRect(clipMin, clipMax, true);
                        for (const auto& ln : wrap->lines) {
                            ImRenderUtils::drawText(ImVec2(sRect.x, y), ln, textDim, descFont, settingsAlpha, true);
                            y += wrap->lineH + descLineSpacing;
                        }
                        drawList->PopClipRect();
                        contentBottomY = y + descPadBottom;
                    }

                    if (anim > 0.01f) {
                        pendingEnumSetting = setting;
                        pendingEnum = s;
                        pendingEnumDropRect = dropRect;
                        pendingEnumDropBorder = dropBorder;
                        pendingEnumAnim = anim;
                        pendingEnumSettingsAlpha = settingsAlpha;
                        pendingEnumDropW = dropW;
                    }
                }
                else if (setting->mType == SettingType::Color)
                {
                    auto s = reinterpret_cast<ColorSetting*>(setting);

                    float descFont = 0.80f * inScale;
                    float descLineSpacing = 2.0f * inScale;
                    float descTopPad = 16.0f * inScale;
                    float descMaxWidth = ((modRect.z - 12.0f) - (modRect.x + 12.0f)) * 0.90f;
                    float descH = wrappedDescriptionHeight(setting, setting->mDescription, descFont, descMaxWidth, descLineSpacing);
                    float descExtra = (descH > 0.0f) ? (descTopPad + descH) : 0.0f;

                    float pickerTarget = (displayColorPicker && lastColorSetting == s) ? 1.0f : 0.0f;
                    s->mSlide = MathUtils::animate(pickerTarget, s->mSlide, deltaTime * 14.0f);

                    float baseH = (52.0f * inScale) + descExtra;
                    float pickerGap = 8.0f * inScale;
                    float anim = std::clamp(s->mSlide, 0.0f, 1.0f);

                    float maxPickerW = (modRect.z - 12.0f) - (modRect.x + 12.0f);
                    float pickerW = std::min(colorPickerWidth * inScale, maxPickerW);
                    float barsW = ImGui::GetFrameHeight();
                    float pickerH = ImMax(barsW, pickerW - 2.0f * (barsW + ImGui::GetStyle().ItemInnerSpacing.x));

                    h = baseH + ((pickerH + pickerGap) * anim);

                    ImVec4 sRect = ImVec4(modRect.x + 12, setY, modRect.z - 12, setY + h);

                    ImRenderUtils::drawText(ImVec2(sRect.x, sRect.y + 5), s->mName, textMain, 0.95f * inScale, settingsAlpha, true);
                    if (!s->mDescription.empty()) {
                        float y = (sRect.y + 5.0f * inScale) + descTopPad;
                        const auto* wrap = getWrappedDescription(setting, s->mDescription, descFont, descMaxWidth);
                        for (const auto& ln : wrap->lines) {
                            ImRenderUtils::drawText(ImVec2(sRect.x, y), ln, textDim, descFont, settingsAlpha, true);
                            y += wrap->lineH + descLineSpacing;
                        }
                    }

                    float rowY = sRect.y + 24.0f * inScale + descExtra;
                    float rowH = 24.0f * inScale;
                    ImVec4 rowRect = ImVec4(sRect.x, rowY, sRect.z, rowY + rowH);

                    ImColor rowBg = (uiTheme == 1) ? ImColor(236, 236, 242, (int)(255 * settingsAlpha)) : ImColor(24, 24, 29, (int)(255 * settingsAlpha));
                    ImColor rowBorderOff = (uiTheme == 1) ? ImColor(206, 206, 218, 255) : ImColor(46, 46, 58, 255);
                    ImColor rowBorderLerp = MathUtils::lerpImColor(rowBorderOff, accentColor, anim);
                    ImColor rowBorder = ImColor((int)(rowBorderLerp.Value.x * 255), (int)(rowBorderLerp.Value.y * 255), (int)(rowBorderLerp.Value.z * 255), (int)(255 * settingsAlpha));
                    drawList->AddRectFilled(ImVec2(rowRect.x, rowRect.y), ImVec2(rowRect.z, rowRect.w), rowBg, 6.0f);
                    drawList->AddRect(ImVec2(rowRect.x, rowRect.y), ImVec2(rowRect.z, rowRect.w), rowBorder, 6.0f, 0, 1.6f);

                    float swatch = 14.0f * inScale;
                    float padX = 8.0f * inScale;
                    ImVec4 swatchRect = ImVec4(rowRect.z - padX - swatch, rowRect.y + (rowH - swatch) / 2.0f, rowRect.z - padX, rowRect.y + (rowH + swatch) / 2.0f);
                    ImColor col = s->getAsImColor();
                    col.Value.w = std::clamp(col.Value.w, 0.0f, 1.0f);
                    drawList->AddRectFilled(ImVec2(swatchRect.x, swatchRect.y), ImVec2(swatchRect.z, swatchRect.w), ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w * settingsAlpha), 4.0f);
                    drawList->AddRect(ImVec2(swatchRect.x, swatchRect.y), ImVec2(swatchRect.z, swatchRect.w), ImColor(255, 255, 255, (int)(40 * settingsAlpha)), 4.0f, 0, 1.2f);

                    std::string label = (displayColorPicker && lastColorSetting == s) ? "Color" : "Pick color";
                    ImRenderUtils::drawText(ImVec2(rowRect.x + 8.0f * inScale, rowRect.y + 5.0f * inScale), label, textDim, 0.9f * inScale, settingsAlpha, true);

                    if (isMouseOver(rowRect) && isEnabled && isMouseOver(contentRect) && ImGui::IsMouseClicked(0))
                    {
                        colorClickConsumed = true;
                        if (displayColorPicker && lastColorSetting == s)
                        {
                            displayColorPicker = false;
                            lastColorSetting = nullptr;
                        }
                        else
                        {
                            displayColorPicker = true;
                            lastColorSetting = s;
                        }
                    }

                    if (anim > 0.01f)
                    {
                        ImVec4 pickerRectFull = ImVec4(rowRect.z - pickerW, rowRect.w + pickerGap, rowRect.z, rowRect.w + pickerGap + pickerH);
                        ImVec4 pickerRect = ImVec4(pickerRectFull.x, pickerRectFull.y, pickerRectFull.z, pickerRectFull.y + (pickerH * anim));

                        ImColor pickerBg = ImColor(15, 15, 20, (int)(235.0f * anim * settingsAlpha));
                        ImColor pickerBorder = ImColor(rowBorder.Value.x, rowBorder.Value.y, rowBorder.Value.z, 0.85f * anim * settingsAlpha);
                        float pickerRounding = 11.0f * inScale;
                        drawList->AddRectFilled(ImVec2(pickerRect.x, pickerRect.y), ImVec2(pickerRect.z, pickerRect.w), pickerBg, pickerRounding);
                        drawList->AddRect(ImVec2(pickerRect.x, pickerRect.y), ImVec2(pickerRect.z, pickerRect.w), pickerBorder, pickerRounding, 0, 1.4f);

                        openColorButtonRect = rowRect;
                        openColorPickerRect = pickerRectFull;

                        if (!displayColorPicker || lastColorSetting != s)
                            resizingColorPicker = false;

                        ImVec2 mp = ImGui::GetIO().MousePos;
                        float gripSz = 12.0f * inScale;
                        ImVec4 gripRect = ImVec4(pickerRectFull.z - gripSz, pickerRectFull.w - gripSz, pickerRectFull.z, pickerRectFull.w);
                        bool gripHovered = isMouseOver(gripRect) && isEnabled && isMouseOver(contentRect);
                        drawList->AddRectFilled(ImVec2(gripRect.x, gripRect.y), ImVec2(gripRect.z, gripRect.w), ImColor(255, 255, 255, (int)(12.0f * anim * settingsAlpha)), 3.0f * inScale);

                        if (gripHovered && ImGui::IsMouseClicked(0))
                        {
                            resizingColorPicker = true;
                            colorPickerResizeStartMouse = mp;
                            colorPickerResizeStartWidth = colorPickerWidth;
                            colorClickConsumed = true;
                        }

                        if (resizingColorPicker && displayColorPicker && lastColorSetting == s)
                        {
                            if (ImGui::IsMouseDown(0))
                            {
                                float dx = (mp.x - colorPickerResizeStartMouse.x) / std::max(0.001f, std::abs(inScale));
                                colorPickerWidth = std::clamp(colorPickerResizeStartWidth + dx, 150.0f, 320.0f);
                                colorClickConsumed = true;
                            }
                            else
                            {
                                resizingColorPicker = false;
                            }
                        }

                        if (isMouseOver(pickerRectFull) && isEnabled && isMouseOver(contentRect) && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)))
                        {
                            colorClickConsumed = true;
                        }

                        if (anim > 0.65f)
                        {
                            ImGuiWindowFlags pickerFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

                            float yOff = 2.0f * inScale;
                            ImGui::SetNextWindowPos(ImVec2(pickerRect.x, pickerRect.y + yOff));
                            ImGui::SetNextWindowSize(ImVec2((pickerRect.z - pickerRect.x), (pickerRect.w - pickerRect.y) - yOff));
                            ImGui::Begin("##modern_color_picker", nullptr, pickerFlags);
                            ImGui::PushID(setting);
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 11.0f * inScale);
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * inScale, 6.0f * inScale));

                            ImGui::SetNextItemWidth(-1.0f);
                            ImGui::ColorPicker4(
                                "##picker",
                                s->mValue,
                                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview
                            );

                            ImGui::PopStyleVar(3);
                            ImGui::PopID();
                            ImGui::End();
                        }
                    }
                }

                setY += h;
            }
            drawList->PopClipRect();
        }

        float totalH = currentModHeight + 10.0f * inScale;
        if (targetCol == 0) {
            col1Y += totalH;
            heightCol1 += totalH;
        }
        else if (targetCol == 1) {
            col2Y += totalH;
            heightCol2 += totalH;
        }
        else {
            col3Y += totalH;
            heightCol3 += totalH;
        }
    }
    }

    float maxHeight = std::max({ heightCol1, heightCol2, heightCol3 });
    maxScrollY = std::max(0.0f, maxHeight - (contentH - 20.0f));

    if (pendingModuleDescModule != nullptr && !pendingModuleDescText.empty() && pendingModuleDescAnim > 0.01f)
    {
        float t = std::clamp(pendingModuleDescAnim, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float panelAlpha = std::sqrt(eased) * pendingModuleDescFade;
        float scale = MathUtils::lerp(0.92f, 1.0f, eased) * pendingModuleDescScale;

        float baseFont = 0.78f * inScale;
        float lineSpacing = 2.0f * inScale;
        float padX = 10.0f * inScale;
        float padY = 8.0f * inScale;
        float desiredW = 240.0f * inScale;

        float edgePad = 6.0f * inScale;
        float gap = 8.0f * inScale;
        float spaceRight = (contentRect.z - edgePad) - (pendingModuleDescIconRect.z + gap);
        float spaceLeft = (pendingModuleDescIconRect.x - gap) - (contentRect.x + edgePad);
        bool useRight = spaceRight >= spaceLeft;

        float panelW = desiredW;
        float maxSideW = std::max(0.0f, useRight ? spaceRight : spaceLeft);
        if (maxSideW > 0.0f) panelW = std::min(panelW, maxSideW);
        panelW = std::max(panelW, 140.0f * inScale);
        panelW = std::min(panelW, (contentRect.z - contentRect.x) - edgePad * 2.0f);

        float textMaxW = std::max(1.0f, panelW - padX * 2.0f);
        const Setting* wrapKey = reinterpret_cast<const Setting*>(pendingModuleDescModule);
        const auto* wrap = getWrappedDescription(wrapKey, pendingModuleDescText, baseFont, textMaxW);
        float textH = 0.0f;
        if (wrap && !wrap->lines.empty())
        {
            textH = (wrap->lineH * (float)wrap->lines.size()) + (lineSpacing * (float)(std::max(0, (int)wrap->lines.size() - 1)));
        }
        float panelH = std::max(0.0f, (padY * 2.0f) + textH);

        float panelX = useRight ? (pendingModuleDescIconRect.z + gap) : (pendingModuleDescIconRect.x - gap - panelW);
        float panelY = pendingModuleDescIconRect.y - 2.0f * inScale;

        if (panelX + panelW > contentRect.z - edgePad) panelX = (contentRect.z - edgePad) - panelW;
        if (panelX < contentRect.x + edgePad) panelX = contentRect.x + edgePad;
        if (panelY + panelH > contentRect.w - edgePad) panelY = (contentRect.w - edgePad) - panelH;
        if (panelY < contentRect.y + edgePad) panelY = contentRect.y + edgePad;

        float cx = panelX + panelW * 0.5f;
        float cy = panelY + panelH * 0.5f;
        ImVec4 panelRect = ImVec4(
            cx - (panelW * scale) * 0.5f,
            cy - (panelH * scale) * 0.5f,
            cx + (panelW * scale) * 0.5f,
            cy + (panelH * scale) * 0.5f
        );
        {
            float minX = contentRect.x + edgePad;
            float maxX = contentRect.z - edgePad;
            float minY = contentRect.y + edgePad;
            float maxY = contentRect.w - edgePad;
            float dx = 0.0f;
            float dy = 0.0f;
            if (panelRect.x < minX) dx = minX - panelRect.x;
            if (panelRect.z > maxX) dx = maxX - panelRect.z;
            if (panelRect.y < minY) dy = minY - panelRect.y;
            if (panelRect.w > maxY) dy = maxY - panelRect.w;
            panelRect.x += dx;
            panelRect.z += dx;
            panelRect.y += dy;
            panelRect.w += dy;
        }

        float r = 10.0f * inScale * scale;
        drawList->PushClipRect(ImVec2(contentRect.x, contentRect.y), ImVec2(contentRect.z, contentRect.w), true);
        drawList->AddRectFilled(
            ImVec2(panelRect.x, panelRect.y + 3.0f * inScale),
            ImVec2(panelRect.z, panelRect.w + 3.0f * inScale),
            ImColor(0, 0, 0, (int)(95.0f * panelAlpha)),
            r
        );
        ImColor panelBg = ColorUtils::getUiCardColor(panelAlpha);
        ImColor panelBorder = ColorUtils::getUiBorderColor(panelAlpha * (25.0f / 255.0f));
        drawList->AddRectFilled(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), panelBg, r);
        drawList->AddRect(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), panelBorder, r, 0, 1.2f);

        if (wrap && !wrap->lines.empty())
        {
            float textX = panelRect.x + (padX * scale);
            float textY = panelRect.y + (padY * scale);
            float font = baseFont * scale;
            float spacing = lineSpacing * scale;
            ImVec2 clipMin = ImVec2(panelRect.x + (padX * scale), panelRect.y + (padY * scale));
            ImVec2 clipMax = ImVec2(panelRect.z - (padX * scale), panelRect.w - (padY * scale));
            drawList->PushClipRect(clipMin, clipMax, true);
            for (size_t i = 0; i < wrap->lines.size(); i++)
            {
                ImRenderUtils::drawText(ImVec2(textX, textY), wrap->lines[i], textDim, font, panelAlpha, true);
                textY += (wrap->lineH * scale);
                if (i + 1 < wrap->lines.size()) textY += spacing;
            }
            drawList->PopClipRect();
        }
        drawList->PopClipRect();
    }

    if (pendingEnumSetting != nullptr && pendingEnum != nullptr && pendingEnumAnim > 0.01f)
    {
        static std::unordered_map<size_t, float> enumOptionAnims;

        float panelGap = 8.0f * inScale;
        float listPadding = 4.0f * inScale;
        float rowH = 22.0f * inScale;
        const auto& pendingEnumValues = pendingEnum->getValues();
        int pendingEnumIndex = pendingEnum->getIndex();
        if (pendingEnumIndex < 0) pendingEnumIndex = 0;
        if (!pendingEnumValues.empty() && pendingEnumIndex >= (int)pendingEnumValues.size()) pendingEnumIndex = (int)pendingEnumValues.size() - 1;
        float listH = (rowH * (float)pendingEnumValues.size()) + (listPadding * 2.0f);

        float panelW = std::max(170.0f * inScale, pendingEnumDropW);
        float panelX = pendingEnumDropRect.z + panelGap;
        float panelY = pendingEnumDropRect.y;

        if (panelX + panelW > contentRect.z - 6.0f * inScale) {
            float leftX = pendingEnumDropRect.x - panelGap - panelW;
            if (leftX >= contentRect.x + 6.0f * inScale) {
                panelX = leftX;
                panelY = pendingEnumDropRect.y;
            }
            else {
                panelX = pendingEnumDropRect.x;
                panelY = pendingEnumDropRect.w + panelGap;
            }
        }
        if (panelY + listH > contentRect.w - 6.0f * inScale) {
            panelY = pendingEnumDropRect.y - listH - panelGap;
        }
        panelY = std::clamp(panelY, contentRect.y + 6.0f * inScale, (contentRect.w - 6.0f * inScale) - listH);

        float t = std::clamp(pendingEnumAnim, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float panelAlpha = std::sqrt(eased) * pendingEnumSettingsAlpha;
        float scale = MathUtils::lerp(0.92f, 1.0f, eased);

        float listPaddingS = listPadding * scale;
        float rowHS = rowH * scale;

        float listHS = listH * scale;
        float panelWS = panelW * scale;
        float cx = panelX + panelW * 0.5f;
        float cy = panelY + listH * 0.5f;
        ImVec4 panelRect = ImVec4(cx - panelWS * 0.5f, cy - listHS * 0.5f, cx + panelWS * 0.5f, cy + listHS * 0.5f);

        float r = 8.0f * inScale * scale;
        drawList->PushClipRect(ImVec2(contentRect.x, contentRect.y), ImVec2(contentRect.z, contentRect.w), true);
        drawList->AddRectFilled(
            ImVec2(panelRect.x, panelRect.y + 3.0f * inScale),
            ImVec2(panelRect.z, panelRect.w + 3.0f * inScale),
            ImColor(0, 0, 0, (int)(95.0f * panelAlpha)),
            r
        );
        ImColor panelBg = ColorUtils::getUiCardColor(panelAlpha);
        ImColor panelBorder = ImColor(
            pendingEnumDropBorder.Value.x,
            pendingEnumDropBorder.Value.y,
            pendingEnumDropBorder.Value.z,
            0.95f * panelAlpha
        );
        drawList->AddRectFilled(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), panelBg, r);
        drawList->AddRect(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), panelBorder, r, 0, 1.2f);

        drawList->PushClipRect(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), true);
        for (int idx = 0; idx < (int)pendingEnumValues.size(); idx++)
        {
            float optY1 = panelRect.y + listPaddingS + (rowHS * (float)idx);
            float optY2 = optY1 + rowHS;
            ImVec4 optRect = ImVec4(panelRect.x + listPaddingS, optY1, panelRect.z - listPaddingS, optY2);

            bool optHovered = isMouseOver(optRect) && isEnabledRaw && isMouseOver(contentRect);
            bool optSelected = (idx == pendingEnumIndex);

            size_t key = (reinterpret_cast<size_t>(pendingEnumSetting) >> 4) ^ (size_t)(idx * 0x9E3779B185EBCA87ULL);
            float& selAnim = enumOptionAnims[key];
            selAnim = MathUtils::animate(optSelected ? 1.0f : 0.0f, selAnim, deltaTime * 12.0f);

            float hoverA = optHovered ? 1.0f : 0.0f;
            float bgT = std::clamp((0.55f * selAnim) + (0.35f * hoverA), 0.0f, 1.0f);
            if (bgT > 0.001f) {
                ImColor bg = ImColor(
                    accentColor.Value.x,
                    accentColor.Value.y,
                    accentColor.Value.z,
                    (0.12f * bgT) * panelAlpha
                );
                drawList->AddRectFilled(ImVec2(optRect.x, optRect.y), ImVec2(optRect.z, optRect.w), bg, 7.0f * inScale);
            }

            float dotX = optRect.x + 12.0f * inScale * scale + ((1.0f - selAnim) * -6.0f * inScale * scale);
            float dotY = (optRect.y + optRect.w) * 0.5f;
            ImVec2 c = ImVec2(dotX, dotY);
            float rad = 5.2f * inScale * scale;
            drawList->AddCircleFilled(c, rad, ImColor(10, 10, 14, (int)(140.0f * panelAlpha)));
            if (selAnim > 0.001f) {
                ImColor dot = ImColor(
                    accentColor.Value.x,
                    accentColor.Value.y,
                    accentColor.Value.z,
                    (0.95f * selAnim) * panelAlpha
                );
                drawList->AddCircleFilled(c, rad * 0.55f, dot);
            }

            float textShift = 10.0f * inScale * scale * selAnim;
            float textX = optRect.x + 22.0f * inScale * scale + textShift;
            ImColor optText = optSelected ? accentColor : textDim;
            ImRenderUtils::drawText(ImVec2(textX, optRect.y + 2.0f * inScale * scale), pendingEnumValues[idx], optText, 0.88f * inScale * scale, panelAlpha, true);

            if (optHovered && ImGui::IsMouseClicked(0)) {
                enumClickConsumed = true;
                pendingEnum->setIndex(idx);
                pendingEnumIndex = idx;
            }
        }
        drawList->PopClipRect();

        if (isMouseOver(panelRect) && isEnabledRaw && isMouseOver(contentRect) && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
            enumClickConsumed = true;
        }

        drawList->PopClipRect();
    }

    if (openEnumSetting != nullptr && isEnabledRaw && (enumModalActive || isEnabled) && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !enumClickConsumed) {
        openEnumSetting = nullptr;
    }
    if (displayColorPicker && lastColorSetting != nullptr && isEnabled && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !colorClickConsumed) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool overBtn = (mp.x >= openColorButtonRect.x && mp.y >= openColorButtonRect.y && mp.x < openColorButtonRect.z && mp.y < openColorButtonRect.w);
        bool overPicker = (mp.x >= openColorPickerRect.x && mp.y >= openColorPickerRect.y && mp.x < openColorPickerRect.z && mp.y < openColorPickerRect.w);
        if (!overBtn && !overPicker)
        {
            displayColorPicker = false;
            lastColorSetting = nullptr;
        }
    }

    if (isCategorySwitching) {
        float overlayA = 1.0f - categoryFade;
        if (overlayA > 0.001f) {
            drawList->AddRectFilled(ImVec2(contentRect.x, contentRect.y), ImVec2(contentRect.z, contentRect.w), ImColor(bgColor.Value.x, bgColor.Value.y, bgColor.Value.z, overlayA), 0.0f);
        }
    }

    drawList->PopClipRect();

    if (themePickerAnim > 0.001f) {
        float t = std::clamp(themePickerAnim, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float alpha = std::sqrt(eased);
        float scale = MathUtils::lerp(0.92f, 1.0f, eased);

        float panelW = themePickerWidth * inScale;
        float barsW = ImGui::GetFrameHeight();
        float panelH = ImMax(barsW, panelW - 2.0f * (barsW + ImGui::GetStyle().ItemInnerSpacing.x));

        float panelX = themeBtnRect.x;
        float panelY = themeBtnRect.w + 8.0f * inScale;

        float minX = mainRect.x + 12.0f * inScale;
        float maxX = mainRect.z - 12.0f * inScale - panelW;
        panelX = std::clamp(panelX, minX, maxX);

        float maxY = mainRect.w - 12.0f * inScale - panelH;
        if (panelY > maxY) {
            panelY = themeBtnRect.y - 8.0f * inScale - panelH;
        }

        float cx = panelX + panelW / 2.0f;
        float cy = panelY + panelH / 2.0f;
        float w = panelW * scale;
        float h = panelH * scale;
        ImVec4 panelRect = ImVec4(cx - w / 2.0f, cy - h / 2.0f, cx + w / 2.0f, cy + h / 2.0f);

        ImColor pickerBg = ImColor(15, 15, 20, (int)(235.0f * alpha));
        ImColor pickerBorder = ImColor(58, 58, 74, (int)(200.0f * alpha));
        float pickerRounding = 11.0f * inScale;
        drawList->AddRectFilled(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), pickerBg, pickerRounding);
        drawList->AddRect(ImVec2(panelRect.x, panelRect.y), ImVec2(panelRect.z, panelRect.w), pickerBorder, pickerRounding, 0, 1.4f);

        if (!isThemePickerOpen)
            resizingThemePicker = false;

        ImVec2 mp = ImGui::GetIO().MousePos;
        float gripSz = 12.0f * inScale;
        ImVec4 gripRect = ImVec4(panelRect.z - gripSz, panelRect.w - gripSz, panelRect.z, panelRect.w);
        bool gripHovered = isMouseOver(gripRect) && isEnabledRaw;
        drawList->AddRectFilled(ImVec2(gripRect.x, gripRect.y), ImVec2(gripRect.z, gripRect.w), ImColor(255, 255, 255, (int)(12.0f * alpha)), 3.0f * inScale);

        if (gripHovered && ImGui::IsMouseClicked(0))
        {
            resizingThemePicker = true;
            themePickerResizeStartMouse = mp;
            themePickerResizeStartWidth = themePickerWidth;
        }

        if (resizingThemePicker && isThemePickerOpen)
        {
            if (ImGui::IsMouseDown(0))
            {
                float dx = (mp.x - themePickerResizeStartMouse.x) / std::max(0.001f, std::abs(inScale));
                themePickerWidth = std::clamp(themePickerResizeStartWidth + dx, 150.0f, 320.0f);
            }
            else
            {
                resizingThemePicker = false;
            }
        }

        if (isThemePickerOpen && themePickerAnim > 0.65f && isEnabledRaw)
        {
            ImGuiWindowFlags pickerFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

            ImVec2 winPos = ImVec2(panelRect.x, panelRect.y);
            ImVec2 winSize = ImVec2((panelRect.z - panelRect.x), (panelRect.w - panelRect.y));

            float yOff = 2.0f * inScale;
            ImGui::SetNextWindowPos(ImVec2(winPos.x, winPos.y + yOff));
            ImGui::SetNextWindowSize(ImVec2(winSize.x, winSize.y - yOff));
            ImGui::Begin("##theme_color_picker", nullptr, pickerFlags);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 11.0f * inScale);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * inScale, 6.0f * inScale));

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::ColorPicker4(
                "##picker",
                customThemeColor,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview
            );

            ImGui::PopStyleVar(3);
            ImGui::End();
        }
    }

    if (pushedComfortaa) ImGui::PopFont();
}

void ModernGui::onWindowResizeEvent(WindowResizeEvent& event)
{
    (void)event;
    std::scoped_lock<std::mutex> lk(uiMutex);
    mWrappedDescCache.clear();
    mWrappedDescUseCounter = 0;

    {
        std::lock_guard<std::mutex> lock(mDescMutex);
        mDescRequested.clear();
        mDescReady.clear();
        mDescUseCounter = 0;
    }
}
