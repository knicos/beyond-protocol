#pragma once
#include "../../peer.hpp"

#include <ftl/profiler.hpp>

#include "quic.hpp"
#include "msquic/connection.hpp"

#include <map>
#include <deque>
#include <array>

namespace beyond_impl 
{
using msgpack_buffer_t = ftl::net::PeerBase::msgpack_buffer_t;

class QuicPeerStream final : public IMsQuicStreamHandler, public ftl::net::PeerBase {
public:
    QuicPeerStream(MsQuicConnection* connection, MsQuicStreamPtr stream, ftl::net::Universe*, ftl::net::Dispatcher* d = nullptr);
    virtual ~QuicPeerStream();

    void set_stream(MsQuicStreamPtr stream);

    void set_type(ftl::protocol::NodeType t) { node_type_ = t; } 

    ftl::protocol::NodeType getType() const override { return node_type_; }

    int pending_bytes() { return pending_bytes_; }
    int pending_sends() { return pending_sends_; }

    bool isValid() const override { return stream_.get() != nullptr; }

    void start() override;

    void close(bool reconnect=false) override;
    
    int32_t getRtt() const override;

protected:
    void periodic_() override;

    void reset();

    msgpack_buffer_t get_buffer_() override;
    int send_buffer_(const std::string& name, msgpack_buffer_t&& buffer, ftl::net::SendFlags flags) override;

    // IMsQuicStreamHandler
    void OnData(MsQuicStream* stream, nonstd::span<const QUIC_BUFFER> data) override;

    void OnShutdown(MsQuicStream* stream) override;
    void OnShutdownComplete(MsQuicStream* stream) override;

    void OnWriteComplete(MsQuicStream* stream, void* Context, bool Cancelled) override;

private:
    // release buffer back to free list
    void release_buffer_(msgpack_buffer_t&&);

    // try to flush all pending sends to network
    void flush_send_queue_();

    // discard all queued sends (not yet passed to network)
    void discard_queued_sends_();

    // TODO: node should tell what type it is (if this code is used)
    ftl::protocol::NodeType node_type_ = ftl::protocol::NodeType::kNode;

    MsQuicConnection* connection_;
    MsQuicStreamPtr stream_;
    std::string name_;
    const bool ws_frame_;

    void notify_recv_and_unlock_(UNIQUE_LOCK_MUTEX_T& lk);

    DECLARE_MUTEX(recv_mtx_);
    std::condition_variable_any recv_cv_;
    msgpack::unpacker recv_buffer_;
    std::vector<msgpack::object_handle> recv_queue_;
    void ProcessRecv();
    bool recv_busy_ = false;
    bool recv_waiting_ = false;

    struct SendEvent {
        SendEvent(msgpack_buffer_t buffer);
        msgpack_buffer_t buffer;
        
        bool pending;
        bool complete;

        int t;

        uint8_t n_buffers;
        std::array<QUIC_BUFFER, 2> quic_vector;
        PROFILER_ASYNC_ZONE_CTX(profiler_ctx);
    };

    DECLARE_MUTEX(send_mtx_); // for send_buffers_free_ and send_queue_
    std::vector<msgpack_buffer_t> send_buffers_free_;
    std::deque<SendEvent> send_queue_;
    std::condition_variable_any send_cv_;
    int send_buffer_count_ = 0;

    std::deque<std::array<char, 14>> ws_headers_;
    SendEvent& queue_send_(msgpack_buffer_t);
    void send_(SendEvent&, ftl::net::SendFlags flags=ftl::net::SendFlags::NONE);
    void clear_completed_();

    // As a workaround for webservice, WebSocket framing is applied here. A simple quic<->tcp relay can be then used
    // to pass frames directly to webservice. The relay only has to perform the websocket handshake. Otherwise the
    // relay has to process msgpack messages and frame them once a complete message is received, increasing complexity
    // significantly there. Clients can still communicate directly with each other when both sides apply websocket
    // framing in this fashion.
    int ws_payload_recvd_ = 0;
    int ws_payload_remaining_ = 0;
    int ws_partial_header_recvd_ = 0;
    bool ws_mask_ = false;
    std::array<unsigned char, 4> ws_mask_key_;
    std::array<uint8_t, 14> ws_header_;
    size_t ws_recv_(QUIC_BUFFER buffer_in, size_t& size_consumed);

    // Send limits. Send methods will block if limits are exceeded (with warning message).

    // Maximum number of send buffers available. If no spare buffers are available, prints a message (likely indicates
    // that the other end can not receive fast enough; the producer should check how much data is pending and reduce
    // the amount of data sent). A large number of very small writes can also trigger this limit (TODO: coalesce sends).
    static const int max_send_buffers_ = 512; //192; TEST 05/10/23, previous default 192

    // Actual required buffer size depends on available bandwidth and network latency. This limit is to 
    // prevent excess memory consumption but may affect network if value is set very low.
    static const int max_pending_bytes_ = 1024*1024*24; // 16MiB

    // Default size of a single send buffer.
    static const int send_buffer_default_size_ = 1024*24; // 24KiB

    static const int recv_buffer_default_size_ = 1024*1024*8; // 8MiB
    
    static const int recv_buffer_reserve_size_ = 1024*512; // 

    std::atomic_int pending_sends_ = 0;
    std::atomic_int pending_bytes_ = 0;

    #ifdef ENABLE_PROFILER
    struct struct_profiler_ids_ {
        const char* stream = nullptr;
        const char* plt_pending_buffers = nullptr;
        const char* plt_pending_bytes = nullptr;
        const char* plt_recv_size = nullptr;
        const char* plt_recv_size_obj = nullptr;
    } profiler_id_;

    void statistics();

    const char* profiler_name_ = nullptr;
    #endif
};

}
