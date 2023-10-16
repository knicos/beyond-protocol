/**
 * @file universe.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <vector>
#include <list>
#include <string>
#include <thread>
#include <map>
#include <unordered_map>
#include <memory>

#include <msgpack.hpp>

#include <ftl/protocol/config.h>
#include <ftl/protocol.hpp>
#include <ftl/protocol/error.hpp>

#include "peer.hpp"
#include "dispatcher.hpp"
#include <ftl/uuid.hpp>
#include <ftl/uri.hpp>
#include <ftl/threads.hpp>
#include <ftl/handle.hpp>

#include <ftl/lib/nlohmann/json_fwd.hpp>

#include "socket.hpp"

#ifdef HAVE_MSQUIC
namespace beyond_impl
{
class QuicPeer;
}
#endif

namespace ftl {
namespace net {

#ifdef HAVE_MSQUIC
using QuicPeer = beyond_impl::QuicPeer;
class QuicUniverse;
#endif

class PeerTcp;
using PeerTcpPtr = std::shared_ptr<PeerTcp>;

struct ReconnectInfo {
    int tries;
    float delay;
    PeerTcpPtr peer;
};

struct NetImplDetail;

using Callback = unsigned int;

/**
 * Represents a group of network peers and their resources, managing the
 * searching of and sharing of resources across peers. Each universe can
 * listen on multiple ports/interfaces for connecting peers, and can connect
 * to any number of peers. The creation of a Universe object also creates a
 * new thread to manage the networking, therefore it is threadsafe but
 * callbacks will execute in a different thread so must also be threadsafe in
 * their actions.
 */
class Universe {
 public:
    friend class PeerTcp;
    friend class PeerBase;

    #ifdef HAVE_MSQUIC
    friend class QuicUniverse;
    friend class beyond_impl::QuicPeer;
    #endif

    Universe();

    /**
     * The destructor will terminate the network thread before completing.
     */
    ~Universe();

    void start();

    /**
     * Open a new listening port on a given interfaces.
     *   eg. "tcp://localhost:9000"
     * @param addr URI giving protocol, interface and port
     */
    bool listen(const ftl::URI &addr);

    std::vector<ftl::URI> getListeningURIs();

    /**
     * Essential to call this before destroying anything that registered
     * callbacks or binds for RPC. It will terminate all connections and
     * stop any network activity but without deleting the net object.
     */
    void shutdown();

    /**
     * Create a new peer connection.
     *   eg. "tcp://10.0.0.2:9000"
     * Supported protocols include tcp and ws.
     *
     * @param addr URI giving protocol, interface and port
     */
    PeerPtr connect(const std::string &addr, bool is_webservice=false); // FIXME: use flags or remove is_webservice
    PeerPtr connect(const ftl::URI &addr, bool is_webservice=false);

    void connectProxy(const ftl::URI &addr);

    bool isConnected(const ftl::URI &uri);
    bool isConnected(const std::string &s);

    size_t numberOfPeers() const { return connection_count_; }

    /**
     * Will block until all currently registered connnections have completed.
     * You should not use this, but rather use onConnect.
     */
    int waitConnections(int seconds = 1);

    /** get peer pointer by peer UUID, returns nullptr if not found */
    PeerPtr getPeer(const ftl::UUID &pid) const;
    /** get webservice peer pointer, returns nullptr if not connected to webservice */
    PeerPtr getWebService() const;

    std::list<PeerPtr> getPeers() const;

    /**
     * Bind a function to an RPC or service call name. This will implicitely
     * be called by any peer making the request.
     */
    template <typename F>
    void bind(const std::string &name, F func);

    void unbind(const std::string &name);

    /**
     * Check if an RPC name is already bound.
     */
    inline bool isBound(const std::string &name) const { return disp_.isBound(name); }

    /**
     * Send a non-blocking RPC call with no return value to all connected
     * peers.
     */
    template <typename... ARGS>
    void broadcast(const std::string &name, ARGS... args);

    template <typename R, typename... ARGS>
    R call(const UUID &pid, const std::string &name, ARGS... args);

    /**
     * Non-blocking Remote Procedure Call using a callback function.
     * 
     * @param pid Peer GUID
     * @param name RPC Function name.
     * @param cb Completion callback.
     * @param args A variable number of arguments for RPC function.
     * 
     * @return A call id for use with cancelCall() if needed.
     */
    template <typename R, typename... ARGS>
    std::future<R> asyncCall(const UUID &pid, const std::string &name, ARGS... args);

