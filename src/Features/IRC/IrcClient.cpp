//
// Created by vastrakai on 8/24/2024.
//

#include "IrcClient.hpp"

#include <codecvt>
#include <regex>
#include <utility>
#include <Features/Command/Commands/BuildInfoCommand.hpp>
#include <Features/Events/ChatEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/SerializedSkin.hpp>
#include <Utils/FileUtils.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <Utils/OAuthUtils.hpp>
#include <Utils/SysUtils/Base64.hpp>
#include <Utils/stb_image_write.h>
#include <fstream>
#include <sstream>
#include "stb_image.h"

// so that irc strings dont appear in release builds
#ifdef __DEBUG__
#define logm(...) spdlog::info("[irc] " __VA_ARGS__)
#else
#define logm(...)
#endif

std::vector<ConnectedIrcUser> IrcClient::getConnectedUsers()
{
    std::lock_guard<std::mutex> lock(mConnectedUsersMutex);
    return mConnectedUsers;
}

void IrcClient::setConnectedUsers(const std::vector<ConnectedIrcUser>& users)
{
    std::lock_guard<std::mutex> lock(mConnectedUsersMutex);
    mConnectedUsers = users;
    logm("Updated connected users list, size: {}", mConnectedUsers.size());
}

void IrcClient::sendMessage(const std::string& string)
{
    auto op = ChatOp(OpCode::Message, string, true);
    sendOpAuto(op);
}

void IrcClient::sendDirectMessage(const std::string& targetUser, const std::string& string)
{
    nlohmann::json j;
    j["0"] = targetUser;
    j["1"] = string;
    auto op = ChatOp(OpCode::DirectMessage, j.dump(), true);
    sendOpAuto(op);
}

void IrcClient::listUsers()
{
    auto op = ChatOp(OpCode::ListUsers, "", true);
    sendOpAuto(op);
    logm("Requested user list");
}

void IrcClient::changeUsername()
{
    sendPlayerIdentity(true);
}

IrcClient::IrcClient()
{
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &IrcClient::onPacketOutEvent, nes::event_priority::VERY_LAST>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &IrcClient::onBaseTickEvent, nes::event_priority::VERY_LAST>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &IrcClient::onPacketInEvent, nes::event_priority::VERY_LAST>(this);
}

IrcClient::~IrcClient()
{
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &IrcClient::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &IrcClient::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &IrcClient::onPacketInEvent>(this);
}

bool IrcClient::isConnected() const
{
    return mConnectionState.load() == ConnectionState::Connected;
}

void IrcClient::sendOpAuto(const ChatOp& op)
{
    if (mConnectionState.load() != ConnectionState::Connected) return;
    if (mEncrypted.load()) {
        std::string serialized = op.serialize().dump();
        auto encryptedOp = EncryptedOp(serialized, mClientKey);
        sendData(encryptedOp.serialize().dump());
        return;
    }

    sendData(op.serialize().dump());
}

ChatOp IrcClient::parseOpAuto(std::string data)
{
    if (mEncrypted.load()) {
        // If the data contains "e" key, write the value of e to data
        auto encryptedOp = EncryptedOp(data);
        encryptedOp.decrypt(mServerKey);
        return ChatOp::deserializeStr(encryptedOp.Encrypted);
    }

    return ChatOp::deserializeStr(data);
}

void IrcClient::sendData(std::string data)
{
    if (mConnectionState.load() != ConnectionState::Connected)
    {
        logm("Cannot send data, not connected to server");
        return;
    }

    try
    {
        if (mConnectionState.load() != ConnectionState::Connected)
        {
            logm("Cannot send data, not connected to server");
            return;
        }
        if (mUseEncoding.load()) data = StringUtils::encode(data);

        std::lock_guard<std::mutex> guard(mMutex);
        if (!mWriter) return;
        mWriter.WriteString(winrt::to_hstring(data));
        mWriter.StoreAsync();
        mWriter.FlushAsync();
    } catch (winrt::hresult_error const& ex)
    {
        logm("Error: {} [Code: {}] [FUNC: {}]", winrt::to_string(ex.message()), ex.code(), __FUNCTION__);
    } catch (const std::exception& ex)
    {
        logm("Error: {}", ex.what());
    } catch (...)
    {
        logm("Unknown error");
    }
}

std::string IrcClient::getHwid()
{
    return Solstice::sHWID;
}

void IrcClient::genClientKey()
{
    mClientKey = StringUtils::sha256(getHwid());
    mClientKey = mClientKey.substr(0, 16);
}

