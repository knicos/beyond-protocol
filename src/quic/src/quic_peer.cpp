#include "quic_peer.hpp"

#include "quic.hpp"

#include "../../universe.hpp"

using namespace beyond_impl;

using ftl::protocol::NodeStatus;

////////////////////////////////////////////////////////////////////////////////

QuicPeerStream::SendEvent::SendEvent(msgpack_buffer_t buffer_in) :
    buffer(std::move(buffer_in)), pending(true), complete(false), t(0)
{
    quic_vector[0].Buffer = (uint8_t*) buffer.data();
    quic_vector[0].Length = buffer.size();
}

////////////////////////////////////////////////////////////////////////////////

QuicPeer::QuicPeer(
    MsQuicContext* ctx,
    ftl::net::Universe* net,
    ftl::net::Dispatcher* disp) :

    ftl::net::PeerBase(ftl::URI(), net, disp),
    msquic_(ctx),
    net_(net)
{
    bind("__handshake__", [this](uint64_t magic, uint32_t version, const ftl::UUIDMSGPACK &pid) {
        process_handshake_(magic, version, pid);
    });

    stream_ = std::make_unique<QuicPeerStream>(this, "default");
    status_ = ftl::protocol::NodeStatus::kConnecting;
}

QuicPeer::~QuicPeer()
{

}

void QuicPeer::initiate_handshake() 
{
    UNIQUE_LOCK_N(lk, peer_mtx_);
    stream_->set_stream(connection_->OpenStream());
    send_handshake_();
    
    LOG(INFO) << "[QUIC] New stream opened (requested by local)";
}

void QuicPeer::start() 
{

}

void QuicPeer::close(bool reconnect)
{
    if (status() == NodeStatus::kConnected)
    {
        LOG(INFO) << "[QUIC] Disconnect";
        send("__disconnect__");
    }

    if (connection_ && connection_->IsOpen())
    {
        stream_->close();
        // MsQuic should close all resources associated with the connection. This is currently required by QuicUniverse
        connection_->Close().wait();
    }

    if (reconnect)
    {
        LOG(ERROR) << "[QUIC] Reconnect requested, not implemented TODO";
    }
}

void QuicPeer::set_connection(MsQuicConnectionPtr conn)
{
    CHECK(conn);
    LOG_IF(WARNING, connection_.get() != nullptr) 
        << "QuicPeer: Connection already set, this will reset all streams (BUG)";
    
    connection_ = std::move(conn);
}

ftl::net::PeerBase::msgpack_buffer_t QuicPeer::get_buffer_()
{
    CHECK(stream_);
    return stream_->get_buffer();
}

int QuicPeer::send_buffer_(const std::string& name, msgpack_buffer_t&& buffer, ftl::net::SendFlags flags)
{
    CHECK(stream_);
    return stream_->send_buffer(std::move(buffer));
}

void QuicPeer::OnConnect(MsQuicConnection* Connection)
{
    // nothing to do; send_handshake() opens stream
}

void QuicPeer::OnDisconnect(MsQuicConnection* Connection)
{
    status_ = ftl::protocol::NodeStatus::kDisconnected;
    net_->notifyDisconnect_(this);

    LOG(INFO) << "[QUIC] Connection closed";
}

void QuicPeer::OnStreamCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicStream> StreamIn) 
{
    UNIQUE_LOCK_N(lk, peer_mtx_);
    
    stream_->set_stream(std::move(StreamIn));
    LOG(INFO) << "[QUIC] New stream opened (requested by remote)";
}

void QuicPeer::OnStreamShutdown(QuicPeerStream* Stream)
{
    UNIQUE_LOCK_N(lk, peer_mtx_);
}

/*void QuicPeer::OnDatagramCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicDatagram> Stream)
{
    LOG(ERROR) << "QuicPeer: Datagram streams not supported";
}*/

int32_t QuicPeer::AvailableBandwidth()
{
    if (!connection_) { return 0; }
    return 0;
}

/// Stream /////////////////////////////////////////////////////////////////////

static std::atomic_int profiler_name_ctr_ = 0;

QuicPeerStream::QuicPeerStream(QuicPeer* peer, const std::string& name) :
    peer_(peer), name_(name)
{
    CHECK(peer_);

    recv_buffer_.reserve_buffer(recv_buffer_default_size_);
    
    #ifdef ENABLE_PROFILER
    profiler_name_ = PROFILER_RUNTIME_PERSISTENT_NAME("QuicStream[" + std::to_string(profiler_name_ctr_++) + "]");
    profiler_id_.plt_pending_buffers =  PROFILER_RUNTIME_PERSISTENT_NAME(peer_->getURI() + ": " + name_ + " pending buffers");
    profiler_id_.plt_pending_bytes =  PROFILER_RUNTIME_PERSISTENT_NAME(peer_->getURI() + ":" + name_ + " pending bytes");
    TracyPlotConfig(profiler_id_.plt_pending_buffers, tracy::PlotFormatType::Percentage, false, true, tracy::Color::Red1);
    TracyPlotConfig(profiler_id_.plt_pending_bytes, tracy::PlotFormatType::Memory, false, true, tracy::Color::Red1);
    #endif
}

