#include "peer.hpp"
#include "universe.hpp"

#include "protocol.hpp"

#include "uuidMSGPACK.hpp"

#include <ftl/time.hpp>

using ftl::net::PeerBase;
using ftl::protocol::NodeStatus;
using ftl::protocol::Error;

std::atomic_int PeerBase::rpcid__ = 0;

PeerBase::PeerBase(const ftl::URI& uri, ftl::net::Universe* net, ftl::net::Dispatcher* d) :
    local_id_(0),
    uri_(uri),
    net_(net),
    disp_(std::make_unique<Dispatcher>(d))
{
    bind("__disconnect__", [this]() {
        DLOG(1) << "[NET] Peer elected to disconnect: " << id().to_string();
        status_ = NodeStatus::kDisconnected;
        close(false);
    });

    bind("__ping__", [this]() {
        return ftl::time::get_time();
    });
}

PeerBase::~PeerBase() {

}

void PeerBase::_dispatchResponse(uint32_t id, msgpack::object &err, msgpack::object &res) {
    UNIQUE_LOCK(cb_mtx_, lk);
    if (callbacks_.count(id) > 0) {
        // Allow for unlock before callback
        auto cb = std::move(callbacks_[id]);
        callbacks_.erase(id);
        lk.unlock();

        // Call the callback with unpacked return value
        try {
            cb(res, err);
        } catch(std::exception &e) {
            net_->notifyError_(this, Error::kRPCResponse, e.what());
        }
    } else {
        net_->notifyError_(this, Error::kRPCResponse, "Missing RPC callback for result - discarding");
    }
}

void PeerBase::cancelCall(int id) {
    UNIQUE_LOCK(cb_mtx_, lk);
    if (callbacks_.count(id) > 0) {
        callbacks_.erase(id);
    }
}

void PeerBase::_sendResponse(uint32_t id, const msgpack::object &res) {
    Dispatcher::response_t res_obj = std::make_tuple(1, id, msgpack::object(), res);
    auto buffer = get_buffer_();
    try {
        msgpack::pack(buffer, res_obj);
        send_buffer_("rpc", std::move(buffer));

    } catch (...) {
        
    }
}

void PeerBase::_sendErrorResponse(uint32_t id, const msgpack::object &res) {
    Dispatcher::response_t res_obj = std::make_tuple(1, id, res, msgpack::object());
    auto buffer = get_buffer_();
    try {
        msgpack::pack(buffer, res_obj);
        send_buffer_("rpc", std::move(buffer));

    } catch (...) {
        
    }
}

/*void PeerBase::_waitCall(int id, std::condition_variable &cv, bool &hasreturned, const std::string &name) {
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
}*/

void PeerBase::waitForCallbacks() {

}

void PeerBase::process_message_(msgpack::object_handle& object) {
    try {
        disp_->dispatch(*this, *object);
    } catch (const std::exception &e) {
        net_->notifyError_(this, ftl::protocol::Error::kDispatchFailed, e.what());
    }
}

void PeerBase::send_handshake() {
    CHECK(status_ == ftl::protocol::NodeStatus::kConnecting)
        << "[BUG] Peer is in an invalid state (" << (int)status_ << ") for protocol handshake";
    
    handshake_sent_ = true;
    send("__handshake__", ftl::net::kMagic, ftl::net::kVersion, ftl::UUIDMSGPACK(net_->id()));
}

bool PeerBase::process_handshake_(uint64_t magic, uint32_t version, const ftl::UUIDMSGPACK &pid) {
    /** Handshake protocol:
     * 	(1). Listening side accepts connection and sends handshake.
     * 	(2). Connecting side acknowledges by replying with own handshake and
     * 		 sets status to kConnected.
     * 	(3). Listening side receives handshake and sets status to kConnected.
     */
    
    // FIXME: should be assert but fails unit tests (tests broken in peer_unit.cpp)
    LOG_IF(ERROR, status_ != ftl::protocol::NodeStatus::kConnecting)
        << "Unexpected handshake, state " << int(status_);
    
    if (magic != ftl::net::kMagic) {
        net_->notifyError_(this, ftl::protocol::Error::kBadHandshake, "invalid magic during handshake");
        return false;
    }
    
    if (version != ftl::net::kVersion) LOG(WARNING) << "net protocol using different versions!";

    version_ = version;
    peerid_ = pid;

    if (!handshake_sent_) {
        send_handshake();
    }

    status_ = ftl::protocol::NodeStatus::kConnected;
    net_->notifyConnect_(this);
    return true;
}

bool PeerBase::process_handshake_(msgpack::object_handle& object) {
    try {
        auto [_, name, message] = object->as<std::tuple<uint32_t, std::string, msgpack::object>>();
        if (name != "__handshake__") {
            net_->notifyError_(this, ftl::protocol::Error::kBadHandshake, "did not receive handshake");
            return false;
        }

        auto [magic, version, pid] = message.as<std::tuple<uint64_t, uint32_t, ftl::UUIDMSGPACK>>();
        return process_handshake_(magic, version, pid);
    }
    catch (const std::exception& ex) {
        net_->notifyError_(this, ftl::protocol::Error::kBadHandshake, "invalid magic during handshake");
    }

    return false;
}

bool PeerBase::waitConnection(int s) {
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

    cv.wait_for(m, std::chrono::seconds(s), [this]() { return status_ == NodeStatus::kConnected;});
    m.unlock();

    return status_ == NodeStatus::kConnected;
}