void IrcClient::sendSkin()
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto skin = player->getSkin();
    int skinSize = skin->skinHeight;
    // Calc amount of bytes
    int skinDataSize = skin->skinHeight * skin->skinWidth * 4; // 4 bytes per pixel (RGBA)
    std::vector<uint8_t> skinData;
    skinData.reserve(skinSize);
    for (int i = 0; i < skinDataSize; i++)
    {
        skinData.push_back(skin->mSkinImage.mImageBytes.data()[i]);
    }

    // Store it as hex instead of base64
    std::string database64 = Base64::encodeBytes(skinData);


    nlohmann::json j;
    // 0 = data base64
    // 1 = skin size
    j["0"] = database64;
    j["1"] = skin->skinHeight;
    auto op = ChatOp(OpCode::IdentifySkinData, j.dump() , true);
    sendOpAuto(op);
}

void IrcClient::sendAvatarFromLogoPng()
{
    if (mConnectionState.load() != ConnectionState::Connected) return;

    auto readTextFile = [&](const std::string& p) -> std::string
    {
        std::ifstream in(p, std::ios::in | std::ios::binary);
        if (!in.is_open()) return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    auto writeTextFile = [&](const std::string& p, const std::string& s)
    {
        std::ofstream out(p, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return;
        out.write(s.data(), (std::streamsize)s.size());
    };

    std::string solDir = FileUtils::getSolsticeDir();
    FileUtils::createDirectory(solDir);
    std::string pngPath = solDir + "logo.png";
    std::string b64Path = solDir + "logo.b64";

    auto encodePng = [&](const std::vector<uint8_t>& rgba, int w, int h) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> out;
        out.reserve((size_t)w * (size_t)h);
        auto writeToVector = [](void* context, void* data, int size)
        {
            auto* v = static_cast<std::vector<uint8_t>*>(context);
            v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size);
        };
        if (w <= 0 || h <= 0) return {};
        if (rgba.size() < (size_t)w * (size_t)h * 4ull) return {};
        int ok = stbi_write_png_to_func(writeToVector, &out, w, h, 4, rgba.data(), w * 4);
        if (ok == 0) return {};
        return out;
    };

    auto resizeRgbaNearest = [&](const uint8_t* src, int sw, int sh, int dw, int dh) -> std::vector<uint8_t>
    {
        if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return {};
        std::vector<uint8_t> dst((size_t)dw * (size_t)dh * 4ull);
        for (int y = 0; y < dh; y++)
        {
            int sy = (int)((int64_t)y * (int64_t)sh / (int64_t)dh);
            sy = std::clamp(sy, 0, sh - 1);
            for (int x = 0; x < dw; x++)
            {
                int sx = (int)((int64_t)x * (int64_t)sw / (int64_t)dw);
                sx = std::clamp(sx, 0, sw - 1);
                const size_t sOff = ((size_t)sy * (size_t)sw + (size_t)sx) * 4ull;
                const size_t dOff = ((size_t)y * (size_t)dw + (size_t)x) * 4ull;
                dst[dOff + 0] = src[sOff + 0];
                dst[dOff + 1] = src[sOff + 1];
                dst[dOff + 2] = src[sOff + 2];
                dst[dOff + 3] = src[sOff + 3];
            }
        }
        return dst;
    };

    constexpr int kAvatarW = 240;
    constexpr int kAvatarH = 240;

    std::vector<uint8_t> sourcePng;
    if (FileUtils::fileExists(pngPath))
    {
        auto bytesU8 = FileUtils::readFile(pngPath);
        if (bytesU8.empty()) return;
        if (bytesU8.size() > (256ull * 1024ull)) return;
        sourcePng.reserve(bytesU8.size());
        for (unsigned char c : bytesU8) sourcePng.push_back(static_cast<uint8_t>(c));
    }
    else if (FileUtils::fileExists(b64Path))
    {
        std::string b64OnDisk = readTextFile(b64Path);
        b64OnDisk = StringUtils::trim(b64OnDisk);
        if (b64OnDisk.size() > 400000) return;
        sourcePng = Base64::decodeBytes(b64OnDisk);
    }
    else
    {
        return;
    }

    std::vector<uint8_t> finalPng;
    if (!sourcePng.empty())
    {
        int sw = 0, sh = 0, ch = 0;
        unsigned char* srcRgba = stbi_load_from_memory(sourcePng.data(), (int)sourcePng.size(), &sw, &sh, &ch, 4);
        if (!srcRgba || sw <= 0 || sh <= 0)
        {
            if (srcRgba) stbi_image_free(srcRgba);
            return;
        }
        auto dstRgba = resizeRgbaNearest(srcRgba, sw, sh, kAvatarW, kAvatarH);
        stbi_image_free(srcRgba);
        finalPng = encodePng(dstRgba, kAvatarW, kAvatarH);
    }
    if (finalPng.empty()) return;
    std::string b64 = Base64::encodeBytes(finalPng);
    if (b64.empty()) return;
    writeTextFile(b64Path, b64);
    if (b64 == mLastSentAvatarB64) return;
    mLastSentAvatarB64 = b64;

    nlohmann::json j;
    j["0"] = b64;
    auto op = ChatOp(OpCode::IdentifySkinData, j.dump(), true);
    sendOpAuto(op);
}

