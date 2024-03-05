#include "quic_peer.hpp"

#include "quic.hpp"

#include "../../universe.hpp"

#include "quic_websocket.cpp"

using namespace beyond_impl;

using ftl::protocol::NodeStatus;

////////////////////////////////////////////////////////////////////////////////

QuicPeerStream::SendEvent::SendEvent(msgpack_buffer_t buffer_in) :
    buffer(std::move(buffer_in)), pending(true), complete(false), t(0), n_buffers(1)
{
    quic_vector[0].Buffer = (uint8_t*) buffer.data();
    quic_vector[0].Length = buffer.size();
}

/// Stream /////////////////////////////////////////////////////////////////////

static std::atomic_int profiler_name_ctr_ = 0;

QuicPeerStream::QuicPeerStream(MsQuicConnection* connection, MsQuicStreamPtr stream, ftl::net::Universe* net, ftl::net::Dispatcher* disp) :
    ftl::net::PeerBase(ftl::URI(), net, disp),
    connection_(connection), stream_(std::move(stream)), ws_frame_(true)
{
    // TODO: remove connection_ (can't use with proxy)
    CHECK(stream_.get());

    bind("__handshake__", [this](uint64_t magic, uint32_t version, const ftl::UUIDMSGPACK &pid) {
        process_handshake_(magic, version, pid);
    });

    stream_->SetStreamHandler(this);
    status_ = ftl::protocol::NodeStatus::kConnecting;

    recv_buffer_.reserve_buffer(recv_buffer_default_size_);
    
    
    #ifdef ENABLE_PROFILER
    profiler_name_ = PROFILER_RUNTIME_PERSISTENT_NAME("QuicStream[" + std::to_string(profiler_name_ctr_++) + "]");
    profiler_id_.plt_pending_buffers =  PROFILER_RUNTIME_PERSISTENT_NAME(getURI() + ": " + name_ + " pending buffers");
    profiler_id_.plt_pending_bytes =  PROFILER_RUNTIME_PERSISTENT_NAME(getURI() + ":" + name_ + " pending bytes");
    profiler_id_.plt_recv_size =  PROFILER_RUNTIME_PERSISTENT_NAME(getURI() + ": " + name_ + " recv size");
    profiler_id_.plt_recv_size_obj =  PROFILER_RUNTIME_PERSISTENT_NAME(getURI() + ":" + name_ + " recv size (msgpack objects)");

    TracyPlotConfig(profiler_id_.plt_pending_buffers, tracy::PlotFormatType::Number, true, true, tracy::Color::Red1);
    TracyPlotConfig(profiler_id_.plt_pending_bytes, tracy::PlotFormatType::Memory, true, true, tracy::Color::Red1);
    TracyPlotConfig(profiler_id_.plt_recv_size, tracy::PlotFormatType::Memory, true, true, tracy::Color::Green1);
    TracyPlotConfig(profiler_id_.plt_recv_size_obj, tracy::PlotFormatType::Number, true, true, tracy::Color::Green1);
    #endif
}

QuicPeerStream::~QuicPeerStream()
{
    close();
}


int32_t QuicPeerStream::getRtt() const {
    return connection_->GetRtt()/1000;
}

void QuicPeerStream::periodic_() {
    connection_->RefreshStatistics();
}

void QuicPeerStream::start()
{
    stream_->EnableRecv();
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
    is_valid_ = !!stream_;
}


void QuicPeerStream::close(bool reconnect)
{
    UNIQUE_LOCK_T(send_mtx_) lk_send(send_mtx_, std::defer_lock);
    UNIQUE_LOCK_T(recv_mtx_) lk_recv(recv_mtx_, std::defer_lock);
    std::lock(lk_send, lk_recv);
    recv_queue_.clear();

    if (recv_waiting_)
    {
        CHECK(recv_busy_);

        // In case ProcessRecv is waiting, notify here and wait the worker to exit
        recv_waiting_ = false;
    }

    if (recv_busy_) {
        recv_cv_.notify_one();
        recv_cv_.wait(lk_recv, [&]() { return !recv_busy_; });
    }
    //LOG(INFO) << "CLOSE: " << this;

    if (stream_)
    {
        if (stream_->IsOpen()) { stream_->Close(); }
        lk_send.unlock();

        recv_cv_.wait(lk_recv, [&]() { return !stream_; });
        lk_recv.unlock();
    }
}

