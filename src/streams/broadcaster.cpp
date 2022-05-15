/**
 * @file broadcaster.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/broadcaster.hpp>

using ftl::protocol::Broadcast;
using ftl::protocol::StreamPacket;
using ftl::protocol::Packet;
using ftl::protocol::Channel;
using ftl::protocol::FrameID;

Broadcast::Broadcast() {}

Broadcast::~Broadcast() {}

void Broadcast::add(const std::shared_ptr<Stream> &s) {
    UNIQUE_LOCK(mtx_, lk);

    auto &entry = streams_.emplace_back();
    entry.stream = s;

    entry.handle = std::move(s->onPacket([this, s](const StreamPacket &spkt, const Packet &pkt) {
        trigger(spkt, pkt);
        return true;
    }));

    entry.avail_handle = std::move(s->onAvailable([this, s](FrameID id, Channel channel) {
        seen(id, channel);
        return true;
    }));

    entry.req_handle = std::move(s->onRequest([this, s](const ftl::protocol::Request &req) {
        request(req);
        return true;
    }));
}

void Broadcast::remove(const std::shared_ptr<Stream> &s) {
    UNIQUE_LOCK(mtx_, lk);
    for (auto it = streams_.begin(); it != streams_.end(); ++it) {
        if (it->stream == s) {
            it->handle.cancel();
            it->req_handle.cancel();
            it->avail_handle.cancel();
            streams_.erase(it);
            break;
        }
    }
}

void Broadcast::clear() {
    UNIQUE_LOCK(mtx_, lk);
    streams_.clear();
}

bool Broadcast::post(const StreamPacket &spkt, const Packet &pkt) {
    bool status = true;
    for (auto &s : streams_) {
        status = s.stream->post(spkt, pkt) && status;
    }
    return status;
}

bool Broadcast::begin() {
    bool r = true;
    for (auto &s : streams_) {
        r = r && s.stream->begin();
    }
    return r;
}

bool Broadcast::end() {
    bool r = true;
    for (auto &s : streams_) {
        r = r && s.stream->end();
    }
    return r;
}

bool Broadcast::active() {
    if (streams_.size() == 0) return false;
    bool r = true;
    for (auto &s : streams_) {
        r = r && s.stream->active();
    }
    return r;
}

void Broadcast::reset() {
    SHARED_LOCK(mtx_, lk);
    for (auto &s : streams_) {
        s.stream->reset();
    }
}

void Broadcast::refresh() {}

bool Broadcast::enable(FrameID id) {
    bool r = false;
    {
        SHARED_LOCK(mtx_, lk);
        for (auto &s : streams_) {
            r = s.stream->enable(id) || r;
        }
    }
    if (r) Stream::enable(id);
    return r;
}

bool Broadcast::enable(FrameID id, ftl::protocol::Channel channel) {
    bool r = false;
    {
        SHARED_LOCK(mtx_, lk);
        for (auto &s : streams_) {
            r = s.stream->enable(id, channel) || r;
        }
    }
    if (r) Stream::enable(id, channel);
    return r;
}

bool Broadcast::enable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    bool r = false;
    {
        SHARED_LOCK(mtx_, lk);
        for (auto &s : streams_) {
            r = s.stream->enable(id, channels) || r;
        }
    }
    if (r) Stream::enable(id, channels);
    return r;
}

void Broadcast::setProperty(ftl::protocol::StreamProperty opt, std::any value) {}

std::any Broadcast::getProperty(ftl::protocol::StreamProperty opt) {
    return 0;
}

bool Broadcast::supportsProperty(ftl::protocol::StreamProperty opt) {
    return false;
}

ftl::protocol::StreamType Broadcast::type() const {
    return ftl::protocol::StreamType::kUnknown;
}
