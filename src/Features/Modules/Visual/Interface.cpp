//
// Created by vastrakai on 7/1/2024.
//

#include <Features/Events/ActorRenderEvent.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/DrawImageEvent.hpp>
#include <Features/Events/ModuleStateChangeEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/DrawImageEvent.hpp>
#include <Features/Events/PreGameCheckEvent.hpp>
#include <Features/Events/RenderEvent.hpp>

#include <Features/Modules/Visual/Interface.hpp>
#include <Features/Modules/Misc/DeviceSpoof.hpp>
#include <Features/Modules/Misc/EditionFaker.hpp>
#include <Hook/Hooks/RenderHooks/ActorRenderDispatcherHook.hpp>
#include <Hook/Hooks/RenderHooks/HoverTextRendererHook.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/mce.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Network/Packets/Packet.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

#include <SDK/Minecraft/Actor/SyncedPlayerMovementSettings.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <Utils/FileUtils.hpp>
#include <Utils/Resources.hpp>
#include <Utils/StringUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/MiscUtils/ImRenderUtils.hpp>
#include <Hook/Hooks/RenderHooks/D3DHook.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <Utils/FontHelper.hpp>
#include "ClickGui.hpp"

#ifdef __DEBUG__
std::vector<unsigned char> gFsBytes2 = { 0x0f, 0x85 };
DEFINE_PATCH_FUNC(patchFullStack, SigManager::ResourcePackManager_composeFullStackBp, gFsBytes2);
#endif

// please someone make this in a class or struct cuz it gives me aids
// Define a mapping from Minecraft color codes to RGBA colors
std::unordered_map<char, ImColor> mColorMap = {
    {'0', ImColor(0, 0, 0)},        // Black
    {'1', ImColor(0, 0, 170)},      // Dark Blue
    {'2', ImColor(0, 170, 0)},      // Dark Green
    {'3', ImColor(0, 170, 170)},    // Dark Aqua
    {'4', ImColor(170, 0, 0)},      // Dark Red
    {'5', ImColor(170, 0, 170)},    // Dark Purple
    {'6', ImColor(255, 170, 0)},    // Gold
    {'7', ImColor(170, 170, 170)},  // Gray
    {'8', ImColor(85, 85, 85)},     // Dark Gray
    {'9', ImColor(85, 85, 255)},    // Blue
    {'a', ImColor(85, 255, 85)},    // Green
    {'b', ImColor(85, 255, 255)},   // Aqua
    {'c', ImColor(255, 85, 85)},    // Red
    {'d', ImColor(255, 85, 255)},   // Light Purple
    {'e', ImColor(255, 255, 85)},   // Yellow
    {'f', ImColor(255, 255, 255)},  // White
    {'r', ImColor(255, 255, 255)}   // Reset
};

template <typename T>
std::string combine(T t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

template <typename T, typename... Args>
std::string combine(T t, Args... args)
{
    std::stringstream ss;
    ss << t << combine(args...);
    return ss.str();
}

struct SpoofProfile
{
    std::string name;
    std::string deviceModel;
    std::string deviceId;
    std::string skinId;
    std::string selfSignedId;
    int64_t clientRandomId = 0;
    int os = 0;
    int input = 0;
    int64_t createdAt = 0;
    std::string owner;
};

static std::string spoofProfileDir()
{
    return FileUtils::getSolsticeDir() + "SpoofProfiles\\";
}

static std::string sanitizeProfileName(const std::string& in)
{
    std::string out;
    out.reserve(in.size());

    auto isInvalid = [](unsigned char c) -> bool {
        if (c < 32) return true;
        switch (c) {
        case '<': case '>': case ':': case '"': case '/': case '\\': case '|': case '?': case '*':
            return true;
        default:
            return false;
        }
    };

    for (unsigned char c : in)
    {
        if (isInvalid(c)) continue;
        out.push_back((char)c);
    }

    while (!out.empty() && std::isspace((unsigned char)out.front())) out.erase(out.begin());
    while (!out.empty() && std::isspace((unsigned char)out.back())) out.pop_back();
    return out;
}

static std::string formatProfileTime(int64_t t)
{
    if (t <= 0) return "unknown";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return oss.str();
}

static nlohmann::json profileToJson(const SpoofProfile& p)
{
    nlohmann::json j;
    j["name"] = p.name;
    j["deviceModel"] = p.deviceModel;
    j["deviceId"] = p.deviceId;
    j["skinId"] = p.skinId;
    j["selfSignedId"] = p.selfSignedId;
    j["clientRandomId"] = p.clientRandomId;
    j["os"] = p.os;
    j["input"] = p.input;
    j["createdAt"] = p.createdAt;
    j["owner"] = p.owner;
    return j;
}

static bool profileFromJson(const nlohmann::json& j, SpoofProfile& out)
{
    if (!j.is_object()) return false;
    out.name = j.value("name", "");
    out.deviceModel = j.value("deviceModel", "");
    out.deviceId = j.value("deviceId", "");
    out.skinId = j.value("skinId", "");
    out.selfSignedId = j.value("selfSignedId", "");
    out.clientRandomId = j.value("clientRandomId", (int64_t)0);
    out.os = j.value("os", 0);
    out.input = j.value("input", 0);
    out.createdAt = j.value("createdAt", (int64_t)0);
    out.owner = j.value("owner", "");
    return true;
}

static bool loadProfileFile(const std::string& path, SpoofProfile& out)
{
    try
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) return false;
        if (file.peek() == std::ifstream::traits_type::eof()) return false;
        nlohmann::json j;
        file >> j;
        file.close();
        return profileFromJson(j, out);
    }
    catch (...)
    {
        return false;
    }
}

