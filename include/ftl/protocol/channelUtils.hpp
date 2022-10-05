/**
 * @file channelUtils.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <string>
#include <ftl/protocol/channels.hpp>

namespace ftl {
namespace protocol {

constexpr bool isVideo(Channel c) { return static_cast<int>(c) < 32; }
constexpr bool isAudio(Channel c) { return static_cast<int>(c) >= 32 && static_cast<int>(c) < 64; }
constexpr bool isData(Channel c) { return static_cast<int>(c) >= 64; }
constexpr bool isPersistent(Channel c) { return static_cast<int>(c) >= 64; }

/** Obtain a string name for channel. */
std::string name(Channel c);

/**
 * @brief Get the channel ID from a name.
 * 
 * @param name 
 * @return Channel 
 */
Channel fromName(const std::string &name);

/** Obtain OpenCV type for channel. */
int type(Channel c);

/** @deprecated */
constexpr bool isFloatChannel(ftl::protocol::Channel chan) {
    switch (chan) {
    case Channel::kGroundTruth  :
    case Channel::kDepth        :
    case Channel::kConfidence   :
    case Channel::kFlow         :
    case Channel::kDensity      :
    case Channel::kEnergy       : return true;
    default                    : return false;
    }
}

}  // namespace protocol
}  // namespace ftl
