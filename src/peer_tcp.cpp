/**
 * @file peer.cpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <iostream>
#include <memory>
#include <algorithm>
#include <tuple>
#include <chrono>
#include <vector>
#include <utility>
#include <string>

#include <ftl/lib/loguru.hpp>
#include <ftl/lib/ctpl_stl.hpp>
#include <ftl/counter.hpp>
#include <ftl/profiler.hpp>

#include "common.hpp"

#include <ftl/uri.hpp>
#include <ftl/time.hpp>

#include "peer_tcp.hpp"
#include "protocol/connection.hpp"

using ftl::net::internal::SocketConnection;

#include "universe.hpp"

using std::tuple;
using std::get;
using ftl::net::PeerBase;
using ftl::net::PeerTcp;
using ftl::net::PeerPtr;
using ftl::URI;
using ftl::net::Dispatcher;
using std::chrono::seconds;
using ftl::net::Universe;
using ftl::net::Callback;
using std::vector;
using ftl::protocol::NodeStatus;
using ftl::protocol::NodeType;
using ftl::protocol::Error;

int PeerTcp::_socket() const {
    if (sock_->is_valid()) {
        return sock_->fd();
    } else {
        return INVALID_SOCKET;
    }
}

bool PeerTcp::isConnected() const {
    return sock_->is_valid() && (status_ == NodeStatus::kConnected);
}

bool PeerTcp::isValid() const {
    return sock_ && sock_->is_valid() && ((status_ == NodeStatus::kConnected) || (status_ == NodeStatus::kConnecting));
}

void PeerTcp::_set_socket_options() {
    CHECK(net_);
    CHECK(sock_);

    const size_t desiredSend = net_->getSendBufferSize(sock_->scheme());
    const size_t desiredRecv = net_->getRecvBufferSize(sock_->scheme());

    // error printed by set methods (return value ignored)
    if (desiredSend > 0) {
        sock_->set_send_buffer_size(desiredSend);
    }
    if (desiredRecv > 0) {
        sock_->set_recv_buffer_size(desiredRecv);
    }

    DLOG(INFO) << "send buffer size: " << (sock_->get_send_buffer_size() >> 10) << "KiB, "
            << "recv buffer size: " << (sock_->get_recv_buffer_size() >> 10) << "KiB";
}

void PeerTcp::_bind_rpc() {
    bind("__handshake__", [this](uint64_t magic, uint32_t version, const ftl::UUIDMSGPACK &pid) {
        process_handshake_(magic, version, pid);
    });
}

void init_profiler() {
    // call once if profiler is enabled to configure plots
    #ifdef TRACY_ENABLE
    [[maybe_unused]] static bool init = [](){
        TracyPlotConfig("rx", tracy::PlotFormatType::Memory, false, true, 0xff0000);
        TracyPlotConfig("tx", tracy::PlotFormatType::Memory, false, true, 0xff0000);
        return true;
    }();
    #endif
}

PeerTcp::PeerTcp(std::unique_ptr<internal::SocketConnection> s, Universe* u, Dispatcher* d) :
        PeerBase(ftl::URI(""), u, d),
        outgoing_(false),
        can_reconnect_(false),
        sock_(std::move(s))
    {
    /* Incoming connection constructor */

    CHECK(sock_) << "incoming SocketConnection pointer null";

    status_ = ftl::protocol::NodeStatus::kConnecting;
    _set_socket_options();
    _updateURI();
    _bind_rpc();
    ++net_->peer_instances_;
    init_profiler();
}

PeerTcp::PeerTcp(const ftl::URI& uri, Universe *u, Dispatcher *d) :
        PeerBase(uri, u, d),
        outgoing_(true),
        can_reconnect_(true),
        sock_(nullptr) 
    {
    /* Outgoing connection constructor */
    status_ = ftl::protocol::NodeStatus::kConnecting;
    _bind_rpc();
    _connect();
    ++net_->peer_instances_;
    init_profiler();
}

void PeerTcp::start() {
    if (outgoing_) {
        
    } else {
        send_handshake();
    }
}

void PeerTcp::_connect() {
    sock_ = ftl::net::internal::createConnection(uri_);  // throws on bad uri
    _set_socket_options();
    sock_->connect(uri_);  // throws on error
    status_ = NodeStatus::kConnecting;
}