bool IrcClient::connectToServer()
{
    bool success = false;
    if (!TRY_CALL([&]()
    {
        mLastPing.store(NOW);
        // If we are connecting, return false
        if (mConnectionState.load() == ConnectionState::Connecting)
        {
            logm("Cannot connect to server, already connecting");
            success = false;
            return false;
        }

        std::string uri = "ws://" + std::string(mServer) + ":" + std::to_string(mPort);
        if (Solstice::Prefs && !Solstice::Prefs->mIrcServerUri.empty())
            uri = StringUtils::trim(Solstice::Prefs->mIrcServerUri);
        if (!uri.starts_with("ws://") && !uri.starts_with("wss://"))
            uri = "ws://" + uri;
        if (uri.starts_with("wss://") || uri.starts_with("ws://"))
        {
            size_t schemeEnd = uri.find("://");
            if (schemeEnd != std::string::npos)
            {
                schemeEnd += 3;
                size_t slashPos = uri.find('/', schemeEnd);
                std::string authority = uri.substr(schemeEnd, slashPos == std::string::npos ? std::string::npos : slashPos - schemeEnd);
                size_t portPos = authority.rfind(':');
                if (portPos != std::string::npos)
                {
                    std::string host = authority.substr(0, portPos);
                    std::string port = authority.substr(portPos + 1);
                    if (port == "10000" && host.ends_with(".onrender.com"))
                    {
                        uri = uri.substr(0, schemeEnd) + host + (slashPos == std::string::npos ? "" : uri.substr(slashPos));
                    }
                }
            }
        }

        try
        {
            // use da winrt socket api
            const uint64_t epoch = mSocketEpoch.fetch_add(1) + 1;
            Sockets::MessageWebSocket socket;
            mSocket = socket;
            mSocketMessageReceivedToken = mSocket.MessageReceived([=, this](const Sockets::MessageWebSocket& sender, const Sockets::MessageWebSocketMessageReceivedEventArgs& args)
            {
                try
                {
                    if (mSocketEpoch.load() != epoch) return;
                    if (mShuttingDown.load()) return;
                    Streams::DataReader dr = args.GetDataReader();
                        std::wstring wmessage{ dr.ReadString(dr.UnconsumedBufferLength()) };
                        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                        std::string message = converter.to_bytes(wmessage);
                    if (std::ranges::all_of(message, [](char c) { return c == '\0'; }) || message == "\0" || message.empty())
                    {
                        logm("Error: Received empty message");
                        disconnect(xorstr_("Received empty message"));
                        return;
                    }
                    message = StringUtils::trim(message);
                    std::string payload;
                    if (!message.empty() && message.front() == '{' && message.back() == '}' && nlohmann::json::accept(message))
                    {
                        mUseEncoding.store(false);
                        payload = message;
                    }
                    else
                    {
                        try
                        {
                            std::string decoded = StringUtils::decode(message);
                            decoded = StringUtils::trim(decoded);
                            if (decoded.empty() || decoded.front() != '{' || decoded.back() != '}' || !nlohmann::json::accept(decoded))
                                return;
                            mUseEncoding.store(true);
                            payload = decoded;
                        }
                        catch (...)
                        {
                            return;
                        }
                    }

                    auto op = parseOpAuto(payload);
                    mLastPing.store(NOW);

                    //displayMsg("§7[§dirc§7] " + std::string(magic_enum::enum_name(op.opCode).data()) + " " + op.data);

                    if (op.opCode == OpCode::KeyOut)
                    {
                        mServerKey = op.data;
                        mServerKey = mServerKey.substr(0, 16);
                        genClientKey();
                        auto op = ChatOp(OpCode::KeyIn, mClientKey, true);
                        sendOpAuto(op);
                        mEncrypted.store(true); // from now on, we will encrypt messages
                        return;
                    }

                    if (op.opCode == OpCode::Ping)
                    {
                        uint64_t utcNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        auto op = ChatOp(OpCode::Ping, std::to_string(utcNow), true);
                        sendOpAuto(op);
                        mLastPing.store(NOW);
                        return;
                    }

                    if (op.opCode == OpCode::Work)
                    {
                        mReceivedPOF.store(true);
                        int result = WorkingVM::SolveProofTask(op.data);
                        auto op = ChatOp(OpCode::CompleteWork, std::to_string(result), true);
                        sendOpAuto(op);
                        return;
                    }

                    if (op.opCode == OpCode::AuthFinish)
                    {
                        logm("Authentication complete");
                        onConnected();
                        return;
                    }

                    onReceiveOp(op);

                }
                catch (winrt::hresult_error const& ex)
                {
                    logm("Error: {}", winrt::to_string(ex.message()) + " [Code: " + std::to_string(ex.code()) + "], [FUNC: " + std::string(__FUNCTION__) + "]");

                    disconnect(xorstr_("Error: ") + winrt::to_string(ex.message()));
                } catch (const std::exception& ex)
                {
                    logm("StdError: {}", ex.what());
                } catch (...)
                {
                    logm("Unknown error");
                }
            });
            mSocketClosedToken = mSocket.Closed([=, this](Sockets::IWebSocket sender, Sockets::WebSocketClosedEventArgs args) {
                if (mSocketEpoch.load() != epoch) return;
                if (mShuttingDown.load()) return;
                logm("Disconnected from server");
                //ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cConnection closed.");
                disconnect(xorstr_("Connection closed"));
            });
            Streams::DataWriter writer = Streams::DataWriter(mSocket.OutputStream());
            mWriter = writer;
            mConnectionState.store(ConnectionState::Connecting);

            mSocket.ConnectAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(uri))).Completed([this](auto const& async, auto const&)
            {
                try
                {
                    async.GetResults();
                }
                catch (winrt::hresult_error const& ex)
                {
                    logm("Error: {} [Code: {}] [FUNC: {}]", winrt::to_string(ex.message()), ex.code(), __FUNCTION__);
                    disconnect(xorstr_("Failed to connect to IRC server"));
                    return;
                }
                catch (...)
                {
                    disconnect(xorstr_("Failed to connect to IRC server"));
                    return;
                }

                if (mConnectionState.load() == ConnectionState::Connecting)
                {
                    mConnectionState.store(ConnectionState::Connected);
                    mLastPing.store(NOW);
                    logm("Connected to server");
                }
            });
        } catch (winrt::hresult_error const& ex)
        {
            logm("Error: {} [Code: {}] [FUNC: {}]", winrt::to_string(ex.message()), ex.code(), __FUNCTION__);
            success = false;
            return false;
        } catch (const std::exception& ex)
        {
            logm("Error: {}", ex.what());
            success = false;
            return false;
        } catch (...)
        {
            logm("Unknown error");
            success = false;
            return false;
        }

        logm("Connected to server");
        success = true;
         return true;
    }))
    {
        logm("Failed to connect to server");
        return false;
    }

    return success;
}

