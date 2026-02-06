#include "NewLight.hpp"

#include <fstream>
#include <Features/FeatureManager.hpp>
#include <Features/Configs/ConfigManager.hpp>
#include <Hook/HookManager.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>

#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/MinecraftGame.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>

#include "spdlog/sinks/stdout_color_sinks-inl.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <winrt/base.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <build_info.h>
#include <Features/IRC/IrcClient.hpp>
#include <Features/Modules/Misc/IRC.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>
#include <Utils/SysUtils/xorstr.hpp>

#include <string>

std::string title = "Social.cc";

void setTitle(std::string title)
{
    auto w = winrt::Windows::ApplicationModel::Core::CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [title]() {
        winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView().Title(winrt::to_hstring(title));
        });
}

std::vector<unsigned char> gBpBytes = { 0x1c };
DEFINE_PATCH_FUNC(patchInHandSlot, SigManager::ItemInHandRenderer_renderItem_bytepatch2 + 2, gBpBytes);

void Solstice::init(HMODULE hModule)
{
    while (ProcUtils::getModuleCount() < 130) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    int64_t start = NOW;

    mModule = hModule;
    mInitialized = true;

#ifdef __DEBUG__
    Logger::initialize();
#endif

    console = spdlog::stdout_color_mt(CC(21, 207, 148) + "Social" + ANSI_COLOR_RESET, spdlog::color_mode::automatic);

    std::string logFile = FileUtils::getSolsticeDir() + xorstr_("Social.log");

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[" + CC(255, 135, 0) + "%H:%M:%S.%e" + ANSI_COLOR_RESET + "] [%n] [%^%l%$] %v");
    console_sink->set_level(spdlog::level::trace);
    console->set_level(spdlog::level::trace);
    console->set_pattern("[" + CC(255, 135, 0) + "%H:%M:%S.%e" + ANSI_COLOR_RESET + "] [%n] [%^%l%$] %v");
    spdlog::set_default_logger(std::make_shared<spdlog::logger>(CC(21, 207, 148) + "Social" + ANSI_COLOR_RESET, spdlog::sinks_init_list{ console_sink }));

    console->info("Welcome to " + CC(0, 255, 0) + "Social" + ANSI_COLOR_RESET + "!");

    spdlog::info("Minecraft version: {}", ProcUtils::getVersion());

    ExceptionHandler::init();

    FileUtils::validateDirectories();

    setTitle(title);

    sHWID = GET_HWID().toString();
    spdlog::info("HWID: {}", sHWID);

    if (MH_Initialize() != MH_OK)
    {
        console->critical("Failed to initialize MinHook!");
    }

    Prefs = PreferenceManager::load();

    HWND hwnd = ProcUtils::getMinecraftWindow();

    console->info("initializing signatures...");
    int64_t sstart = NOW;
    OffsetProvider::initialize();
    SigManager::initialize();
    int64_t send = NOW;

    int failedSigs = 0;

    for (const auto& [sig, result] : SigManager::mSigs)
    {
        if (result == 0)
        {
            console->critical("[signatures] Failed to find signature: {}", sig);
            failedSigs++;
        }
    }

    for (const auto& [sig, result] : OffsetProvider::mSigs)
    {
        if (result == 0)
        {
            console->critical("[offsets] Failed to find offset: {}", sig);
            failedSigs++;
        }
    }

    if (failedSigs > 0)
    {
        console->critical("Failed to find {} signatures/offsets!", failedSigs);
        console->info("Skipping Failed to find signatures/offsets!");
    }

    console->info("initialized signatures in {}ms", send - sstart);

    console->info("clientinstance addr @ 0x{:X}", reinterpret_cast<uintptr_t>(ClientInstance::get()));
    console->info("mcgame from clientinstance addr @ 0x{:X}", reinterpret_cast<uintptr_t>(ClientInstance::get()->getMinecraftGame()));
    console->info("localplayer addr @ 0x{:X}", reinterpret_cast<uintptr_t>(ClientInstance::get()->getLocalPlayer()));

    if (failedSigs > 0)
    {
        console->info("Skipping Failed to find signatures/offsets!");
    }

    gFeatureManager = std::make_shared<FeatureManager>();
    gFeatureManager->init();

    console->info("initializing hooks...");
    HookManager::init(false);

    console->info("initialized in {}ms", NOW - start);

    ClientInstance::get()->getMinecraftGame()->playUi("ambient.weather.thunder", 1, 0.7f);

    console->info("Press END to eject dll.");
    mLastTick = NOW;

    mThread = std::thread(&Solstice::shutdownThread);
    mThread.detach();
}

void Solstice::shutdownThread()
{
    bool firstCall = true;
    bool isLpValid = false;
    while (!mRequestEject)
    {
        if (firstCall)
        {
            NotifyUtils::notify("Social initialized!", 5.0f, Notification::Type::Info);
            firstCall = false;
        }

        if (!isLpValid && ClientInstance::get()->getLocalPlayer())
        {
            isLpValid = true;
            HookManager::init(true);

            auto ircModule = gFeatureManager->mModuleManager->getModule<IRC>();
            if (ircModule && !ircModule->mEnabled) ircModule->toggle();

            if (!Prefs->mDefaultConfigName.empty())
            {
                if (ConfigManager::configExists(Prefs->mDefaultConfigName))
                {
                    ConfigManager::loadConfig(Prefs->mDefaultConfigName);
                }
                else
                {
                    console->warn("Default config does not exist! Clearing default config...");
                    NotifyUtils::notify("Default config does not exist! Clearing default config...", 10.0f, Notification::Type::Warning);
                    Prefs->mDefaultConfigName = "";
                    PreferenceManager::save(Prefs);
                }
            }
            else {
                console->warn("No default config set!");
            }
        }

        patchInHandSlot(ClientInstance::get()->getLocalPlayer() != nullptr);

        mLastTick = NOW;
        gFeatureManager->mModuleManager->onClientTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    mRequestEject = true;

    setTitle("");

    patchInHandSlot(false);

    HookManager::shutdown();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    gFeatureManager->shutdown();

    console->warn("Shutting down...");

    ClientInstance::get()->getMinecraftGame()->playUi("beacon.deactivate", 1, 1.0f);

    mInitialized = false;
    SigManager::deinitialize();
    OffsetProvider::deinitialize();

    Sleep(1000);

    Logger::deinitialize();
    FreeLibraryAndExitThread(mModule, 0);
}
// нет
