/**
 * @file error.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

namespace ftl {
namespace protocol {

/**
 * @brief Error codes for asynchronous error events.
 * 
 */
enum struct Error {
    kNoError = 0,
    kUnknown = 1,
    kPacketFailure,
    kDispatchFailed,
    kMissingHandshake,
    kRPCResponse,
    kSocketError,
    kBufferSize,
    kReconnectionFailed,
    kBadHandshake,
    kConnectionFailed,
    kSelfConnect,
    kListen,
    kURIAlreadyExists,
    kURIDoesNotExist,
    kBadURI,
    kBadVersion
};

}  // namespace protocol
}  // namespace ftl
