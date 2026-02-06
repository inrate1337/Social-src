#include "CustomChat.hpp"

#include <Features/Events/ChatEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/KeyEvent.hpp>
#include <SDK/Minecraft/Network/Packets/TextPacket.hpp>
#include <SDK/Minecraft/Network/Packets/CommandRequestPacket.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>

namespace {
    constexpr size_t kMaxStoredMessages = 200;
    constexpr size_t kMaxCachedMessages = 200;

    template <typename T>
    void trimToMax(std::vector<T>& vec, size_t maxCount)
    {
        if (vec.size() <= maxCount) return;
        vec.erase(vec.begin(), vec.begin() + static_cast<std::ptrdiff_t>(vec.size() - maxCount));
    }
}

void CustomChat::onEnable()
{
    gFeatureManager->mDispatcher->listen<PacketInEvent, &CustomChat::onPacketInEvent, nes::event_priority::ABSOLUTE_LAST>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &CustomChat::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<KeyEvent, &CustomChat::onKeyEvent>(this);
}

void CustomChat::onDisable()
{
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &CustomChat::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &CustomChat::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<KeyEvent, &CustomChat::onKeyEvent>(this);
    mInputFocused = false;
    mInputBuffer.clear();
    mMessages.clear();
    mCachedMessages.clear();
}

struct ParsedText {
    std::string text;
    ImVec2 pos;
    ImU32 color;
};

enum class ChatColor : char {
    BLACK = '0',
    DARK_BLUE = '1',
    DARK_GREEN = '2',
    DARK_AQUA = '3',
    DARK_RED = '4',
    DARK_PURPLE = '5',
    GOLD = '6',
    GRAY = '7',
    DARK_GRAY = '8',
    BLUE = '9',
    GREEN = 'a',
    AQUA = 'b',
    RED = 'c',
    LIGHT_PURPLE = 'd',
    YELLOW = 'e',
    WHITE = 'f'
};

static const std::unordered_map<char, ImU32> colorMap = {
    {'0', IM_COL32(0, 0, 0, 255)},         
    {'1', IM_COL32(0, 0, 170, 255)},       
    {'2', IM_COL32(0, 170, 0, 255)},       
    {'3', IM_COL32(0, 170, 170, 255)},     
    {'4', IM_COL32(170, 0, 0, 255)},       
    {'5', IM_COL32(170, 0, 170, 255)},     
    {'6', IM_COL32(255, 170, 0, 255)},     
    {'7', IM_COL32(170, 170, 170, 255)},   
    {'8', IM_COL32(85, 85, 85, 255)},      
    {'9', IM_COL32(85, 85, 255, 255)},     
    {'a', IM_COL32(85, 255, 85, 255)},     
    {'b', IM_COL32(85, 255, 255, 255)},    
    {'c', IM_COL32(255, 85, 85, 255)},     
    {'d', IM_COL32(255, 85, 255, 255)},    
    {'e', IM_COL32(255, 255, 85, 255)},    
    {'f', IM_COL32(255, 255, 255, 255)},   
    {'r', IM_COL32(255, 255, 255, 255)},   
};

template <typename T>
ImU32 getColorValue(const std::unordered_map<char, ImU32>& map, char code) {
    auto it = map.find(code);
    if (it != map.end()) {
        return it->second;
    }
    return IM_COL32(255, 255, 255, 255); 
}

std::vector<ParsedText> parseMessage(const std::string& message) {
    std::vector<ParsedText> parsedText;
    ImU32 currentColor = IM_COL32(255, 255, 255, 255); 
    std::string currentSegment;

    for (size_t i = 0; i < message.length(); ++i) {
        if (message[i] == '§') {
            if (!currentSegment.empty()) {
                ParsedText p;
                p.text = currentSegment;
                p.color = currentColor;
                parsedText.emplace_back(p);
                currentSegment.clear();
            }

            if (i + 1 < message.length()) {
                char codeChar = std::tolower(static_cast<unsigned char>(message[i + 1]));
                auto it = colorMap.find(codeChar);
                if (it != colorMap.end()) {
                    currentColor = it->second;
                    i++;
                }
                else {
                    currentSegment += '§';
                    currentSegment += message[i + 1];
                    i++;
                }
            }
            else {
                currentSegment += '§';
            }
        }
        else {
            currentSegment += message[i];
        }
    }

    if (!currentSegment.empty()) {
        ParsedText p;
        p.text = currentSegment;
        p.color = currentColor;
        parsedText.emplace_back(p);
    }

    return parsedText;
}

