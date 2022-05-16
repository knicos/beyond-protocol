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

struct PacketMSGPACK : ftl::protocol::DataPacket {
    MSGPACK_DEFINE(codec, reserved, frame_count, bitrate, dataFlags, data);
};

class StreamPacker {
 public:
    explicit StreamPacker(StreamPacket *p) : packet(p) {}

    MSGPACK_DEFINE(
        packet->timestamp,
        packet->streamID,
        packet->frame_number,
        packet->channel,
        packet->flags);

    StreamPacket *packet;
};

class DataPacker {
 public:
    explicit DataPacker(DataPacket *p) : packet(p) {}

    MSGPACK_DEFINE(
        packet->codec,
        packet->reserved,
        packet->frame_count,
        packet->bitrate,
        packet->dataFlags,
        packet->data);

    DataPacket *packet;
};

class Packer {
 public:
    Packer() : spack_(nullptr), dpack_(nullptr) {}
    explicit Packer(Packet *p) : spack_(p), dpack_(p) {}
    void set(Packet *p) {
        spack_.packet = p;
        dpack_.packet = p;
    }

    MSGPACK_DEFINE_ARRAY(spack_, dpack_);
 private:
    StreamPacker spack_;
    DataPacker dpack_;
};

static_assert(sizeof(StreamPacketMSGPACK) == sizeof(StreamPacket));
static_assert(sizeof(PacketMSGPACK) == sizeof(DataPacket));

}  // namespace protocol
}  // namespace ftl
