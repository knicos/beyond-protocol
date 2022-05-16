/**
 * @file muxer.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/muxer.hpp>
#include <ftl/lib/loguru.hpp>

using ftl::protocol::Muxer;
using ftl::protocol::Stream;
using ftl::protocol::StreamPacket;
using ftl::protocol::FrameID;
using ftl::protocol::StreamType;

Muxer::Muxer() {}

Muxer::~Muxer() {
    UNIQUE_LOCK(mutex_, lk);
    for (auto &se : streams_) {
        se.handle.cancel();
        se.req_handle.cancel();
        se.avail_handle.cancel();
    }
}

FrameID Muxer::_mapFromInput(Muxer::StreamEntry *s, FrameID id) {
    SHARED_LOCK(mutex_, lk);
    int64_t iid = (int64_t(s->id) << 32) | id.id;
    auto it = imap_.find(iid);
    if (it != imap_.end()) {
        return it->second;
    } else {
        // Otherwise allocate something.
        lk.unlock();
        UNIQUE_LOCK(mutex_, ulk);

        FrameID newID;
        if (s->fixed_fs >= 0) {
            int source = sourcecount_[s->fixed_fs]++;
            newID = FrameID(s->fixed_fs, source);
        } else {
            int fsiid = (s->id << 16) | id.frameset();
            if (fsmap_.count(fsiid) == 0) fsmap_[fsiid] = framesets_++;
            newID = FrameID(fsmap_[fsiid], id.source());
        }

        imap_[iid] = newID;
        auto &op = omap_[newID];
        op.first = id;
        op.second = s;
        return newID;
    }
}

std::pair<FrameID, Muxer::StreamEntry*> Muxer::_mapToOutput(FrameID id) const {
    SHARED_LOCK(mutex_, lk);
    auto it = omap_.find(id);
    if (it != omap_.end()) {
        return it->second;
    } else {
        return {id, nullptr};
    }
}

void Muxer::add(const std::shared_ptr<Stream> &s, int fsid) {
    UNIQUE_LOCK(mutex_, lk);

    auto &se = streams_.emplace_back();
    se.id = stream_ids_++;
    se.stream = s;
    se.fixed_fs = fsid;
    Muxer::StreamEntry *ptr = &se;

    se.handle = std::move(s->onPacket([this, ptr](const StreamPacket &spkt, const DataPacket &pkt) {
        FrameID newID = _mapFromInput(ptr, FrameID(spkt.streamID, spkt.frame_number));

        StreamPacket spkt2 = spkt;
        spkt2.streamID = newID.frameset();
        spkt2.frame_number = newID.source();

        trigger(spkt2, pkt);
        return true;
    }));

    se.avail_handle = std::move(s->onAvailable([this, ptr](FrameID id, Channel channel) {
        FrameID newID = _mapFromInput(ptr, id);
        seen(newID, channel);
        return true;
    }));

    se.req_handle = std::move(s->onRequest([this, ptr](const Request &req) {
        FrameID newID = _mapFromInput(ptr, req.id);
        Request newRequest = req;
        newRequest.id = newID;
        request(newRequest);
        return true;
    }));

    se.err_handle = std::move(s->onError([this](ftl::protocol::Error err, const std::string &str) {
        error(err, str);
        return true;
    }));
}

void Muxer::remove(const std::shared_ptr<Stream> &s) {
    UNIQUE_LOCK(mutex_, lk);
    for (auto i = streams_.begin(); i != streams_.end(); ++i) {
        if (i->stream == s) {
            auto *se = &(*i);

            se->handle.cancel();
            se->req_handle.cancel();
            se->avail_handle.cancel();

            // Cleanup imap and omap
            for (auto j = imap_.begin(); j != imap_.end();) {
                const auto &e = *j;
                if (e.first >> 32 == se->id) j = imap_.erase(j);
                else
                    ++j;
            }
            for (auto j = omap_.begin(); j != omap_.end();) {
                const auto &e = *j;
                if (e.second.second == se) j = omap_.erase(j);
                else
                    ++j;
            }

            streams_.erase(i);
            return;
        }
    }
}

std::shared_ptr<Stream> Muxer::originStream(FrameID id) const {
    auto p = _mapToOutput(id);
    return (p.second) ? p.second->stream : nullptr;
}

bool Muxer::post(const StreamPacket &spkt, const DataPacket &pkt) {
    auto p = _mapToOutput(FrameID(spkt.streamID, spkt.frame_number));
    if (!p.second) return false;
    StreamPacket spkt2 = spkt;
    spkt2.streamID = p.first.frameset();
    spkt2.frame_number = p.first.source();
    return p.second->stream->post(spkt2, pkt);
}

bool Muxer::begin() {
    bool r = true;
    for (auto &s : streams_) {
        r = r && s.stream->begin();
    }
    return r;
}

bool Muxer::end() {
    bool r = true;
    for (auto &s : streams_) {
        r = r && s.stream->end();
    }
    return r;
}

bool Muxer::active() {
    bool r = true;
    for (auto &s : streams_) {
        r = r && s.stream->active();
    }
    return r;
}

void Muxer::reset() {
    for (auto &s : streams_) {
        s.stream->reset();
    }
}

bool Muxer::enable(FrameID id) {
    auto p = _mapToOutput(id);
    if (!p.second) return false;
    bool r = p.second->stream->enable(p.first);
    if (r) Stream::enable(id);
    return r;
}

bool Muxer::enable(FrameID id, ftl::protocol::Channel channel) {
    auto p = _mapToOutput(id);
    if (!p.second) return false;
    bool r = p.second->stream->enable(p.first, channel);
    if (r) Stream::enable(id, channel);
    return r;
}

bool Muxer::enable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    auto p = _mapToOutput(id);
    if (!p.second) return false;
    bool r = p.second->stream->enable(p.first, channels);
    if (r) Stream::enable(id, channels);
    return r;
}

void Muxer::setProperty(ftl::protocol::StreamProperty opt, std::any value) {
    for (auto &s : streams_) {
        s.stream->setProperty(opt, value);
    }
}

std::any Muxer::getProperty(ftl::protocol::StreamProperty opt) {
    for (auto &s : streams_) {
        if (s.stream->supportsProperty(opt)) return s.stream->getProperty(opt);
    }
    return 0;
}

bool Muxer::supportsProperty(ftl::protocol::StreamProperty opt) {
    for (auto &s : streams_) {
        if (s.stream->supportsProperty(opt)) return true;
    }
    return false;
}

StreamType Muxer::type() const {
    StreamType t = StreamType::kUnknown;
    for (const auto &s : streams_) {
        const StreamType tt = s.stream->type();
        if (t == StreamType::kUnknown) t = tt;
        else if (t != tt) t = StreamType::kMixed;
    }
    return t;
}
