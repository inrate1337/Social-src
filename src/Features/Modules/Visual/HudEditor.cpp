#include "HudEditor.hpp"

#include <Features/Events/KeyEvent.hpp>
#include <Features/Events/ModuleStateChangeEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/MinecraftGame.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>

#include "ClickGui.hpp"

#include <cstdio>

bool lastCscState = false;

static bool isRightAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopRight || anchor == HudElement::Anchor::MiddleRight || anchor == HudElement::Anchor::BottomRight;
}

static bool isMiddleXAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::TopMiddle || anchor == HudElement::Anchor::Middle || anchor == HudElement::Anchor::BottomMiddle;
}

static bool isBottomAnchored(HudElement::Anchor anchor)
{
    return anchor == HudElement::Anchor::BottomLeft || anchor == HudElement::Anchor::BottomMiddle || anchor == HudElement::Anchor::BottomRight;
}

static ImVec2 getElementTopLeft(const HudElement* element)
{
    ImVec2 pos = element->getPos();
    ImVec2 size = element->mSize;

    if (element->mCentered)
    {
        pos.x -= size.x * 0.5f;
        pos.y -= size.y * 0.5f;
        return pos;
    }

    if (isRightAnchored(element->mAnchor)) pos.x -= size.x;
    else if (isMiddleXAnchored(element->mAnchor)) pos.x -= size.x * 0.5f;

    if (isBottomAnchored(element->mAnchor)) pos.y -= size.y;

    return pos;
}

static ImVec2 topLeftToElementRefPos(const HudElement* element, ImVec2 topLeft)
{
    ImVec2 size = element->mSize;

    if (element->mCentered)
    {
        return ImVec2(topLeft.x + size.x * 0.5f, topLeft.y + size.y * 0.5f);
    }

    ImVec2 ref = topLeft;

    if (isRightAnchored(element->mAnchor)) ref.x += size.x;
    else if (isMiddleXAnchored(element->mAnchor)) ref.x += size.x * 0.5f;

    if (isBottomAnchored(element->mAnchor)) ref.y += size.y;

    return ref;
}

static void setElementTopLeftFixedAnchor(HudElement* element, ImVec2 topLeft)
{
    if (!element) return;
    const ImVec2 refPos = topLeftToElementRefPos(element, topLeft);
    const ImVec2 anchorPos = HudElement::getAnchorPos(element->mAnchor);
    element->mPos = { refPos.x - anchorPos.x, refPos.y - anchorPos.y };
}

void HudEditor::showAllElements()
{
    for (auto element : mElements) {
        if (!element) continue;
        element->mSampleMode = true;
    }
}

void HudEditor::hideAllElements()
{
    for (auto element : mElements) {
        if (!element) continue;
        element->mSampleMode = false;
    }
}

