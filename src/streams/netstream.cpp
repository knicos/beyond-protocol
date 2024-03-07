/**
 * @file netstream.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <list>
#include <string>
#include <memory>
#include <utility>
#include <algorithm>
#include <thread>
#include <chrono>
#include "netstream.hpp"
#include <ftl/time.hpp>
#include <ftl/counter.hpp>
#include <ftl/profiler.hpp>
#include "../uuidMSGPACK.hpp"
#include "packetMsgpack.hpp"

#define LOGURU_REPLACE_GLOG 1
#include <ftl/lib/loguru.hpp>

#ifndef WIN32
#include <unistd.h>
#include <limits.h>
#else
#include <timeapi.h>
#pragma comment(lib, "Winmm")
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
using std::this_thread::sleep_for;
using std::chrono::high_resolution_clock;

std::atomic_size_t Net::req_bitrate__ = 0;
std::atomic_size_t Net::tx_bitrate__ = 0;
std::atomic_size_t Net::rx_sample_count__ = 0;
std::atomic_size_t Net::tx_sample_count__ = 0;
int64_t Net::last_msg__ = 0;
MUTEX Net::msg_mtx__;

#ifdef DEBUG_NETSTREAM

void dbg_check_pkt(std::mutex& mtx, std::unordered_map<uint64_t, int64_t>& ts, const ftl::protocol::StreamPacket& spkt, const std::string& name)
{
    // This check verifies per-channel timestmaps are always in increasing order. See comments header for details.

    auto lk = std::unique_lock(mtx);
    auto channel = spkt.channel;
    auto id = ftl::protocol::FrameID(spkt.frameSetID(), spkt.frameNumber());
    uint64_t key = uint64_t(id.id) << 32;
    key |= uint64_t(channel);
    auto ts_new = spkt.timestamp;
    if (ts.count(key) > 0) {
        auto ts_old = ts[key];
        LOG_IF(ERROR, ts_old > ts_new) << "Out of Order " << name << " for channel " << int(channel) << ", diff: " << (ts_new - ts_old) << ", new ts. " << ts_new;
        ts[key] = ts_new;
    }
    else {
        ts[key] = ts_new;
    }
}

#define DEBUG_CHECK_PKT(mtx, ts, spkt, name) dbg_check_pkt(mtx, ts, spkt, name)
#else
#define DEBUG_CHECK_PKT(mtx, ts, spkt, name) {}
#endif

static SHARED_MUTEX stream_mutex;
static std::list<std::string> net_streams;

void Net::installRPC(ftl::net::Universe *net) {
    static std::atomic_int installRPC_count = 0;

    // net->bind() should fail, but print an error message to indicate incorrect use.
    LOG_IF(ERROR, ++installRPC_count == 0)
        << "Net::installRPC() called more than once (total: " << installRPC_count << "), "
        << "should only be called once at initialization (BUG)";

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
        net_(net), uri_(uri), host_(host) {
    ftl::URI u(uri_);
    if (!u.isValid() || !(u.getScheme() == ftl::URI::SCHEME_FTL)) {
        error(Error::kBadURI, uri_);
        throw FTL_Error("Bad stream URI");
    }
    base_uri_ = u.getBaseURI();
    profiler_frame_id_ = PROFILER_RUNTIME_PERSISTENT_NAME("net: " + uri);

    // callbacks for processing bound in begin()

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
    pending_packets_.stop();
    pending_packets_.wait();
}

int Net::postQueueSize(FrameID frame_id, Channel channel) const {
    return 0;
}

bool Net::net_send_(ftl::net::PeerBase* peer, const std::string &name, int16_t ttimeoff, const ftl::protocol::StreamPacket& spkt, const ftl::protocol::DataPacket& dpkt) {
    DEBUG_CHECK_PKT(dbg_mtx_send_, dbg_send_, spkt, "send");
    return peer->send(name, ttimeoff, reinterpret_cast<const StreamPacketMSGPACK&>(spkt), reinterpret_cast<const PacketMSGPACK&>(dpkt));
}

bool Net::net_send_(const ftl::UUID &pid, const std::string &name, int16_t ttimeoff, const ftl::protocol::StreamPacket& spkt, const ftl::protocol::DataPacket& dpkt) {
    DEBUG_CHECK_PKT(dbg_mtx_send_, dbg_send_, spkt, "send");
    return net_->send(pid, name, ttimeoff, reinterpret_cast<const StreamPacketMSGPACK&>(spkt), reinterpret_cast<const PacketMSGPACK&>(dpkt));
}

bool Net::send(const StreamPacket &spkt, const DataPacket &pkt) {
    if (!active_) return false;
    if (paused_) return true;

    bool hasStaleClients = false;

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
        if (clients_local_.count(frameId) > 0) {
            auto &clients = clients_local_.at(frameId);

            for (auto &client : clients) {
                // Strip packet data if channel is not wanted by client
                const bool strip =
                    static_cast<int>(spkt.channel) < 32 && pkt.data.size() > 0          // is a video channel?
                    && ((1 << static_cast<int>(spkt.channel)) & client.channels) == 0;  // not included in bitmask?

                try {
                    int16_t pre_transmit_latency = int16_t(ftl::time::get_time() - spkt.localTimestamp);

                    // TODO(Nick): msgpack only once and broadcast.
                    auto peer = net_->getPeer(client.peerid);
                    
                    // Send with QUIC should be non-blocking (assuming there are send buffers available)
                    if (!peer || !net_send_(
                            peer.get(),
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
                    hasStaleClients = true;
                }
            }
        }
    } else {
        try {
            int16_t pre_transmit_latency = int16_t(ftl::time::get_time() - spkt.localTimestamp);

            net_send_(*peer_,
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

    if (hasStaleClients) _cleanUp();

    hasPosted(spkt, pkt);

    return true;
}

bool Net::post(const StreamPacket &spkt, const DataPacket &pkt) {
    // Quic won't block unless output buffers full. Likely causes issues with
    // blocking TCP/when no free output buffers (Quic will print a warning,
    // old TCP networking will die without clear error messages).
    send(spkt, pkt);
    return true;
}

void Net::_earlyProcessPacket(ftl::net::PeerBase *p, int16_t ttimeoff, const StreamPacket &spkt, DataPacket &pkt) {
    // Better to crash here than crash later because of out-of-bounds write. StreamID 255 means "all streams".
    CHECK(spkt.streamID < 5 || spkt.streamID == 255) << "FIXME: Frameset ID must be less than 5 or 255 (tally_[] is a fixed size array)";
    DEBUG_CHECK_PKT(dbg_mtx_recv_, dbg_recv_, spkt, "recv");

    if (!active_) return;

    bool isRequest = host_ && pkt.data.size() == 0 && (spkt.flags & ftl::protocol::kFlagRequest);

    FrameID localFrame(spkt.streamID, spkt.frame_number);

    if (!isRequest) {
        seen(localFrame, spkt.channel); // What uses this?
    }

    if (paused_) return;

    // Manage recuring requests
    if (!host_ && spkt.channel == Channel::kEndFrame && localFrame.frameset() < tally_.size()) {
        // Are we close to reaching the end of our frames request?
        if (tally_[localFrame.frameset()] <= frames_to_request_ / 2) {
            // Yes, so send new requests
            for (const auto f : enabled(localFrame.frameset())) {
                const auto &sel = enabledChannels(f);
                for (auto c : sel) {
                    _sendRequest(c, f.frameset(), f.source(), frames_to_request_, 255);
                }
            }
            tally_[localFrame.frameset()] = frames_to_request_; // Why replace and not increment?
        } else {
            --tally_[localFrame.frameset()];
        }
    }
}

void Net::_processPacket(ftl::net::PeerBase *p, int16_t ttimeoff, const StreamPacket &spkt_raw, DataPacket &pkt) {
    if (!active_) return;

    auto now = ftl::time::get_time();

    ftl::protocol::PacketPair pair;
    StreamPacket &spkt = pair.first;
    spkt = spkt_raw;
    spkt.localTimestamp = now - int64_t(ttimeoff); // What is this used for?
    spkt.hint_capability = 0;
    spkt.hint_source_total = 0;
    spkt.version = 4;

    bool isRequest = pkt.data.size() == 0 && (spkt.flags & ftl::protocol::kFlagRequest);

    bytes_received_ += pkt.data.size();

    // If hosting and no data then it is a request for data
    // Note: A non host can receive empty data, meaning data is available but that you did not request it
    if (host_ && isRequest) {
        _processRequest(p, &spkt, pkt);
    }

    trigger(spkt, pkt);
    if (pkt.data.size() > 0) _checkRXRate(pkt.data.size(), now-(spkt.timestamp+ttimeoff), spkt.timestamp);
}

void Net::inject(const ftl::protocol::StreamPacket &spkt, ftl::protocol::DataPacket &pkt) {
    _processPacket(nullptr, 0, spkt, pkt);
}

void Net::run_() {
    CHECK(!thread_.joinable());
    thread_ = std::thread(&Net::netstream_thread_, this);
}

static std::once_flag warning_large_peerid;
static std::once_flag warning_more_than_one_peer_streaming;

void Net::allowFrameInterleaving(bool value) {
    auto lk = std::unique_lock(queue_mtx_);
    synchronize_on_recv_timestamps_ = true;
}

void Net::queuePacket_(ftl::net::PeerBase* peer, ftl::protocol::StreamPacket spkt, ftl::protocol::DataPacket dpkt) {
    int64_t t_now = ftl::time::get_time();
    auto fid = FrameID(spkt.streamID, spkt.frame_number);
    bool is_eof_packet = spkt.channel == Channel::kEndFrame;

    auto queue_lock = std::unique_lock(queue_mtx_);

    // Buffering assumes 1:1 relationship between Peer and receiving (non-hosting) NetStream (this class) instance.
    // A warning is outputted once if packets are received from more than one peer. This may print false alarm if same
    // Peer gets a new LocalId (reconnetion?).
    if (auto peer_id = (unsigned int)(peer->localID()) < 64) {
        int64_t mask = 1 << peer_id;
        dbg_streams_peers_mask_ |= mask;

        // This could just as well be an assert
        if (dbg_streams_peers_mask_ != mask) {
            std::call_once(warning_more_than_one_peer_streaming, [](){
                LOG(ERROR) << "More than one Peer streams to the same NetStream instance (Local PeerId changed); buffering will likely fail.";
            });
        }
    }
    else {
        std::call_once(warning_large_peerid, [](){
            LOG(INFO) << "Local PeerId larger than 64 (additional debug checks not performed)";
        });
    }

    // Adjust buffering based on latency. Assumes only one Peer, in principle could be updated to work with more than
    // one peer if bufferi is determined by the peer with most delay.  Buffering is updated only if underruns occurred.
    // TODO: Documentation; Whas is the exact use case here? Is single netstream supposed to process input from more
    //       than one peer? The code appears to permit multiple peers to use stream into the same netstream.
    if (buffering_auto_ && (t_now - t_buffering_updated_) > buffering_update_delay_ms_) {
        t_buffering_updated_ = t_now;
        
        // These should be adjustable parameters
        float buffer_rtt_size = 2.1f;
        float grow_factor = 1.2;
        float shrink_threshold = 1.33;
        float shrink_factor = 0.9;
        int32_t buffer_diff_max_ms = 20; // Absolute maximum increment/decrement allowed per update

        int32_t buffering_auto = float(peer->getRtt())*buffer_rtt_size;

        #ifdef TRACY_ENABLE
        TracyPlot("network buffer underruns ", double(underruns_));
        #endif

        if (underruns_ > 0) {
            last_underrun_buffering_ = buffering_;

            if (buffering_ < buffering_auto) {
                buffering_ = std::min(buffering_auto, buffering_ + buffer_diff_max_ms);
            }
            else {
                buffering_ =  std::min<int32_t>(buffering_auto*grow_factor, buffering_ + buffer_diff_max_ms);;
            }
            buffering_ = std::max(buffering_, buffering_min_ms_);
            LOG_IF(1, buffering_ != last_underrun_buffering_) << "Netstream: buffering increased to " << buffering_ << "ms";
            underruns_ = 0;
        }
        else if (buffering_ > buffering_auto*shrink_threshold) {
            // This branch shrinks the buffer in case it gets large (compared to buffering_auto)
            auto buffering_old = buffering_;
            buffering_ = std::max(
                std::max<int32_t>(buffering_*shrink_factor, buffering_ - buffer_diff_max_ms),
                buffering_min_ms_
            );
            LOG_IF(1, buffering_ != buffering_old) << "Netstream: buffering decreased to " << buffering_ << "ms";
        }
    }

    // Create a new entry in unordered set if not seen before, using this packet's timestamp as base
    auto [queue_itr, did_insert] = packet_queue_.try_emplace(fid, spkt.timestamp); 
    auto& queue = queue_itr->second;

    // Buffering uses this as base value to calculate when frames should be released
    if (did_insert) {
        #ifdef TRACY_ENABLE
        std::string name = uri_ + " (" + std::to_string(fid.frameset()) + ", " + std::to_string(fid.source()) + ")";
        queue.profiler_id_queue_length_ = 
            PROFILER_RUNTIME_PERSISTENT_NAME("packet buffer length (ms): " + name);
        queue.profiler_id_queue_underrun_count_ =
            PROFILER_RUNTIME_PERSISTENT_NAME("packet buffer underruns: " + name);
        #endif

        /* The code below tries to estimate base timestamp of the stream in local clock. Time synchronization
           can not be relied on: it is not going to work over webservice. Instead the time of arrival for first 
           packet is used as an optimistic guess (sensitivity to startup delay).

        // Calculate local timestamp for the first packet using Peer's time offset
        int64_t ts_local = spkt.timestamp + peer->getClockOffset();

        // Estimated current network delay (half of rtt)
        int32_t net_delay = peer->getRtt()/2;

        if (ts_local > 0 && ts_local - net_delay - buffering_ < t_now) {
            // Timestamp within buffering, set base to original timestamp (in local clock)
            queue.ts_base_local = ts_local;
        }
        else {
            // Use receive time as base (recorded stream etc)
            queue.ts_base_local = t_now;
        }
        */
        
        queue.ts_base_local = t_now;
    }

    queue_lock.unlock();

    // Insert into queue, update completed time if End of Frame packet
    auto lock = std::unique_lock(queue.mtx);
    const int64_t ts = spkt.timestamp;
    const int64_t ts_rel = ts - queue.ts_base_spkt;
    queue.packets.emplace_back(std::move(spkt), std::move(dpkt), ts_rel);

    auto& count = queue.incomplete_packet_count[spkt.timestamp];
    count++;

    // EndOfFrame dpkt.packet_count has total number packets sent for this timestamp
    if (is_eof_packet) { count -= dpkt.packet_count; }
    
    if (count == 0) {
        queue.incomplete_packet_count.erase(ts);
        FTL_PROFILE_FRAME_END(profiler_frame_id_);
    }

    // It might be necessary to add book keeping for end of frame counts per frameset ...
    // On the other hand, is there anything to be done here if something is missing?
}

