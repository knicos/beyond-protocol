/**
 * @file packet.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <tuple>
#include <utility>
#include <ftl/protocol/codecs.hpp>
#include <ftl/protocol/channels.hpp>

namespace ftl {
namespace protocol {

static constexpr uint8_t kFlagRequest = 0x01;    ///< Used for empty data packets to mark a request for data
static constexpr uint8_t kFlagCompleted = 0x02;  ///< Last packet for timestamp
static constexpr uint8_t kFlagReset = 0x04;      ///< Request full data, including key frames.
static constexpr uint8_t kFlagFull = 0x04;       ///< If set on EndFrame packet then that frame contained full data

static constexpr uint8_t kAllFrames = 255;
static constexpr uint8_t kAllFramesets = 255;
static constexpr uint8_t kCurrentFTLVersion = 5;  ///< Protocol version number in use.

/**
 * First bytes of our file format.
 */
struct Header {
    const char magic[4] = {'F', 'T', 'L', 'F'};
    uint8_t version = kCurrentFTLVersion;
};

/**
 * Version 2 header padding for potential indexing use.
 */
struct IndexHeader {
    int64_t reserved[8];
};

/**
 * A single network packet for the compressed video stream. It includes the raw
 * data along with any block metadata required to reconstruct. The underlying
 * codec may use its own blocks and packets, in which case this is essentially
 * an empty wrapper around that. It is used in the encoding callback.
 */
struct DataPacket {
    ftl::protocol::Codec codec = ftl::protocol::Codec::kInvalid;
    uint8_t reserved = 0;
    uint8_t frame_count = 1;     // v4+ Frames included in this packet

    uint8_t bitrate = 0;         // v4+ For multi-bitrate encoding, 0=highest

    union {
        uint8_t dataFlags = 0;       // Codec dependent flags (eg. I-Frame or P-Frame)
        uint8_t packet_count;
    };
    std::vector<uint8_t> data;
};

static constexpr unsigned int kStreamCap_Static = 0x01;
static constexpr unsigned int kStreamCap_Recorded = 0x02;
static constexpr unsigned int kStreamCap_NewConnection = 0x04;

/** V4 packets have no stream flags field */
/*struct StreamPacketV4 {
    int version = 4;                   // FTL version, Not encoded into stream

    int64_t timestamp;
    uint8_t streamID;                  // Source number [or v4 frameset id]
    uint8_t frame_number;              // v4+ First frame number (packet may include multiple frames)
    ftl::protocol::Channel channel;    // Actual channel of this current set of packets

    inline int frameNumber() const { return (version >= 4) ? frame_number : streamID; }
    inline size_t frameSetID() const { return (version >= 4) ? streamID : 0; }

    int64_t localTimestamp;            // Not message packet / saved
    unsigned int hint_capability;      // Is this a video stream, for example
    size_t hint_source_total;          // Number of tracks per frame to expect

    operator std::string() const;
};*/

/**
 * Add timestamp and channel information to a raw encoded frame packet. This
 * allows the packet to be located within a larger stream and should be sent
 * or included before a frame packet structure.
 */
struct StreamPacket {
    int version = kCurrentFTLVersion;    // FTL version, Not encoded into stream

    int64_t timestamp;
    uint8_t streamID;                    // Source number [or v4 frameset id]
    uint8_t frame_number;                // v4+ First frame number (packet may include multiple frames)
    ftl::protocol::Channel channel;      // Actual channel of this current set of packets
    uint8_t flags = 0;

    inline int frameNumber() const { return (version >= 4) ? frame_number : streamID; }
    inline size_t frameSetID() const { return (version >= 4) ? streamID : 0; }

    int64_t localTimestamp = 0;                 // Not message packet / saved
    mutable unsigned int hint_capability = 0;   // Is this a video stream, for example
    size_t hint_source_total = 0;               // Number of tracks per frame to expect
    int retry_count = 0;                        // Decode retry count
    unsigned int hint_peerid = 0;

    operator std::string() const;
};

struct Packet : public StreamPacket, public DataPacket {};

using PacketPair = std::pair<StreamPacket, DataPacket>;

}  // namespace protocol
}  // namespace ftl
