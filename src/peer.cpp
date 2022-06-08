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

#include "common.hpp"

#include <ftl/uri.hpp>
#include <ftl/time.hpp>
#include "peer.hpp"
#include "uuidMSGPACK.hpp"

#include "protocol/connection.hpp"

using ftl::net::internal::SocketConnection;

#include "universe.hpp"

using std::tuple;
using std::get;
using ftl::net::Peer;
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

std::atomic_int Peer::rpcid__ = 0;

int Peer::_socket() const {
    if (sock_->is_valid()) {
        return sock_->fd();
    } else {
        return INVALID_SOCKET;
    }
}

bool Peer::isConnected() const {
    return sock_->is_valid() && (status_ == NodeStatus::kConnected);
}

bool Peer::isValid() const {
    return sock_ && sock_->is_valid() && ((status_ == NodeStatus::kConnected) || (status_ == NodeStatus::kConnecting));
}

void Peer::_set_socket_options() {
    CHECK(net_);
    CHECK(sock_);

    // error printed by set methods (return value ignored)
    sock_->set_send_buffer_size(net_->getSendBufferSize(sock_->scheme()));
    sock_->set_recv_buffer_size(net_->getRecvBufferSize(sock_->scheme()));

    DLOG(1) << "send buffer size: " << (sock_->get_send_buffer_size() >> 10) << "KiB, "
            << "recv buffer size: " << (sock_->get_recv_buffer_size() >> 10) << "KiB";
}

void Peer::_send_handshake() {
    DLOG(INFO) << "(" << (outgoing_ ? "connecting" : "listening")
            << " peer) handshake sent, status: "
            << (isConnected() ? "connected" : "connecting");

    send("__handshake__", ftl::net::kMagic, ftl::net::kVersion, ftl::UUIDMSGPACK(net_->id()));
}

void Peer::_process_handshake(uint64_t magic, uint32_t version, const UUID &pid) {
    /** Handshake protocol:
     * 	(1). Listening side accepts connection and sends handshake.
     * 	(2). Connecting side acknowledges by replying with own handshake and
     * 		 sets status to kConnected.
     * 	(3). Listening side receives handshake and sets status to kConnected.
     */
    if (magic != ftl::net::kMagic) {
        net_->_notifyError(this, ftl::protocol::Error::kBadHandshake, "invalid magic during handshake");
        _close(reconnect_on_protocol_error_);
    } else {
        if (version != ftl::net::kVersion) LOG(WARNING) << "net protocol using different versions!";

        DLOG(INFO) << "(" << (outgoing_ ? "connecting" : "listening")
                  << " peer) handshake received from remote for " << pid.to_string();

        status_ = NodeStatus::kConnected;
        version_ = version;
        peerid_ = pid;

        if (outgoing_) {
            // only outgoing connection replies with handshake, listening socket
            // sends initial handshake on connect
            _send_handshake();
        }

        ++connection_count_;
        net_->_notifyConnect(this);
    }
}

void Peer::_bind_rpc() {
    // Install return handshake handler.
    bind("__handshake__", [this](uint64_t magic, uint32_t version, const ftl::UUIDMSGPACK &pid) {
        _process_handshake(magic, version, pid);
    });

    bind("__disconnect__", [this]() {
        close(reconnect_on_remote_disconnect_);
        DLOG(1) << "peer elected to disconnect: " << id().to_string();
    });

    bind("__ping__", [this]() {
        return ftl::time::get_time();
    });
}

Peer::Peer(std::unique_ptr<internal::SocketConnection> s, Universe* u, Dispatcher* d) :
        outgoing_(false),
        local_id_(0),
        uri_("0"),
        status_(NodeStatus::kConnecting),
        can_reconnect_(false),
        net_(u),
        sock_(std::move(s)),
        disp_(std::make_unique<Dispatcher>(d)) {
    /* Incoming connection constructor */

    CHECK(sock_) << "incoming SocketConnection pointer null";
    _set_socket_options();
    _updateURI();
    _bind_rpc();
    ++net_->peer_instances_;
}

Peer::Peer(const ftl::URI& uri, Universe *u, Dispatcher *d) :
        outgoing_(true),
        local_id_(0),
        uri_(uri),
        status_(NodeStatus::kInvalid),
        can_reconnect_(true),
        net_(u),
        disp_(std::make_unique<Dispatcher>(d)) {
    /* Outgoing connection constructor */

    _bind_rpc();
    _connect();
    ++net_->peer_instances_;
}

void Peer::start() {
    if (outgoing_) {
        // Connect needs to be in constructor
    } else {
        _send_handshake();
    }
}

void Peer::_connect() {
    sock_ = ftl::net::internal::createConnection(uri_);  // throws on bad uri
    _set_socket_options();
    sock_->connect(uri_);  // throws on error
    status_ = NodeStatus::kConnecting;
}

/** Called from ftl::Universe::_periodic() */
bool Peer::reconnect() {
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
        net_->_notifyError(this, ftl::protocol::Error::kReconnectionFailed, ex.what());
    }

    close(true);
    return false;
}