void QuicPeerStream::OnShutdown(MsQuicStream* stream)
{
    LOG(WARNING) << "TODO: Event QuicStreamShutdown";
}

void QuicPeerStream::OnShutdownComplete(MsQuicStream* stream)
{
    UNIQUE_LOCK_T(send_mtx_) lk_send(send_mtx_, std::defer_lock);
    UNIQUE_LOCK_T(recv_mtx_) lk_recv(recv_mtx_, std::defer_lock);
    std::lock(lk_send, lk_recv);
    recv_cv_.notify_all();
    
    // MsQuic releases stream instance after this callback. Any use of it later is a bug. 
    is_valid_ = false;
    stream_ = nullptr;
}

#ifdef ENABLE_PROFILER
void QuicPeerStream::statistics()
{
    TracyPlot(profiler_id_.plt_pending_bytes, float(pending_bytes_));
    TracyPlot(profiler_id_.plt_pending_buffers, float(pending_sends_));
}
#endif

// Buffer/Queue ////////////////////////////////////////////////////////////////

msgpack_buffer_t QuicPeerStream::get_buffer_()
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

QuicPeerStream::SendEvent& QuicPeerStream::queue_send_(msgpack_buffer_t buffer)
{
    auto buffer_ptr = buffer.data();
    auto buffer_size = buffer.size();
    auto& event = send_queue_.emplace_back(std::move(buffer));

    if (ws_frame_)
    {
        auto key = GenerateMaskingKey();
        auto& header_buffer = ws_headers_.emplace_back();
        int32_t header_size = WsWriteHeader(BINARY_FRAME, true, key, buffer_size, (uint8_t*) header_buffer.data(), header_buffer.size());
        
        CHECK(header_size > 0);
        Mask(buffer_ptr, buffer_size, key);
        event.quic_vector[0] = { (uint32_t) header_size, (uint8_t*) header_buffer.data() };
        event.quic_vector[1] = { (uint32_t) buffer_size, (uint8_t*) buffer_ptr };
        event.n_buffers = 2;
    }

    return event;
}

void QuicPeerStream::send_(SendEvent& event, ftl::net::SendFlags flags)
{
    bool delay = flags & ftl::net::SendFlags::DELAY;
    event.pending = !stream_->Write({event.quic_vector.data(), event.n_buffers}, &event, delay);
}

void QuicPeerStream::clear_completed_()
{
    while((send_queue_.size() > 0) && send_queue_.front().complete)
    {
        if (ws_frame_)
        {
            auto& event = send_queue_.front();
            CHECK((char*)event.quic_vector[0].Buffer == ws_headers_.front().data());
            ws_headers_.pop_front();
        }
        
        send_queue_.pop_front();
    }
}

void QuicPeerStream::flush_send_queue_()
{
    // Tries to push all previously queued (but not sent) buffers to MsQuic

    for (auto itr = send_queue_.rbegin(); (itr != send_queue_.rend() && itr->pending); itr++)
    {
        send_(*itr);
        if (!itr->pending) { return; }
        itr->pending = false;
    }
}

int QuicPeerStream::send_buffer_(const std::string& name, msgpack_buffer_t&& buffer, ftl::net::SendFlags flags) {

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
    //peer_->pending_bytes_ += buffer.size();

    if (!stream_)
    {
        // This really shouldn't be allowed even if msquic allows writes to streams immediately
        LOG(WARNING) << "[QUIC] Attempting to write before stream opened, queued for later (BUG)";
        queue_send_(std::move(buffer));
        return -1;
    }
    else
    {
        flush_send_queue_();

        auto& event = queue_send_(std::move(buffer));
        send_(event);

        if (event.pending)
        {
            LOG(WARNING) << "[QUIC] Write failed, is stream closed?";
        }
    }

    #ifdef ENABLE_PROFILER
    statistics();
    #endif

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
    //peer_->pending_bytes_ -= event->buffer.size();
    
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
        clear_completed_();
    }
    else
    {
        LOG(WARNING) << "[QUIC] out of order send";
    }

    // PROFILER_ASYNC_ZONE_END(profiler_ctx_local);
    send_cv_.notify_all();
}

