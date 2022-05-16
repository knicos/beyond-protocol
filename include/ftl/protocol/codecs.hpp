/**
 * @file codecs.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <cstdint>
#include <utility>

namespace ftl {

namespace protocol {

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

}  // namespace protocol
}  // namespace ftl