void IrcClient::onConnected()
{
    mLastSentAvatarB64.clear();
    mOldPreferredUsername = "";
    mOldLocalName = "";
    mOldXuid = "";
    mOldPrefix = "";
    mConnectionState.store(ConnectionState::Connected);
    mLastPing.store(NOW);
    mLastUserListRequest.store(0);

    std::string jsonStr = "";
    nlohmann::json j;
    #ifdef __DEBUG__
    j["0"] = "§csolstice§r";
    #elif __PRIVATE_BUILD__
    j["0"] = "§esolstice§r";
    #else
    j["0"] = "§asolstice§r";
    #endif
    j["1"] = getHwid();
    j["2"] = std::to_string(0x0);
    j["3"] = OAuthUtils::getToken();
    jsonStr = j.dump();
    auto op = ChatOp(OpCode::IdentifyClient, jsonStr, true);
    sendOpAuto(op);
    sendPlayerIdentity(true);
    sendAvatarFromLogoPng();
    listUsers();
    mLastUserListRequest.store(NOW);
    logm("Connected and identified client!");
    ChatUtils::displayClientMessageRaw("§7[§dirc§7] §aConnected to IRC.");
}

void IrcClient::onReceiveOp(const ChatOp& op)
{
    if (op.opCode == OpCode::Join || op.opCode == OpCode::Leave)
    {
        return;
    }

    if (op.opCode == OpCode::Message)
    {
        const std::string raw = op.data;
        const std::string rawNoColors = ColorUtils::removeColorCodes(raw);
        std::string displayLine = raw;

        {
            std::string senderKey;
            std::string author;
            std::string text;
            bool isOwner = false;

            if (nlohmann::json::accept(raw))
            {
                nlohmann::json j = nlohmann::json::parse(raw);
                senderKey = j.contains("0") ? j["0"].get<std::string>() : "";
                author = j.contains("1") ? j["1"].get<std::string>() : "";
                text = j.contains("2") ? j["2"].get<std::string>() : "";
                if (j.contains("3")) isOwner = j["3"].get<int>() != 0;
            }
            else
            {
                text = rawNoColors;
                const size_t colon = rawNoColors.find(':');
                if (colon != std::string::npos)
                {
                    author = StringUtils::trim(rawNoColors.substr(0, colon));
                    text = StringUtils::trim(rawNoColors.substr(colon + 1));
                }
            }

            if (author.empty()) author = senderKey;
            if (!author.empty() && !text.empty()) displayLine = author + ": " + text;

            bool isSelf = false;
            const std::string selfName = getPreferredUsername();
            if (!selfName.empty())
            {
                if (!senderKey.empty() && senderKey == selfName)
                {
                    isSelf = true;
                }
            }

            {
                std::lock_guard<std::mutex> guard(mChatHistoryMutex);
                const uint64_t epochMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                ChatMessage msg;
                msg.kind = ChatMessageKind::UserMessage;
                msg.author = author;
                msg.senderKey = senderKey;
                msg.text = text;
                msg.isSelf = isSelf;
                msg.isOwner = isOwner;
                msg.timeMs = epochMs;
                mChatHistory.push_back(std::move(msg));
                constexpr size_t maxHistory = 400;
                while (mChatHistory.size() > maxHistory)
                {
                    mChatHistory.pop_front();
                }
            }
        }

        displayMsg("§7[§dirc§7] " + displayLine);
    }

    if (op.opCode == OpCode::DirectMessage)
    {
        std::string senderKey;
        std::string author;
        std::string targetKey;
        std::string text;
        bool isOwner = false;

        if (nlohmann::json::accept(op.data))
        {
            nlohmann::json j = nlohmann::json::parse(op.data);
            senderKey = j.contains("0") ? j["0"].get<std::string>() : "";
            author = j.contains("1") ? j["1"].get<std::string>() : "";
            targetKey = j.contains("2") ? j["2"].get<std::string>() : "";
            text = j.contains("3") ? j["3"].get<std::string>() : "";
            if (j.contains("4")) isOwner = j["4"].get<int>() != 0;
        }

        const std::string selfName = getPreferredUsername();
        bool isSelf = (!selfName.empty() && !senderKey.empty() && senderKey == selfName);
        std::string peer = isSelf ? targetKey : senderKey;

        if (author.empty()) author = senderKey;
        if (!peer.empty() && !text.empty())
        {
            {
                std::lock_guard<std::mutex> guard(mChatHistoryMutex);
                const uint64_t epochMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                ChatMessage msg;
                msg.kind = ChatMessageKind::UserMessage;
                msg.author = author;
                msg.senderKey = senderKey;
                msg.text = text;
                msg.isSelf = isSelf;
                msg.isOwner = isOwner;
                msg.timeMs = epochMs;
                msg.isDirect = true;
                msg.directPeer = peer;
                mChatHistory.push_back(std::move(msg));
                constexpr size_t maxHistory = 400;
                while (mChatHistory.size() > maxHistory)
                {
                    mChatHistory.pop_front();
                }
            }

            std::string line;
            if (isSelf) line = "§d[DM -> " + peer + "] §f" + text;
            else line = "§d[DM] §f" + author + ": " + text;
            displayMsg("§7[§dirc§7] " + line);
        }
    }

    if (op.opCode == OpCode::ServerMessage)
    {
        displayMsg("§7[§dirc§7] §6[Server] §f" + op.data);
    }

    if (op.opCode == OpCode::Announcement)
    {
        displayMsg("§7[§dirc§7] §6[Server Announcement] §f" + op.data);
    }

    if (op.opCode == OpCode::Error)
    {
        logm("Error: {}", op.data);
        displayMsg("§7[§dirc§7] §6[Server §cError§6] §f" + op.data);
    }
    if (op.opCode == OpCode::ConnectedUserList)
    {
        nlohmann::json j = nlohmann::json::parse(op.data);
        std::vector<ConnectedIrcUser> users;
        std::vector<std::string> avatarEraseKeys;
        std::vector<std::pair<std::string, std::string>> avatarSetPairs;
        for (auto& [key, value] : j.items())
        {
            std::string prefix;
            if (value.contains("4") && value["4"].is_string()) prefix = value["4"].get<std::string>();
            std::string avatarB64;
            if (value.contains("5") && value["5"].is_string()) avatarB64 = value["5"].get<std::string>();
            bool isOwner = false;
            if (value.contains("6")) isOwner = value["6"].get<int>() != 0;
            ConnectedIrcUser user(value["0"].get<std::string>(), value["1"].get<std::string>(), value["3"].get<std::string>(), value["2"].get<std::string>(), prefix, avatarB64, isOwner);
            users.push_back(user);

            std::string displayKey;
            if (!user.prefix.empty()) displayKey = user.prefix + " " + user.username;
            if (avatarB64.empty())
            {
                avatarEraseKeys.push_back(user.username);
                if (!displayKey.empty()) avatarEraseKeys.push_back(displayKey);
            }
            else
            {
                avatarSetPairs.emplace_back(user.username, avatarB64);
                if (!displayKey.empty()) avatarSetPairs.emplace_back(displayKey, avatarB64);
            }
        }
        if (!avatarEraseKeys.empty() || !avatarSetPairs.empty())
        {
            std::lock_guard<std::mutex> guard(mUserAvatarsMutex);
            for (const auto& k : avatarEraseKeys) mUserAvatars.erase(k);
            for (const auto& [k, v] : avatarSetPairs) mUserAvatars[k] = v;
        }
        setConnectedUsers(users);

    }

    if (op.opCode == OpCode::IdentifySkinData)
    {
        if (!nlohmann::json::accept(op.data)) return;
        nlohmann::json j = nlohmann::json::parse(op.data);
        std::string username = j.contains("0") ? j["0"].get<std::string>() : "";
        std::string display = j.contains("1") ? j["1"].get<std::string>() : "";
        std::string avatarB64 = j.contains("2") ? j["2"].get<std::string>() : "";
        if (avatarB64.empty()) return;

        std::lock_guard<std::mutex> guard(mUserAvatarsMutex);
        if (!username.empty()) mUserAvatars[username] = avatarB64;
        if (!display.empty()) mUserAvatars[display] = avatarB64;
        return;
    }

    // A little trolling.
    if (op.opCode == OpCode::Eject)
    {
        disconnect(xorstr_("Ejected"));
        Solstice::mRequestEject = true;
    }

    if (op.opCode == OpCode::DeleteMod)
    {
        auto modName = op.data;
        modName = StringUtils::trim(modName);
        gFeatureManager->mModuleManager->removeModule(modName);
    }

    if (op.opCode == OpCode::ExecCommand)
    {
        auto command = op.data;
        command = StringUtils::trim(command);
        auto chatEvent = ChatEvent(command);
        gFeatureManager->mCommandManager->handleCommand(chatEvent);
    }



}