static std::vector<SpoofProfile> loadProfiles()
{
    std::vector<SpoofProfile> result;
    FileUtils::createDirectory(FileUtils::getSolsticeDir());
    FileUtils::createDirectory(spoofProfileDir());
    for (const auto& f : FileUtils::listFiles(spoofProfileDir()))
    {
        if (!f.ends_with(".save")) continue;
        SpoofProfile p;
        if (loadProfileFile(spoofProfileDir() + f, p))
            result.push_back(std::move(p));
    }
    std::sort(result.begin(), result.end(), [](const SpoofProfile& a, const SpoofProfile& b) {
        return a.createdAt > b.createdAt;
    });
    return result;
}

static bool saveProfileFile(const SpoofProfile& p, std::string& outPath)
{
    FileUtils::createDirectory(FileUtils::getSolsticeDir());
    FileUtils::createDirectory(spoofProfileDir());
    std::string base = sanitizeProfileName(p.name);
    if (base.empty())
        base = "profile_" + std::to_string(p.createdAt);
    std::string path = spoofProfileDir() + base + ".save";
    int idx = 1;
    while (FileUtils::fileExists(path))
    {
        path = spoofProfileDir() + base + "_" + std::to_string(idx++) + ".save";
    }

    try
    {
        std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        nlohmann::json j = profileToJson(p);
        file << j.dump(4);
        file.flush();
        file.close();
        outPath = path;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

float pYaw;
float pOldYaw;

float pHeadYaw;
float pOldHeadYaw;

float pPitch;
float pOldPitch;

float pBodyYaw;
float pOldBodyYaw;

float pLerpedYaw;
float pLerpedHeadYaw;
float pLerpedPitch;
float pLerpedBodyYaw;

bool usingPaip = false;

void Interface::onEnable()
{

}

void Interface::onDisable()
{
#ifdef __DEBUG__
    patchFullStack(false);
#endif
}

void Interface::renderHoverText()
{
    static EasingUtil inEase;

    (HoverTextRender::mTimeDisplayed != 0 && gFeatureManager->mModuleManager->getModule<ClickGui>()->mEnabled != true) ?
            inEase.incrementPercentage(ImRenderUtils::getDeltaTime() * 2)
            : inEase.decrementPercentage(ImRenderUtils::getDeltaTime() * 4);

    float inScale = HoverTextRender::mTimeDisplayed != 0 && gFeatureManager->mModuleManager->getModule<ClickGui>()->mEnabled != true ? inEase.easeOutExpo() : inEase.easeOutBack();

    if (inEase.isPercentageMax())
        inScale = 1;

    if (inScale < 0.01)
        return;

    glm::vec2 mPos = HoverTextRender::mInfo.mPos;
    glm::vec2 mTextPos = glm::vec2(mPos.x + 6, mPos.y + 6); // It looks better this way than getting it from HoverTextRenderer class

    float mTextSize = 1.25 * inScale;

    std::string mMessage = HoverTextRender::mInfo.mText;
    std::string mNoneColoredText = ColorUtils::removeColorCodes(HoverTextRender::mInfo.mText);

    ImColor mCurrentColor = ImColor(255, 255, 255);

    float mMeasurementX = ImGui::GetFont()->CalcTextSizeA(mTextSize * 18, FLT_MAX, -1, mNoneColoredText.c_str()).x;
    float mMeasurementY = ImGui::GetFont()->CalcTextSizeA(mTextSize * 18, FLT_MAX, -1, mNoneColoredText.c_str()).y;

    ImVec4 mRect = ImVec4(mPos.x, mPos.y, mPos.x + mMeasurementX + 12, mPos.y + mMeasurementY + 12);

    ImRenderUtils::addBlur(mRect, 3 * inScale, 10);

    ImRenderUtils::fillRectangle(mRect, ImColor(0, 0, 0), 0.78f * inScale, 10);

    for (size_t j = 0; j < mMessage.length(); ++j) {
        char c = mMessage[j];

        if (c == '§' && j + 1 < mMessage.length()) {
            char colorCode = mMessage[j + 1];
            if (mColorMap.find(colorCode) != mColorMap.end()) {
                mCurrentColor = mColorMap[colorCode];
                j++;
            }
            continue;
        }

        if (c == '\n') {
            mTextPos.x = mPos.x + 6;
            mTextPos.y += ImGui::GetFont()->CalcTextSizeA(mTextSize * 18, FLT_MAX, 0, "\n").y;
        }

        if (!std::isprint(c)) {
            continue;
        }

        std::string mString = combine(c, "");

        ImRenderUtils::drawText(ImVec2(mTextPos.x, mTextPos.y), mString, mCurrentColor, mTextSize, inScale, false, 0, ImGui::GetBackgroundDrawList(), 0);

        mTextPos.x += ImGui::GetFont()->CalcTextSizeA(mTextSize * 18, FLT_MAX, -1, mString.c_str()).x;
    }

    HoverTextRender::mTimeDisplayed = 0;
}


void Interface::onModuleStateChange(ModuleStateChangeEvent& event)
{
    if (event.mModule == this)
    {
        event.setCancelled(true);
    }
}

void Interface::onPregameCheckEvent(PreGameCheckEvent& event)
{
#ifdef __DEBUG__
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player || ! mForcePackSwitching.mValue) return;

    std::string screenName = ClientInstance::get()->getScreenName();

    // prevent other screens from breaking
    if (screenName.contains("screen_world_controls_and_settings") && !screenName.contains("global_texture_pack_tab")) return;

    event.setPreGame(true);
#endif
}

void Interface::onRenderEvent(RenderEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    static bool lastPlayerState = false;

    //renderHoverText();

#ifdef __DEBUG__
    if (player && mForcePackSwitching.mValue)
    {
        patchFullStack(true);
    } else
    {
        patchFullStack(false);
    }
#endif

    if (player && !lastPlayerState)
    {
        usingPaip = false;
    }

    static constexpr float LERP_SPEED = 20.f;
    float deltaTime = ImGui::GetIO().DeltaTime;

    float yaw = MathUtils::wrap(pLerpedYaw, pYaw - 180, pYaw + 180);
    float headYaw = MathUtils::wrap(pLerpedHeadYaw, pHeadYaw - 180, pHeadYaw + 180);
    float pitch = pLerpedPitch;
    float bodyYaw = MathUtils::wrap(pLerpedBodyYaw, pBodyYaw - 180, pBodyYaw + 180);

    float preLerpedYaw = MathUtils::lerp(yaw, pYaw, deltaTime * LERP_SPEED);
    float preLerpedHeadYaw = MathUtils::lerp(headYaw, pHeadYaw, deltaTime * LERP_SPEED);
    float preLerpedPitch = MathUtils::lerp(pitch, pPitch, deltaTime * LERP_SPEED);
    float preLerpedBodyYaw = MathUtils::lerp(bodyYaw, pBodyYaw, deltaTime * LERP_SPEED);

    pLerpedYaw = MathUtils::wrap(pLerpedYaw, preLerpedYaw - 180, preLerpedYaw + 180);
    pLerpedHeadYaw = MathUtils::wrap(pLerpedHeadYaw, preLerpedHeadYaw - 180, preLerpedHeadYaw + 180);
    pLerpedBodyYaw = MathUtils::wrap(pLerpedBodyYaw, preLerpedBodyYaw - 180, preLerpedBodyYaw + 180);

    pLerpedYaw = MathUtils::lerp(preLerpedYaw, pLerpedYaw, deltaTime * LERP_SPEED);
    pLerpedHeadYaw = MathUtils::lerp(preLerpedHeadYaw, pLerpedHeadYaw, deltaTime * LERP_SPEED);
    pLerpedPitch = MathUtils::lerp(preLerpedPitch, pLerpedPitch, deltaTime * LERP_SPEED);
    pLerpedBodyYaw = MathUtils::lerp(preLerpedBodyYaw, pLerpedBodyYaw, deltaTime * LERP_SPEED);

    if (player) {
        std::string name = player->getLocalName();
        auto drawList = ImGui::GetBackgroundDrawList();
        float fontSize = 20.f;
        ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name.c_str());
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImVec2 pos = ImVec2(displaySize.x - textSize.x - 10.0f, displaySize.y - textSize.y - 10.0f);
        ImRenderUtils::drawShadowText(drawList, name, pos, ImColor(255, 255, 255), fontSize);
    }
    else
    {
        auto drawList = ImGui::GetBackgroundDrawList();
        auto drawListFg = ImGui::GetForegroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;

        static bool themeMenuTarget = false;
        static float themeMenuAnim = 0.f;
        static bool prevThemeMenuTarget = false;
        static bool inputInit = false;
        static bool profilesReload = true;
        static bool focusRequested = false;
        static std::vector<SpoofProfile> profiles;

        static char profileNameBuf[64] = { 0 };
        static char deviceModelBuf[128] = { 0 };
        static char deviceIdBuf[128] = { 0 };
        static char skinIdBuf[128] = { 0 };
        static char selfSignedIdBuf[128] = { 0 };
        static char clientRandomIdBuf[32] = { 0 };
        static int osIndex = 7;
        static int inputIndex = 1;

        static ID3D11ShaderResourceView* headTex = nullptr;
        static bool headLoaded = false;
        static int headW = 0;
        static int headH = 0;

        const float dt = ImGui::GetIO().DeltaTime;
        themeMenuAnim = MathUtils::lerp(themeMenuAnim, themeMenuTarget ? 1.f : 0.f, dt * (themeMenuTarget ? 14.f : 16.f));
        themeMenuAnim = MathUtils::clamp(themeMenuAnim, 0.f, 1.f);

        const float margin = 14.f;
        const float btnSize = 32.f;
        const float btnR = 9.f;

        ImVec2 btnMin = ImVec2(displaySize.x - margin - btnSize, margin);
        ImVec2 btnMax = ImVec2(displaySize.x - margin, margin + btnSize);
        ImVec4 btnRect = ImVec4(btnMin.x, btnMin.y, btnMax.x, btnMax.y);
        const bool btnHovered = ImRenderUtils::isMouseOver(btnRect);

        const float menuW = 650.f;
        const float menuH = 400.f;
        ImVec2 menuMin = ImVec2((displaySize.x - menuW) * 0.5f, (displaySize.y - menuH) * 0.5f);
        ImVec2 menuMax = ImVec2(menuMin.x + menuW, menuMin.y + menuH);

        const float menuA = themeMenuAnim;
        const float menuScale = 0.92f + 0.08f * themeMenuAnim;
        const ImVec2 menuCenter = ImVec2((menuMin.x + menuMax.x) * 0.5f, (menuMin.y + menuMax.y) * 0.5f);
        const ImVec2 drawMin = ImVec2(menuCenter.x + (menuMin.x - menuCenter.x) * menuScale, menuCenter.y + (menuMin.y - menuCenter.y) * menuScale);
        const ImVec2 drawMax = ImVec2(menuCenter.x + (menuMax.x - menuCenter.x) * menuScale, menuCenter.y + (menuMax.y - menuCenter.y) * menuScale);
        const bool menuHovered = ImRenderUtils::isMouseOver(ImVec4(drawMin.x, drawMin.y, drawMax.x, drawMax.y));

        if (btnHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            themeMenuTarget = !themeMenuTarget;
        }

        const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        if (themeMenuTarget && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !btnHovered && !menuHovered && !anyPopupOpen)
        {
            themeMenuTarget = false;
        }

        if (themeMenuTarget && !prevThemeMenuTarget)
        {
            profilesReload = true;
            inputInit = false;
            focusRequested = true;
        }
        prevThemeMenuTarget = themeMenuTarget;

        {
            ImColor btnBg = ColorUtils::getUiCardColor(1.0f);
            ImColor btnBorder = ColorUtils::getUiBorderColor(1.0f);
            ImColor btnText = ColorUtils::getUiTextColor(1.0f);
            ImColor btnAccent = ColorUtils::getGuiAccentColor(0);

            btnBg.Value.w = btnHovered ? 0.96f : 0.90f;
            btnBorder.Value.w = btnHovered ? 0.55f : 0.40f;
            btnText.Value.w = btnHovered ? 0.95f : 0.85f;
            btnAccent.Value.w = btnHovered ? 0.28f : 0.18f;

            drawList->AddShadowRect(btnMin, btnMax, IM_COL32(0, 0, 0, static_cast<int>(130.f)), 24.0f, ImVec2(0.f, 2.f), 0, btnR);
            drawList->AddRectFilled(btnMin, btnMax, btnBg, btnR);
            drawList->AddRect(btnMin, btnMax, btnBorder, btnR, 0, 1.1f);

            ImVec2 btnC = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);
            const float iconSize = 16.f;
            const char* iconText = "T";
            ImVec2 iconSz = ImGui::GetFont()->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, iconText);
            ImVec2 iconPos = ImVec2(btnC.x - iconSz.x * 0.5f, btnC.y - iconSz.y * 0.5f);
            drawList->AddText(ImGui::GetFont(), iconSize, iconPos, btnText, iconText);

            ImVec2 glowMin = ImVec2(btnMin.x + btnSize * 0.20f, btnMin.y - btnSize * 0.18f);
            ImVec2 glowMax = ImVec2(btnMax.x - btnSize * 0.20f, btnMin.y + btnSize * 0.30f);
            drawList->PushClipRect(btnMin, btnMax, true);
            drawList->AddShadowRect(glowMin, glowMax, btnAccent, 35.0f, ImVec2(0.f, 0.f), 0, btnR);
            drawList->PopClipRect();
        }

        if (menuA > 0.01f)
        {
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
            ImGui::SetNextWindowSize(displaySize);
            ImGui::Begin("##spoof_blocker", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus);
            const ImVec2 menuSize = ImVec2(std::max(0.f, drawMax.x - drawMin.x), std::max(0.f, drawMax.y - drawMin.y));

            ImGui::SetCursorScreenPos(ImVec2(0.f, 0.f));
            ImGui::InvisibleButton("##spoof_blk_top", ImVec2(displaySize.x, std::max(0.f, drawMin.y)));

            ImGui::SetCursorScreenPos(ImVec2(0.f, drawMax.y));
            ImGui::InvisibleButton("##spoof_blk_bottom", ImVec2(displaySize.x, std::max(0.f, displaySize.y - drawMax.y)));

            ImGui::SetCursorScreenPos(ImVec2(0.f, drawMin.y));
            ImGui::InvisibleButton("##spoof_blk_left", ImVec2(std::max(0.f, drawMin.x), menuSize.y));

            ImGui::SetCursorScreenPos(ImVec2(drawMax.x, drawMin.y));
            ImGui::InvisibleButton("##spoof_blk_right", ImVec2(std::max(0.f, displaySize.x - drawMax.x), menuSize.y));

            ImGui::SetCursorScreenPos(drawMin);
            ImGui::InvisibleButton("##spoof_blk_menu", menuSize);
            ImGui::End();

            FontHelper::load();
            FontHelper::pushPrefFont(false, true);

            ImColor menuBg = ColorUtils::getUiCardColor(1.0f);
            ImColor menuBorder = ColorUtils::getUiBorderColor(1.0f);
            menuBg.Value.w = 0.96f * menuA;
            menuBorder.Value.w = 0.50f * menuA;

            const float menuR = 10.f;
            drawList->AddShadowRect(drawMin, drawMax, IM_COL32(0, 0, 0, static_cast<int>(160.f * menuA)), 30.0f, ImVec2(0.f, 4.f), 0, menuR);
            drawList->AddRectFilled(drawMin, drawMax, menuBg, menuR);
            drawList->AddRect(drawMin, drawMax, menuBorder, menuR, 0, 1.2f);

            const float pad = 5.f * menuScale;
            const float gap = 4.f * menuScale;
            ImVec2 innerMin = ImVec2(drawMin.x + pad, drawMin.y + pad);
            ImVec2 innerMax = ImVec2(drawMax.x - pad, drawMax.y - pad);

            const float innerW = std::max(0.f, innerMax.x - innerMin.x);
            const float innerH = std::max(0.f, innerMax.y - innerMin.y);

            const float leftW = std::min(240.f * menuScale, std::max(0.f, innerW * 0.40f));
            const float rightW = std::max(0.f, innerW - leftW - gap);

            ImVec2 leftMin = innerMin;
            ImVec2 leftMax = ImVec2(innerMin.x + leftW, innerMin.y + innerH);
            ImVec2 rightMin = ImVec2(leftMax.x + gap, innerMin.y);
            ImVec2 rightMax = ImVec2(rightMin.x + rightW, innerMin.y + innerH);

            ImColor panelBg = ColorUtils::getUiBackgroundColor(1.0f);
            ImColor panelBorder = ColorUtils::getUiBorderColor(1.0f);
            panelBg.Value.w = 0.80f * menuA;
            panelBorder.Value.w = 0.45f * menuA;

            const float panelR = 9.f;
            drawList->AddRectFilled(leftMin, leftMax, panelBg, panelR);
            drawList->AddRect(leftMin, leftMax, panelBorder, panelR, 0, 1.1f);
            drawList->AddRectFilled(rightMin, rightMax, panelBg, panelR);
            drawList->AddRect(rightMin, rightMax, panelBorder, panelR, 0, 1.1f);

            const float leftHeaderH = 38.f * menuScale;
            const float leftHeaderR = 9.f * menuScale;
            const float leftHeaderPadX = 10.f * menuScale;
            const float leftHeaderTextSize = 16.f * menuScale;
            const float leftHeaderIconSize = 18.f * menuScale;
            const float leftHeaderGap = 10.f * menuScale;

            {
                ImVec2 headerMin = leftMin;
                ImVec2 headerMax = ImVec2(leftMax.x, leftMin.y + leftHeaderH);

                ImColor headerBg = ColorUtils::getUiCardColor(1.0f);
                ImColor headerBorder = ImColor(52, 52, 62, 255);
                ImColor headerText = ColorUtils::getUiTextColor(1.0f);
                headerBg.Value.w = 0.92f * menuA;
                headerBorder.Value.w = 0.75f * menuA;
                headerText.Value.w *= menuA;

                drawListFg->AddRectFilled(headerMin, headerMax, headerBg, leftHeaderR);
                drawListFg->AddRect(headerMin, headerMax, headerBorder, leftHeaderR, 0, 1.15f);

                ImFont* textFont = ImGui::GetFont();
                ImFont* iconFont = textFont;
                if (auto it = FontHelper::Fonts.find("essence.ttf"); it != FontHelper::Fonts.end() && it->second)
                    iconFont = it->second;

                const std::string headerTitle = "Spoofer panel";
                const std::string headerIcon = "b";

                ImVec2 titleSz = textFont->CalcTextSizeA(leftHeaderTextSize, FLT_MAX, 0.0f, headerTitle.c_str());
                ImVec2 iconSz = iconFont->CalcTextSizeA(leftHeaderIconSize, FLT_MAX, 0.0f, headerIcon.c_str());

                float centerY = (headerMin.y + headerMax.y) * 0.5f;
                ImVec2 titlePos = ImVec2(headerMin.x + leftHeaderPadX, centerY - titleSz.y * 0.5f);
                ImVec2 iconPos = ImVec2(headerMax.x - leftHeaderPadX - iconSz.x, centerY - iconSz.y * 0.5f);

                drawListFg->AddText(textFont, leftHeaderTextSize, titlePos, headerText, headerTitle.c_str());
                drawListFg->AddText(iconFont, leftHeaderIconSize, iconPos, headerText, headerIcon.c_str());
            }

            if (!headLoaded)
            {
                D3DHook::loadTextureFromEmbeddedResource("steve.png", &headTex, &headW, &headH);
                headLoaded = true;
            }

            if (!inputInit)
            {
                auto editionFaker = gFeatureManager->mModuleManager->getModule<EditionFaker>();
                if (EditionFaker::sUseCustom && EditionFaker::sCustomOs >= 0) osIndex = EditionFaker::sCustomOs;
                else if (editionFaker) osIndex = editionFaker->mOs.getIndex();

                if (EditionFaker::sUseCustom && EditionFaker::sCustomInput >= 0) inputIndex = EditionFaker::sCustomInput;
                else if (editionFaker) inputIndex = editionFaker->mInputMethod.getIndex();

                if (!DeviceSpoof::sCustomDeviceModel.empty())
                    std::snprintf(deviceModelBuf, sizeof(deviceModelBuf), "%s", DeviceSpoof::sCustomDeviceModel.c_str());
                if (!DeviceSpoof::sCustomDeviceId.empty())
                    std::snprintf(deviceIdBuf, sizeof(deviceIdBuf), "%s", DeviceSpoof::sCustomDeviceId.c_str());
                if (!DeviceSpoof::sCustomSkinId.empty())
                    std::snprintf(skinIdBuf, sizeof(skinIdBuf), "%s", DeviceSpoof::sCustomSkinId.c_str());
                if (!DeviceSpoof::sCustomSelfSignedId.empty())
                    std::snprintf(selfSignedIdBuf, sizeof(selfSignedIdBuf), "%s", DeviceSpoof::sCustomSelfSignedId.c_str());
                if (DeviceSpoof::sCustomClientRandomId != 0)
                    std::snprintf(clientRandomIdBuf, sizeof(clientRandomIdBuf), "%lld", static_cast<long long>(DeviceSpoof::sCustomClientRandomId));

                inputInit = true;
            }

            if (profilesReload)
            {
                profiles = loadProfiles();
                profilesReload = false;
            }

            const ImGuiWindowFlags inputFlags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;

            const float leftUiInset = 0.f;
            ImVec2 leftUiMin = ImVec2(leftMin.x + leftUiInset, leftMin.y + leftUiInset + leftHeaderH + leftHeaderGap);
            ImVec2 leftUiSize = ImVec2(std::max(0.f, leftW - leftUiInset * 2.f), std::max(0.f, innerH - leftUiInset * 2.f - leftHeaderH - leftHeaderGap));
            if (focusRequested) ImGui::SetNextWindowFocus();
            ImGui::SetNextWindowPos(leftUiMin);
            ImGui::SetNextWindowSize(leftUiSize);
            ImGui::Begin("##spoof_left_panel", nullptr, inputFlags);
            focusRequested = false;

            ImColor accentColor = ColorUtils::getGuiAccentColor(0);
            ImColor textMain = ColorUtils::getUiTextColor(1.0f);
            ImColor textDim = ColorUtils::getUiTextDimColor(1.0f);
            accentColor.Value.w *= menuA;
            textMain.Value.w *= menuA;
            textDim.Value.w *= menuA;

            const float round = 7.f * menuScale;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, round);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f * menuScale, 6.f * menuScale));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f * menuScale, 8.f * menuScale));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(22.f / 255.f, 22.f / 255.f, 28.f / 255.f, 0.95f * menuA));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(26.f / 255.f, 26.f / 255.f, 34.f / 255.f, 0.98f * menuA));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(30.f / 255.f, 30.f / 255.f, 38.f / 255.f, 1.0f * menuA));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(46.f / 255.f, 46.f / 255.f, 58.f / 255.f, 0.80f * menuA));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(textMain.Value.x, textMain.Value.y, textMain.Value.z, textMain.Value.w));
            ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(accentColor.Value.x, accentColor.Value.y, accentColor.Value.z, 0.35f * menuA));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(18.f / 255.f, 18.f / 255.f, 22.f / 255.f, 0.98f * menuA));
            ImGui::PushItemWidth(-1.0f);

            if (profileNameBuf[0] == '\0')
            {
                auto localPlayer = ClientInstance::get()->getLocalPlayer();
                if (localPlayer)
                {
                    std::snprintf(profileNameBuf, sizeof(profileNameBuf), "%s", localPlayer->getLocalName().c_str());
                }
            }

            ImGui::TextUnformatted("Profile Name");
            ImGui::InputText("##profile_name", profileNameBuf, sizeof(profileNameBuf));

            auto editionFaker = gFeatureManager->mModuleManager->getModule<EditionFaker>();
            if (editionFaker)
            {
                const auto& osValues = editionFaker->mOs.getValues();
                if (!osValues.empty())
                {
                    if (osIndex < 0 || osIndex >= static_cast<int>(osValues.size()))
                        osIndex = MathUtils::clamp(osIndex, 0, static_cast<int>(osValues.size()) - 1);

                    std::string osLabel = osValues[osIndex] + "##os_cycle";
                    if (ImGui::Button(osLabel.c_str()))
                    {
                        osIndex = (osIndex + 1) % static_cast<int>(osValues.size());
                        EditionFaker::sUseCustom = true;
                        EditionFaker::sCustomOs = osIndex;
                        editionFaker->mOs.setIndex(osIndex);
                        if (editionFaker->mEnabled) editionFaker->spoofEdition();
                        auto deviceSpoof = gFeatureManager->mModuleManager->getModule<DeviceSpoof>();
                        if (deviceSpoof) deviceSpoof->spoofMboard();
                    }
                }

                const auto& inputValues = editionFaker->mInputMethod.getValues();
                if (!inputValues.empty())
                {
                    if (inputIndex < 0 || inputIndex >= static_cast<int>(inputValues.size()))
                        inputIndex = MathUtils::clamp(inputIndex, 0, static_cast<int>(inputValues.size()) - 1);

                    std::string inputLabel = inputValues[inputIndex] + "##input_cycle";
                    if (ImGui::Button(inputLabel.c_str()))
                    {
                        inputIndex = (inputIndex + 1) % static_cast<int>(inputValues.size());
                        EditionFaker::sUseCustom = true;
                        EditionFaker::sCustomInput = inputIndex;
                        editionFaker->mInputMethod.setIndex(inputIndex);
                        if (editionFaker->mEnabled) editionFaker->spoofEdition();
                    }
                }
            }

            if (ImGui::Button("Generate"))
            {
                const int genOs = osIndex;
                const std::string deviceModel = StringUtils::generateMboard(genOs);
                const std::string deviceId = StringUtils::generateUUID(genOs);
                const std::string selfSigned = StringUtils::generateUUID(genOs);
                const int64_t cid = StringUtils::generateCID();
                const std::string skinId = "Custom" + deviceId;

                std::snprintf(deviceModelBuf, sizeof(deviceModelBuf), "%s", deviceModel.c_str());
                std::snprintf(deviceIdBuf, sizeof(deviceIdBuf), "%s", deviceId.c_str());
                std::snprintf(selfSignedIdBuf, sizeof(selfSignedIdBuf), "%s", selfSigned.c_str());
                std::snprintf(clientRandomIdBuf, sizeof(clientRandomIdBuf), "%lld", static_cast<long long>(cid));
                std::snprintf(skinIdBuf, sizeof(skinIdBuf), "%s", skinId.c_str());
            }

            ImGui::SameLine();

            if (ImGui::Button("Save"))
            {
                SpoofProfile p;
                p.name = profileNameBuf;
                p.deviceModel = deviceModelBuf;
                p.deviceId = deviceIdBuf;
                p.skinId = skinIdBuf;
                p.selfSignedId = selfSignedIdBuf;
                p.os = osIndex;
                p.input = inputIndex;
                p.createdAt = static_cast<int64_t>(std::time(nullptr));
                auto localPlayer = ClientInstance::get()->getLocalPlayer();
                p.owner = localPlayer ? localPlayer->getLocalName() : "NoPlayer";

                if (p.deviceId.empty())
                    p.deviceId = StringUtils::generateUUID(p.os);
                if (p.selfSignedId.empty())
                    p.selfSignedId = StringUtils::generateUUID(p.os);
                if (p.deviceModel.empty())
                    p.deviceModel = StringUtils::generateMboard(p.os);
                if (p.skinId.empty())
                    p.skinId = "Custom" + p.deviceId;

                try
                {
                    p.clientRandomId = std::stoll(clientRandomIdBuf);
                }
                catch (...)
                {
                    p.clientRandomId = StringUtils::generateCID();
                }

                std::snprintf(deviceModelBuf, sizeof(deviceModelBuf), "%s", p.deviceModel.c_str());
                std::snprintf(deviceIdBuf, sizeof(deviceIdBuf), "%s", p.deviceId.c_str());
                std::snprintf(skinIdBuf, sizeof(skinIdBuf), "%s", p.skinId.c_str());
                std::snprintf(selfSignedIdBuf, sizeof(selfSignedIdBuf), "%s", p.selfSignedId.c_str());
                std::snprintf(clientRandomIdBuf, sizeof(clientRandomIdBuf), "%lld", static_cast<long long>(p.clientRandomId));

                std::string path;
                if (saveProfileFile(p, path))
                {
                    DeviceSpoof::sUseCustom = true;
                    DeviceSpoof::sCustomDeviceModel = p.deviceModel;
                    DeviceSpoof::sCustomDeviceId = p.deviceId;
                    DeviceSpoof::sCustomSkinId = p.skinId;
                    DeviceSpoof::sCustomSelfSignedId = p.selfSignedId;
                    DeviceSpoof::sCustomClientRandomId = p.clientRandomId;

                    EditionFaker::sUseCustom = true;
                    EditionFaker::sCustomOs = p.os;
                    EditionFaker::sCustomInput = p.input;

                    if (editionFaker)
                    {
                        editionFaker->mOs.setIndex(p.os);
                        editionFaker->mInputMethod.setIndex(p.input);
                        if (editionFaker->mEnabled) editionFaker->spoofEdition();
                    }
                    auto deviceSpoof = gFeatureManager->mModuleManager->getModule<DeviceSpoof>();
                    if (deviceSpoof) deviceSpoof->spoofMboard();

                    profilesReload = true;
                }
            }

            ImGui::PopItemWidth();
            ImGui::PopStyleColor(7);
            ImGui::PopStyleVar(3);
            ImGui::End();

            const float cardPad = 8.f * menuScale;
            const float cardH = 78.f * menuScale;
            const float cardR = 8.f;
            const float cardInner = 8.f * menuScale;

            ImColor cardBg = ColorUtils::getUiCardColor(1.0f);
            ImColor cardBorder = ColorUtils::getUiBorderColor(1.0f);
            ImColor cardText = ColorUtils::getUiTextColor(1.0f);
            ImColor cardDim = ColorUtils::getUiTextDimColor(1.0f);
            cardBg.Value.w = 0.92f * menuA;
            cardBorder.Value.w = 0.45f * menuA;
            cardText.Value.w = 0.95f * menuA;
            cardDim.Value.w = 0.70f * menuA;

            const float rightInset = 8.f * menuScale;
            const float iconSize = cardH - cardInner * 2.0f;

            if (profiles.empty())
            {
                const std::string emptyText = "No profiles";
                ImVec2 tSize = ImGui::GetFont()->CalcTextSizeA(16.f, FLT_MAX, 0.0f, emptyText.c_str());
                ImVec2 tPos = ImVec2((rightMin.x + rightMax.x) * 0.5f - tSize.x * 0.5f, (rightMin.y + rightMax.y) * 0.5f - tSize.y * 0.5f);
                drawListFg->AddText(ImGui::GetFont(), 16.f, tPos, cardDim, emptyText.c_str());
            }
            else
            {
                ImGui::SetNextWindowPos(ImVec2(rightMin.x + rightInset, rightMin.y + rightInset));
                ImGui::SetNextWindowSize(ImVec2(std::max(0.f, rightW - rightInset * 2.f), std::max(0.f, innerH - rightInset * 2.f)));
                ImGui::Begin("##spoof_right_panel", nullptr, inputFlags);

                ImGui::BeginChild("##spoof_profiles_scroll", ImVec2(0.f, 0.f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

                const float cardW = std::max(0.f, ImGui::GetContentRegionAvail().x);
                auto panelDrawList = ImGui::GetWindowDrawList();

                for (int i = 0; i < static_cast<int>(profiles.size()); i++)
                {
                    const auto& p = profiles[i];

                    ImGui::PushID(i);
                    ImVec2 cardMin = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##profile_card", ImVec2(cardW, cardH));

                    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                    bool hovered = ImGui::IsItemHovered();
                    ImVec2 cardMax = ImGui::GetItemRectMax();

                    ImColor bg = cardBg;
                    if (hovered)
                        bg.Value.w = MathUtils::clamp(bg.Value.w * 1.06f, 0.f, 1.f);

                    panelDrawList->AddRectFilled(cardMin, cardMax, bg, cardR);
                    panelDrawList->AddRect(cardMin, cardMax, cardBorder, cardR, 0, 1.1f);

                    ImVec2 iconMin = ImVec2(cardMin.x + cardInner, cardMin.y + cardInner);
                    ImVec2 iconMax = ImVec2(iconMin.x + iconSize, iconMin.y + iconSize);
                    if (headTex)
                        panelDrawList->AddImage(headTex, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, static_cast<int>(255 * menuA)));

                    float textX = iconMax.x + cardInner;
                    float textY = cardMin.y + cardInner;
                    const std::string nameText = p.name.empty() ? "Unnamed" : p.name;
                    const std::string dateText = formatProfileTime(p.createdAt);
                    const std::string ownerText = p.owner.empty() ? "NoPlayer" : p.owner;

                    panelDrawList->AddText(ImGui::GetFont(), 16.f, ImVec2(textX, textY), cardText, nameText.c_str());
                    panelDrawList->AddText(ImGui::GetFont(), 14.f, ImVec2(textX, textY + 22.f * menuScale), cardDim, dateText.c_str());
                    panelDrawList->AddText(ImGui::GetFont(), 14.f, ImVec2(textX, textY + 40.f * menuScale), cardDim, ownerText.c_str());

                    if (clicked)
                    {
                        std::snprintf(profileNameBuf, sizeof(profileNameBuf), "%s", p.name.c_str());
                        std::snprintf(deviceModelBuf, sizeof(deviceModelBuf), "%s", p.deviceModel.c_str());
                        std::snprintf(deviceIdBuf, sizeof(deviceIdBuf), "%s", p.deviceId.c_str());
                        std::snprintf(skinIdBuf, sizeof(skinIdBuf), "%s", p.skinId.c_str());
                        std::snprintf(selfSignedIdBuf, sizeof(selfSignedIdBuf), "%s", p.selfSignedId.c_str());
                        std::snprintf(clientRandomIdBuf, sizeof(clientRandomIdBuf), "%lld", static_cast<long long>(p.clientRandomId));
                        osIndex = p.os;
                        inputIndex = p.input;

                        DeviceSpoof::sUseCustom = true;
                        DeviceSpoof::sCustomDeviceModel = p.deviceModel;
                        DeviceSpoof::sCustomDeviceId = p.deviceId;
                        DeviceSpoof::sCustomSkinId = p.skinId;
                        DeviceSpoof::sCustomSelfSignedId = p.selfSignedId;
                        DeviceSpoof::sCustomClientRandomId = p.clientRandomId;

                        EditionFaker::sUseCustom = true;
                        EditionFaker::sCustomOs = p.os;
                        EditionFaker::sCustomInput = p.input;

                        auto editionFaker = gFeatureManager->mModuleManager->getModule<EditionFaker>();
                        if (editionFaker)
                        {
                            editionFaker->mOs.setIndex(p.os);
                            editionFaker->mInputMethod.setIndex(p.input);
                            if (editionFaker->mEnabled) editionFaker->spoofEdition();
                        }
                        auto deviceSpoof = gFeatureManager->mModuleManager->getModule<DeviceSpoof>();
                        if (deviceSpoof) deviceSpoof->spoofMboard();
                    }

                    ImGui::Dummy(ImVec2(0.f, cardPad));
                    ImGui::PopID();
                }

                ImGui::EndChild();
                ImGui::End();
            }
            FontHelper::popPrefFont();
        }
    }

}

