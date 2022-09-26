/**
 * @file netstream.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <list>
#include <string>
#include <algorithm>
#include "netstream.hpp"
#include <ftl/time.hpp>
#include "../uuidMSGPACK.hpp"
#include "packetMsgpack.hpp"

#define LOGURU_REPLACE_GLOG 1
#include <ftl/lib/loguru.hpp>

#ifndef WIN32
#include <unistd.h>
#include <limits.h>
#endif

using ftl::protocol::Net;
using ftl::protocol::NetStats;
using ftl::protocol::StreamPacket;
using ftl::protocol::PacketMSGPACK;
using ftl::protocol::StreamPacketMSGPACK;
using ftl::protocol::DataPacket;
using ftl::protocol::Channel;
using ftl::protocol::Codec;
using ftl::protocol::FrameID;
using ftl::protocol::Error;
using ftl::protocol::StreamProperty;
using std::string;
using std::optional;
using std::chrono::time_point_cast;
using std::chrono::milliseconds;
using std::chrono::high_resolution_clock;

std::atomic_size_t Net::req_bitrate__ = 0;
std::atomic_size_t Net::tx_bitrate__ = 0;
std::atomic_size_t Net::rx_sample_count__ = 0;
std::atomic_size_t Net::tx_sample_count__ = 0;
int64_t Net::last_msg__ = 0;
MUTEX Net::msg_mtx__;

static std::list<std::string> net_streams;
static SHARED_MUTEX stream_mutex;

void Net::installRPC(ftl::net::Universe *net) {
    net->bind("find_stream", [net](const std::string &uri) -> optional<ftl::UUIDMSGPACK> {
        DLOG(INFO) << "Request for stream: " << uri;

        ftl::URI u1(uri);
        std::string base = u1.getBaseURI();

        SHARED_LOCK(stream_mutex, lk);
        for (const auto &s : net_streams) {
            ftl::URI u2(s);
            // Don't compare query string components.
            if (base == u2.getBaseURI()) {
                ftl::UUIDMSGPACK mpuuid(net->id());
                return std::reference_wrapper(mpuuid);
            }
        }
        return {};
    });

    net->bind("list_streams", []() {
        SHARED_LOCK(stream_mutex, lk);
        return net_streams;
    });

    net->bind("enable_stream", [](const std::string &uri, unsigned int fsid, unsigned int fid) {
        // Nothing to do here, used by web service
    });

    net->bind("add_stream", [](const std::string &uri) {
        // TODO(Nick): Trigger some callback
    });

    // TODO(Nick): Call "list_streams" to get all available locally
    // This call should be done on any Peer connection
    // and perhaps periodically
}

Net::Net(const std::string &uri, ftl::net::Universe *net, bool host) :
        net_(net), time_peer_(ftl::UUID(0)), uri_(uri), host_(host) {
    ftl::URI u(uri_);
    if (!u.isValid() || !(u.getScheme() == ftl::URI::SCHEME_FTL)) {
        error(Error::kBadURI, uri_);
        throw FTL_Error("Bad stream URI");
    }
    base_uri_ = u.getBaseURI();

    if (host_) {
        // Automatically set name
        name_.resize(1024);
        #ifdef WIN32
        DWORD size = name_.capacity();
        GetComputerName(name_.data(), &size);
        #else
        gethostname(name_.data(), name_.capacity());
        #endif
    } else {
        name_ = "No name";
    }
}

Net::~Net() {
    end();

    // FIXME: Wait to ensure no net callbacks are active.
    // Do something better than this
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

bool Net::post(const StreamPacket &spkt, const DataPacket &pkt) {
    if (!active_) return false;
    if (paused_) return true;
    bool hasStale = false;

    // Cast to include msgpack methods
    auto spkt_net = reinterpret_cast<const StreamPacketMSGPACK&>(spkt);

    // Version of packet without data but with msgpack methods
    PacketMSGPACK pkt_strip;
    pkt_strip.codec = pkt.codec;
    pkt_strip.bitrate = pkt.bitrate;
    pkt_strip.frame_count = pkt.frame_count;
    pkt_strip.dataFlags = pkt.dataFlags;

    if (host_) {
        SHARED_LOCK(mutex_, lk);

        const FrameID frameId(spkt.streamID, spkt.frame_number);

        // If this particular frame has clients then loop over them
        if (clients_.count(frameId) > 0) {
            auto &clients = clients_.at(frameId);

            for (auto &client : clients) {
                // Strip packet data if channel is not wanted by client
                const bool strip =
                    static_cast<int>(spkt.channel) < 32 && pkt.data.size() > 0
                    && ((1 << static_cast<int>(spkt.channel)) & client.channels) == 0;

                try {
                    int16_t pre_transmit_latency = int16_t(ftl::time::get_time() - spkt.localTimestamp);

                    // TODO(Nick): msgpack only once and broadcast.
                    // TODO(Nick): send in parallel and then wait on all futures?
                    // Or send non-blocking and wait
                    if (!net_->send(client.peerid,
                            base_uri_,
                            pre_transmit_latency,  // Time since timestamp for tx
                            spkt_net,
                            (strip) ? pkt_strip : reinterpret_cast<const PacketMSGPACK&>(pkt))) {
                        // Send failed so mark as client stream completed
                        client.txcount = 0;
                    } else {
                        if (!strip && pkt.data.size() > 0) _checkTXRate(pkt.data.size(), 0, spkt.timestamp);

                        // Count every frame sent
                        if (spkt.channel == Channel::kEndFrame) {
                            --client.txcount;
                        }
                    }
                } catch(...) {
                    client.txcount = 0;
                }

                if (client.txcount <= 0) {
                    hasStale = true;
                }
            }
        }
    } else {
        try {
            int16_t pre_transmit_latency = int16_t(ftl::time::get_time() - spkt.localTimestamp);

            net_->send(*peer_,
                base_uri_,
                pre_transmit_latency,  // Time since timestamp for tx
                spkt_net,
                reinterpret_cast<const PacketMSGPACK&>(pkt));

            if (pkt.data.size() > 0) _checkTXRate(pkt.data.size(), 0, spkt.timestamp);
        } catch(...) {
            // TODO(Nick): Some disconnect error
            return false;
        }
    }

    if (hasStale) _cleanUp();

    hasPosted(spkt, pkt);

    return true;
}

void Net::_processPacket(ftl::net::Peer *p, int16_t ttimeoff, const StreamPacket &spkt_raw, const DataPacket &pkt) {
    int64_t now = time_point_cast<milliseconds>(high_resolution_clock::now()).time_since_epoch().count();

    if (!active_) return;

    ftl::protocol::PacketPair pair;
    StreamPacket &spkt = pair.first;
    spkt = spkt_raw;
    spkt.localTimestamp = now - int64_t(ttimeoff);
    spkt.hint_capability = 0;
    spkt.hint_source_total = 0;
    spkt.version = 4;
    if (p) spkt.hint_peerid = p->localID();

    bool isRequest = host_ && pkt.data.size() == 0 && (spkt.flags & ftl::protocol::kFlagRequest);

    FrameID localFrame(spkt.streamID, spkt.frame_number);

    if (!isRequest) {
        seen(localFrame, spkt.channel);
    }

    if (paused_) return;

    // Manage recuring requests
    if (!host_ && last_frame_ != spkt.timestamp) {
        UNIQUE_LOCK(mutex_, lk);
        if (last_frame_ != spkt.timestamp) {
            // int tc = now - last_completion_;          // Milliseconds since last frame completed
            frame_time_ = spkt.timestamp - last_frame_;  // Milliseconds per frame
            last_completion_ = now;
            bytes_received_ = 0;
            last_frame_ = spkt.timestamp;

            lk.unlock();

            // Are we close to reaching the end of our frames request?
            if (tally_ <= 5) {
                // Yes, so send new requests
                // FIXME: Do this for all frames, or use tally be frame
                // for (size_t i = 0; i < size(); ++i) {
                    const auto &sel = enabledChannels(localFrame);
                    for (auto c : sel) {
                        _sendRequest(c, localFrame.frameset(), localFrame.source(), frames_to_request_, 255);
                    }
                //}
                tally_ = frames_to_request_;
            } else {
                --tally_;
            }
        }
    }

    bytes_received_ += pkt.data.size();
    // time_at_last_ = now;

    // If hosting and no data then it is a request for data
    // Note: a non host can receive empty data, meaning data is available
    // but that you did not request it
    if (isRequest) {
        _processRequest(p, &spkt, pkt);
    }

    pair.second = std::move(pkt);
    mgr_.submit(pair, [this, now, ttimeoff, p](const ftl::protocol::PacketPair &pair) { 
        const StreamPacket &spkt = pair.first;
        const DataPacket &pkt = pair.second;

        trigger(spkt, pkt);
        if (pkt.data.size() > 0) _checkRXRate(pkt.data.size(), now-(spkt.timestamp+ttimeoff), spkt.timestamp);
    });
}

void Net::inject(const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
    _processPacket(nullptr, 0, spkt, pkt);
}

bool Net::begin() {
    if (active_) return true;

    if (net_->isBound(base_uri_)) {
        error(Error::kURIAlreadyExists, std::string("Stream already exists: ") + uri_);
        active_ = false;
        return false;
    }

    // FIXME: Potential race between above check and new binding

    // Add the RPC handler for the URI
    net_->bind(base_uri_, [this](
            ftl::net::Peer &p,
            int16_t ttimeoff,
            StreamPacketMSGPACK &spkt_raw,
            PacketMSGPACK &pkt) {

        _processPacket(&p, ttimeoff, spkt_raw, pkt);
    });

    if (host_) {
        DLOG(INFO) << "Hosting stream: " << uri_;

        {
            // Add to list of available streams
            UNIQUE_LOCK(stream_mutex, lk);
            net_streams.push_back(uri_);
        }

        active_ = true;
        net_->broadcast("add_stream", uri_);

    } else {
        tally_ = frames_to_request_;
        active_ = true;
    }

    return true;
}

void Net::refresh() {
    Stream::refresh();

    UNIQUE_LOCK(mutex_, lk);

    for (const auto &i : enabled()) {
        auto sel = enabledChannels(i);

        for (auto c : sel) {
            _sendRequest(c, i.frameset(), i.source(), frames_to_request_, 255, true);
        }
    }
    tally_ = frames_to_request_;
}

void Net::reset() {
    Stream::reset();
}

bool Net::_enable(FrameID id) {
    if (host_) { return false; }
    if (enabled(id)) return true;

    // not hosting, try to find peer now
    // First find non-proxy version, then check for proxy version if no match
    auto p = net_->findOne<ftl::UUIDMSGPACK>("find_stream", uri_);

    if (p) {
        peer_ = *p;
    } else {
        // use webservice (if connected)
        auto ws = net_->getWebService();
        if (ws) {
            peer_ = ws->id();
        } else {
            error(Error::kURIDoesNotExist, std::string("Stream not found: ") + uri_);
            return false;
        }
    }

    // TODO(Nick): check return value
    net_->send(*peer_, "enable_stream", uri_, id.frameset(), id.source());
    return true;
}

bool Net::enable(FrameID id) {
    if (host_) { return false; }
    if (!_enable(id)) return false;
    if (!Stream::enable(id)) return false;
    _sendRequest(Channel::kColour, id.frameset(), id.source(), kFramesToRequest, 255, true);

    return true;
}

bool Net::enable(FrameID id, Channel c) {
    if (host_) { return false; }
    if (!_enable(id)) return false;
    if (!Stream::enable(id, c)) return false;
    _sendRequest(c, id.frameset(), id.source(), kFramesToRequest, 255, true);
    return true;
}

bool Net::enable(FrameID id, const ChannelSet &channels) {
    if (host_) { return false; }
    if (!_enable(id)) return false;
    if (!Stream::enable(id, channels)) return false;
    for (auto c : channels) {
        _sendRequest(c, id.frameset(), id.source(), kFramesToRequest, 255, true);
    }
    return true;
}

bool Net::_sendRequest(Channel c, uint8_t frameset, uint8_t frames, uint8_t count, uint8_t bitrate, bool doreset) {
    if (!active_ || host_) return false;

    PacketMSGPACK pkt = {
        Codec::kAny,       // TODO(Nick): Allow specific codec requests
        0,
        count,
        bitrate_,
        0
    };

    uint8_t sflags = ftl::protocol::kFlagRequest;
    if (doreset) sflags |= ftl::protocol::kFlagReset;

    StreamPacketMSGPACK spkt = {
        5,
        ftl::time::get_time(),
        frameset,
        frames,
        c,
        sflags,
        0,
        0,
        0
    };

    net_->send(*peer_, base_uri_, (int16_t)0, spkt, pkt);
    hasPosted(spkt, pkt);
    return true;
}

void Net::_cleanUp() {
    UNIQUE_LOCK(mutex_, lk);
    for (auto i = clients_.begin(); i != clients_.end();) {
        auto &clients = i->second;
        for (auto j = clients.begin(); j != clients.end();) {
            auto &client = *j;
            if (client.txcount <= 0) {
                if (client.peerid == time_peer_) {
                    time_peer_ = ftl::UUID(0);
                }
                DLOG(INFO) << "Remove peer: " << client.peerid.to_string();
                j = clients.erase(j);
            } else {
                ++j;
            }
        }
        if (clients.size() == 0) {
            i = clients_.erase(i);
        } else {
            ++i;
        }
    }
}

/* Packets for specific framesets, frames and channels are requested in
 * batches (max 255 unique frames by timestamp). Requests are in the form
 * of packets that match the request except the data component is empty.
 */