void Peer::_updateURI() {
    // should be same as provided uri for connecting sockets, for connections
    // created by listening socket should generate some meaningful value
    uri_ = sock_->uri();
}

void Peer::rawClose() {
    UNIQUE_LOCK(send_mtx_, lk_send);
    // UNIQUE_LOCK(recv_mtx_, lk_recv);
    sock_->close();
    status_ = NodeStatus::kDisconnected;
}

void Peer::close(bool retry) {
    // Attempt to inform about disconnect
    if (sock_->is_valid() && status_ == NodeStatus::kConnected) {
        send("__disconnect__");
    }

    UNIQUE_LOCK(send_mtx_, lk_send);
    // UNIQUE_LOCK(recv_mtx_, lk_recv);

    _close(retry);
}

void Peer::_close(bool retry) {
    if (status_ != NodeStatus::kConnected && status_ != NodeStatus::kConnecting) return;

    // Attempt auto reconnect?
    if (retry && can_reconnect_) {
        status_ = NodeStatus::kReconnecting;
    } else {
        status_ = NodeStatus::kDisconnected;
    }

    if (sock_->is_valid()) {
        net_->_notifyDisconnect(this);
        sock_->close();
    }
}

bool Peer::socketError() {
    int errcode = sock_->getSocketError();

    if (!sock_->is_fatal(errcode)) return false;

    if (errcode == ECONNRESET) {
        _close(reconnect_on_socket_error_);
        return true;
    }

    net_->_notifyError(this, Error::kSocketError, std::string("Socket error: ") + std::to_string(errcode));
    _close(reconnect_on_socket_error_);
    return true;
}

void Peer::error(int e) {}

NodeType Peer::getType() const {
    if ((uri_.getScheme() == URI::SCHEME_WS)
        || (uri_.getScheme() == URI::SCHEME_WSS)) {
        return NodeType::kWebService;
    }
    return NodeType::kNode;
}

void Peer::_createJob() {
    ++job_count_;

    ftl::pool.push([this](int id) {
        try {
            _data();
        } catch (const std::exception &e) {
            net_->_notifyError(this, ftl::protocol::Error::kUnknown, e.what());
        }
        --job_count_;
    });
}

void Peer::data() {
    if (!sock_->is_valid()) { return; }

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
            net_->_notifyError(this, Error::kBufferSize, "Too much data received");
            // Increase buffer size
            if (recv_buf_max_ < kMaxMessage) {
                recv_buf_max_ += 512 * 1024;
            }
        }
        if (cap < (recv_buf_max_ / 10)) {
            net_->_notifyError(this, Error::kBufferSize, "Buffer is at capacity");
        }
    } catch (std::exception& ex) {
        net_->_notifyError(this, Error::kSocketError, ex.what());
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

    // May possibly need locking
    recv_buf_.buffer_consumed(rc);

    recv_checked_.clear();
    if (!already_processing_.test_and_set()) {
        // lk.unlock();
        _createJob();
    }
}

bool Peer::_has_next() {
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

bool Peer::_data() {
    // lock before trying to acquire handle to buffer
    // UNIQUE_LOCK(recv_mtx_, lk);

    // msgpack::object is valid as long as handle is
    msgpack::object_handle msg_handle;

    try {
        recv_checked_.test_and_set();

        UNIQUE_LOCK(recv_mtx_, lk);
        bool has_next = _has_next() && recv_buf_.next(msg_handle);
        lk.unlock();

        if (!has_next) {
            already_processing_.clear();
            if (!recv_checked_.test_and_set() && !already_processing_.test_and_set()) {
                return _data();
            }
            return false;
        }
    } catch (const std::exception& ex) {
        net_->_notifyError(this, ftl::protocol::Error::kPacketFailure, ex.what());
        _close(reconnect_on_protocol_error_);
        return false;
    }

    // lk.unlock();

    msgpack::object obj = msg_handle.get();

    if (status_ == NodeStatus::kConnecting) {
        // If not connected, must lock to make sure no other thread performs this step
        // lk.lock();

        // Verify still not connected after lock
        // if (status_ == NodeStatus::kConnecting) {
            // First message must be a handshake
            try {
                tuple<uint32_t, std::string, msgpack::object> hs;
                obj.convert(hs);

                if (get<1>(hs) != "__handshake__") {
                    DLOG(WARNING) << "Missing handshake - got '" << get<1>(hs) << "'";

                    // Allow a small delay in case another thread is doing the handshake
                    // lk.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if (status_ == NodeStatus::kConnecting) {
                        net_->_notifyError(this, Error::kMissingHandshake, "failed to get handshake");
                        close(reconnect_on_protocol_error_);
                        // lk.lock();
                        return false;
                    }
                } else {
                    // Must handle immediately with no other thread able
                    // to read next message before completion.
                    // The handshake handler must not block.

                    try {
                        disp_->dispatch(*this, obj);
                    } catch (const std::exception &e) {
                        net_->_notifyError(this, ftl::protocol::Error::kDispatchFailed, e.what());
                    }

                    _createJob();
                    return true;
                }
            } catch(...) {
                DLOG(WARNING) << "Bad first message format... waiting";
                // Allow a small delay in case another thread is doing the handshake

                // lk.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (status_ == NodeStatus::kConnecting) {
                    net_->_notifyError(this, Error::kMissingHandshake, "failed to get handshake");
                    close(reconnect_on_protocol_error_);
                    return false;
                }
            }
        // } else {
            // lk.unlock();
        // }
    }

    // Process more data...
    _createJob();

    try {
        disp_->dispatch(*this, obj);
    } catch (const std::exception &e) {
        net_->_notifyError(this, Error::kDispatchFailed, e.what());
    }

    // Lock again before freeing msg_handle (destruction order).
    // msgpack::object_handle destructor modifies recv_buffer_
    // lk.lock();
    return true;
}