void HudEditor::saveToFile()
{
    try
    {
        static std::string path = FileUtils::getSolsticeDir() + "hud.json";
        nlohmann::json j;
        for (auto element : mElements)
        {
            if (!element) continue;
            if (!element->mParentTypeIdentifier || element->mParentTypeIdentifier[0] == '\0') continue;
            j[element->mParentTypeIdentifier] = {
                {"pos", {element->mPos.x, element->mPos.y}},
                {"size", {element->mSize.x, element->mSize.y}},
                {"anchor", element->mAnchor}
            };

            spdlog::info("Saved element: {}", element->mParentTypeIdentifier);
        }

        for (auto& element : mCustomElements)
        {
            if (!element) continue;
            if (!element->mParentTypeIdentifier || element->mParentTypeIdentifier[0] == '\0') continue;
            j[element->mParentTypeIdentifier] = {
                {"pos", {element->mPos.x, element->mPos.y}},
                {"size", {element->mSize.x, element->mSize.y}},
                {"anchor", element->mAnchor},
                {"text", element->mText},
                {"fontSize", element->mFontSize},
                {"bold", element->mBold},
                {"useThemeColor", element->mUseThemeColor},
                {"color", {element->mColor.Value.x, element->mColor.Value.y, element->mColor.Value.z, element->mColor.Value.w}}
            };

            spdlog::info("Saved custom element: {}", element->mText);
        }

        j["snapDistance"] = mSnapDistance;

        std::ofstream file(path);
        file << j.dump(4);
        file.close();

        spdlog::info("Saved hud elements to file!");
    }
    catch (const std::exception& e)
    {
        spdlog::error("Failed to save hud elements to file: {}", e.what());
    }
    catch (const nlohmann::json::exception& e)
    {
        spdlog::error("Failed to save hud elements to file: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("Failed to save hud elements to file: unknown error");
    }
}

void HudEditor::loadFromFile()
{
    try
    {
        static std::string path = FileUtils::getSolsticeDir() + "hud.json";
        if (!FileUtils::fileExists(path))
        {
            spdlog::warn("No hud elements file found, creating one!");
            saveToFile();
            return;
        }
        nlohmann::json j;
        std::ifstream file(path);
        file >> j;
        file.close();

        for (auto element : mElements)
        {
            if (!element) continue;
            if (!element->mParentTypeIdentifier || element->mParentTypeIdentifier[0] == '\0') continue;
            if (j.contains(element->mParentTypeIdentifier))
            {
                auto& data = j[element->mParentTypeIdentifier];
                element->mPos = { data["pos"][0], data["pos"][1] };
                element->mSize = { data["size"][0], data["size"][1] };
                element->mAnchor = data["anchor"];

                j.erase(element->mParentTypeIdentifier);
            }
        }

        if (j.contains("snapDistance"))
        {
            mSnapDistance = j["snapDistance"];
            j.erase("snapDistance");
        }

        for (auto& [key, value] : j.items())
        {
            const bool useThemeColor = value.contains("useThemeColor") ? static_cast<bool>(value["useThemeColor"]) : false;
            auto element = std::make_unique<CustomHudElement>(key.c_str(), value["text"], CustomHudElement::Type::Text, value["text"], useThemeColor, ImColor(static_cast<float>(value["color"][0]), value["color"][1], value["color"][2], value["color"][3]));
            element->mPos = { value["pos"][0], value["pos"][1] };
            element->mSize = { value["size"][0], value["size"][1] };
            element->mAnchor = value["anchor"];
            element->mFontSize = value["fontSize"];
            element->mBold = value["bold"];
            element->mUseThemeColor = useThemeColor;

            auto colorArray = value["color"];
            element->mColor = ImColor(static_cast<float>(colorArray[0]), colorArray[1], colorArray[2], colorArray[3]);

            std::snprintf(element->mInputBuffer, sizeof(element->mInputBuffer), "%s", element->mText.c_str());

            bool found = false;
            for (auto& customElement : mCustomElements)
            {
                if (customElement && customElement->mParentTypeIdentifier && customElement->mParentTypeIdentifier == key)
                {
                    customElement->mPos = { value["pos"][0], value["pos"][1] };
                    customElement->mSize = { value["size"][0], value["size"][1] };
                    customElement->mAnchor = value["anchor"];
                    customElement->mFontSize = value["fontSize"];
                    customElement->mText = value["text"];
                    customElement->mUseThemeColor = useThemeColor;
                    customElement->mColor = ImColor(static_cast<float>(colorArray[0]), colorArray[1], colorArray[2], colorArray[3]);
                    customElement->mVisible = true;
                    customElement->mBold = value["bold"];
                    std::snprintf(customElement->mInputBuffer, sizeof(customElement->mInputBuffer), "%s", customElement->mText.c_str());
                    found = true;
                }
            }

            if (!found)
                mCustomElements.push_back(std::move(element));
        }

        spdlog::info("Loaded hud elements from file!");
    }
    catch (const std::exception& e)
    {
        spdlog::error("Failed to load hud elements from file: {}", e.what());
    }
    catch (const nlohmann::json::exception& e)
    {
        spdlog::error("Failed to load hud elements from file: {}", e.what());
        saveToFile();
    }
    catch (...)
    {
        spdlog::error("Failed to load hud elements from file: unknown error");
    }
}

void HudEditor::onInit()
{
    loadFromFile();
}

void HudEditor::onEnable()
{
    gFeatureManager->mDispatcher->listen<KeyEvent, &HudEditor::onKeyEvent>(this);
    gFeatureManager->mDispatcher->listen<MouseEvent, &HudEditor::onMouseEvent>(this);
    gFeatureManager->mDispatcher->listen<ModuleStateChangeEvent, &HudEditor::onModuleStateChangeEvent, nes::event_priority::FIRST>(this);

    auto ci = ClientInstance::get();
    if (ci)
    {
        lastCscState = ci->getMouseGrabbed();
        ci->releaseMouse();
    }
    else
    {
        lastCscState = false;
    }

    loadFromFile();
    showAllElements();

    static auto clickGuiModule = gFeatureManager->mModuleManager->getModule<ClickGui>();
    if (clickGuiModule) clickGuiModule->setEnabled(false);
}

void HudEditor::onDisable()
{
    gFeatureManager->mDispatcher->deafen<KeyEvent, &HudEditor::onKeyEvent>(this);
    gFeatureManager->mDispatcher->deafen<MouseEvent, &HudEditor::onMouseEvent>(this);
    gFeatureManager->mDispatcher->deafen<ModuleStateChangeEvent, &HudEditor::onModuleStateChangeEvent>(this);

    auto ci = ClientInstance::get();
    if (ci)
    {
        if (lastCscState) ci->grabMouse();
        else ci->releaseMouse();
    }

    saveToFile();
    hideAllElements();
}

void HudEditor::onRenderEvent(RenderEvent& event)
{
    if (!mEnabled) return;

    static auto clickGuiModule = gFeatureManager->mModuleManager->getModule<ClickGui>();
    if (clickGuiModule && clickGuiModule->mEnabled) {
        clickGuiModule->setEnabled(false);
    }

    auto drawList = ImGui::GetBackgroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;

    auto ci = ClientInstance::get();
    if (ci) ci->releaseMouse();

    drawList->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 50));

    const int snapDistance = mSnapDistance;

    static bool dragging = false;
    static ImVec2 dragStart;
    static ImVec2 dragOffset;
    static ImVec2 dragSmoothedTopLeft;
    static HudElement* draggedElement = nullptr;

    if (!mCustomGuiOpen)
    {
        const float cx = display.x * 0.5f;
        const float cy = display.y * 0.5f;
        drawList->AddLine(ImVec2(cx, 0.f), ImVec2(cx, display.y), IM_COL32(255, 255, 255, 140), 1.5f);
        drawList->AddLine(ImVec2(0.f, cy), ImVec2(display.x, cy), IM_COL32(255, 255, 255, 140), 1.5f);
    }

    if (ImGui::IsMouseClicked(0) && !mCustomGuiOpen)
    {
        dragStart = ImGui::GetMousePos();
        dragOffset = { 0, 0 };
        draggedElement = nullptr;
        dragging = false;

        for (auto element : mElements)
        {
            if (!element) continue;
            if (!element->mVisible) continue;

            ImVec2 pos = getElementTopLeft(element);
            ImVec2 size = element->mSize;

            if (dragStart.x > pos.x && dragStart.x < pos.x + size.x &&
                dragStart.y > pos.y && dragStart.y < pos.y + size.y)
            {
                dragging = true;
                draggedElement = element;
                dragOffset = { dragStart.x - pos.x, dragStart.y - pos.y };
                dragSmoothedTopLeft = pos;
                break;
            }
        }

        for (auto& element : mCustomElements)
        {
            if (!element->mVisible) continue;

            ImVec2 pos = getElementTopLeft(element.get());
            ImVec2 size = element->mSize;

            if (dragStart.x > pos.x && dragStart.x < pos.x + size.x &&
                dragStart.y > pos.y && dragStart.y < pos.y + size.y)
            {
                dragging = true;
                draggedElement = element.get();
                dragOffset = { dragStart.x - pos.x, dragStart.y - pos.y };
                dragSmoothedTopLeft = pos;
                break;
            }
        }
    }

    if (dragging && draggedElement != nullptr)
    {
        const float delta = ImGui::GetIO().DeltaTime;
        const ImVec2 mouse = ImGui::GetMousePos();

        ImVec2 targetTopLeft = ImVec2(mouse.x - dragOffset.x, mouse.y - dragOffset.y);

        const ImVec2 size = draggedElement->mSize;

        if (!mCustomGuiOpen)
        {
            const float cx = display.x * 0.5f;
            const float cy = display.y * 0.5f;

            const float elemCenterX = targetTopLeft.x + size.x * 0.5f;
            const float elemCenterY = targetTopLeft.y + size.y * 0.5f;

            const bool crossesCenterX = targetTopLeft.x <= cx && (targetTopLeft.x + size.x) >= cx;
            const bool crossesCenterY = targetTopLeft.y <= cy && (targetTopLeft.y + size.y) >= cy;

            if (crossesCenterX || std::fabs(elemCenterX - cx) <= mSnapPointDist) targetTopLeft.x = cx - size.x * 0.5f;
            if (crossesCenterY || std::fabs(elemCenterY - cy) <= mSnapPointDist) targetTopLeft.y = cy - size.y * 0.5f;
        }

        targetTopLeft.x = std::clamp(targetTopLeft.x, 0.f, display.x - size.x);
        targetTopLeft.y = std::clamp(targetTopLeft.y, 0.f, display.y - size.y);

        const float speed = 28.f;
        const float t = (delta <= 0.f) ? 1.f : (1.f - std::exp(-speed * delta));
        dragSmoothedTopLeft.x = dragSmoothedTopLeft.x + (targetTopLeft.x - dragSmoothedTopLeft.x) * t;
        dragSmoothedTopLeft.y = dragSmoothedTopLeft.y + (targetTopLeft.y - dragSmoothedTopLeft.y) * t;

        setElementTopLeftFixedAnchor(draggedElement, dragSmoothedTopLeft);

        if (!ImGui::IsMouseDown(0))
        {
            setElementTopLeftFixedAnchor(draggedElement, targetTopLeft);
            dragging = false;
            draggedElement = nullptr;
        }
    }

    for (auto element : mElements)
    {
        if (!element) continue;
        if (!element->mVisible) continue;

        if (dragging && draggedElement == element) continue;
        ImVec2 topLeft = getElementTopLeft(element);
        topLeft.x = std::clamp(topLeft.x, 0.f, display.x - element->mSize.x);
        topLeft.y = std::clamp(topLeft.y, 0.f, display.y - element->mSize.y);
        setElementTopLeftFixedAnchor(element, topLeft);
    }

    for (auto& element : mCustomElements)
    {
        if (!element->mVisible) continue;

        if (dragging && draggedElement == element.get()) continue;
        ImVec2 topLeft = getElementTopLeft(element.get());
        topLeft.x = std::clamp(topLeft.x, 0.f, display.x - element->mSize.x);
        topLeft.y = std::clamp(topLeft.y, 0.f, display.y - element->mSize.y);
        setElementTopLeftFixedAnchor(element.get(), topLeft);
    }

    for (auto element : mElements)
    {
        if (!element) continue;
        if (!element->mVisible) continue;

        ImVec2 pos = getElementTopLeft(element);
        ImVec2 size = element->mSize;

        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 125));
    }

    for (auto& element : mCustomElements)
    {
        if (!element->mVisible) continue;

        ImVec2 pos = getElementTopLeft(element.get());
        ImVec2 size = element->mSize;

        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 125));
    }

    float radius = 15;
    ImVec2 buttonPos = { 30, display.y - 30 };
    ImVec4 buttonRect = { buttonPos.x - radius, buttonPos.y - radius, buttonPos.x + radius, buttonPos.y + radius };

    static ImColor butColor = ImColor(255, 255, 255, 125);
    ImColor targetColor = mCustomGuiOpen ? ImColor(0, 255, 0, 200) : ImRenderUtils::isMouseOver(buttonRect) ? ImColor(255, 255, 255, 255) : ImColor(255, 255, 255, 125);
    butColor.Value = ImLerp(butColor.Value, targetColor.Value, ImGui::GetIO().DeltaTime * 10);

    drawList->AddCircleFilled(buttonPos, radius, butColor);
    drawList->AddLine(ImVec2(buttonRect.x + 5, buttonRect.y + radius), ImVec2(buttonRect.z - 5, buttonRect.y + radius), IM_COL32(0, 0, 0, 255), 2);
    drawList->AddLine(ImVec2(buttonRect.x + radius, buttonRect.y + 5), ImVec2(buttonRect.x + radius, buttonRect.w - 5), IM_COL32(0, 0, 0, 255), 2);

    if (ImGui::IsMouseClicked(0)) {
        if (ImRenderUtils::isMouseOver(buttonRect)) {
            mCustomGuiOpen = !mCustomGuiOpen;
        }
    }

    if (!mCustomGuiOpen) return;

    ImVec2 center = { display.x / 2, display.y / 2 };
    ImVec2 size = { 700, 700 };
    ImVec2 pos = { center.x - size.x / 2, center.y - size.y / 2 };

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("Hud Editor", &mCustomGuiOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

    ImGui::Text("Edit custom hud elements here!");

    if (ImGui::Button("Add Text"))
    {
        auto element = std::make_unique<CustomHudElement>(StringUtils::randomString(32).c_str(), "Sample Text", CustomHudElement::Type::Text, "Sample Text", false, ImColor(255, 255, 255, 255));
        element->mVisible = true;
        std::snprintf(element->mInputBuffer, sizeof(element->mInputBuffer), "%s", element->mText.c_str());

        mCustomElements.push_back(std::move(element));
        NotifyUtils::notify("Added new text element!", 1.f, Notification::Type::Info);
    }

    for (int i = 0; i < mCustomElements.size(); i++)
    {
        auto& element = mCustomElements[i];
        if (!element->mVisible) continue;

        std::string inputLabel = "Text##" + std::to_string(i);
        ImGui::InputText(inputLabel.c_str(), element->mInputBuffer, 256);

        ImGui::SameLine();

        std::string fontSizeLabel = "Font Size##" + std::to_string(i);
        ImGui::InputFloat(fontSizeLabel.c_str(), &element->mFontSize);

        std::string boldLabel = "Bold##" + std::to_string(i);
        ImGui::Checkbox(boldLabel.c_str(), &element->mBold);

        ImGui::SameLine();

        ImGui::Checkbox(("Use Theme Color##" + std::to_string(i)).c_str(), &element->mUseThemeColor);

        if (!element->mUseThemeColor)
        {
            ImGui::ColorEdit4(("Color##" + std::to_string(i)).c_str(), &element->mColor.Value.x);
        }

        if (ImGui::Button(("Delete##" + std::to_string(i)).c_str()))
        {
            NotifyUtils::notify("Deleted element!", 1.f, Notification::Type::Info);

            mCustomElements.erase(mCustomElements.begin() + i);
            i--;
        }

        ImGui::SameLine();

        if (ImGui::Button(("Set##" + std::to_string(i)).c_str()))
        {
            element->mText = element->mInputBuffer;
            NotifyUtils::notify("Set text!", 1.f, Notification::Type::Info);
        }

        ImGui::SameLine();

        if (ImGui::Button(("Clear##" + std::to_string(i)).c_str()))
        {
            element->mText = "";
            element->mInputBuffer[0] = '\0';
            NotifyUtils::notify("Cleared text!", 1.f, Notification::Type::Info);
        }
    }

    ImGui::End();
}

