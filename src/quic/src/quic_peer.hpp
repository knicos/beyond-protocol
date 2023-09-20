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

class QuicPeerStream;

class QuicPeer : public ftl::net::PeerBase, public IMsQuicConnectionHandler
{
    friend class QuicPeerStream;

public:
    explicit QuicPeer(MsQuicContext* msquic, ftl::net::Universe*, ftl::net::Dispatcher* d = nullptr);
    virtual ~QuicPeer();

    bool isValid() const override { return true; }

    void start() override;

    void close(bool reconnect) override;

    int pending_bytes() { return pending_bytes_; }

    int32_t AvailableBandwidth() override;

    /** Open default stream and send handshake */
    void initiate_handshake();

    void set_connection(MsQuicConnectionPtr conn);

    void process_message(msgpack::object_handle& obj) { process_message_(obj); };

protected:
    // acquire msgpack buffer for send
    msgpack_buffer_t get_buffer_() override;

    // send buffer to network. must call return_buffer_ once send is complete. if throws, caller must call return_buffer_.
    int send_buffer_(const std::string& name, msgpack_buffer_t&& buffer, ftl::net::SendFlags flags) override;

    // IMsQuicConnectionHandler
    void OnConnect(MsQuicConnection* Connection) override;
    void OnDisconnect(MsQuicConnection* Connection) override;
    void OnStreamCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicStream> Stream) override;
    //void OnDatagramCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicDatagram> Stream) override;

private:
    MsQuicContext* msquic_;
    ftl::net::Universe* net_;

    DECLARE_MUTEX(peer_mtx_);
    MsQuicConnectionPtr connection_;
    std::unique_ptr<QuicPeerStream> stream_;

    // QuicPeerStream

    void OnStreamShutdown(QuicPeerStream* stream);
    // pending bytes (send) for all streams of this connection
    std::atomic_int pending_bytes_ = 0;
};

class QuicPeerStream : public IMsQuicStreamHandler {
public:

    QuicPeerStream(QuicPeer* peer, const std::string& name);
    virtual ~QuicPeerStream();

    void set_stream(MsQuicStreamPtr stream);

    msgpack_buffer_t get_buffer();
    int32_t send_buffer(msgpack_buffer_t&&);

    int pending_bytes() { return pending_bytes_; }
    int pending_sends() { return pending_sends_; }

    void close();

    void reset();
    
protected:
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

    QuicPeer* peer_;
    MsQuicStreamPtr stream_;
    std::string name_;

    DECLARE_MUTEX(recv_mtx_);
    void ProcessRecv();
    msgpack::unpacker recv_buffer_;
    bool recv_busy_ = false;

    struct SendEvent {
        SendEvent(msgpack_buffer_t buffer);
        msgpack_buffer_t buffer;
        
        bool pending;
        bool complete;

        int t;

        std::array<QUIC_BUFFER, 1> quic_vector;
        PROFILER_ASYNC_ZONE_CTX(profiler_ctx);
    };

    DECLARE_MUTEX(send_mtx_); // for send_buffers_free_ and send_queue_
    std::vector<msgpack_buffer_t> send_buffers_free_;
    std::deque<SendEvent> send_queue_;
    std::condition_variable_any send_cv_;
    int send_buffer_count_ = 0;

    // Send limits. Send methods will block if limits are exceeded (with warning message).

    // Maximum number of send buffers available. If no spare buffers are available, prints a message (likely indicates
    // that the other end can not receive fast enough; the producer should check how much data is pending and reduce
    // the amount of data sent). A large number of very small writes can also trigger this limit (TODO: coalesce sends).
    static const int max_send_buffers_ = 128;

    // Actual required buffer size depends on available bandwidth and network latency. This limit is to 
    // prevent excess memory consumption but may affect network if value is set very low.
    static const int max_pending_bytes_ = 1024*1024*16; // 16MiB

    // Default size of a single send buffer.
    static const int send_buffer_default_size_ = 1024*12; // 12KiB

    static const int recv_buffer_default_size_ = 1024*1024*8; // 8MiB
    
    static const int recv_buffer_reserve_size_ = 1024*512; // 

    std::atomic_int pending_sends_ = 0;
    std::atomic_int pending_bytes_ = 0;

    #ifdef ENABLE_PROFILER
    struct struct_profiler_ids_ {
        const char* stream = nullptr;
        const char* plt_pending_buffers = nullptr;
        const char* plt_pending_bytes = nullptr;
    } profiler_id_;

    void statistics();

    const char* profiler_name_ = nullptr;
    #endif
};

}