bool Net::_processRequest(ftl::net::Peer *p, const StreamPacket *spkt, const DataPacket &pkt) {
    bool found = false;

    if (spkt->streamID == 255 || spkt->frame_number == 255) {
        // Generate a batch of requests
        ftl::protocol::StreamPacket spkt2 = *spkt;
        for (const auto &i : frames()) {
            spkt2.streamID = i.frameset();
            spkt2.frame_number = i.source();
            _processRequest(p, &spkt2, pkt);
        }
        return false;
    }

    DLOG(INFO) << "processing request: " << int(spkt->streamID) << ", " << int(spkt->channel);

    const FrameID frameId(spkt->streamID, spkt->frame_number);

    if (p) {
        SHARED_LOCK(mutex_, lk);

        if (clients_.count(frameId) > 0) {
            auto &clients = clients_.at(frameId);

            // Does the client already exist
            for (auto &c : clients) {
                if (c.peerid == p->id()) {
                    // Yes, so reset internal request counters
                    c.txcount = std::max(static_cast<int>(c.txcount), static_cast<int>(pkt.frame_count));
                    if (static_cast<int>(spkt->channel) < 32) {
                        c.channels |= 1 << static_cast<int>(spkt->channel);
                    }
                    found = true;
                    // break;
                }
            }
        }
    }

    // No existing client, so add a new one.
    if (p && !found) {
        {
            UNIQUE_LOCK(mutex_, lk);

            auto &clients = clients_[frameId];
            auto &client = clients.emplace_back();
            client.peerid = p->id();
            client.quality = 255;  // TODO(Nick): Use quality given in packet
            client.txcount = std::max(static_cast<int>(client.txcount), static_cast<int>(pkt.frame_count));
            if (static_cast<int>(spkt->channel) < 32) {
                client.channels |= 1 << static_cast<int>(spkt->channel);
            }
        }

        spkt->hint_capability |= ftl::protocol::kStreamCap_NewConnection;

        try {
            connect_cb_.trigger(p);
        } catch (const ftl::exception &e) {
            LOG(ERROR) << "Exception in stream connect callback: " << e.what();
        }
    }

    ftl::protocol::Request req;
    req.bitrate = pkt.bitrate;
    req.channel = spkt->channel;
    req.id = FrameID(spkt->streamID, spkt->frame_number);
    req.count = pkt.frame_count;
    req.codec = pkt.codec;
    request(req);

    return false;
}