void CustomChat::addMessage(std::string message) {
    ChatMessage chatMessage;
    chatMessage.mText = message;
    chatMessage.mLifeTime = mMaxLifeTime.mValue;
    chatMessage.mTime = std::chrono::system_clock::now();
    chatMessage.mPercent = 0.f;
    chatMessage.mCount = 1;
    mMessages.push_back(chatMessage);
    trimToMax(mMessages, kMaxStoredMessages);
}

void CustomChat::sendMessage(const std::string& message) {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (message.empty()) return;

    auto ci = ClientInstance::get();
    if (!ci) return;

    if (message[0] == '/') {
        auto pkt = MinecraftPackets::createPacket<CommandRequestPacket>();
        pkt->mCommand = message.substr(1);
        pkt->mOrigin.mType = CommandOriginType::Player;
        pkt->mOrigin.mUuid = mce::UUID::generate();
        pkt->mOrigin.mRequestId = "";
        pkt->mOrigin.mPlayerId = player->getRuntimeID();
        pkt->mInternalSource = false;
        ci->getPacketSender()->sendToServer(pkt.get());
    }
    else {
        auto pkt = MinecraftPackets::createPacket<TextPacket>();
        pkt->mType = TextPacketType::Chat;
        pkt->mMessage = message;
        pkt->mAuthor = player->getRawName();
        ci->getPacketSender()->sendToServer(pkt.get());
    }
}

void CustomChat::onKeyEvent(KeyEvent& event) {
    if (!mEnableInput.mValue) return;

    auto ci = ClientInstance::get();
    if (!ci) return;

    if (event.mPressed && !mInputFocused) {
        if (event.mKey == 'T' || event.mKey == VK_RETURN) {
            mInputFocused = true;
            mInputBuffer.clear();
            ci->releaseMouse();
            event.cancel();
            return;
        }
        if (event.mKey == VK_OEM_2) { 
            mInputFocused = true;
            mInputBuffer = "/";
            ci->releaseMouse();
            event.cancel();
            return;
        }
    }

    if (mInputFocused) {
        event.cancel(); 

        if (event.mPressed) {
            if (event.mKey == VK_RETURN) {
                if (!mInputBuffer.empty()) {
                    sendMessage(mInputBuffer);
                }
                mInputBuffer.clear();
                mInputFocused = false;
                ci->grabMouse();
                return;
            }

            if (event.mKey == VK_ESCAPE) {
                mInputBuffer.clear();
                mInputFocused = false;
                ci->grabMouse();
                return;
            }

            if (event.mKey == VK_BACK && !mInputBuffer.empty()) {
                mInputBuffer.pop_back();
                return;
            }

            if (event.mKey >= 32 && event.mKey <= 126) {
                char c = static_cast<char>(event.mKey);

                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                if (c >= 'A' && c <= 'Z') {
                    if (!shift) c = std::tolower(c);
                }
                else if (shift) {
                    const char shiftMap[] = ")!@#$%^&*(";
                    if (c >= '0' && c <= '9') {
                        c = shiftMap[c - '0'];
                    }
                }

                mInputBuffer += c;
            }

            if (event.mKey == VK_SPACE) {
                mInputBuffer += ' ';
            }
        }
    }
}

