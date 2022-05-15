/**
 * @file packetMsgpack.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

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

static_assert(sizeof(StreamPacketMSGPACK) == sizeof(StreamPacket));
static_assert(sizeof(PacketMSGPACK) == sizeof(Packet));

}  // namespace protocol
}  // namespace ftl