    template <typename... ARGS>
    bool send(const UUID &pid, const std::string &name, ARGS... args);

    template <typename... ARGS>
    int try_send(const UUID &pid, const std::string &name, ARGS... args);

    template <typename R, typename... ARGS>
    std::optional<R> findOne(const std::string &name, ARGS... args);

    template <typename R, typename... ARGS>
    std::vector<R> findAll(const std::string &name, ARGS... args);

    void setLocalID(const ftl::UUID &u) { this_peer = u; }
    const ftl::UUID &id() const { return this_peer; }

    // --- Event Handlers -----------------------------------------------------

    ftl::Handle onConnect(const std::function<bool(const ftl::net::PeerPtr&)>&);
    ftl::Handle onDisconnect(const std::function<bool(const ftl::net::PeerPtr&)>&);
    ftl::Handle onError(
        const std::function<bool(const ftl::net::PeerPtr&, ftl::protocol::Error, const std::string &)>&);

    size_t getSendBufferSize(ftl::URI::scheme_t s);
    size_t getRecvBufferSize(ftl::URI::scheme_t s);
    void setSendBufferSize(ftl::URI::scheme_t s, size_t size);
    void setRecvBufferSize(ftl::URI::scheme_t s, size_t size);

    float getKBitsPerSecondTX() const { return stats_txkbps_ * 8.0f; }
    float getKBitsPerSecondRX() const { return stats_rxkbps_ * 8.0f; }

    static inline std::shared_ptr<Universe> getInstance() { return instance_; }

    void setMaxConnections(size_t m);
    size_t getMaxConnections() const { return peers_.size(); }

    // --- Test support -------------------------------------------------------

    PeerTcpPtr injectFakePeer(std::unique_ptr<ftl::net::internal::SocketConnection> s);

    // Used by Peer implementations
    Dispatcher* dispatcher_() { return &disp_; }

    void removePeer_(PeerPtr &p);
    void insertPeer_(const ftl::net::PeerPtr &ptr);

    void notifyConnect_(ftl::net::PeerBase*); // called after successful handshake
    void notifyDisconnect_(ftl::net::PeerBase*); // called on any peer disconnect
    void notifyError_(ftl::net::PeerBase* , ftl::protocol::Error, const std::string &);

 private:
    void _run();
    void _setDescriptors();
    void _cleanupPeers();

    // no-op? TODO: remove
    void installBindings_();
    void installBindings_(const ftl::net::PeerPtr&);

    ftl::net::PeerPtr _findPeer(const ftl::net::PeerBase *p);

    void _periodic();
    void _garbage();

    static void __start(Universe *u);

    bool active_;
    ftl::UUID this_peer;
    mutable DECLARE_SHARED_MUTEX(net_mutex_);
    std::condition_variable_any socket_cv_;

    std::unique_ptr<NetImplDetail> impl_;

    // Statistics data.
    float stats_txkbps_ = 0.0f;
    float stats_rxkbps_ = 0.0f;
    std::atomic_size_t txBytes_ = 0;
    std::atomic_size_t rxBytes_ = 0;
    int64_t stats_lastTS_ = 0;

    std::vector<std::unique_ptr<ftl::net::internal::SocketServer>> listeners_;
    std::vector<ftl::net::PeerPtr> peers_;
    std::unordered_map<std::string, size_t> peer_by_uri_;
    std::map<ftl::UUID, size_t> peer_ids_;

    ftl::net::Dispatcher disp_;
    std::list<ReconnectInfo> reconnects_;
    size_t phase_;
    std::list<ftl::net::PeerPtr> garbage_;
    ftl::Handle garbage_timer_;

    // size_t send_size_;
    // size_t recv_size_;
    double periodic_time_;
    int reconnect_attempts_;
    std::atomic_int connection_count_ = 0;  // Active connections
    std::atomic_int peer_instances_ = 0;    // Actual peers dependent on Universe

    ftl::Handler<const ftl::net::PeerPtr&> on_connect_;
    ftl::Handler<const ftl::net::PeerPtr&> on_disconnect_;
    ftl::Handler<const ftl::net::PeerPtr&, ftl::protocol::Error, const std::string &> on_error_;

    static std::shared_ptr<Universe> instance_;