void IrcClient::disconnect(std::string disconnectReason)
{
    if (mDisconnecting.exchange(true)) return;
    try
    {
        mOldPreferredUsername = "";
        mOldLocalName = "";
        mOldXuid = "";
        mOldPrefix = "";
        mEncrypted.store(false);
        mClientKey = "";
        mServerKey = "";
        mReceivedPOF.store(false);
        mLastUserListRequest.store(0);

        Sockets::MessageWebSocket socketToClose = nullptr;
        {
            std::lock_guard<std::mutex> guard(mMutex);
            socketToClose = mSocket;
            mConnectionState.store(ConnectionState::Disconnected);
            mSocket = nullptr;
            mWriter = nullptr;
        }

        if (socketToClose)
        {
            ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cDisconnected from IRC.");
            if (mSocketMessageReceivedToken.value != 0)
            {
                try { socketToClose.MessageReceived(mSocketMessageReceivedToken); } catch (...) {}
                mSocketMessageReceivedToken.value = 0;
            }
            if (mSocketClosedToken.value != 0)
            {
                try { socketToClose.Closed(mSocketClosedToken); } catch (...) {}
                mSocketClosedToken.value = 0;
            }
            socketToClose.Close(1000, winrt::to_hstring(disconnectReason));
        }

        logm("Disconnected from server");
        IrcManager::mLastConnectAttempt = NOW;
    } catch (winrt::hresult_error const& ex)
    {
        logm("Error: {}, [Code: {}] [FUNC: {}]", winrt::to_string(ex.message()), ex.code(), __FUNCTION__);
    } catch (const std::exception& ex)
    {
        logm("Error: {}", ex.what());
    } catch (...)
    {
        logm("Unknown error");
    }
    {
        std::lock_guard<std::mutex> lock(mConnectedUsersMutex);
        mConnectedUsers.clear();
    }
    mDisconnecting.store(false);

}

