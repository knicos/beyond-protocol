/**
 * @file streams.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/streams.hpp>
#include <ftl/protocol/channelUtils.hpp>
#include <ftl/protocol/channelSet.hpp>

using ftl::protocol::Stream;
using ftl::protocol::Channel;
using ftl::protocol::ChannelSet;
using ftl::protocol::FrameID;
using ftl::protocol::isPersistent;

std::string Stream::name() const {
    return "Unknown";
}

bool Stream::available(FrameID id) const {
    SHARED_LOCK(mtx_, lk);
    return state_.count(id) > 0;
}

bool Stream::available(FrameID id, Channel channel) const {
    auto state = _getState(id);
    if (!state) return false;
    if (isPersistent(channel)) {
        SHARED_LOCK(mtx_, lk);
        return state->availablePersistent.count(channel) > 0;
    } else {
        return state->availableLast & (1ull << static_cast<int>(channel));
    }
}

bool Stream::available(FrameID id, const ChannelSet &channels) const {
    auto state = _getState(id);
    if (!state) return false;
    for (auto channel : channels) {
        if (isPersistent(channel)) {
            SHARED_LOCK(mtx_, lk);
            if (state->availablePersistent.count(channel) == 0) return false;
        } else {
            if ((state->availableLast & (1ull << static_cast<int>(channel))) == 0) return false;
        }
    }
    return true;
}

ftl::protocol::ChannelSet Stream::channels(FrameID id) const {
    auto state = _getState(id);
    if (!state) return {};

    SHARED_LOCK(mtx_, lk);
    ChannelSet result = state->availablePersistent;
    lk.unlock();

    uint64_t last = state->availableLast;

    for (int i = 0; i < 64; ++i) {
        if ((1ull << i) & last) {
            result.insert(static_cast<Channel>(i));
        }
    }
    return result;
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
        if (!s.second) continue;
        if (s.second->enabled) {
            result.emplace(s.first);
        }
    }
    return result;
}

std::unordered_set<FrameID> Stream::enabled(unsigned int fs) const {
    SHARED_LOCK(mtx_, lk);
    std::unordered_set<FrameID> result;
    for (const auto &s : state_) {
        if (!s.second) continue;
        if (s.second->enabled && FrameID(s.first).frameset() == fs) {
            result.emplace(s.first);
        }
    }
    return result;
}

bool Stream::enabled(FrameID id) const {
    auto state = _getState(id);
    if (!state) return false;
    return state->enabled;
}

bool Stream::enabled(FrameID id, ftl::protocol::Channel channel) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        if (!it->second) return false;
        return it->second->selected.count(channel) > 0;
    }
    return false;
}

ftl::protocol::ChannelSet Stream::enabledChannels(FrameID id) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) {
        if (!it->second) return {};
        return it->second->selected;
    }
    return {};
}

bool Stream::enable(FrameID id) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    if (!p) p = std::make_shared<Stream::FSState>();
    p->enabled = true;
    return true;
}

bool Stream::enable(FrameID id, ftl::protocol::Channel channel) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    if (!p) p = std::make_shared<Stream::FSState>();
    p->enabled = true;
    p->selected.insert(channel);
    return true;
}

bool Stream::enable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    if (!p) p = std::make_shared<Stream::FSState>();
    p->enabled = true;
    p->selected.insert(channels.begin(), channels.end());
    return true;
}

void Stream::disable(FrameID id) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    if (!p) p = std::make_shared<Stream::FSState>();
    p->enabled = false;
}

void Stream::disable(FrameID id, ftl::protocol::Channel channel) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    if (!p) p = std::make_shared<Stream::FSState>();
    p->selected.erase(channel);
    if (p->selected.size() == 0) {
        p->enabled = false;
    }
}

void Stream::disable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    UNIQUE_LOCK(mtx_, lk);
    auto &p = state_[id];
    if (!p) p = std::make_shared<Stream::FSState>();
    for (const auto &c : channels) {
        p->selected.erase(c);
    }
    if (p->selected.size() == 0) {
        p->enabled = false;
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

std::shared_ptr<Stream::FSState> Stream::_getState(FrameID id) {
    {
        SHARED_LOCK(mtx_, lk);
        auto it = state_.find(id);
        if (it != state_.end()) return it->second;
    }
    UNIQUE_LOCK(mtx_, lk);
    if (!state_[id]) state_[id] = std::make_shared<Stream::FSState>();
    return state_[id];
}

std::shared_ptr<Stream::FSState> Stream::_getState(FrameID id) const {
    SHARED_LOCK(mtx_, lk);
    auto it = state_.find(id);
    if (it != state_.end()) return it->second;
    return nullptr;
}

void Stream::seen(FrameID id, ftl::protocol::Channel channel) {
    auto state = _getState(id);
    if (channel == Channel::kEndFrame) {
        state->availableLast = static_cast<uint64_t>(state->availableNext);
        state->availableNext = 0;
    } else if (isPersistent(channel)) {
        {
            SHARED_LOCK(mtx_, lk);
            if (state->availablePersistent.count(channel) > 0) return;
        }
        UNIQUE_LOCK(mtx_, lk);
        state->availablePersistent.insert(channel);
    } else {
        state->availableNext |= 1ull << static_cast<int>(channel);
        if (state->availableLast & (1ull << static_cast<int>(channel))) return;
    }

    avail_cb_.trigger(id, channel);
}

void Stream::request(const ftl::protocol::Request &req) {
    request_cb_.trigger(req);
}

void Stream::error(ftl::protocol::Error err, const std::string &str) {
    error_cb_.trigger(err, str);
}

bool Stream::active(FrameID id) {
    return active();
}