void Net::process_buffered_packets_(Net* stream, std::vector<std::tuple<ftl::protocol::StreamPacket, ftl::protocol::DataPacket>> packets, bool sync_frames) {
    if (!stream->active_) { return; }

    std::vector<std::tuple<ftl::protocol::StreamPacket, ftl::protocol::DataPacket>*> packets_sorted;
    packets_sorted.reserve(packets.size());
    for (auto& pkt : packets) { packets_sorted.push_back(&pkt); }

    // Sort packets by timestamp and place kEndFrame as last for each timestmap.
    std::stable_sort(packets_sorted.begin(), packets_sorted.end(),
        [](const auto& a, const auto& b) { 
            auto& a_spkt = std::get<StreamPacket>(*a);
            auto& b_spkt = std::get<StreamPacket>(*b);
            if (a_spkt.timestamp == b_spkt.timestamp) {
                return b_spkt.channel == Channel::kEndFrame;
            }
            else {
                return a_spkt.timestamp < b_spkt.timestamp;
            }
    });

    // Parallelize on expensive (video) channels (channel < 32).
    // The model of parallelization is the same as if multiple quic streams are used (without buffering);
    // video frames can arrive in parallel to other channels.
    ftl::threads::Batch batch;

    for (auto* pkt : packets_sorted) {
        auto& spkt = std::get<StreamPacket>(*pkt);
        auto& dpkt = std::get<DataPacket>(*pkt);

        auto channel = spkt.channel;
        if (int(channel) < 32) {
            batch.add([&](){ stream->_processPacket(nullptr, 0, spkt, dpkt); });
        }
        else {
            // TODO: kEndFrame must be passed to consumer only after previous callbacks have finished
            //       sync_frames set to true as workaround here (concurrency needs to be implemented differently).
            sync_frames = true;
            if (sync_frames && spkt.channel == Channel::kEndFrame) { batch.wait(); }

            stream->_processPacket(nullptr, 0, spkt, dpkt);
        }
    }

    // Batch.wait() called by ~Batch()
}