// RECEIVE /////////////////////////////////////////////////////////////////////

size_t QuicPeerStream::ws_recv_(QUIC_BUFFER buffer_in, size_t& size_consumed)
{
    // Amount of bytes for websocket header consumed
    size_t size_ws = 0;
    
    while (buffer_in.Length > 0)
    {
        // Complete frame has not yet been received
        if (ws_payload_remaining_ > 0)
        {
            auto recv_size = std::min<int32_t>(buffer_in.Length, ws_payload_remaining_);
            auto ws_buffer = nonstd::span<char>((char*)buffer_in.Buffer, recv_size);
            
            if (ws_mask_) { Mask(ws_buffer.data(), ws_buffer.size(), ws_mask_key_, ws_payload_recvd_); }
            ws_payload_recvd_ += recv_size;
            ws_payload_remaining_ -= recv_size;
            
            memcpy(recv_buffer_.buffer() + size_consumed, ws_buffer.data(), ws_buffer.size());
            size_consumed += ws_buffer.size();

            buffer_in.Length -= recv_size;
            buffer_in.Buffer += recv_size;
            
            // Process any remaining buffer
            continue;
        }

        // No previous frames or previous frame is complete, this buffer must contain header
        WebSocketHeader header;
        WsParseHeaderStatus status = INVALID;

        if (ws_partial_header_recvd_ > 0)
        {
            // Read remaining header
            CHECK(ws_partial_header_recvd_ < (int)ws_header_.size());
            size_t cpy_size = std::min<size_t>(ws_header_.size() - ws_partial_header_recvd_, buffer_in.Length);
            memcpy(ws_header_.data() + ws_partial_header_recvd_, buffer_in.Buffer, cpy_size);
            status = WsParseHeader(ws_header_.data(), ws_partial_header_recvd_ + cpy_size, header);

            // FIXME: In principle it is possible that the header still is not complete,
            //         but rest of the code has to be checked for correctness (unit tests)
            CHECK(status != NOT_ENOUGH_DATA);
        }
        else
        {
            status = WsParseHeader(buffer_in.Buffer, buffer_in.Length, header);
        }

        if (status == INVALID)
        {
            LOG(ERROR) << "[Quic/WebSocket] Protocol error, invalid header. Closing connection.";
            close();
        }
        else if (status == NOT_ENOUGH_DATA)
        {
            CHECK(buffer_in.Length < ws_header_.size() && ws_partial_header_recvd_ == 0);
            size_t cpy_size = std::min<size_t>(ws_header_.size() - ws_partial_header_recvd_, buffer_in.Length);
            memcpy(ws_header_.data() + ws_partial_header_recvd_, buffer_in.Buffer, cpy_size);
            ws_partial_header_recvd_ = std::min<uint32_t>(ws_header_.size(), ws_partial_header_recvd_ + buffer_in.Length);

            size_ws += cpy_size;
            buffer_in.Length -= cpy_size;
            buffer_in.Buffer += cpy_size;
        }
        else if (status == OK)
        {
            if (header.OpCode == OpCodeType::CLOSE)
            {
                LOG(WARNING) << "[Quic/WebSocket] Received close control frame. Closing connection.";
                close();
            }
            
            auto offset = header.HeaderSize - ws_partial_header_recvd_;
            size_ws += offset;
            buffer_in.Length -= offset;
            buffer_in.Buffer += offset;

            ws_mask_ = header.Mask;
            ws_mask_key_ = header.MaskingKey;

            ws_partial_header_recvd_ = 0;
            ws_payload_recvd_ = 0;
            ws_payload_remaining_ = header.PayloadLength;

            if (header.OpCode != OpCodeType::BINARY_FRAME)
            {
                LOG(WARNING) << "[Quic/WebSocket] Received non-binary frame "
                             << "(OpCode: " << header.OpCode << ", size (header): " <<  header.HeaderSize << ", size (payload): "
                             << header.PayloadLength << "). Frame ignored.";

                if (header.OpCode == OpCodeType::TEXT_FRAME)
                {
                    LOG(WARNING) << "[Quic/WebSocket] Text Frame: " << std::string((char*)buffer_in.Buffer + header.HeaderSize, header.PayloadLength);
                }
                
                size_ws += ws_payload_remaining_;
                buffer_in.Length -= ws_payload_remaining_;
                buffer_in.Buffer += ws_payload_remaining_;
                ws_payload_remaining_ = 0;
            }
        }
    }

    return size_ws;
}