QuicPeerStream::~QuicPeerStream()
{
    close();
}

void QuicPeerStream::set_stream(MsQuicStreamPtr stream)
{
    if (stream_)
    {
        LOG(WARNING) << "[QUIC] Quic stream replaced (NOT TESTED)";
        stream_->EnableRecv(false);
        UNIQUE_LOCK_N(lk, recv_mtx_);
        // TODO: Implement a better method to flush recv queue
        while(recv_busy_) { lk.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); lk.lock(); }
    }
    stream_ = std::move(stream);
    stream_->SetStreamHandler(this);
    stream_->EnableRecv();
}


void QuicPeerStream::close()
{
    std::unique_lock<MUTEX_T> lk_send(send_mtx_, std::defer_lock);
    std::unique_lock<MUTEX_T> lk_recv(recv_mtx_, std::defer_lock);
    std::lock(lk_send, lk_recv);

    if (stream_ && stream_->IsOpen())
    {
        auto future = stream_->Close();
        lk_send.unlock();
        lk_recv.unlock();

        future.wait();
    }
}

void QuicPeerStream::OnShutdown(MsQuicStream* stream)
{
    LOG(WARNING) << "TODO: Event QuicStreamShutdown";
}

void QuicPeerStream::OnShutdownComplete(MsQuicStream* stream)
{
    std::unique_lock<MUTEX_T> lk_send(send_mtx_, std::defer_lock);
    std::unique_lock<MUTEX_T> lk_recv(recv_mtx_, std::defer_lock);
    std::lock(lk_send, lk_recv);
    
    // MsQuic releases stream instanca after this callback. Any use of it later is a bug. 
    stream_ = nullptr;
    peer_->OnStreamShutdown(this);
}

#ifdef ENABLE_PROFILER
void QuicPeerStream::statistics()
{
    TracyPlot(profiler_id_.plt_pending_bytes, float(pending_bytes_));
    TracyPlot(profiler_id_.plt_pending_buffers, float(pending_sends_));
}
#endif

// Buffer/Queue ////////////////////////////////////////////////////////////////

msgpack_buffer_t QuicPeerStream::get_buffer()
{
    UNIQUE_LOCK_N(lk, send_mtx_);

    // return existing buffer if available
    if (send_buffers_free_.size() > 0)
    {
        auto buffer = std::move(send_buffers_free_.back());
        send_buffers_free_.pop_back();
        return buffer;
    }

    // block if already at maximum number of buffers
    while (send_buffer_count_ >= max_send_buffers_)
    {
        LOG(WARNING)
            << "[QUIC] No free send buffers available, "
            << pending_sends_ << " writes pending ("
            << pending_bytes_ / 1024 << " KiB). Network performance degraded.";

        send_cv_.wait(lk);

        if (send_buffers_free_.size() > 0)
        {
            auto buffer = std::move(send_buffers_free_.back());
            send_buffers_free_.pop_back();
            return buffer;
        }
    }
    
    // create a new buffer
    send_buffer_count_++;
    return msgpack_buffer_t(send_buffer_default_size_);
}

void QuicPeerStream::release_buffer_(msgpack_buffer_t&& buffer)
{
    buffer.clear();
    send_buffers_free_.push_back(std::move(buffer));
}

void QuicPeerStream::reset()
{
    UNIQUE_LOCK_N(lk, send_mtx_);
    discard_queued_sends_();
}

void QuicPeerStream::discard_queued_sends_()
{
    for (auto& send : send_queue_)
    {
        if (send.pending)
        {
            pending_bytes_ -= send.buffer.size();
            pending_sends_--;
            release_buffer_(std::move(send.buffer));
        }
    }
}

// SEND ////////////////////////////////////////////////////////////////////////

void QuicPeerStream::flush_send_queue_()
{
    // Tries to push all previously queued (but not sent) buffers to MsQuic

    for (auto itr = send_queue_.rbegin(); (itr != send_queue_.rend() && itr->pending); itr++)
    {
        if (!stream_->Write({itr->quic_vector.data(), itr->quic_vector.size()}, &(*itr)))
        {
            return;
        }
        itr->pending = false;
    }
}

