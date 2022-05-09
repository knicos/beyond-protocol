#pragma once

#include <ftl/protocol/packet.hpp>
#include <msgpack.hpp>

MSGPACK_ADD_ENUM(ftl::protocol::Codec);
MSGPACK_ADD_ENUM(ftl::protocol::Channel);

namespace ftl {
namespace protocol {

struct StreamPacketMSGPACK : ftl::protocol::StreamPacket {
    MSGPACK_DEFINE(timestamp, streamID, frame_number, channel, flags);
};

struct PacketMSGPACK : ftl::protocol::Packet {
    MSGPACK_DEFINE(codec, reserved, frame_count, bitrate, flags, data);
};

}
}
