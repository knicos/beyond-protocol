/**
 * @file packetmanager.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <utility>
#include <vector>
#include <limits>
#include "packetmanager.hpp"
#include <ftl/protocol/frameid.hpp>

#include <loguru.hpp>

using ftl::PacketManager;
using ftl::StreamState;
using ftl::protocol::PacketPair;
using ftl::protocol::FrameID;
using ftl::protocol::Channel;

StreamState &PacketManager::getState(FrameID id) {
    {
        SHARED_LOCK(mtx_, lk);
        auto it = state_.find(id.id);
        if (it != state_.end()) return it->second;
    }
    UNIQUE_LOCK(mtx_, lk);
    return state_[id.id];
}

void PacketManager::submit(PacketPair &packets, const std::function<void(const PacketPair &)> &cb, bool noLoop) {
    auto &state = getState(FrameID(packets.first.frameSetID(), packets.first.frameNumber()));

    if (state.timestamp == packets.first.timestamp) {
        if (packets.first.channel == Channel::kEndFrame) {
            state.expected = packets.second.packet_count;
        }
        cb(packets);
        ++state.processed;

        if (state.processed == state.expected) {
            UNIQUE_LOCK(state.mtx, lk);
            if (state.processed == state.expected) {
                state.processed = 0;
                state.expected = -1;

                if (state.writePos > state.readPos) {
                    size_t start = state.readPos;
                    size_t stop = state.writePos;
                    state.readPos = stop;

                    int64_t ts = std::numeric_limits<int64_t>::max();
                    for (size_t i = start; i < stop; ++i) {
                        ts = std::min(
                            ts,
                            state.buffer[i % StreamState::kMaxBuffer].first.timestamp);
                    }

                    state.timestamp = ts;

                    lk.unlock();
                    // Loop over the buffer, checking for anything that can be processed
                    for (size_t i = start; i < stop; ++i) {
                        if (state.buffer[i % StreamState::kMaxBuffer].first.channel == Channel::kEndFrame) {
                            --state.bufferedEndFrames;
                        }
                        submit(state.buffer[i % StreamState::kMaxBuffer], cb, true);
                    }
                } else {
                    state.timestamp = -1;
                    return;
                }
            }
        }
    } else if (state.timestamp > packets.first.timestamp) {
        LOG(WARNING) << "Old packet received";
        // Note: not ideal but still better than discarding
        cb(packets);
        return;
    } else {
        DLOG(WARNING) << "Buffer packets: " << packets.first.timestamp;
        // Change the current frame
        UNIQUE_LOCK(state.mtx, lk);
        if (state.timestamp == packets.first.timestamp) {
            lk.unlock();
            submit(packets, cb);
            return;
        }

        if (state.timestamp == -1) {
            state.timestamp = packets.first.timestamp;
            lk.unlock();
            submit(packets, cb);
            return;
        }

        // Add packet to buffer;
        auto wpos = state.writePos++;
        state.buffer[wpos % StreamState::kMaxBuffer] = std::move(packets);
        lk.unlock();

        if (packets.first.channel == Channel::kEndFrame) {
            ++state.bufferedEndFrames;
        }

        if (state.bufferedEndFrames > 4) {
            LOG(WARNING) << "Discarding incomplete frame: " << state.timestamp;
            UNIQUE_LOCK(state.mtx, lk);
            if (state.bufferedEndFrames > 4) {
                state.processed = 0;
                state.expected = -1;

                size_t start = state.readPos;
                size_t stop = state.writePos;
                state.readPos = stop;

                int64_t ts = std::numeric_limits<int64_t>::max();
                for (size_t i = start; i < stop; ++i) {
                    ts = std::min(
                        ts,
                        state.buffer[i % StreamState::kMaxBuffer].first.timestamp);
                }

                state.timestamp = ts;

                lk.unlock();
                // Loop over the buffer, checking for anything that can be processed
                for (size_t i = start; i < stop; ++i) {
                    if (state.buffer[i % StreamState::kMaxBuffer].first.channel == Channel::kEndFrame) {
                        --state.bufferedEndFrames;
                    }
                    submit(state.buffer[i % StreamState::kMaxBuffer], cb, true);
                    std::vector<uint8_t> temp;
                    state.buffer[i % StreamState::kMaxBuffer].second.data.swap(temp);
                }
            }
        }
    }
}
