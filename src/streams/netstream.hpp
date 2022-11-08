/**
 * @file netstream.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <string>
#include <list>
#include <atomic>
#include <unordered_map>
#include "../universe.hpp"
#include <ftl/threads.hpp>
#include <ftl/protocol/packet.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/handle.hpp>
#include "packetmanager.hpp"

namespace ftl {
namespace protocol {

namespace detail {
struct StreamClient {
    ftl::UUID peerid;
    std::atomic<int> txcount;           // Frames sent since last request
    std::atomic<uint32_t> channels;     // A channel mask, those that have been requested
    uint8_t quality;
};
}

struct NetStats {
    float rxRate;
    float txRate;
};

/**
 * Send and receive packets over a network. This class manages the connection
 * of clients or the discovery of a stream and deals with bitrate adaptations.
 * Each packet post is forwarded to each connected client that is still active.
 */
class Net : public Stream {
 public:
    Net(const std::string &uri, ftl::net::Universe *net, bool host = false);
    virtual ~Net();

    bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &) override;

    bool begin() override;
    bool end() override;
    bool active() override;
    bool active(FrameID id) override;

    bool enable(FrameID id) override;
    bool enable(FrameID id, ftl::protocol::Channel c) override;
    bool enable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

    void reset() override;
    void refresh() override;

    void setProperty(ftl::protocol::StreamProperty opt, std::any value) override;
    std::any getProperty(ftl::protocol::StreamProperty opt) override;
    bool supportsProperty(ftl::protocol::StreamProperty opt) override;
    StreamType type() const override;

    inline const ftl::UUID &getPeer() const {
        if (host_) { throw FTL_Error("Net::getPeer() not possible, hosting stream"); }
        if (!peer_) { throw FTL_Error("steram::Net has no valid Peer. Not found earlier?"); }
        return *peer_;
    }

    inline ftl::Handle onClientConnect(const std::function<bool(ftl::net::Peer*)> &cb) { return connect_cb_.on(cb); }

    /**
     * Return the average bitrate of all streams since the last call to this
     * function. Units are Mbps.
     */
    static NetStats getStatistics();

    static void installRPC(ftl::net::Universe *net);

    static constexpr int kFramesToRequest = 30;

    // Unit test support
    virtual void hasPosted(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &) {}
    void inject(const ftl::protocol::StreamPacket &, ftl::protocol::DataPacket &);

 private:
    SHARED_MUTEX mutex_;
    bool active_ = false;
    ftl::net::Universe *net_;
    std::optional<ftl::UUID> peer_;
    std::string uri_;
    std::string base_uri_;
    const bool host_;
    std::array<std::atomic_int, 5> tally_ = {};
    uint8_t bitrate_ = 255;
    std::atomic_int64_t bytes_received_ = 0;
    bool paused_ = false;
    int frames_to_request_ = kFramesToRequest;
    std::string name_;
    ftl::PacketManager mgr_;
    ftl::Handler<ftl::net::Peer*> connect_cb_;
    int64_t buffering_ = 0;

    static std::atomic_size_t req_bitrate__;
    static std::atomic_size_t tx_bitrate__;
    static std::atomic_size_t rx_sample_count__;
    static std::atomic_size_t tx_sample_count__;
    static int64_t last_msg__;
    static MUTEX msg_mtx__;

    struct PacketBuffer {
        ftl::protocol::PacketPair packets;
        ftl::net::Peer *peer = nullptr;
        std::atomic_bool done = false;
    };

    struct FrameState {
        ftl::protocol::FrameID id;
        std::atomic_int active = 0;
        MUTEX mtx;
        std::list<PacketBuffer> buffer;
        int64_t base_pkt_ts_ = 0;
        int64_t base_local_ts_ = 0;
    };

    SHARED_MUTEX statesMtx_;
    std::unordered_map<uint32_t, std::unique_ptr<FrameState>> frameStates_;

    std::unordered_map<ftl::protocol::FrameID, std::list<detail::StreamClient>> clients_;
    std::thread thread_;

    FrameState *_getFrameState(FrameID id);
    bool _enable(FrameID id);
    bool _processRequest(ftl::net::Peer *p, const ftl::protocol::StreamPacket *spkt, ftl::protocol::DataPacket &pkt);
    void _checkRXRate(size_t rx_size, int64_t rx_latency, int64_t ts);
    void _checkTXRate(size_t tx_size, int64_t tx_latency, int64_t ts);
    bool _sendRequest(
        ftl::protocol::Channel c,
        uint8_t frameset,
        uint8_t frames,
        uint8_t count,
        uint8_t bitrate,
        bool doreset = false);
    void _cleanUp();
    void _processPacket(ftl::net::Peer *p, int16_t ttimeoff, const StreamPacket &spkt_raw, DataPacket &pkt);
    void _run();
};

}  // namespace protocol
}  // namespace ftl
