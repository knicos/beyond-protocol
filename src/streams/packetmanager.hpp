/**
 * @file packetmanager.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <functional>
#include <list>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <tuple>
#include <array>
#include <ftl/protocol/packet.hpp>
#include <ftl/threads.hpp>
#include <ftl/protocol/frameid.hpp>

namespace ftl {

struct StreamState {
    static constexpr int kMaxBuffer = 100;

    SHARED_MUTEX mtx;
    std::array<ftl::protocol::PacketPair, kMaxBuffer> buffer;

    int64_t timestamp = -1;
    int expected = -1;
    std::atomic_int processed = 0;
    size_t readPos = 0;
    size_t writePos = 0;
    std::atomic_int bufferedEndFrames = 0;
};

class PacketManager {
 public:
    void submit(
        ftl::protocol::PacketPair &,
        const std::function<void(const ftl::protocol::PacketPair &)> &,
        bool noLoop = false);

    void reset();

 private:
    SHARED_MUTEX mtx_;
    std::unordered_map<uint32_t, StreamState> state_;

    StreamState &getState(ftl::protocol::FrameID);
};

}  // namespace ftl
