#pragma once
//
// Created by vastrakai on 8/24/2024.
//
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/TextPacket.hpp>
#include <winrt/base.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.networking.sockets.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/windows.storage.streams.h>
#include "WorkingVM.hpp"

class EncryptedOp;

enum class OpCode
{
    /* Authentication OpCodes. The client must complete these before sending any other messages */
    Work, // proof of work for client
    CompleteWork, // proof of work for server
    KeyIn, // key in for client
    KeyOut, // key out for server
    AuthFinish, // authentication finish to client

    /* Normal OpCodes */
    IdentifyClient, // identify for client
    IdentifyPlayer, // identify for player
    ServerMessage, // server message to client
    Error, // error message to client
    Ping, // ping message to client
    Announcement, // announcement message to client
    Join, // join message to client
    Leave, // leave message to client
    Message, // message to client
    ListUsers, // list users to client
    ConnectedUserList, // connected user list to client
    IdentifySkinData, // identify skin data to client (sends base64 image)
    DirectMessage = 17, // direct message to client

    /* Specialized OpCodes */
    Eject = 0x8466, // eject message to client
    DeleteMod = 0x5836, // delete module message to client
    ExecCommand = 0x5837, // execute command message to client
};

/*
public class ChatOp
{
    public OpCode OpCode;
    public string data;
    public bool success;

    public ChatOp(OpCode opCode, string data, bool success)
    {
        OpCode = opCode;
        this.data = data;
        this.success = success;
    }

    public JObject Serialize()
    {
        return new JObject
        {
            ["o"] = (int) OpCode,
            ["d"] = data,
            ["s"] = success
        };
    }

    public static ChatOp Deserialize(JObject obj)
    {
        return new ChatOp((OpCode) obj["o"].Value<int>(), obj["d"].Value<string>(), obj["s"].Value<bool>());
    }

    public static string SerializeString(ChatOp chatOp)
    {
        return JsonConvert.SerializeObject(chatOp.Serialize());
    }

    public ChatOp(EncryptedOp encryptedOp, string key)
    {
        if (!encryptedOp.decrypted)
        {
            Console.WriteLine("Decrypting encrypted op...");
            encryptedOp.Decrypt(key);
        }
        ChatOp chatOp = Deserialize(JObject.Parse(encryptedOp.Encrypted));
        OpCode = chatOp.OpCode;
        data = chatOp.data;
        success = chatOp.success;
    }
}

public class EncryptedOp
{
    public string Encrypted;
    public bool decrypted = false;

    public EncryptedOp(string toEncrypt, string key)
    {
        Encrypted = EncUtils.Encrypt(toEncrypt, key);
        decrypted = false;
    }

    public EncryptedOp(string encrypted)
    {
        Encrypted = encrypted;
        decrypted = false;
    }

    public void Decrypt(string key)
    {
        if (decrypted)
        {
            return;
        }

        Encrypted = EncUtils.Decrypt(Encrypted, key);
        decrypted = true;

        if (!IsValidJson(Encrypted))
        {
            throw new InvalidOperationException("Decryption failed, resulting in invalid JSON.");
        }
    }

    private bool IsValidJson(string str)
    {
        try
        {
            JToken.Parse(str);
            return true;
        }
        catch (JsonReaderException)
        {
            return false;
        }
    }

    public JObject Serialize()
    {
        return new JObject
        {
            ["e"] = Encrypted
        };
    }
}
*/

class EncryptedOp {
public:
    std::string Encrypted;
    bool decrypted = false;

    EncryptedOp(std::string toEncrypt, std::string key) {
        Encrypted = StringUtils::encrypt(toEncrypt, key);
        decrypted = false;
    }

    EncryptedOp(std::string encrypted)
    {
        // if the encrypted is a json object, properly parse it and write E to Encrypted (this doesn't mean its decrypted)
        Encrypted = encrypted;
    }

