/**
 * @file codecs.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <cstdint>
#include <utility>

namespace ftl {

/**
 * Video and data encoding / decoding components are located in this namespace. 
 * Audio codecs are for now in `ftl::audio` namespace.
 */
namespace protocol {

static constexpr uint8_t kFlagRequest = 0x01;    ///< Used for empty data packets to mark a request for data
static constexpr uint8_t kFlagCompleted = 0x02;  ///< Last packet for timestamp
static constexpr uint8_t kFlagReset = 0x04;

/**
 * Compression format used.
 */
enum struct Codec : uint8_t {
    /* Video (image) codecs */
    kJPG = 0,
    kPNG,
    kH264,
    kHEVC,          // H265
    kH264Lossless,
    kHEVCLossLess,

    /* Audio codecs */
    kWave = 32,
    kOPUS,

    /* Data "codecs" */
    kJSON = 100,    // A JSON string
    kCalibration,   // Camera parameters object [deprecated]
    kPose,          // 4x4 eigen matrix [deprecated]
    kMsgPack,
    kString,        // Null terminated string
    kRaw,           // Some unknown binary format

    kInvalid = 254,
    kAny = 255
};

/** Given a frame count, return a width x height tile configuration. */
std::pair<int, int> chooseTileConfig(int size);

}  // namespace protocol
}  // namespace ftl