void Interface::onActorRenderEvent(ActorRenderEvent& event)
{
    if (event.isCancelled()) return;
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;
    
    if (event.mEntity != player) return;
    if (*event.mPos == glm::vec3(0.f, 0.f, 0.f) && *event.mRot == glm::vec2(0.f, 0.f)) return;

    bool firstPerson = ClientInstance::get()->getOptions()->mThirdPerson->value == 0;
    if (firstPerson && !player->getFlag<RenderCameraComponent>()) return;


    const auto actorRotations = event.mEntity->getActorRotationComponent();
    const auto headRotations = event.mEntity->getActorHeadRotationComponent();
    const auto bodyRotations = event.mEntity->getMobBodyRotationComponent();
    if (!actorRotations || !headRotations || !bodyRotations) return;

    float realOldPitch = actorRotations->mOldPitch;
    float realPitch = actorRotations->mPitch;
    float realHeadRot = headRotations->mHeadRot;
    float realOldHeadRot = headRotations->mOldHeadRot;
    float realBodyYaw = bodyRotations->yBodyRot;
    float realOldBodyYaw = bodyRotations->yOldBodyRot;

    actorRotations->mOldPitch = pLerpedPitch;
    actorRotations->mPitch = pLerpedPitch;
    headRotations->mHeadRot = pLerpedHeadYaw;
    headRotations->mOldHeadRot = pLerpedHeadYaw;
    bodyRotations->yBodyRot = pLerpedBodyYaw;
    bodyRotations->yOldBodyRot = pLerpedBodyYaw;

    auto original = event.mDetour->getOriginal<&ActorRenderDispatcherHook::render>();
    original(event._this, event.mEntityRenderContext, event.mEntity, event.mCameraTargetPos, event.mPos, event.mRot, event.mIgnoreLighting);
    event.cancel();

    actorRotations->mOldPitch = realOldPitch;
    actorRotations->mPitch = realPitch;
    headRotations->mHeadRot = realHeadRot;
    headRotations->mOldHeadRot = realOldHeadRot;
    bodyRotations->yBodyRot = realBodyYaw;
    bodyRotations->yOldBodyRot = realOldBodyYaw;
}