int32_t QuicPeerStream::send_buffer(msgpack_buffer_t&& buffer)
{
    UNIQUE_LOCK_N(lk, send_mtx_);
    // probably doesn't work due to concurrency
    // PROFILER_ASYNC_ZONE_SCOPE("QuicSend");
    // PROFILER_ASYNC_ZONE_BEGIN(profiler_name_, profiler_ctx_local);

    if (!stream_ || !stream_->IsOpen())
    {
        release_buffer_(std::move(buffer));
        LOG_IF(ERROR, !stream_) << "[QUIC] Write to closed stream, discarded";
        return -1;
    }

    if (pending_bytes_ > max_pending_bytes_)
    {
        LOG(WARNING)
            << "[QUIC] Send queue size exceeded " << (pending_bytes_/1024) << " KiB of "
            << (max_pending_bytes_/1024) << " KiB. Network performance degraded.";
        
        while (pending_bytes_ > max_pending_bytes_) { send_cv_.wait(lk); }
    }

    // PROFILER_ASYNC_ZONE_CTX_ASSIGN(event.profiler_ctx, profiler_ctx_local);

    pending_sends_++;
    pending_bytes_ += buffer.size();
    peer_->pending_bytes_ += buffer.size();

    if (!stream_)
    {
        // this really shouldn't be supported
        LOG(WARNING) << "[QUIC] Attempting to write before stream opened, queued for later (BUG)";
        send_queue_.emplace_back(std::move(buffer));
        return -1;
    }
    else
    {
        flush_send_queue_();

        auto& event = send_queue_.emplace_back(std::move(buffer));
        event.pending = !stream_->Write({event.quic_vector.data(), event.quic_vector.size()}, &event);
        if (event.pending)
        {
            LOG(WARNING) << "[QUIC] Write failed, is stream closed?";
        }
    }

    return 1;
}

void QuicPeerStream::OnWriteComplete(MsQuicStream* stream, void* Context, bool Cancelled)
{
    // PROFILER_ASYNC_ZONE_SCOPE("QuicSend");
    SendEvent* event = static_cast<SendEvent*>(Context);
    
    // PROFILER_ASYNC_ZONE_CTX(profiler_ctx_local);
    // PROFILER_ASYNC_ZONE_CTX_ASSIGN(profiler_ctx_local, event->profiler_ctx);
    
    LOG_IF(WARNING, Cancelled) << "[QUIC] Send was cancelled, transmission not complete";

    pending_sends_--;
    pending_bytes_ -= event->buffer.size();
    peer_->pending_bytes_ -= event->buffer.size();
    
    UNIQUE_LOCK_N(lk, send_mtx_);

    event->complete = true;
    release_buffer_(std::move(event->buffer));

    // Clear the front of the queue. Sends/completions should always happen in
    // FIFO, except if a send was cancelled it could also happen in middle of
    // the queue, in which case it can not be removed until all the writes in
    // front of it are complete (buffers no longer required), std::deque
    // pointers remain stable when insertions/deletions happen in the beginning
    // or the end of the dequeue.
    if (event == &send_queue_.front())
    {
        while((send_queue_.size() > 0) && send_queue_.front().complete)
        {
            send_queue_.pop_front();
        }
    }
    else
    {
        LOG(WARNING) << "[QUIC] out of order send";
    }

    // PROFILER_ASYNC_ZONE_END(profiler_ctx_local);
    send_cv_.notify_all();
}

// RECEIVE /////////////////////////////////////////////////////////////////////

void QuicPeerStream::OnData(MsQuicStream* stream, nonstd::span<const QUIC_BUFFER> data)
{
    size_t size_consumed = 0;
    size_t size_total = 0;
    
    for (auto& buffer_in : data)
    {
        if ((buffer_in.Length + size_consumed) > recv_buffer_.buffer_capacity())
        {
            // reserve_buffer() not thread safe (may allocate)
            UNIQUE_LOCK_N(lk, recv_mtx_);
            recv_buffer_.buffer_consumed(size_consumed);
            size_total += size_consumed;
            size_consumed = 0;

            size_t size_reserve = std::max<size_t>(recv_buffer_reserve_size_, buffer_in.Length);
            // msgpack::unpacker.reserve_buffer() rewinds the internal buffer if possible, otherwise allocates
            // FIXME: this might grow without upper limit
            recv_buffer_.reserve_buffer(size_reserve);
        }

        memcpy(recv_buffer_.buffer() + size_consumed, buffer_in.Buffer, buffer_in.Length);
        size_consumed += buffer_in.Length;
    }

    {
        UNIQUE_LOCK_N(lk, recv_mtx_);
        recv_buffer_.buffer_consumed(size_consumed);

        if (!recv_busy_)
        {
            recv_busy_ = true;
            ftl::pool.push([this](int){ ProcessRecv(); });
        }
    }
    
    stream_->Consume(size_consumed + size_total);
    
    size_t sz = 0;
    for (auto& buffer_in : data) { sz += buffer_in.Length; }
    CHECK((size_consumed + size_total) == sz);
}

void QuicPeerStream::ProcessRecv()
{
    UNIQUE_LOCK_N(lk, recv_mtx_);
    msgpack::object_handle obj;

    while (recv_buffer_.next(obj))
    {
        lk.unlock();
        peer_->process_message(obj);
        lk.lock();
    }

    recv_busy_ = false;
}