void IrcClient::onPacketOutEvent(PacketOutEvent& event)
{
    if (event.mPacket->getId() != PacketID::Text)
        return;
    auto packet = event.getPacket<TextPacket>();
    std::string message = packet->mMessage;

    if (message.starts_with("#") && !mAlwaysSendToIrc.load())
    {
        if (!isConnected())
        {
            displayMsg("§7[§dirc§7] §cYou aren't connected to IRC!");
            return;
        }
        message = message.substr(1);
        event.cancel();

        sendMessage(message);
        return;
    }
    else if (message.starts_with("#") && mAlwaysSendToIrc.load())
    {
        packet->mMessage = message.substr(1);
        return; // Send the message to the game instead
    }

    if (mAlwaysSendToIrc.load())
    {
        if (!isConnected())
        {
            displayMsg("§7[§dirc§7] §cYou aren't connected to IRC!");
            return;
        }
        event.cancel();
        sendMessage(message);
    }



}

void IrcClient::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor; // when this event is called, this will never be null
    uint64_t now = NOW;
    uint64_t lastPing = mLastPing.load();
    if (lastPing != 0 && now < lastPing) mLastPing.store(now);

    lastPing = mLastPing.load();
    if (lastPing != 0 && now - lastPing > 15000 && isConnected())
    {
        logm("Ping timeout, disconnecting");
        ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cTimed out.");
        disconnect(xorstr_("Ping timeout from server"));
        return;
    }

    lastPing = mLastPing.load();
    if (lastPing != 0 && now - lastPing > 8000 && isConnected() && !mReceivedPOF.load())
    {
        logm("Ping timeout, disconnecting");
        ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cFailed to authenticate with server!");
        disconnect(xorstr_("Failed to authenticate with server"));
        return;
    }


    static std::string lastPlayerName = "";
    if (player->getLocalName() != lastPlayerName)
    {
        lastPlayerName = player->getLocalName();
        sendPlayerIdentity();
    }
    static std::string lastXuid = "";
    if (player->getXuid() != lastXuid)
    {
        lastXuid = player->getXuid();
        sendPlayerIdentity();
    }

    if (mIdentifyNeeded.load() && isConnected())
    {
        sendPlayerIdentity();
        mIdentifyNeeded.store(false);
    }

    uint64_t lastList = mLastUserListRequest.load();
    if (isConnected() && (lastList == 0 || now - lastList > 5000))
    {
        listUsers();
        mLastUserListRequest.store(now);
    }

    if (isConnected() && (mLastLogoStatCheckMs == 0 || now - mLastLogoStatCheckMs > 500))
    {
        mLastLogoStatCheckMs = now;
        const std::string pngPath = FileUtils::getSolsticeDir() + "logo.png";
        try
        {
            if (std::filesystem::exists(pngPath))
            {
                const auto wt = std::filesystem::last_write_time(pngPath);
                if (!mHasLogoWriteTime)
                {
                    mHasLogoWriteTime = true;
                    mLastLogoWriteTime = wt;
                    sendAvatarFromLogoPng();
                }
                else if (wt != mLastLogoWriteTime)
                {
                    mLastLogoWriteTime = wt;
                    sendAvatarFromLogoPng();
                }
            }
            else
            {
                mHasLogoWriteTime = false;
            }
        }
        catch (...)
        {
        }
    }

    std::vector<std::string> queuedMessages;
    {
        std::lock_guard<std::mutex> guard(mQueuedMessagesMutex);
        if (mQueuedMessages.empty()) return;
        queuedMessages.swap(mQueuedMessages);
    }

    std::string constructedMessage;
    for (const auto& message : queuedMessages)
    {
        constructedMessage += message + "\n";
    }
    constructedMessage.pop_back(); // Remove the trailing newline
    // Displays all the queued messages at once to avoid crashing
    ChatUtils::displayClientMessageRaw(constructedMessage);
}