void Net::_checkRXRate(size_t rx_size, int64_t rx_latency, int64_t ts) {
    req_bitrate__ += rx_size * 8;
    rx_sample_count__ += 1;
}

void Net::_checkTXRate(size_t tx_size, int64_t tx_latency, int64_t ts) {
    tx_bitrate__ += tx_size * 8;
    tx_sample_count__ += 1;
}

NetStats Net::getStatistics() {
    int64_t ts = ftl::time::get_time();
    UNIQUE_LOCK(msg_mtx__, lk);
    const float r = (static_cast<float>(req_bitrate__) / static_cast<float>(ts - last_msg__) * 1000.0f / 1048576.0f);
    const float t = (static_cast<float>(tx_bitrate__) / static_cast<float>(ts - last_msg__) * 1000.0f / 1048576.0f);
    last_msg__ = ts;
    req_bitrate__ = 0;
    tx_bitrate__ = 0;
    rx_sample_count__ = 0;
    tx_sample_count__ = 0;
    return {r, t};
}

bool Net::end() {
    if (!active_) return false;

    {
        UNIQUE_LOCK(stream_mutex, lk);
        auto i = std::find(net_streams.begin(), net_streams.end(), uri_);
        if (i != net_streams.end()) net_streams.erase(i);
    }

    active_ = false;
    net_->unbind(base_uri_);
    return true;
}