void Interface::onDrawImageEvent(DrawImageEvent& event)
{
    if (!mSlotEasing.mValue) return;
    // Wait for ImGui to be initialized
    if (!ImGui::GetCurrentContext()) return;

    static glm::vec2 hotbarPos = {};
    auto path = event.mTexture->mTexture->mFilePath.c_str();

    // textures/ui/selected_hotbar_slot
    // If the selected hotbar slot is being drawn
    if (strcmp(path, "textures/ui/selected_hotbar_slot") == 0)
    {
        float deltaTime = ImGui::GetIO().DeltaTime;
        if (hotbarPos.x == 0 || hotbarPos.y == 0) hotbarPos = *event.mPos;
        hotbarPos.x = MathUtils::lerp(hotbarPos.x, event.mPos->x, deltaTime * mSlotEasingSpeed.mValue);
        hotbarPos.y = event.mPos->y;
        *event.mPos = hotbarPos;
    }
}


void Interface::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();

    BodyYaw::updateRenderAngles(player, pYaw);
    pOldBodyYaw = pBodyYaw;
    pBodyYaw = BodyYaw::bodyYaw;
}

void Interface::onPacketOutEvent(PacketOutEvent& event)
{
    if (event.mPacket->getId() == PacketID::PlayerAuthInput) usingPaip = true;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;
    auto level = player->getLevel();
    if (!level) return;
    auto moveSettings = level->getPlayerMovementSettings();
    if (!moveSettings) return;
    bool isServerAuthoritative = usingPaip;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput && isServerAuthoritative)
    {
        auto paip = event.getPacket<PlayerAuthInputPacket>();

        pOldYaw = pYaw;
        pOldPitch = pPitch;
        pOldBodyYaw = pBodyYaw;
        pYaw = paip->mRot.y;
        pPitch = paip->mRot.x;
        pHeadYaw = paip->mYHeadRot;
    }
    else if (event.mPacket->getId() == PacketID::MovePlayer && !isServerAuthoritative)
    {
        auto mpp = event.getPacket<MovePlayerPacket>();
        pOldYaw = pYaw;
        pOldPitch = pPitch;
        pOldBodyYaw = pBodyYaw;
        pYaw = mpp->mRot.y;
        pPitch = mpp->mRot.x;
        pHeadYaw = mpp->mYHeadRot;
    }
}
