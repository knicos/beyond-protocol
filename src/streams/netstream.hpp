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
#include <set>
#include "../universe.hpp"
#include <ftl/threads.hpp>
#include <ftl/protocol/packet.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/handle.hpp>
#include "packetmanager.hpp"

#define DEBUG_NETSTREAM 

namespace ftl {
namespace protocol {

namespace detail {

struct StreamClientLocal {
    int peerid;                         // Local id
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

    int postQueueSize(FrameID frame_id, Channel channel) const override;

    bool begin() override;
    bool end() override;
    bool active() override;
    bool active(FrameID id) override;

    bool enable(FrameID id) override;
    bool enable(FrameID id, ftl::protocol::Channel c) override;
    bool enable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

    void reset() override;
    void refresh() override;

    void setBuffering(float seconds);

    void setAutoBufferAdjust(bool enable);

    void setProperty(ftl::protocol::StreamProperty opt, std::any value) override;
    std::any getProperty(ftl::protocol::StreamProperty opt) override;
    bool supportsProperty(ftl::protocol::StreamProperty opt) override;
    StreamType type() const override;

    inline const ftl::UUID &getPeer() const {
        if (host_) { throw FTL_Error("Net::getPeer() not possible, hosting stream"); }
        if (!peer_) { throw FTL_Error("steram::Net has no valid Peer. Not found earlier?"); }
        return *peer_;
    }

    inline ftl::Handle onClientConnect(const std::function<bool(ftl::net::PeerBase*)> &cb) { return connect_cb_.on(cb); }

    /**
     * Return the average bitrate of all streams since the last call to this
     * function. Units are Mbps.
     */
    static NetStats getStatistics();

    static void installRPC(ftl::net::Universe *net);

    static constexpr int kFramesToRequest = 80;

    // Unit test support
    virtual void hasPosted(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &) {}
    void inject(const ftl::protocol::StreamPacket &, ftl::protocol::DataPacket &);

    // Allow frames with different timestmaps to interleave. If disabled, newer frame packets are processed
    // only after previous frame receives kFrameEnd and Stream waits until kFrameEnd is processed by the consumer.
    // When enabled (default), packets for next frame may be sent to consumer before the previous frame had received 
    // kFrameEnd and before all previous frame's callbacks have returned. Only relevant for receiving stream
    // (streaming from remote). 
    // 
    // Note that  there is currently no way to guarantee that the Stream will produce frames in timestamp order if 
    // actual receives can interleave (not implemented). If Peer is allowed interleave (multiple quic streams) packets
    // of different frames, simplest way to support this might be by adding frame counter via new channel kFrameBegin
    // gives each frame a sequence number and order by it.
    // 
    // See also synchronize_frame_timestamps_.
    void allowFrameInterleaving(bool);

 private:
    bool send(const ftl::protocol::StreamPacket&, const ftl::protocol::DataPacket&);

    /** Build with -DDEBUG_NETSTREAM or define DEBUG_NETSTREAM in header to enable send/recv asserts on 
     *  packet (timestamp) order. If parallel send/receive (with multple quic streams) is implemented, assumption
     *  on received timestamps always larger or equal to earlier received timestmaps no longer holds. See also
     *  above note on allowFrameInterleaving()  */
    #ifdef DEBUG_NETSTREAM
    std::unordered_map<uint64_t, int64_t> dbg_send_;
    std::unordered_map<uint64_t, int64_t> dbg_recv_;
    std::mutex dbg_mtx_send_;
    std::mutex dbg_mtx_recv_;
    #endif
    
    bool net_send_(const ftl::UUID &pid, const std::string &name, int16_t ttimeoff, const ftl::protocol::StreamPacket&, const ftl::protocol::DataPacket&);
    bool net_send_(ftl::net::PeerBase* peer, const std::string &name, int16_t ttimeoff, const ftl::protocol::StreamPacket&, const ftl::protocol::DataPacket&);

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
    ftl::Handler<ftl::net::PeerBase*> connect_cb_;

    static std::atomic_size_t req_bitrate__;
    static std::atomic_size_t tx_bitrate__;
    static std::atomic_size_t rx_sample_count__;
    static std::atomic_size_t tx_sample_count__;
    static int64_t last_msg__;
    static MUTEX msg_mtx__;

    // Recv Buffering; All access to recv buffering variables must be synchronized with queue_mtx_
    std::mutex queue_mtx_;
    std::condition_variable queue_cv_;