void Net::netstream_thread_() {
    loguru::set_thread_name("netstream");
    #ifndef WIN32
    // TODO: Just remove? This probably was never effective, as in older
    //       versions the return value was not checked.
    sched_param p;
    p.sched_priority = sched_get_priority_max(SCHED_RR);
    if (pthread_setschedparam(thread_.native_handle(), SCHED_RR, &p) != 0) 
    {
        LOG(ERROR) << "Could not set netstream thread priority";
    }
    #endif

    // There should be no assumptions on the accuracy of this timer.

    int64_t next_frame_ts_local = ftl::time::get_time() + buffering_min_ms_;
    int64_t t_now_u = ftl::time::get_time_micro();

    std::vector<std::tuple<StreamPacket, DataPacket>> packets;
    std::vector<std::tuple<StreamPacket, DataPacket>*> packets_sorted; 
    std::vector<PacketQueue*> queues;

    int64_t buffering = buffering_;
    bool sync_frames = synchronize_on_recv_timestamps_;

    while(active_) {
        FTL_PROFILE_SCOPE("NetStream Buffering");

        t_now_u = ftl::time::get_time_micro();

        packets.clear();
        packets_sorted.clear();
        queues.clear();
        
        {
            auto lk = std::unique_lock(queue_mtx_);

            int64_t t_wait_ms = std::max<int64_t>(next_frame_ts_local - ftl::time::get_time(), 1);
            LOG_IF(WARNING, t_wait_ms > 1000) << "netstream waiting for " << t_wait_ms;
            queue_cv_.wait_for(lk, std::chrono::milliseconds(t_wait_ms), [&](){ return !active_; });
            if (packet_queue_.size() == 0) { continue; }

            for (auto& [fid, queue] : packet_queue_) {
                if (queue.packets.size() > 0) {
                    queues.push_back(&queue);
                }
                // TODO: If necessary, unused queues could be removed by setting a flag (set once in,
                //       when "removed", second time here to indicate the queue won't be used later).
                //       Actual remove can then happen on either callback.
            }

            buffering = buffering_;
            sync_frames = synchronize_on_recv_timestamps_;

            if (queues.size() == 0) { // No packets in any of the queues
                underruns_++;
            }
        }

        for (auto* queue : queues) {
            auto lk = std::unique_lock(queue->mtx);

            // Actual buffer length in milliseconds
            int buffer_length_ms = queue->packets.back().spkt.timestamp - queue->packets.front().spkt.timestamp;
            #ifdef TRACY_ENABLE
            TracyPlot(queue->profiler_id_queue_length_, double(buffer_length_ms));
            #endif

            auto should_process_now = [&](const PacketBuffer& pkt) {
                bool have_all_packets = queue->incomplete_packet_count.count(pkt.spkt.timestamp) == 0;

                if (sync_frames && !have_all_packets) {
                    // See note on allowFrameInterleaving(). This is NOT sufficient to guarantee that callbacks
                    // are always in timestmap order. This can only reorder long enough network buffers by 
                    // deferring callbacks until all packets are received.
                    return false;
                }

                if (pkt.spkt.channel == Channel::kEndFrame && !have_all_packets) {
                    // All packets not yet received, but kEndOfFrame (with number of packets sent) already
                    // received. This packet must not be sent until all actual data packets are.

                    // This should not happen frames are assumed to be sent in order and not interleaved, see
                    // comment for allowFrameInterleaving(). However, the best place to handle this is
                    // probably in here (this thread).
                    
                    return false;
                }

                // Calculate time to release from buffer 
                int64_t t_buffered_ms = queue->ts_base_local + pkt.ts_rel + buffering;
                return (t_buffered_ms*1000 < t_now_u);
            };

            for (auto& packet : queue->packets) {
                if (packet.packet_consumed) { continue; }

                if (should_process_now(packet)) {
                    packets.emplace_back(std::move(packet.spkt), std::move(packet.dpkt));
                    packet.packet_consumed = true;
                }
                else {
                    next_frame_ts_local = std::min(
                        queue->ts_base_local + packet.ts_rel + buffering,
                        next_frame_ts_local);
                }
            }

            // Clear all processed packets in front of the queue
            while((queue->packets.size() > 0) && queue->packets.front().packet_consumed) {
                 queue->packets.pop_front();
            }
        }

        if (packets.size() > 0) {
            int n_pending = packet_process_queue_.queue(this, std::move(packets), sync_frames);
            // Consumer should be fixed, dropping here is not a good idea (encoded video etc.)
            LOG_IF(WARNING, n_pending > 3)  << "Netstream buffering: " << n_pending << " "
                                            << "framesets pending (processing too slow)";
        }
    }
    pending_packets_.stop(true);
    pending_packets_.wait();
}