bool Net::active() {
    return active_;
}

bool Net::active(FrameID id) {
    SHARED_LOCK(mtx_, lk);
    return active_ && clients_.count(id) > 0;
}

void Net::setProperty(ftl::protocol::StreamProperty opt, std::any value) {
    switch (opt) {
    case StreamProperty::kBitrate       :
    case StreamProperty::kMaxBitrate    :  bitrate_ = std::any_cast<int>(value); break;
    case StreamProperty::kPaused        :  paused_ = std::any_cast<bool>(value); break;
    case StreamProperty::kName          :  name_ = std::any_cast<std::string>(value); break;
    case StreamProperty::kObservers     :
    case StreamProperty::kBytesSent     :
    case StreamProperty::kBytesReceived :
    case StreamProperty::kLatency       :
    case StreamProperty::kFrameRate     :
    case StreamProperty::kURI           :  throw FTL_Error("Readonly property");
    default                             :  throw FTL_Error("Unsupported property");
    }
}

std::any Net::getProperty(ftl::protocol::StreamProperty opt) {
    switch (opt) {
    case StreamProperty::kBitrate       :
    case StreamProperty::kMaxBitrate    :  return bitrate_;
    case StreamProperty::kObservers     :  return clients_.size();
    case StreamProperty::kURI           :  return base_uri_;
    case StreamProperty::kPaused        :  return paused_;
    case StreamProperty::kBytesSent     :  return 0;
    case StreamProperty::kBytesReceived :  return int64_t(bytes_received_);
    case StreamProperty::kFrameRate     :  return (frame_time_ > 0) ? 1000 / frame_time_ : 0;
    case StreamProperty::kLatency       :  return 0;
    case StreamProperty::kName          :  return name_;
    default                             :  throw FTL_Error("Unsupported property");
    }
}

bool Net::supportsProperty(ftl::protocol::StreamProperty opt) {
    switch (opt) {
    case StreamProperty::kBitrate       :
    case StreamProperty::kMaxBitrate    :
    case StreamProperty::kObservers     :
    case StreamProperty::kPaused        :
    case StreamProperty::kBytesSent     :
    case StreamProperty::kBytesReceived :
    case StreamProperty::kLatency       :
    case StreamProperty::kFrameRate     :
    case StreamProperty::kName          :
    case StreamProperty::kURI           :  return true;
    default                             :  return false;
    }
}

ftl::protocol::StreamType Net::type() const {
    return ftl::protocol::StreamType::kLive;
}