void HudEditor::onCustomRenderEvent(RenderEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci || !ci->getLocalPlayer()) return;
    for (auto& element : mCustomElements)
    {
        FontHelper::pushPrefFont(element->mFontSize > 30, element->mBold);

        ImVec2 pos = element->getPos();

        auto drawList = ImGui::GetBackgroundDrawList();

        ImColor color = element->mUseThemeColor ? ColorUtils::getThemedColor(0) : element->mColor;

        drawList->AddText(ImGui::GetFont(), element->mFontSize, pos, color, element->mText.c_str());
        ImVec2 size = ImGui::GetFont()->CalcTextSizeA(element->mFontSize, FLT_MAX, 0, element->mText.c_str());
        element->mSize = { size.x, size.y };

        FontHelper::popPrefFont();
    }
}

bool mShiftHeld = false;

void HudEditor::onKeyEvent(KeyEvent& event)
{
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    if (event.mKey == mKey && event.mPressed)
    {
        setEnabled(false);
        return;
    }

    if (event.mKey == VK_SHIFT)
    {
        mShiftHeld = event.mPressed;
        event.cancel();
        return;
    }

    if (event.mKey == VK_ESCAPE && event.mPressed)
    {
        event.cancel();
        if (mCustomGuiOpen) mCustomGuiOpen = false;
        else setEnabled(false);
    }
}

void HudEditor::onMouseEvent(MouseEvent& event)
{
    event.cancel();
}

void HudEditor::onModuleStateChangeEvent(ModuleStateChangeEvent& event)
{
    if (!event.mEnabled) return;
    if (!gFeatureManager || !gFeatureManager->mModuleManager) return;

    auto* clickGuiModule = gFeatureManager->mModuleManager->getModule<ClickGui>();
    if (clickGuiModule && event.mModule == clickGuiModule)
    {
        event.cancel();
    }
}