    // Socket buffer sizes
    size_t tcp_send_buffer_;
    size_t tcp_recv_buffer_;
    size_t ws_send_buffer_;
    size_t ws_recv_buffer_;

#ifdef HAVE_MSQUIC
    std::unique_ptr<QuicUniverse> quic_;
#endif

    // NOTE: Must always be last member
    std::thread thread_;
};

//------------------------------------------------------------------------------

template <typename F>
void Universe::bind(const std::string &name, F func) {
    // UNIQUE_LOCK(net_mutex_, lk);
    disp_.bind(name, func,
        typename ftl::internal::func_kind_info<F>::result_kind(),
        typename ftl::internal::func_kind_info<F>::args_kind(),
        typename ftl::internal::func_kind_info<F>::has_peer());
}

template <typename... ARGS>
void Universe::broadcast(const std::string &name, ARGS... args) {
    SHARED_LOCK(net_mutex_, lk);
    for (const auto &p : peers_) {
        if (!p || !p->waitConnection()) continue;
        p->send(name, args...);
    }
}

template <typename R, typename... ARGS>
std::optional<R> Universe::findOne(const std::string &name, ARGS... args) {
    std::vector<std::future<std::optional<R>>> futures;

    {
        SHARED_LOCK(net_mutex_, lk);
        for (const auto &p : peers_) {
            if (!p || !p->waitConnection()) { continue; }
            futures.push_back(std::move(p->asyncCall<std::optional<R>>(name, args...)));
        }
    }

    for (auto &f : futures) {
        if (f.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
            continue;
        }
        return f.get();
    }

    return {};
}

template <typename R, typename... ARGS>
std::vector<R> Universe::findAll(const std::string &name, ARGS... args) {
    std::vector<std::future<std::vector<R>>> futures;

    {
        SHARED_LOCK(net_mutex_, lk);
        for (const auto &p : peers_) {
            if (!p || !p->waitConnection()) { continue; }
            futures.push_back(std::move(p->asyncCall<std::vector<R>>(name, args...)));
        }
    }

    std::vector<R> results;

    for (auto &f : futures) {
        if (f.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
            continue;
        }
        auto v = f.get();
        results.insert(results.end(), v.begin(), v.end());
    }

    return results;
}

template <typename R, typename... ARGS>
R Universe::call(const ftl::UUID &pid, const std::string &name, ARGS... args) {
    PeerPtr p = getPeer(pid);
    if (p == nullptr || !p->isConnected()) {
        if (p == nullptr) throw FTL_Error("Attempting to call an unknown peer : " << pid.to_string());
        else
            throw FTL_Error("Attempting to call an disconnected peer : " << pid.to_string());
    }
    return p->call<R>(name, args...);
}

template <typename R, typename... ARGS>
std::future<R> Universe::asyncCall(const ftl::UUID &pid, const std::string &name, ARGS... args) {
    PeerPtr p = getPeer(pid);
    if (p == nullptr || !p->isConnected()) {
        if (p == nullptr) throw FTL_Error("Attempting to call an unknown peer : " << pid.to_string());
        else
            throw FTL_Error("Attempting to call an disconnected peer : " << pid.to_string());
    }
    return p->asyncCall(name, args...);
}

template <typename... ARGS>
bool Universe::send(const ftl::UUID &pid, const std::string &name, ARGS... args) {
    PeerPtr p = getPeer(pid);
    if (p == nullptr) {
        DLOG(WARNING) << "Attempting to call an unknown peer : " << pid.to_string();
        return false;
    }

    if (!p->isConnected()) { return false; }

    try {
        p->send(name, args...);
        return true;
    }
    catch(const std::exception& ex) {
        // TODO/FIXME: throw instead?
        LOG(ERROR) << "Peer::send() failed: " << ex.what();
        return false;
    }
}

template <typename... ARGS>
int Universe::try_send(const ftl::UUID &pid, const std::string &name, ARGS... args) {
    PeerPtr p = getPeer(pid);
    if (p == nullptr) {
        return false;
    }

    if (!p->isConnected()) { return false; }

    try {
        p->try_send(name, args...);
        return true;
    }
    catch(const std::exception& ex) {
        // TODO/FIXME: throw instead?
        LOG(ERROR) << "Peer::send() failed: " << ex.what();
        return false;
    }
}

};  // namespace net
};  // namespace ftl
