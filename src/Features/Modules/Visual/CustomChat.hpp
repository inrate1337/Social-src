#pragma once

class CustomChat : public ModuleBase<CustomChat> {
public:
    NumberSetting mMaxLifeTime = NumberSetting("Life Time", "The max amount of seconds a message will be displayed for", 5, 1, 8, 1);
    BoolSetting mEnableInput = BoolSetting("Enable Input", "Allow typing in custom chat", true);

    CustomChat() : ModuleBase("CustomChat", "A customized, Solstice-themed chat!", ModuleCategory::Visual, 0, false) {
        mNames = {
            {Lowercase, "customchat"},
            {LowercaseSpaced, "custom chat"},
            {Normal, "CustomChat"},
            {NormalSpaced, "Custom Chat"},
        };
        addSettings(&mMaxLifeTime, &mEnableInput);
    }

    struct ChatMessage {
        std::chrono::time_point<std::chrono::system_clock> mTime;
        float mLifeTime;
        std::string mText;
        int mCount = 1;
        float mPercent;
    };

    std::vector<ChatMessage> mMessages;
    std::vector<ChatMessage> mCachedMessages;
    std::string mLastMessage;
    std::map<std::string, int> messageCount;

    std::string mInputBuffer;
    bool mInputFocused = false;
    float mCursorBlinkTime = 0.0f;

    void addMessage(std::string message);
    void sendMessage(const std::string& message);

    void onEnable() override;
    void onDisable() override;
    void onRenderEvent(class RenderEvent& event);
    void onPacketInEvent(class PacketInEvent& event);
    void onKeyEvent(class KeyEvent& event);
};