void CustomChat::onRenderEvent(RenderEvent& event)
{
    FontHelper::pushPrefFont();
    auto drawList = ImGui::GetBackgroundDrawList();
    const auto delta = ImGui::GetIO().DeltaTime;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 windowSize = { 600, 450 };
    ImVec2 windowPos = { 10, displaySize.y - 85.f };
    float rounding = 15.f;
    float totalHeight = 0.f;
    float fontSize = 20.0f;

    static float easedHeight = 0.f;
    static float maxHeight = 0.f;

    auto ci = ClientInstance::get();
    auto isInGyat = ci && (ci->getScreenName() == "chat_screen" || mInputFocused);

    ImRect rect = ImVec4(windowPos.x, windowPos.y - 10.f, windowPos.x + windowSize.x, windowPos.y - 10 - easedHeight);
    ImRect flipped = ImVec4(rect.Min.x, rect.Max.y, rect.Max.x, rect.Min.y);

    if (rect.Min.y - rect.Max.y >= 1)
    {
        ImRenderUtils::addBlur(rect.ToVec4(), 3, rounding);
        drawList->AddRectFilled(flipped.Min, flipped.Max, IM_COL32(0, 0, 0, 200), rounding);

        drawList->PushClipRect({ rect.Min.x, rect.Max.y }, { rect.Max.x, rect.Min.y });

        auto fontHeight = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0, "").y;

        ImVec2 cursorPos = { windowPos.x + 10, windowPos.y - 15.f };
        auto now = std::chrono::system_clock::now();

        for (auto it = mMessages.rbegin(); it != mMessages.rend(); ) {
            float elapsed = std::chrono::duration<float>(now - it->mTime).count();
            bool hasElapsed = elapsed >= it->mLifeTime;

            if (hasElapsed && !isInGyat) {
                it->mPercent -= delta * 2.5f;
                if (easedHeight > cursorPos.y) {
                    it = std::reverse_iterator(mMessages.erase((++it).base()));
                    continue;
                }
            }
            else {
                it->mPercent = MathUtils::lerp(it->mPercent, 1.f, delta * 8.f);
            }
            it->mPercent = std::clamp(it->mPercent, 0.f, 1.f);

            if (it->mPercent > 0.0f) {
                cursorPos.y = MathUtils::lerp(cursorPos.y, cursorPos.y - fontHeight - 5.0f, hasElapsed && !isInGyat ? 1.f : it->mPercent);
                int alpha = static_cast<int>(255 * ((!hasElapsed || isInGyat) ? it->mPercent : 1.f));

                drawList->AddText(
                    ImGui::GetFont(),
                    fontSize,
                    cursorPos,
                    IM_COL32(255, 255, 255, alpha),
                    it->mText.c_str()
                );

                if (it->mCount > 1) {
                    std::string countText = " x" + std::to_string(it->mCount);
                    drawList->AddText(
                        ImGui::GetFont(),
                        fontSize,
                        { cursorPos.x + ImGui::CalcTextSize(it->mText.c_str()).x + 5.0f, cursorPos.y },
                        IM_COL32(170, 170, 170, alpha),
                        countText.c_str()
                    );
                }
            }

            if (!hasElapsed || isInGyat) {
                if (mMessages.size() < 12) {
                    totalHeight += (fontHeight + 5.0f);
                    maxHeight = totalHeight;
                }
                else {
                    totalHeight = maxHeight;
                }
            }
            ++it;
        }

        easedHeight = MathUtils::lerp(easedHeight, isInGyat ? (fontHeight + 5.0f) * 12 : totalHeight, delta * 8.f);
        drawList->PopClipRect();
    }
    else {
        auto fontHeight = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0, "").y;
        easedHeight = MathUtils::lerp(easedHeight, isInGyat ? (fontHeight + 5.0f) * 12 : totalHeight, delta * 8.f);
    }

    if (mInputFocused) {
        float inputGap = 5.0f;
        float inputHeight = 35.0f;
        ImVec2 inputPanelMin = { windowPos.x, windowPos.y + inputGap };
        ImVec2 inputPanelMax = { windowPos.x + windowSize.x, windowPos.y + inputGap + inputHeight };

        ImRenderUtils::addBlur(ImVec4(inputPanelMin.x, inputPanelMin.y, inputPanelMax.x, inputPanelMax.y), 3, rounding);
        drawList->AddRectFilled(inputPanelMin, inputPanelMax, IM_COL32(0, 0, 0, 200), rounding);

        ImVec2 inputPos = { windowPos.x + 10, windowPos.y + inputGap + 7.5f }; 

        mCursorBlinkTime += delta;
        bool showCursor = fmod(mCursorBlinkTime, 1.0f) < 0.5f;

        std::string displayText = mInputBuffer;
        if (showCursor) displayText += "_";

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            inputPos,
            IM_COL32(255, 255, 255, 255),
            displayText.c_str()
        );
    }

    FontHelper::popPrefFont();
}

void CustomChat::onPacketInEvent(PacketInEvent& event)
{
    if (event.isCancelled()) return;
    if (event.mPacket->getId() != PacketID::Text) return;
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto textPacket = event.getPacket<TextPacket>();
    std::string message = textPacket->getText();

    if (textPacket->mType == TextPacketType::Chat) {
        message = textPacket->mAuthor.empty() ? message : "<" + textPacket->mAuthor + "> " + message;
    }

    if (!mMessages.empty() && mMessages.back().mText == message) {
        mMessages.back().mCount++;
        mMessages.back().mTime = std::chrono::system_clock::now();
        mMessages.back().mLifeTime = mMaxLifeTime.mValue;
        mMessages.back().mPercent = 0.f;
        mCachedMessages.push_back(mMessages.back());
        trimToMax(mCachedMessages, kMaxCachedMessages);
        return;
    }

    ChatMessage chatMessage;
    chatMessage.mText = message;
    chatMessage.mLifeTime = mMaxLifeTime.mValue;
    chatMessage.mTime = std::chrono::system_clock::now();
    chatMessage.mPercent = 0.f;
    chatMessage.mCount = 1;

    mMessages.push_back(chatMessage);
    mCachedMessages.push_back(chatMessage);
    trimToMax(mMessages, kMaxStoredMessages);
    trimToMax(mCachedMessages, kMaxCachedMessages);
}
