/**
 * @file streams.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/streams.hpp>

using ftl::protocol::Stream;
using ftl::protocol::Channel;
using ftl::protocol::ChannelSet;
using ftl::protocol::FrameID;

std::string Stream::name() const {
    return "Unknown";
}

bool Stream::available(FrameID id) const {
    SHARED_LOCK(mtx_, lk);
    return state_.count(id) > 0;
}

bool Stream::available(FrameID id, Channel channel) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        return it->second.available.count(channel) > 0;
    }
    return false;
}

bool Stream::available(FrameID id, ChannelSet channels) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        const auto &set = it->second.available;
        for (auto channel : channels) {
            if (set.count(channel) == 0) return false;
        }
        return true;
    }
    return false;
}

ftl::protocol::ChannelSet Stream::channels(FrameID id) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        return it->second.available;
    }
    return {};
}

std::unordered_set<FrameID> Stream::frames() const {
    SHARED_LOCK(mtx_, lk);
    std::unordered_set<FrameID> result;
    for (const auto &s : state_) {
        result.insert(FrameID(s.first));
    }
    return result;
}

std::unordered_set<FrameID> Stream::enabled() const {
    SHARED_LOCK(mtx_, lk);
    std::unordered_set<FrameID> result;
    for (const auto &s : state_) {
        if (s.second.enabled) {
            result.emplace(s.first);
        }
    }
    return result;
}

bool Stream::enabled(FrameID id) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        return it->second.enabled;
    }
    return false;
}

bool Stream::enabled(FrameID id, ftl::protocol::Channel channel) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        return it->second.selected.count(channel) > 0;
    }
    return false;
}

ftl::protocol::ChannelSet Stream::enabledChannels(FrameID id) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        return it->second.selected;
    }
    return {};
}

bool Stream::enable(FrameID id) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    p.enabled = true;
    return true;
}

bool Stream::enable(FrameID id, ftl::protocol::Channel channel) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    p.enabled = true;
    p.selected.insert(channel);
    return true;
}

bool Stream::enable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    p.enabled = true;
    p.selected.insert(channels.begin(), channels.end());
    return true;
}

void Stream::disable(FrameID id) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    p.enabled = false;
}

void Stream::disable(FrameID id, ftl::protocol::Channel channel) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    p.selected.erase(channel);
    if (p.selected.size() == 0) {
        p.enabled = false;
    }
}

void Stream::disable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    for (const auto &c : channels) {
        p.selected.erase(c);
    }
    if (p.selected.size() == 0) {
        p.enabled = false;
    }
}

void Stream::reset() {
    UNIQUE_LOCK(mtx_, lk);
    state_.clear();
}

void Stream::refresh() {}

void Stream::trigger(const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
    cb_.trigger(spkt, pkt);
}

void Stream::seen(FrameID id, ftl::protocol::Channel channel) {
    if (!available(id, channel)) {
        {
            UNIQUE_LOCK(mtx_, lk);
            auto &p = state_[id];
            p.available.insert(channel);
        }
        avail_cb_.trigger(id, channel);
    }
}

void Stream::request(const ftl::protocol::Request &req) {
    request_cb_.trigger(req);
}

void Stream::error(ftl::protocol::Error err, const std::string &str) {
    error_cb_.trigger(err, str);
}
