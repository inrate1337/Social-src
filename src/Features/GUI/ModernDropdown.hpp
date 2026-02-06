#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <Features/FeatureManager.hpp>
#include <Features/Modules/Setting.hpp>



//
// Created by Tozic 7/15/2024.
//


class Module;

class ModernGui
{
public:
    ModernGui();
    ~ModernGui();
    std::mutex uiMutex;
    std::shared_ptr<Module> lastMod = nullptr;
    bool isBinding = false;
    bool isBoolSettingBinding = false;
    BoolSetting* lastBoolSetting = nullptr;
    ColorSetting* lastColorSetting = nullptr;
    bool displayColorPicker = false;
    Setting* openEnumSetting = nullptr;
    std::unordered_map<Setting*, float> enumDropdownAnims;
    bool isSearching = false;
    bool searchWantsFocus = false;
    float searchAnim = 0.0f;
    float searchTextAnim = 0.0f;
    char searchText[64] = { 0 };
    bool isConfigDropdownOpen = false;
    float configDropdownAnim = 0.0f;
    int configSelectedIndex = -1;
    std::string selectedConfigName = "";
    std::vector<std::string> configNames;
    char configSearchText[64] = { 0 };
    bool configSearchWantsFocus = false;
    bool configSortLengthDesc = true;
    float configSortAnim = 0.0f;
    std::unordered_map<std::string, float> configRowAnims;
    std::unordered_map<std::string, float> configRowY;
    float configListScroll = 0.0f;
    float configListScrollTarget = 0.0f;
    bool isThemePickerOpen = false;
    float themePickerAnim = 0.0f;
    int uiTheme = 0;
    int themeIndex = 0;
    bool useCustomTheme = false;
    float customThemeColor[4] = { 99.0f / 255.0f, 102.0f / 255.0f, 241.0f / 255.0f, 1.0f };
    float colorPickerWidth = 180.0f;
    float themePickerWidth = 180.0f;
    bool resizingColorPicker = false;
    ImVec2 colorPickerResizeStartMouse = ImVec2(0.0f, 0.0f);
    float colorPickerResizeStartWidth = 0.0f;
    bool resizingThemePicker = false;
    ImVec2 themePickerResizeStartMouse = ImVec2(0.0f, 0.0f);
    float themePickerResizeStartWidth = 0.0f;
    int autobuySelectedIndex = 0;
    int autobuyLastSelectedIndex = -1;
    char autobuyPriceInput[64] = { 0 };
    int autobuyQuantityInput = 1;
    char autobuyQuantityInputBuf[16] = { 0 };
    bool isTargetEspPanelOpen = false;
    float targetEspPanelAnim = 0.0f;
    bool isAnimationsPanelOpen = false;
    float animationsPanelAnim = 0.0f;
    bool isTargetEspModeSwitching = false;
    bool isTargetEspModeFadingOut = false;
    float targetEspModeFade = 1.0f;
    int targetEspModeShown = -1;
    int targetEspModeTarget = -1;
    float espPreviewAnim = 0.0f;
    bool isIrcPanelOpen = false;
    float ircPanelAnim = 0.0f;
    bool ircAutoScroll = true;
    char ircInput[256] = { 0 };
    std::string ircDmTargetKey = "";
    std::string ircDmTargetLabel = "";
    bool isInitialLoading = false;
    uint64_t initialLoadStart = 0;
    void startPreload();
    void openGui();
    void closeGui();
    void requestClose();
    bool consumeCloseRequest();

    bool isMouseOver(const ImVec4& rect);
    void render(float animation, float inScale, int& scrollDirection, char* h, float midclickRounding, bool isPressingShift);
    void render(float animation, float inScale, int& scrollDirection, char* h, float blurStrength, float midclickRounding, bool isPressingShift);
    void onWindowResizeEvent(class WindowResizeEvent& event);

private:
    void resetTransientState();
    void refreshConfigList();
    void requestModuleSearch(std::string query, std::vector<std::shared_ptr<Module>> snapshot);
    bool tryConsumeModuleSearch(const std::string& query, uint64_t& outReadyId, std::vector<std::shared_ptr<Module>>& outSnapshot);
    void moduleSearchWorker();

    void requestDescriptionTokens(const Setting* setting, std::string description);
    bool tryGetDescriptionTokens(const Setting* setting, const std::string& description, std::vector<std::string>& outTokens);
    void descriptionTokenWorker();

    struct DescTokensCache
    {
        std::string description;
        std::vector<std::string> tokens;
        uint64_t lastUse = 0;
    };

    struct WrappedDescCacheKey
    {
        const Setting* setting = nullptr;
        int fontPx10 = 0;
        int maxWidth10 = 0;

        bool operator==(const WrappedDescCacheKey& other) const
        {
            return setting == other.setting && fontPx10 == other.fontPx10 && maxWidth10 == other.maxWidth10;
        }
    };

    struct WrappedDescCacheKeyHash
    {
        size_t operator()(const WrappedDescCacheKey& k) const noexcept
        {
            size_t h1 = std::hash<const void*>{}(k.setting);
            size_t h2 = std::hash<int>{}(k.fontPx10);
            size_t h3 = std::hash<int>{}(k.maxWidth10);
            return (h1 ^ (h2 + 0x9e3779b9u + (h1 << 6) + (h1 >> 2))) ^ (h3 + 0x9e3779b9u + (h2 << 6) + (h2 >> 2));
        }
    };

    struct WrappedDescCacheValue
    {
        std::string description;
        std::vector<std::string> lines;
        float lineH = 0.0f;
        uint64_t lastUse = 0;
    };

    std::mutex mSearchMutex;
    std::condition_variable mSearchCv;
    std::thread mSearchThread;
    std::atomic<bool> mSearchStop{ false };

    uint64_t mSearchRequestId = 0;
    uint64_t mSearchProcessingId = 0;
    uint64_t mSearchReadyId = 0;
    std::string mSearchRequestedQuery;
    std::vector<std::shared_ptr<Module>> mSearchRequestedSnapshot;
    std::string mSearchReadyQuery;
    std::vector<std::shared_ptr<Module>> mSearchReadySnapshot;

    std::mutex mDescMutex;
    std::condition_variable mDescCv;
    std::thread mDescThread;
    std::atomic<bool> mDescStop{ false };
    std::unordered_map<const Setting*, std::string> mDescRequested;
    std::unordered_map<const Setting*, DescTokensCache> mDescReady;

    std::unordered_map<WrappedDescCacheKey, WrappedDescCacheValue, WrappedDescCacheKeyHash> mWrappedDescCache;

    uint64_t mDescUseCounter = 0;
    uint64_t mWrappedDescUseCounter = 0;
    std::atomic<bool> mCloseRequested{ false };

    static constexpr size_t kMaxDescReadyEntries = 512;
    static constexpr size_t kMaxWrappedDescEntries = 1024;
};