    bool buffering_auto_ = true; // Disable adaptive buffering
    int underruns_ = 0;                 // TODO: Move to PacketQueue
    int last_underrun_buffering_ = 0;   // TODO: Move to PacketQueue
    int32_t buffering_ = 0; // Network buffering delay before dispatched for processing (milliseconds) // TODO: Move to PacketQueue

    int32_t buffering_default_ = 0; // Default value for buffering if automatic adjustment is disabled. If not set, current value used if adjustment disabled.
    int32_t buffering_min_ms_ = 20; // Minimum network buffer size (milliseconds)

    int64_t t_buffering_updated_ = 0; // TODO: Move to PacketQueue
    int64_t buffering_update_delay_ms_ = 50;    // Delay between buffering adjustments (prevent too rapid changes)

    struct PacketBuffer {
        ftl::protocol::StreamPacket spkt;
        ftl::protocol::DataPacket dpkt;

        // Relative timestamp since first frame
        int64_t ts_rel;

        // Set to true once this entry can be removed from PacketQueue::packets
        bool packet_consumed;
        
        PacketBuffer(ftl::protocol::StreamPacket spkt_in, ftl::protocol::DataPacket dpkt_in, int64_t ts_local=0) : 
            spkt(std::move(spkt_in)), dpkt(std::move(dpkt_in)), ts_rel(ts_local), packet_consumed(false) {};
    };

    struct PacketQueue {
        std::mutex mtx;
        // Timestamps must be always increasing: push back, pop front.
        std::deque<PacketBuffer> packets;

        /** First received frame timestamps is used to calculate relative timestamps */
        const int64_t ts_base_spkt = 0;

        /** Local clock timestmap (guess) for first seen timestmap. */
        int64_t ts_base_local = 0;

        /** Packet counter, maps timestamp for number of packets received, zeroed on last packet. */
        std::unordered_map<int64_t, int> incomplete_packet_count;
        //std::unordered_set<int64_t> completed; redundant

        #ifdef TRACY_ENABLE
        char const* profiler_id_queue_length_; // Profiler id for queue length (in milliseconds)
        char const* profiler_id_queue_underrun_count_; // Profiler id for queue length (in milliseconds)
        #endif

        PacketQueue(int64_t base_spkt, int64_t base_local=0) : ts_base_spkt(base_spkt), ts_base_local(base_local) {}
    };

    std::unordered_map<ftl::protocol::FrameID, PacketQueue> packet_queue_;
    ftl::TaskQueue pending_packets_;

    /** If enabled, packet callbacks are synchronized by timestamp: callbacks are waited before next processing for 
     *  frame(set) with next timestamp begins. When disabled, callbacks of different channels may interleave, but
     *  ordering by timestamp for each channel is always guaranteed. This option is here for backward compatibility.
     */
    bool synchronize_on_recv_timestamps_ = false;

    void queuePacket_(ftl::net::PeerBase*, ftl::protocol::StreamPacket, ftl::protocol::DataPacket);

    // Used for warning message if more than one peer in streaming to this instance (buffering will likely fail)
    int64_t dbg_streams_peers_mask_ = 0;

    // End of Recv Buffering

    std::unordered_map<ftl::protocol::FrameID, std::list<detail::StreamClientLocal>> clients_local_;

    bool _enable(FrameID id);
    bool _processRequest(ftl::net::PeerBase *p, const ftl::protocol::StreamPacket *spkt, ftl::protocol::DataPacket &pkt);
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
    void _processPacket(ftl::net::PeerBase *p, int16_t ttimeoff, const StreamPacket &spkt_raw, DataPacket &pkt);
    void _earlyProcessPacket(ftl::net::PeerBase *p, int16_t ttimeoff, const StreamPacket &spkt_raw, DataPacket &pkt);

    // processing loop for non-hosted netstreams (runs in dedicated thread)
    void run_();
    void netstream_thread_();

    char const*  profiler_frame_id_;
    char const*  profiler_queue_id_;

    std::thread thread_;
    static void process_buffered_packets_(
        Net* stream,
        std::vector<std::tuple<ftl::protocol::StreamPacket, ftl::protocol::DataPacket>>,
        bool sync_frames);
    ftl::WorkerQueue<process_buffered_packets_, Net*, std::vector<std::tuple<ftl::protocol::StreamPacket, ftl::protocol::DataPacket>>, bool> packet_process_queue_;
};

}  // namespace protocol
}  // namespace ftl
