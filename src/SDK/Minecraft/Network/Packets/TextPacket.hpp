//
// Created by vastrakai on 6/28/2024.
//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "Packet.hpp"

enum class TextPacketType : std::int8_t {
    Raw                    = 0x0,
    Chat                   = 0x1,
    Translate              = 0x2,
    Popup                  = 0x3,
    JukeboxPopup           = 0x4,
    Tip                    = 0x5,
    SystemMessage          = 0x6,
    Whisper                = 0x7,
    Announcement           = 0x8,
    TextObjectWhisper      = 0x9,
    TextObject             = 0xA,
    TextObjectAnnouncement = 0xB,
};

class TextPacket : public Packet {
public:
    static const PacketID ID = PacketID::Text;

    TextPacketType                 mType;
    std::string                    mAuthor;
    std::string                    mMessage;
    std::vector<std::string>       mParams;
    std::optional<std::string>     mFilteredMessage;
    bool                           mLocalize = false;
    std::string                    mXuid;
    std::string                    mPlatformId;

    [[nodiscard]] const std::string& getText() const
    {
        if (mFilteredMessage && !mFilteredMessage->empty())
            return *mFilteredMessage;
        return mMessage;
    }
};

static_assert(sizeof(TextPacket) == 0x100);