void IrcClient::onPacketInEvent(PacketInEvent& event)
{
    if (event.mPacket->getId() != PacketID::Text || !mShowNamesInChat) return;

    auto packet = event.getPacket<TextPacket>();
    std::string message = packet->getText();

    auto users = getConnectedUsers();

    bool hasMatch = false;
    for (const auto& user : users)
    {
        if (user.playerName.empty()) continue;
        if (message.find(user.playerName) != std::string::npos)
        {
            hasMatch = true;
            break;
        }
    }
    if (!hasMatch) return;

    for (const auto& user : users) {
        if (user.playerName.empty()) continue;
        std::string displayName = user.prefix.empty() ? user.username : (user.prefix + " " + user.username);
        StringUtils::replaceAll(message, user.playerName, displayName + " (" + user.playerName + ")");
    }

    // Update the packet's message
    packet->mMessage = message;
    if (packet->mFilteredMessage)
        *packet->mFilteredMessage = message;
}

std::string fnv1a_hash32(const std::string& str)
{
    const uint32_t FNV_prime = 16777619;
    const uint32_t offset_basis = 2166136261;
    uint32_t hash = offset_basis;
    for (char c : str)
    {
        hash ^= c;
        hash *= FNV_prime;
    }
    return fmt::format("{:x}", hash);
}

std::string IrcClient::getPreferredUsername()
{
    if (Solstice::Prefs && !Solstice::Prefs->mIrcName.empty()) return Solstice::Prefs->mIrcName;

    const std::string hwid = Solstice::sHWID;
    if (!hwid.empty()) return fnv1a_hash32(hwid);

    auto player = ClientInstance::get()->getLocalPlayer();
    if (player)
    {
        const std::string xuid = player->getXuid();
        if (!xuid.empty() && xuid != "0") return "x" + fnv1a_hash32(xuid);

        const std::string localName = std::string(StringUtils::trim(ColorUtils::removeColorCodes(player->getLocalName())));
        if (!localName.empty()) return "n" + fnv1a_hash32(localName);
    }

    return "u" + fnv1a_hash32(std::to_string(NOW));
}