void QuicPeerStream::OnData(MsQuicStream* stream, nonstd::span<const QUIC_BUFFER> data)
{
    FTL_PROFILE_SCOPE("OnData");

    size_t size_consumed = 0; // bytes written to current msgpack buffer
    size_t size_total = 0;    // bytes already passed to msgpack
    size_t size_ws = 0;       // number of bytes from websocket headers

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

        if (ws_frame_)
        {
            size_ws += ws_recv_(buffer_in, size_consumed);
        }
        else
        {
            memcpy(recv_buffer_.buffer() + size_consumed, buffer_in.Buffer, buffer_in.Length);
            size_consumed += buffer_in.Length;
        }
    }

    stream_->Consume(size_consumed + size_total + size_ws);
    size_t sz = 0;
    for (auto& buffer_in : data) { sz += buffer_in.Length; }
    CHECK((size_consumed + size_total + size_ws) == sz);

    {
        UNIQUE_LOCK_N(lk, recv_mtx_);
        // Try to parse received buffer (msgpack parsing is fast)
        // 2024/01 Increase in execution time is less than 1 microsecond per call.
        recv_buffer_.buffer_consumed(size_consumed);
        recv_queue_.resize(recv_queue_.size() + 1);
        int offset = recv_queue_.size() - 1;
        int count = 0;
        for(; recv_buffer_.next(recv_queue_[count + offset]); recv_queue_.resize(++count + offset + 1));
        recv_queue_.pop_back(); // Last element always invalid (next() returns false)
        
        #ifdef TRACY_ENABLE
        TracyPlot(profiler_id_.plt_recv_size, double(size_consumed + size_total + size_ws));
        TracyPlot(profiler_id_.plt_recv_size_obj, double(recv_queue_.size()));
        #endif
        notify_recv_and_unlock_(lk);
    }
}

void QuicPeerStream::notify_recv_and_unlock_(UNIQUE_LOCK_MUTEX_T& lk)
{
    bool notify = false;
    if (!recv_busy_)
    {
        recv_busy_ = true;
        lk.unlock();
        // There is at most only one thread working on a this Peer's queue. recv_busy_ is set to false on worker exit.
        ftl::pool.push([this](int){ ProcessRecv(); });
    }
    else 
    {
        notify = recv_waiting_ && (recv_queue_.size() > 0);
        lk.unlock();
    }

    if (notify)
    { 
        // OK outside lock, if resumed due to timeout buffer already available
        recv_cv_.notify_one();
    }

    CHECK(!lk.owns_lock());
}

void QuicPeerStream::ProcessRecv()
{
    const int t_wait_ms = 300; // Time to wait before returning
    std::vector<msgpack::object_handle> objs;
    objs.reserve(4);

    UNIQUE_LOCK_N(lk, recv_mtx_);
    while (true)
    {
        if (recv_queue_.size() == 0)
        {
            auto pred = [&]() { return recv_waiting_ && recv_queue_.size() > 0; };
            // Wait a bit before exit. The idea is to use the same thread for the same connection/stream.
            // TODO: Probably not a good idea to use the shared thread pool for this. 
            recv_waiting_ = true;
            bool has_data = recv_cv_.wait_for(lk, std::chrono::milliseconds(t_wait_ms), pred);
            recv_waiting_ = false;
            if (!has_data)
            {
                // Buffer was not updated or didn't get a complete message
                break;
            }
            else
            {
                continue;
            }
        }

        CHECK(objs.size() == 0);
        std::swap(recv_queue_, objs);

        lk.unlock();
        FTL_PROFILE_SCOPE("ProcessRecv");
        for (auto& obj : objs) { process_message_(obj); }
        objs.clear();
        lk.lock();
    }

    recv_busy_ = false;
    recv_cv_.notify_all();
    lk.unlock();
}