/** Called from ftl::Universe::_periodic() */
bool PeerTcp::reconnect() {
    if (status_ != NodeStatus::kConnecting || !can_reconnect_) return false;

    URI uri(uri_);

    DLOG(INFO) << "Reconnecting to " << uri_.to_string() << " ...";

    // First, ensure all stale jobs and buffer data are removed.
    while (job_count_ > 0 && ftl::pool.size() > 0) {
        DLOG(1) << "Waiting on peer jobs before reconnect " << job_count_;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    recv_buf_.remove_nonparsed_buffer();
    recv_buf_.reset();

    try {
        _connect();
        return true;
    } catch(const std::exception& ex) {
        net_->notifyError_(this, ftl::protocol::Error::kReconnectionFailed, ex.what());
    }

    close(true);
    return false;
}

void PeerTcp::_updateURI() {
    // should be same as provided uri for connecting sockets, for connections
    // created by listening socket should generate some meaningful value
    uri_ = sock_->uri();
}

void PeerTcp::rawClose() {
    // UNIQUE_LOCK(recv_mtx_, lk_recv);
    status_ = NodeStatus::kDisconnected;

    // Must make sure no jobs are active
    while (job_count_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    UNIQUE_LOCK(send_mtx_, lk_send);
    sock_->close();
    // should sock_ be set to nullptr?
}

void PeerTcp::close(bool retry) {
    // Attempt to inform about disconnect
    if (sock_->is_valid() && status_ == NodeStatus::kConnected) {
        send("__disconnect__");
    }

    UNIQUE_LOCK(send_mtx_, lk_send);
    // UNIQUE_LOCK(recv_mtx_, lk_recv);

    _close(retry);
}

void PeerTcp::_close(bool retry) {
    if (status_ != NodeStatus::kConnected && status_ != NodeStatus::kConnecting) return;

    // Attempt auto reconnect?
    if (retry && can_reconnect_) {
        status_ = NodeStatus::kReconnecting;
    } else {
        status_ = NodeStatus::kDisconnected;
    }

    if (sock_->is_valid()) {
        net_->notifyDisconnect_(this);
        sock_->close();
    }
}

bool PeerTcp::socketError() {
    int errcode = sock_->getSocketError();

    if (!sock_->is_fatal(errcode)) return false;

    if (errcode == ECONNRESET) {
        _close(reconnect_on_socket_error_);
        return true;
    }

    net_->notifyError_(this, Error::kSocketError, std::string("Socket error: ") + std::to_string(errcode));
    _close(reconnect_on_socket_error_);
    return true;
}

void PeerTcp::error(int e) {}

NodeType PeerTcp::getType() const {
    if ((uri_.getScheme() == URI::SCHEME_WS)
        || (uri_.getScheme() == URI::SCHEME_WSS)) {
        return NodeType::kWebService;
    }
    return NodeType::kNode;
}

PeerBase::msgpack_buffer_t PeerTcp::get_buffer_() {
    send_mtx_.lock();
    return std::move(send_buf_);
}

void PeerTcp::set_buffer_(PeerBase::msgpack_buffer_t&& buffer) {
    send_buf_ = std::move(buffer);
    send_buf_.clear();
    send_mtx_.unlock();
}

int PeerTcp::send_buffer_(const std::string& name, msgpack_buffer_t&& send_buffer, SendFlags flags) {
    if (!sock_->is_valid()) return -1;

    ssize_t c = 0;

    try {
        // In trivial tests (serializing large buffers) sbuffer turned out to be about 12% faster as well
        iovec vec = { send_buffer.data(), send_buffer.size() };
        const iovec* vec_ptr = &vec; 
        //      send_buf_.vector();
        size_t vec_size = 1; 
        //      send_buf_.vector_size();

        c = sock_->writev(vec_ptr, vec_size);
        if (c <= 0) {
            // writev() should probably throw exception which is reported here
            // at the moment, error message is (should be) printed by writev()
            net_->notifyError_(this, ftl::protocol::Error::kSocketError, "writev() failed");
            set_buffer_(std::move(send_buffer));
            return c;
        }

        ssize_t sz = 0; for (size_t i = 0; i < vec_size; i++) {
            sz += vec_ptr[i].iov_len;
        }
        if (c != sz) {
            net_->notifyError_(this, ftl::protocol::Error::kSocketError, "writev(): incomplete send");
            _close(reconnect_on_socket_error_);
        }

        set_buffer_(std::move(send_buffer));

    } catch (std::exception& ex) {
        net_->notifyError_(this, ftl::protocol::Error::kSocketError, ex.what());
        _close(reconnect_on_socket_error_);
        set_buffer_(std::move(send_buffer));
    }

    net_->txBytes_ += c;
    #ifdef TRACY_ENABLE
    TracyPlot("tx", double(c));
    #endif

    // API change, return send id (synchronous socket, nothing to cancel)
    return 1;
}

///

void PeerTcp::recv() {
    ftl::Counter counter(&job_count_);
    if (!sock_->is_valid()) { return; }
    if (status_ == NodeStatus::kDisconnected) return;

    int rc = 0;

    // Only need to lock and reserve buffer if there isn't enough
    if (recv_buf_.buffer_capacity() < recv_buf_max_) {
        UNIQUE_LOCK(recv_mtx_, lk);
        recv_buf_.reserve_buffer(recv_buf_max_);
    }

    size_t cap = recv_buf_.buffer_capacity();

    try {
        rc = sock_->recv(recv_buf_.buffer(), recv_buf_.buffer_capacity());

        if (rc >= static_cast<int>(cap - 1)) {
            net_->notifyError_(this, Error::kBufferSize, "Too much data received");
            // Increase buffer size
            if (recv_buf_max_ < kMaxMessage) {
                recv_buf_max_ += 512 * 1024;
            }
        }
        if (cap < (recv_buf_max_ / 10)) {
            net_->notifyError_(this, Error::kBufferSize, "Buffer is at capacity");
        }
    } catch (std::exception& ex) {
        net_->notifyError_(this, Error::kSocketError, ex.what());
        close(reconnect_on_socket_error_);
        return;
    }

    if (rc == 0) {  // retry later
        CHECK(sock_->is_valid() == false);
        // close(reconnect_on_socket_error_);
        return;
    }
    if (rc < 0) {  // error so close peer
        sock_->close();
        close(reconnect_on_socket_error_);
        return;
    }

    net_->rxBytes_ += rc;
    #ifdef TRACY_ENABLE
    TracyPlot("rx", double(rc));
    #endif

    {
        // recv_buffer_.buffer_consumed() updates same m_used field which is also
        // accessed by recv_buffer_.next() (in another thread). this is probably fine as 
        // old value is also valid (next will not read all available data) and therefore this
        // call is probably fine without lock (assumes PeerTcp::recv() isn't called concurrently)
        recv_buf_.buffer_consumed(rc);
    }

    recv_checked_.clear();
    if (!already_processing_.test_and_set()) {
        _createJob();
    }
}

void PeerTcp::_createJob() {
    ftl::pool.push([this, c = std::move(ftl::Counter(&job_count_))](int id) {
        try {
            while (_data());
        } catch (const std::exception &e) {
            net_->notifyError_(this, ftl::protocol::Error::kUnknown, e.what());
        }
        already_processing_.clear();
    });
}

bool PeerTcp::_has_next() {
    if (!sock_->is_valid()) { return false; }

    bool has_next = true;
    // buffer might contain non-msgpack data (headers etc). check with
    // prepare_next() and skip if necessary
    size_t skip;
    auto buffer = recv_buf_.nonparsed_buffer();
    auto buffer_len = recv_buf_.nonparsed_size();
    has_next = sock_->prepare_next(buffer, buffer_len, skip);

    if (has_next) { recv_buf_.skip_nonparsed_buffer(skip); }

    return has_next;
}

bool PeerTcp::_data() {
    msgpack::object_handle msg_handle;

    try {
        recv_checked_.test_and_set();
        bool has_next = false;
        {
            UNIQUE_LOCK(recv_mtx_, lk);
            has_next = _has_next() && recv_buf_.next(msg_handle);
        }

        if (!has_next) {
            already_processing_.clear();
            if (!recv_checked_.test_and_set() && !already_processing_.test_and_set()) {
                return true;
            }
            return false;
        }
    } catch (const std::exception& ex) {
        net_->notifyError_(this, ftl::protocol::Error::kPacketFailure, ex.what());
        _close(reconnect_on_protocol_error_);
        return false;
    }

    try {
        process_message_(msg_handle);
    } catch (const std::exception &e) {
        LOG(ERROR) << "[PeerTcp] Uncaught exception: " << e.what();
        net_->notifyError_(this, Error::kDispatchFailed, e.what());
    }

    // is it safe to release msgpack object handle here without locking msgpack::unpacker?
    return true;
}

void PeerTcp::shutdown() {
    rawClose();
}

PeerTcp::~PeerTcp() {
    --net_->peer_instances_;
    {
        UNIQUE_LOCK(send_mtx_, lk1);
        // UNIQUE_LOCK(recv_mtx_,lk2);
        _close(false);
    }

    // Prevent deletion if there are any jobs remaining
    int count = 10;
    while (job_count_ > 0 && ftl::pool.size() > 0 && count-- > 0) {
        DLOG(1) << "Waiting on peer jobs... " << job_count_;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    if (job_count_ > 0) LOG(FATAL) << "Peer jobs not terminated";
}