bool Net::begin() {
    if (active_) return true;

    if (net_->isBound(base_uri_)) {
        error(Error::kURIAlreadyExists, std::string("Stream already exists: ") + uri_);
        active_ = false;
        return false;
    }

    // FIXME: Potential race between above check and new binding

    // Add the RPC handler for the URI (called by Peer::process_message_())
            
    if (!host_) {
        net_->bind(base_uri_, [this](
            ftl::net::PeerBase &p,
            int16_t ttimeoff, // Offset between capture and transmission
            StreamPacketMSGPACK &spkt,
            PacketMSGPACK &pkt) {
                _earlyProcessPacket(&p, ttimeoff, spkt, pkt);

                if (spkt.flags & ftl::protocol::kFlagOutOfBand) {
                    // FIXME: Timestamps are going to be incorrect; perhaps instead to next frame
                    //        and rewrite timestamp to work around the design.
                    _processPacket(&p, ttimeoff, spkt, pkt);
                    return;
                }

                queuePacket_(&p, std::move(spkt), std::move(pkt));
        });
    }
    else {
        net_->bind(base_uri_, [this](
                ftl::net::PeerBase &p,
                int16_t ttimeoff, // Offset between capture and transmission
                StreamPacketMSGPACK &spkt,
                PacketMSGPACK &pkt) {
            
            // process immediately
            _earlyProcessPacket(&p, ttimeoff, spkt, pkt);
            _processPacket(&p, ttimeoff, spkt, pkt);
        });
    }

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
        for (size_t i = 0; i < tally_.size(); ++i) tally_[i] = frames_to_request_;
        active_ = true;
    }

    if (!host_) run_();

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

    for (size_t i = 0; i < tally_.size(); ++i) tally_[i] = frames_to_request_;
}

