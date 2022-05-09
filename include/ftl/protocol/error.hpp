#pragma once

namespace ftl {
namespace protocol {

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
    kListen
};

}
}
