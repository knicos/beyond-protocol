/**
 * @file protocol.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 * 
 * Unused?
 */

#pragma once

#include <ftl/protocol/config.h>
#include <tuple>
#include <ftl/uuid.hpp>
#include "uuidMSGPACK.hpp"

namespace ftl {
namespace net {

typedef std::tuple<uint64_t, uint32_t, ftl::UUIDMSGPACK> Handshake;

static const uint64_t kMagic = 0x0009340053640912;
static const uint32_t kVersion = (FTL_VERSION_MAJOR << 16) +
        (FTL_VERSION_MINOR << 8) + FTL_VERSION_PATCH;

}  // namespace net
}  // namespace ftl
