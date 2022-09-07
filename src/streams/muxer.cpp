/**
 * @file muxer.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/muxer.hpp>
#include <ftl/lib/loguru.hpp>
#include <ftl/uri.hpp>

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
        se.err_handle.cancel();
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

FrameID Muxer::_mapFromInput(const Muxer::StreamEntry *s, FrameID id) const {
    SHARED_LOCK(mutex_, lk);
    int64_t iid = (int64_t(s->id) << 32) | id.id;
    auto it = imap_.find(iid);
    if (it != imap_.end()) {
        return it->second;
    } else {
        throw FTL_Error("No mapping");
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

std::shared_ptr<Stream> Muxer::findStream(const std::string &uri) const {
    SHARED_LOCK(mutex_, lk);
    for (const auto &e : streams_) {
        if (std::any_cast<std::string>(e.stream->getProperty(StreamProperty::kURI)) == uri) {
            return e.stream;
        }
    }
    return nullptr;
}

FrameID Muxer::findLocal(const std::string &uri) const {
    ftl::URI u(uri);
    const StreamEntry *entry = nullptr;

    int fsid = 0;
    int fid = 0;
    if (u.hasAttribute("set")) {
        fsid = u.getAttribute<int>("set");
    }
    if (u.hasAttribute("frame")) {
        fid = u.getAttribute<int>("frame");
    }
    FrameID remote(fsid, fid);

    {
        SHARED_LOCK(mutex_, lk);
        for (const auto &e : streams_) {
            if (std::any_cast<std::string>(e.stream->getProperty(StreamProperty::kURI)) == uri) {
                entry = &e;
                break;
            }
        }
    }

    if (entry) {
        return _mapFromInput(entry, remote);
    } else {
        throw FTL_Error("No stream");
    }
}

FrameID Muxer::findLocal(const std::string &uri, FrameID remote) const {
    const StreamEntry *entry = nullptr;

    {
        SHARED_LOCK(mutex_, lk);
        for (const auto &e : streams_) {
            if (std::any_cast<std::string>(e.stream->getProperty(StreamProperty::kURI)) == uri) {
                entry = &e;
                break;
            }
        }
    }

    if (entry) {
        return _mapFromInput(entry, remote);
    } else {
        throw FTL_Error("No stream");
    }
}

FrameID Muxer::findLocal(const std::shared_ptr<Stream> &stream, FrameID remote) const {
    const StreamEntry *entry = nullptr;

    {
        SHARED_LOCK(mutex_, lk);
        for (const auto &e : streams_) {
            if (e.stream == stream) {
                entry = &e;
                break;
            }
        }
    }

    if (entry) {
        return _mapFromInput(entry, remote);
    } else {
        throw FTL_Error("No stream");
    }
}

FrameID Muxer::findRemote(FrameID local) const {
    auto m = _mapToOutput(local);
    if (m.second == nullptr) {
        throw FTL_Error("No mapping");
    }
    return m.first;
}

std::list<std::shared_ptr<Stream>> Muxer::streams() const {
    std::list<std::shared_ptr<Stream>> result;
    result.resize(streams_.size());
    std::transform(streams_.begin(), streams_.end(), result.begin(), [](const StreamEntry &e) {
        return e.stream;
    });
    return result;
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
        if (req.id.frameset() == 255 || req.id.source() == 255) {
            for (const auto &i : ptr->stream->frames()) {
                if (req.id.frameset() != 255 && req.id.frameset() != i.frameset()) continue;
                if (req.id.source() != 255 && req.id.source() != i.source()) continue;

                FrameID newID = _mapFromInput(ptr, i);
                Request newRequest = req;
                newRequest.id = newID;
                request(newRequest);
            }
        } else {
            FrameID newID = _mapFromInput(ptr, req.id);
            Request newRequest = req;
            newRequest.id = newID;
            request(newRequest);
        }
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

bool Muxer::active(FrameID id) {
    auto p = _mapToOutput(id);
    if (p.second) {
        return p.second->stream->active(p.first);
    } else {
        return false;
    }
}

void Muxer::reset() {
    for (auto &s : streams_) {
        s.stream->reset();
    }
}

bool Muxer::enable(FrameID id) {
    bool r = true;

    if (id.frameset() == 255 || id.source() == 255) {
        for (const auto &i : frames()) {
            if (id.frameset() != 255 && id.frameset() != i.frameset()) continue;
            if (id.source() != 255 && id.source() != i.source()) continue;

            auto p = _mapToOutput(i);
            if (!p.second) return false;
            bool rr = p.second->stream->enable(p.first);
            if (rr) Stream::enable(i);
            r = r && rr;
        }
    } else {
        auto p = _mapToOutput(id);
        if (!p.second) return false;
        r = p.second->stream->enable(p.first);
        if (r) Stream::enable(id);
    }
    return r;
}

bool Muxer::enable(FrameID id, ftl::protocol::Channel channel) {
    bool r = true;

    if (id.frameset() == 255 || id.source() == 255) {
        for (const auto &i : frames()) {
            if (id.frameset() != 255 && id.frameset() != i.frameset()) continue;
            if (id.source() != 255 && id.source() != i.source()) continue;

            auto p = _mapToOutput(i);
            if (!p.second) return false;
            bool rr = p.second->stream->enable(p.first, channel);
            if (rr) Stream::enable(i, channel);
            r = r && rr;
        }
    } else {
        auto p = _mapToOutput(id);
        if (!p.second) return false;
        r = p.second->stream->enable(p.first, channel);
        if (r) Stream::enable(id, channel);
    }
    return r;
}

bool Muxer::enable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    bool r = true;

    if (id.frameset() == 255 || id.source() == 255) {
        for (const auto &i : frames()) {
            if (id.frameset() != 255 && id.frameset() != i.frameset()) continue;
            if (id.source() != 255 && id.source() != i.source()) continue;

            auto p = _mapToOutput(i);
            if (!p.second) return false;
            bool rr = p.second->stream->enable(p.first, channels);
            if (rr) Stream::enable(i, channels);
            r = r && rr;
        }
    } else {
        auto p = _mapToOutput(id);
        if (!p.second) return false;
        r = p.second->stream->enable(p.first, channels);
        if (r) Stream::enable(id, channels);
    }
    return r;
}

void Muxer::disable(FrameID id) {
    auto p = _mapToOutput(id);
    if (!p.second) return;
    p.second->stream->disable(p.first);
    Stream::disable(id);
}

void Muxer::disable(FrameID id, ftl::protocol::Channel channel) {
    auto p = _mapToOutput(id);
    if (!p.second) return;
    p.second->stream->disable(p.first, channel);
    Stream::disable(id, channel);
}

void Muxer::disable(FrameID id, const ftl::protocol::ChannelSet &channels) {
    auto p = _mapToOutput(id);
    if (!p.second) return;
    p.second->stream->disable(p.first, channels);
    Stream::disable(id, channels);
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