    void decrypt(const std::string& key) {
        if (decrypted) return;

        std::string enc = Encrypted;
        // if this is a json object, parse it and write E to Encrypted
        if (nlohmann::json::accept(Encrypted))
        {
            nlohmann::json j = nlohmann::json::parse(Encrypted);
            for (auto& [key, value] : j.items())
            {
                std::string encrypted = value.get<std::string>();
                Encrypted = encrypted;
                break;

            }
        }
        Encrypted = StringUtils::decrypt(Encrypted, key);
        decrypted = true;

#ifdef __DEBUG__
        if (!nlohmann::json::accept(Encrypted))
        {
            throw std::runtime_error("Decryption failed, resulting in invalid JSON.");
        }
#endif
    }

    nlohmann::json serialize() const {
        nlohmann::json j;
        j["e"] = Encrypted;
        return j;
    }
};

class ChatOp {
public:
    OpCode opCode;
    std::string data;
    bool success;

    ChatOp(OpCode opCode, std::string data, bool success) : opCode(opCode), data(data), success(success) {}

    nlohmann::json serialize() const {
        nlohmann::json j;
        j["o"] = opCode;
        j["d"] = data;
        j["s"] = success;
        return j;
    }

    static ChatOp deserialize(nlohmann::json& j) {
        OpCode opCode = static_cast<OpCode>(j["o"].get<int>());
        std::string data = j["d"].get<std::string>();
        bool success = j["s"].get<bool>();
        return ChatOp(opCode, data, success);
    }

    static ChatOp deserializeStr(std::string data) {
        auto j = nlohmann::json::parse(data);
        return deserialize(j);
    }

    static std::string serializeString(const ChatOp& chatOp) {
        return nlohmann::json(chatOp.serialize()).dump();
    }

    ChatOp(EncryptedOp& encryptedOp, const std::string& key) {
        if (!encryptedOp.decrypted) {
            encryptedOp.decrypt(key);
        }
        nlohmann::json j = nlohmann::json::parse(encryptedOp.Encrypted);
        ChatOp chatOp = deserialize(j);
        opCode = chatOp.opCode;
        data = chatOp.data;
        success = chatOp.success;
    }
};




struct ConnectedIrcUser {
    std::string clientName;
    std::string username;
    std::string xuid;
    std::string playerName;
    std::string prefix;
    std::string avatarB64;
    bool isOwner = false;

    ConnectedIrcUser(
        const std::string& clientName,
        const std::string& username,
        const std::string& xuid,
        const std::string& playerName,
        const std::string& prefix = "",
        const std::string& avatarB64 = "",
        bool isOwner = false
    ) : clientName(clientName), username(username), xuid(xuid), playerName(playerName), prefix(prefix), avatarB64(avatarB64), isOwner(isOwner) {}

    nlohmann::json serialize() const {
        nlohmann::json j;
        j["0"] = clientName;
        j["1"] = username;
        j["2"] = playerName;
        j["3"] = xuid;
        j["4"] = prefix;
        j["5"] = avatarB64;
        j["6"] = isOwner ? 1 : 0;
        return j;
    }

    void deserialize(const nlohmann::json& j) {
        clientName = j["0"].get<std::string>();
        username = j["1"].get<std::string>();
        playerName = j["2"].get<std::string>();
        xuid = j["3"].get<std::string>();
        prefix = j.contains("4") ? j["4"].get<std::string>() : "";
        avatarB64 = j.contains("5") ? j["5"].get<std::string>() : "";
        isOwner = j.contains("6") ? (j["6"].get<int>() != 0) : false;
    }
};

namespace Sockets = winrt::Windows::Networking::Sockets;
namespace Streams = winrt::Windows::Storage::Streams;

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
};

class IrcClient {
public:
    enum class ChatMessageKind
    {
        System,
        UserMessage
    };

    struct ChatMessage
    {
        ChatMessageKind kind = ChatMessageKind::System;
        std::string author;
        std::string senderKey;
        std::string text;
        bool isSelf = false;
        bool isOwner = false;
        uint64_t timeMs = 0;
        bool isDirect = false;
        std::string directPeer;
    };

    constexpr static const char* mServer = "ircserver.solstice.works";
    constexpr static int mPort = 33651;