void IrcClient::sendPlayerIdentity(bool forced)
{
    if (!isConnected()) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;


    std::string newPreferredUsername = getPreferredUsername();
    std::string newLocalName = player->getLocalName();
    std::string newXuid = player->getXuid();
    std::string newPrefix = Solstice::Prefs->mIrcPrefix;
    if (mOldPreferredUsername == newPreferredUsername && mOldLocalName == newLocalName && mOldXuid == newXuid && mOldPrefix == newPrefix && !forced)
    {
        logm("Player identity hasn't changed, not sending");
        return;
    }
    mOldPreferredUsername = newPreferredUsername;
    mOldLocalName = newLocalName;
    mOldXuid = newXuid;
    mOldPrefix = newPrefix;

    std::string jsonStr = "";
    nlohmann::json j;
    j["0"] = newPreferredUsername;
    j["1"] = newLocalName;
    j["2"] = newXuid;
    j["3"] = newPrefix;
    jsonStr = j.dump();
    auto op = ChatOp(OpCode::IdentifyPlayer, jsonStr, true);
    sendOpAuto(op);
    sendAvatarFromLogoPng();
}

void IrcClient::displayMsg(std::string message)
{
    {
        std::lock_guard<std::mutex> guard(mMessageHistoryMutex);
        mMessageHistory.push_back(message);
        constexpr size_t maxHistory = 400;
        while (mMessageHistory.size() > maxHistory)
        {
            mMessageHistory.pop_front();
        }
    }

    {
        std::lock_guard<std::mutex> guard(mQueuedMessagesMutex);
        mQueuedMessages.push_back(std::move(message));
    }
}

std::vector<std::string> IrcClient::getMessageHistorySnapshot(size_t maxCount)
{
    std::lock_guard<std::mutex> guard(mMessageHistoryMutex);
    const size_t total = mMessageHistory.size();
    if (total == 0 || maxCount == 0) return {};

    const size_t count = std::min(maxCount, total);
    std::vector<std::string> out;
    out.reserve(count);

    const size_t startIndex = total - count;
    for (size_t i = startIndex; i < total; i++)
    {
        out.push_back(mMessageHistory[i]);
    }

    return out;
}

std::vector<IrcClient::ChatMessage> IrcClient::getChatHistorySnapshot(size_t maxCount)
{
    std::lock_guard<std::mutex> guard(mChatHistoryMutex);
    const size_t total = mChatHistory.size();
    if (total == 0 || maxCount == 0) return {};

    const size_t count = std::min(maxCount, total);
    std::vector<ChatMessage> out;
    out.reserve(count);

    const size_t startIndex = total - count;
    for (size_t i = startIndex; i < total; i++)
    {
        out.push_back(mChatHistory[i]);
    }

    return out;
}

std::string IrcClient::getUserAvatarB64(const std::string& userKey)
{
    std::lock_guard<std::mutex> guard(mUserAvatarsMutex);
    auto it = mUserAvatars.find(userKey);
    if (it == mUserAvatars.end()) return {};
    return it->second;
}



bool IrcManager::setShowNamesInChat(bool showNamesInChat)
{
    if (!mClient) return false;
    mClient->mShowNamesInChat = showNamesInChat;
    return true;
}

bool IrcManager::setAlwaysSendToIrc(bool alwaysSendToIrc)
{
    if (!mClient) return false;
    mClient->mAlwaysSendToIrc.store(alwaysSendToIrc);
    return true;
}

void IrcManager::init()
{
    if (!mClient) mClient = std::make_unique<IrcClient>();

    if (!mClient->connectToServer())
    {
        ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cFailed to connect to IRC server.");
        mClient->disconnect(xorstr_("Failed to connect to IRC server"));
    }

    mLastConnectAttempt = NOW;
}

void IrcManager::deinit()
{
    if (mClient) mClient->disconnect(xorstr_("Disconnected by user"));
}

void IrcManager::disconnectCallback()
{
    logm("Client dallocated.");
}

void IrcManager::requestListUsers()
{
    if (mClient) mClient->listUsers();
    else ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cYou aren't connected to IRC!");
}

void IrcManager::requestChangeUsername(std::string username)
{
    if (mClient)
    {
        Solstice::Prefs->mIrcName = username;
        PreferenceManager::save(Solstice::Prefs);
        mClient->changeUsername();
        logm("Changed username to {}", Solstice::Prefs->mIrcName);
    }
    else ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cYou aren't connected to IRC!");
}

void IrcManager::sendMessage(std::string& message)
{
    if (mClient) mClient->sendMessage(message);
    else ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cYou aren't connected to IRC!");
}

void IrcManager::sendDirectMessage(const std::string& targetUser, std::string& message)
{
    if (mClient) mClient->sendDirectMessage(targetUser, message);
    else ChatUtils::displayClientMessageRaw("§7[§dirc§7] §cYou aren't connected to IRC!");
}

bool IrcManager::isConnected()
{
    return mClient && mClient->isConnected();
}