void Peer::_dispatchResponse(uint32_t id, const std::string &name, msgpack::object &res) {
    UNIQUE_LOCK(cb_mtx_, lk);
    if (callbacks_.count(id) > 0) {
        // Allow for unlock before callback
        auto cb = std::move(callbacks_[id]);
        callbacks_.erase(id);
        lk.unlock();

        // Call the callback with unpacked return value
        try {
            (*cb)(res);
        } catch(std::exception &e) {
            net_->_notifyError(this, Error::kRPCResponse, e.what());
        }
    } else {
        net_->_notifyError(this, Error::kRPCResponse, "Missing RPC callback for result - discarding: " + name);
    }
}

void Peer::cancelCall(int id) {
    UNIQUE_LOCK(cb_mtx_, lk);
    if (callbacks_.count(id) > 0) {
        callbacks_.erase(id);
    }
}

void Peer::_sendResponse(uint32_t id, const std::string &name, const msgpack::object &res) {
    Dispatcher::response_t res_obj = std::make_tuple(1, id, name, res);
    UNIQUE_LOCK(send_mtx_, lk);
    msgpack::pack(send_buf_, res_obj);
    _send();
}

void Peer::_waitCall(int id, std::condition_variable &cv, bool &hasreturned, const std::string &name) {
    std::mutex m;

    int64_t beginat = ftl::time::get_time();
    std::function<void(int)> j;
    while (!hasreturned) {
        // Attempt to do a thread pool job if available
        if (static_cast<bool>(j = ftl::pool.pop())) {
            j(-1);
        } else {
            // Block for a little otherwise
            std::unique_lock<std::mutex> lk(m);
            cv.wait_for(lk, std::chrono::milliseconds(2), [&hasreturned]{return hasreturned;});
        }

        if (ftl::time::get_time() - beginat > 1000) break;
    }

    if (!hasreturned) {
        cancelCall(id);
        throw FTL_Error("RPC failed with timeout: " << name);
    }
}

bool Peer::waitConnection(int s) {
    if (status_ == NodeStatus::kConnected) return true;
    else if (status_ == NodeStatus::kDisconnected) return false;

    std::mutex m;
    m.lock();
    std::condition_variable_any cv;

    auto h = net_->onConnect([this, &cv](const PeerPtr &p) {
        if (p.get() == this) {
            cv.notify_one();
        }
        return true;
    });

    cv.wait_for(m, seconds(s), [this]() { return status_ == NodeStatus::kConnected;});
    m.unlock();
    return status_ == NodeStatus::kConnected;
}

int Peer::_send() {
    if (!sock_->is_valid()) return -1;

    ssize_t c = 0;

    try {
        c = sock_->writev(send_buf_.vector(), send_buf_.vector_size());
        if (c <= 0) {
            // writev() should probably throw exception which is reported here
            // at the moment, error message is (should be) printed by writev()
            net_->_notifyError(this, ftl::protocol::Error::kSocketError, "writev() failed");
            return c;
        }

        ssize_t sz = 0; for (size_t i = 0; i < send_buf_.vector_size(); i++) {
            sz += send_buf_.vector()[i].iov_len;
        }
        if (c != sz) {
            net_->_notifyError(this, ftl::protocol::Error::kSocketError, "writev(): incomplete send");
            _close(reconnect_on_socket_error_);
        }

        send_buf_.clear();
    } catch (std::exception& ex) {
        net_->_notifyError(this, ftl::protocol::Error::kSocketError, ex.what());
        _close(reconnect_on_socket_error_);
    }

    return c;
}

Peer::~Peer() {
    --net_->peer_instances_;
    {
        UNIQUE_LOCK(send_mtx_, lk1);
        // UNIQUE_LOCK(recv_mtx_,lk2);
        _close(false);
    }

    // Prevent deletion if there are any jobs remaining
    if (job_count_ > 0 && ftl::pool.size() > 0) {
        DLOG(1) << "Waiting on peer jobs... " << job_count_;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (job_count_ > 0) LOG(FATAL) << "Peer jobs not terminated";
    }
}