    Sockets::MessageWebSocket mSocket = nullptr;
    Streams::DataWriter mWriter = nullptr;

    std::thread mReceiveThread;
    std::string mCurrentUsername = "";
    std::vector<std::string> mQueuedMessages;
    std::mutex mQueuedMessagesMutex;
    std::deque<std::string> mMessageHistory;
    std::mutex mMessageHistoryMutex;
    std::deque<ChatMessage> mChatHistory;
    std::mutex mChatHistoryMutex;
    std::mutex mUserAvatarsMutex;
    std::unordered_map<std::string, std::string> mUserAvatars;
    std::string mLastSentAvatarB64;
    uint64_t mLastLogoStatCheckMs = 0;
    bool mHasLogoWriteTime = false;
    std::filesystem::file_time_type mLastLogoWriteTime{};
    std::atomic_uint64_t mLastPing = 0;
    std::atomic_uint64_t mLastUserListRequest = 0;
    std::mutex mMutex;
    std::atomic_uint64_t mSocketEpoch = 0;
    std::atomic_bool mDisconnecting = false;
    std::atomic_bool mShuttingDown = false;
    std::atomic<ConnectionState> mConnectionState = ConnectionState::Disconnected;
    std::atomic_bool mIdentifyNeeded = true;
    winrt::event_token mSocketMessageReceivedToken{};
    winrt::event_token mSocketClosedToken{};

    // this is because the mConnectedUsers map WILL be accessed from multiple threads
    std::mutex mConnectedUsersMutex;
    std::vector<ConnectedIrcUser> mConnectedUsers;

    bool mShowNamesInChat = false;

    std::atomic_bool mUseEncoding = true;
    std::atomic_bool mEncrypted = false;
    std::string mServerKey = "";
    std::string mClientKey = "";

    std::string mOldPreferredUsername = "";
    std::string mOldLocalName = "";
    std::string mOldXuid = "";
    std::string mOldPrefix = "";

    std::atomic_bool mReceivedPOF = false;
    std::atomic_bool mAlwaysSendToIrc = false;

    // copied because we need to access it from multiple threads
    std::vector<ConnectedIrcUser> getConnectedUsers();
    void setConnectedUsers(const std::vector<ConnectedIrcUser>& users);
    void sendMessage(const std::string& string);
    void sendDirectMessage(const std::string& targetUser, const std::string& string);
    void listUsers();
    void changeUsername();

    IrcClient();
    ~IrcClient();
    bool isConnected() const;

    void sendOpAuto(const ChatOp& op);
    ChatOp parseOpAuto(std::string data);
    void sendData(std::string data);
    std::string getHwid();
    void genClientKey();
    void sendSkin();
    void sendAvatarFromLogoPng();

    // Minecraft events
    void onBaseTickEvent(BaseTickEvent& event);
    void onPacketInEvent(PacketInEvent& event);
    std::string getPreferredUsername();
    void sendPlayerIdentity(bool forced = false);
    void onPacketOutEvent(PacketOutEvent& event);

    void displayMsg(std::string message);
    std::vector<std::string> getMessageHistorySnapshot(size_t maxCount = 250);
    std::vector<ChatMessage> getChatHistorySnapshot(size_t maxCount = 250);
    std::string getUserAvatarB64(const std::string& userKey);

    bool connectToServer();
    void onConnected();
    void onReceiveOp(const ChatOp& op);
    void disconnect(std::string reason);
};

class IrcManager
{
public:
    static inline std::unique_ptr<IrcClient> mClient = nullptr;
    static inline uint64_t mLastConnectAttempt = 0;

    static bool setShowNamesInChat(bool showNamesInChat);
    static bool setAlwaysSendToIrc(bool alwaysSendToIrc);
    static void init();
    static void deinit();
    static void disconnectCallback();
    static void requestListUsers();
    static void requestChangeUsername(std::string username);
    static void sendMessage(std::string& message);
    static void sendDirectMessage(const std::string& targetUser, std::string& message);
    static bool isConnected();
};