void Net::reset() {
    Stream::reset();
}

bool Net::_enable(FrameID id) {
    if (host_) { return false; }
    if (peer_) { return true; }
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
    _sendRequest(Channel::kColour, id.frameset(), id.source(), frames_to_request_, 255, true);

    return true;
}

bool Net::enable(FrameID id, Channel c) {
    if (host_) { return false; }
    if (!_enable(id)) return false;
    if (!Stream::enable(id, c)) return false;
    _sendRequest(c, id.frameset(), id.source(), frames_to_request_, 255, true);
    return true;
}

bool Net::enable(FrameID id, const ChannelSet &channels) {
    if (host_) { return false; }
    if (!_enable(id)) return false;
    if (!Stream::enable(id, channels)) return false;
    for (auto c : channels) {
        _sendRequest(c, id.frameset(), id.source(), frames_to_request_, 255, true);
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

    net_send_(*peer_, base_uri_, (int16_t)0, spkt, pkt);
    hasPosted(spkt, pkt);
    
    return true;
}

void Net::_cleanUp() {
    UNIQUE_LOCK(mutex_, lk);
    for (auto i = clients_local_.begin(); i != clients_local_.end();) {
        auto &clients = i->second;
        for (auto j = clients.begin(); j != clients.end();) {
            auto &client = *j;
            if (client.txcount <= 0) {
                LOG(1) << "Netstream " << uri_ << " , removing peer: " << client.peerid << " (local id)";
                j = clients.erase(j);
            } else {
                ++j;
            }
        }
        if (clients.size() == 0) {
            i = clients_local_.erase(i);
        } else {
            ++i;
        }
    }
}

/* Packets for specific framesets, frames and channels are requested in
 * batches (max 255 unique frames by timestamp). Requests are in the form
 * of packets that match the request except the data component is empty.
 */
bool Net::_processRequest(ftl::net::PeerBase *p, const StreamPacket *spkt, DataPacket &pkt) {
    bool found = false;

    if (spkt->streamID == 255 || spkt->frame_number == 255) {
        // Generate a batch of requests
        ftl::protocol::StreamPacket spkt2 = *spkt;
        for (const auto &i : frames()) {
            if (spkt->streamID != 255 && i.frameset() != spkt->streamID) continue;
            if (spkt->frame_number != 255 && i.source() != spkt->frame_number) continue;
            spkt2.streamID = i.frameset();
            spkt2.frame_number = i.source();
            _processRequest(p, &spkt2, pkt);
        }
        return false;
    }

    const FrameID frameId(spkt->streamID, spkt->frame_number);

    if (p) {
        SHARED_LOCK(mutex_, lk);

        if (clients_local_.count(frameId) > 0) {
            auto &clients = clients_local_.at(frameId);

            // Does the client already exist
            for (auto &c : clients) {
                // localID used here to allow same peer to connect multiple times. If peer
                //  uuid would be used, the Peer would get removed on first disconnect.
                if (c.peerid == p->localID()) {
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

            auto &clients = clients_local_[frameId];
            auto &client = clients.emplace_back();
            client.peerid = p->localID();
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
            DLOG(ERROR) << "Exception in stream connect callback: " << e.what();
        }
    }

    if (static_cast<int>(spkt->channel) < 32) {
        pkt.bitrate = std::min(pkt.bitrate, bitrate_);
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

    {
        auto lk = std::unique_lock(queue_mtx_);
        active_ = false;
    }
    queue_cv_.notify_all();
    
    // unbind() returns only after any calls to base_uri_ have returned
    net_->unbind(base_uri_);
    if (thread_.joinable()) thread_.join();

    return true;
}

bool Net::active() {
    return active_;
}

bool Net::active(FrameID id) {
    SHARED_LOCK(mtx_, lk);
    return active_ && clients_local_.count(id) > 0;
}

void Net::setBuffering(float seconds) {
    auto lk = std::unique_lock(queue_mtx_);
    buffering_ = seconds*1000.0f;
    buffering_default_ = seconds*1000.0f;
}

void Net::setAutoBufferAdjust(bool enable) {
    auto lk = std::unique_lock(queue_mtx_);
    buffering_auto_ = enable;
    LOG(1) << "Automatic buffer size adjustment " << (enable ? "enabled" : "disabled");
    if (!buffering_auto_) {
        buffering_ = std::max(buffering_default_, buffering_min_ms_);
    }
}

void Net::setProperty(ftl::protocol::StreamProperty opt, std::any value) {
    switch (opt) {
    case StreamProperty::kBitrate       :
    case StreamProperty::kMaxBitrate    :  bitrate_ = std::any_cast<int>(value); break;
    case StreamProperty::kPaused        :  paused_ = std::any_cast<bool>(value); break;
    case StreamProperty::kName          :  name_ = std::any_cast<std::string>(value); break;
    case StreamProperty::kRequestSize   :  frames_to_request_ = std::any_cast<int>(value); break;
    case StreamProperty::kBuffering     :  setBuffering(std::any_cast<float>(value)); break;
    case StreamProperty::kAutoBufferAdjust: setAutoBufferAdjust(std::any_cast<bool>(value)); break;
    case StreamProperty::kObservers     :
    case StreamProperty::kBytesSent     :
    case StreamProperty::kBytesReceived :
    case StreamProperty::kLatency       :
    case StreamProperty::kFrameRate     :
    case StreamProperty::kUnderunCount  :
    case StreamProperty::kDropCount     :
    case StreamProperty::kURI           :  throw FTL_Error("Readonly property");
    default                             :  throw FTL_Error("Unsupported property");
    }
}

std::any Net::getProperty(ftl::protocol::StreamProperty opt) {
    switch (opt) {
    case StreamProperty::kBitrate       :
    case StreamProperty::kMaxBitrate    :  return bitrate_;
    case StreamProperty::kObservers     :  return clients_local_.size();
    case StreamProperty::kURI           :  return base_uri_;
    case StreamProperty::kPaused        :  return paused_;
    case StreamProperty::kBytesSent     :  return 0;
    case StreamProperty::kBytesReceived :  return int64_t(bytes_received_);
    case StreamProperty::kFrameRate     :  return 0;
    case StreamProperty::kLatency       :  return 0;
    case StreamProperty::kName          :  return name_;
    case StreamProperty::kBuffering     :  return static_cast<float>(buffering_)/1000.0f;
    case StreamProperty::kAutoBufferAdjust: return buffering_auto_;
    case StreamProperty::kRequestSize   :  return frames_to_request_;
    case StreamProperty::kUnderunCount  :  return static_cast<int>(underruns_);
    case StreamProperty::kDropCount     :  return static_cast<int>(-1);
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
    case StreamProperty::kRequestSize   :
    case StreamProperty::kBuffering     :
    case StreamProperty::kUnderunCount  :
    // case StreamProperty::kDropCount     : // Netstream can't drop, but api has to be refactored to indicate when
                                             // stream is at capacity (more data is being passed than can be actually
                                             // sent) and any drops must happen at the source.
    case StreamProperty::kAutoBufferAdjust :
    case StreamProperty::kURI           :  return true;
    default                             :  return false;
    }
}

ftl::protocol::StreamType Net::type() const {
    return ftl::protocol::StreamType::kLive;
}
