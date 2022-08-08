/**
 * @file universe.cpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <chrono>
#include <utility>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <optional>
#include "universe.hpp"
#include "socketImpl.hpp"

#define LOGURU_REPLACE_GLOG 1
#include <ftl/lib/loguru.hpp>

#include <nlohmann/json.hpp>

#include "protocol/connection.hpp"
#include "protocol/tcp.hpp"

#ifdef WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#endif

#ifndef WIN32
#include <signal.h>
#include <poll.h>
#endif

using std::string;
using std::vector;
using std::thread;
using ftl::net::Peer;
using ftl::net::PeerPtr;
using ftl::net::Universe;
using nlohmann::json;
using ftl::UUID;
using std::optional;
using ftl::net::Callback;
using ftl::net::internal::socket_t;
using ftl::protocol::NodeStatus;
using ftl::protocol::NodeType;
using ftl::net::internal::SocketServer;
using ftl::net::internal::Server_TCP;
using std::chrono::milliseconds;

constexpr int kDefaultMaxConnections = 10;

namespace ftl {
namespace net {

std::unique_ptr<SocketServer> create_listener(const ftl::URI &uri) {
    if (uri.getProtocol() == ftl::URI::scheme_t::SCHEME_TCP) {
        return std::make_unique<Server_TCP>(uri.getHost(), uri.getPort());
    }
    if (uri.getProtocol() == ftl::URI::scheme_t::SCHEME_WS) {
        throw FTL_Error("WebSocket listener not implemented");
    }
    return nullptr;
}

struct NetImplDetail {
    std::vector<pollfd> pollfds;
    std::unordered_map<int, size_t> idMap;
};

}  // namespace net
}  // namespace ftl

// TODO(Seb): move to ServerSocket and ClientSocket
// Defaults, should be changed in config
#define TCP_SEND_BUFFER_SIZE    (1024*1024)
#define TCP_RECEIVE_BUFFER_SIZE (1024*1024)  // Perhaps try 24K?
#define WS_SEND_BUFFER_SIZE     (1024*1024)
#define WS_RECEIVE_BUFFER_SIZE  (62*1024)

std::shared_ptr<Universe> Universe::instance_ = nullptr;

Universe::Universe() :
        active_(true),
        this_peer(ftl::protocol::id),
        impl_(new ftl::net::NetImplDetail()),
        peers_(kDefaultMaxConnections),
        phase_(0),
        periodic_time_(1.0),
        reconnect_attempts_(5),
        tcp_send_buffer_(TCP_SEND_BUFFER_SIZE),
        tcp_recv_buffer_(TCP_RECEIVE_BUFFER_SIZE),
        ws_send_buffer_(WS_SEND_BUFFER_SIZE),
        ws_recv_buffer_(WS_RECEIVE_BUFFER_SIZE),
        thread_(Universe::__start, this) {
    _installBindings();
}

Universe::~Universe() {
    shutdown();
    CHECK_EQ(peer_instances_, 0);
}

void Universe::setMaxConnections(size_t m) {
    UNIQUE_LOCK(net_mutex_, lk);
    peers_.resize(m);
}

size_t Universe::getSendBufferSize(ftl::URI::scheme_t s) {
    switch (s) {
        case ftl::URI::scheme_t::SCHEME_WS:
        case ftl::URI::scheme_t::SCHEME_WSS:
            return ws_send_buffer_;

        default:
            return tcp_send_buffer_;
    }
}

size_t Universe::getRecvBufferSize(ftl::URI::scheme_t s) {
    switch (s) {
        case ftl::URI::scheme_t::SCHEME_WS:
        case ftl::URI::scheme_t::SCHEME_WSS:
            return ws_recv_buffer_;
        default:
            return tcp_recv_buffer_;
    }
}

void Universe::setSendBufferSize(ftl::URI::scheme_t s, size_t size) {
    switch (s) {
        case ftl::URI::scheme_t::SCHEME_WS:
        case ftl::URI::scheme_t::SCHEME_WSS:
            ws_send_buffer_ = size;
            break;

        default:
            tcp_send_buffer_ = size;
    }
}

void Universe::setRecvBufferSize(ftl::URI::scheme_t s, size_t size) {
    switch (s) {
        case ftl::URI::scheme_t::SCHEME_WS:
        case ftl::URI::scheme_t::SCHEME_WSS:
            ws_recv_buffer_ = size;
            break;
        default:
            tcp_recv_buffer_ = size;
    }
}

void Universe::start() {
    /*auto l = get<json_t>("listen");

    if (l && (*l).is_array()) {
        for (auto &ll : *l) {
            listen(ftl::URI(ll));
        }
    } else if (l && (*l).is_string()) {
        listen(ftl::URI((*l).get<string>()));
    }
    
    auto p = get<json_t>("peers");
    if (p && (*p).is_array()) {
        for (auto &pp : *p) {
            try {
                connect(pp);
            } catch (const ftl::exception &ex) {
                LOG(ERROR) << "Could not connect to: " << std::string(pp);
            }
        }
    }*/
}

void Universe::shutdown() {
    if (!active_) return;
    DLOG(1) << "Cleanup Network ...";

    {
        SHARED_LOCK(net_mutex_, lk);

        for (auto &l : listeners_) {
            l->close();
        }

        for (auto &s : peers_) {
            if (s) s->rawClose();
        }
    }

    active_ = false;
    thread_.join();

    _cleanupPeers();
    while (garbage_.size() > 0) {
        _garbage();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Try 10 times to delete the peers
    // Note: other threads may still be using the peer object
    int count = 10;
    while (peer_instances_ > 0 && count-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

bool Universe::listen(const ftl::URI &addr) {
    try {
        auto l = create_listener(addr);
        l->bind();

        {
            UNIQUE_LOCK(net_mutex_, lk);
            DLOG(1) << "listening on " << l->uri().to_string();
            listeners_.push_back(std::move(l));
        }
        socket_cv_.notify_one();
        return true;
    } catch (const std::exception &ex) {
        DLOG(INFO) << "Can't listen " << addr.to_string() << ", " << ex.what();
        _notifyError(nullptr, ftl::protocol::Error::kListen, ex.what());
        return false;
    }
}

std::vector<ftl::URI> Universe::getListeningURIs() {
    SHARED_LOCK(net_mutex_, lk);
    std::vector<ftl::URI> uris(listeners_.size());
    std::transform(listeners_.begin(), listeners_.end(), uris.begin(), [](const auto &l){ return l->uri(); });
    return uris;
}

bool Universe::isConnected(const ftl::URI &uri) {
    SHARED_LOCK(net_mutex_, lk);
    return (peer_by_uri_.find(uri.getBaseURI()) != peer_by_uri_.end());
}

bool Universe::isConnected(const std::string &s) {
    ftl::URI uri(s);
    return isConnected(uri);
}

void Universe::_insertPeer(const PeerPtr &ptr) {
    UNIQUE_LOCK(net_mutex_, lk);
    for (size_t i = 0; i < peers_.size(); ++i) {
        if (!peers_[i]) {
            ++connection_count_;
            peers_[i] = ptr;
            peer_by_uri_[ptr->getURIObject().getBaseURI()] = i;
            peer_ids_[ptr->id()] = i;
            ptr->local_id_ = i;

            lk.unlock();
            socket_cv_.notify_one();
            return;
        }
    }
    throw FTL_Error("Too many connections");
}

PeerPtr Universe::connect(const ftl::URI &u) {
    // Check if already connected or if self (when could this happen?)
    {
        SHARED_LOCK(net_mutex_, lk);
        if (peer_by_uri_.find(u.getBaseURI()) != peer_by_uri_.end()) {
            return peers_[peer_by_uri_.at(u.getBaseURI())];
        }

        if (u.getHost() == "localhost" || u.getHost() == "127.0.0.1") {
            if (std::any_of(
                    listeners_.begin(),
                    listeners_.end(),
                    [u](const auto &l) { return l->port() == u.getPort(); })) {
                throw FTL_Error("Cannot connect to self");
            }
        }
    }

    auto p = std::make_shared<Peer>(u, this, &disp_);

    _insertPeer(p);
    _installBindings(p);
    p->start();

    return p;
}

PeerPtr Universe::connect(const std::string& addr) {
    return connect(ftl::URI(addr));
}

void Universe::unbind(const std::string &name) {
    UNIQUE_LOCK(net_mutex_, lk);
    disp_.unbind(name);
}

int Universe::waitConnections(int seconds) {
    SHARED_LOCK(net_mutex_, lk);
    auto peers = peers_;
    lk.unlock();
    return std::count_if(peers.begin(), peers.end(), [seconds](const auto &p) {
        return p && p->waitConnection(seconds);
    });
}

void Universe::_setDescriptors() {
    SHARED_LOCK(net_mutex_, lk);

    impl_->pollfds.clear();
    impl_->idMap.clear();

    // Set file descriptor for the listening sockets.
    for (auto &l : listeners_) {
        if (l) {
            auto sock = l->fd();
            if (sock != INVALID_SOCKET) {
                pollfd fdentry;
                #ifdef WIN32
                fdentry.events = POLLIN;
                #else
                fdentry.events = POLLIN;  // | POLLERR;
                #endif
                fdentry.fd = sock;
                fdentry.revents = 0;
                impl_->pollfds.push_back(fdentry);
                impl_->idMap[sock] = impl_->pollfds.size() - 1;
            }
        }
    }

    // Set the file descriptors for each client
    for (const auto &s : peers_) {
        if (s && s->isValid()) {
            auto sock = s->_socket();
            if (sock != INVALID_SOCKET) {
                pollfd fdentry;
                #ifdef WIN32
                fdentry.events = POLLIN;
                #else
                fdentry.events = POLLIN;  // | POLLERR;
                #endif
                fdentry.fd = sock;
                fdentry.revents = 0;
                impl_->pollfds.push_back(fdentry);
                impl_->idMap[sock] = impl_->pollfds.size() - 1;
            }
        }
    }
}

void Universe::_installBindings(const PeerPtr &p) {}

void Universe::_installBindings() {}

void Universe::_removePeer(PeerPtr &p) {
    UNIQUE_LOCK(net_mutex_, ulk);

    if (p && (!p->isValid() ||
            p->status() == NodeStatus::kReconnecting ||
            p->status() == NodeStatus::kDisconnected)) {
        auto ix = peer_ids_.find(p->id());
        if (ix != peer_ids_.end()) peer_ids_.erase(ix);

        for (auto j=peer_by_uri_.begin(); j != peer_by_uri_.end(); ++j) {
            if (peers_[j->second] == p) {
                peer_by_uri_.erase(j);
                break;
            }
        }

        if (p->status() == NodeStatus::kReconnecting) {
            reconnects_.push_back({reconnect_attempts_, 1.0f, p});
        } else {
            garbage_.push_back(p);
        }

        --connection_count_;

        DLOG(1) << "Removing disconnected peer: " << p->id().to_string();
        on_disconnect_.triggerAsync(p);

        p.reset();
    }
}

void Universe::_cleanupPeers() {
    SHARED_LOCK(net_mutex_, lk);
    auto i = peers_.begin();
    while (i != peers_.end()) {
        auto &p = *i;
        if (p && (!p->isValid() ||
            p->status() == NodeStatus::kReconnecting ||
            p->status() == NodeStatus::kDisconnected)) {
            lk.unlock();
            _removePeer(p);
            lk.lock();
        }
        ++i;
    }
}

PeerPtr Universe::getPeer(const UUID &id) const {
    SHARED_LOCK(net_mutex_, lk);
    auto ix = peer_ids_.find(id);
    if (ix == peer_ids_.end()) return nullptr;
    else
        return peers_[ix->second];
}

PeerPtr Universe::getWebService() const {
    SHARED_LOCK(net_mutex_, lk);
    auto it = std::find_if(peers_.begin(), peers_.end(), [](const auto &p) {
        return p && p->getType() == NodeType::kWebService;
    });
    return (it != peers_.end()) ? *it : nullptr;
}

std::list<PeerPtr> Universe::getPeers() const {
    SHARED_LOCK(net_mutex_, lk);
    std::list<PeerPtr> result;
    std::copy_if(peers_.begin(), peers_.end(), std::back_inserter(result), [](const PeerPtr &ptr){ return !!ptr; });
    return result;
}

void Universe::_periodic() {
    auto i = reconnects_.begin();
    while (i != reconnects_.end()) {
        std::string addr = i->peer->getURI();

        {
            SHARED_LOCK(net_mutex_, lk);
            ftl::URI u(addr);
            bool removed = false;

            if (u.getHost() == "localhost" || u.getHost() == "127.0.0.1") {
                for (const auto &l : listeners_) {
                    if (l->port() == u.getPort()) {
                        _notifyError(nullptr, ftl::protocol::Error::kSelfConnect, "Cannot connect to self");
                        garbage_.push_back((*i).peer);
                        i = reconnects_.erase(i);
                        removed = true;
                        break;
                    }
                }
            }

            if (removed) continue;
        }

        auto peer = i->peer;
        _insertPeer(peer);
        peer->status_ = NodeStatus::kConnecting;
        i = reconnects_.erase(i);
        // ftl::pool.push([peer](int id) {
            peer->reconnect();
        // });

        /*if ((*i).peer->reconnect()) {
            _insertPeer((*i).peer);
            i = reconnects_.erase(i);
        }
        else if ((*i).tries > 0) {
            (*i).tries--;
            i++;
        }
         else {
            garbage_.push_back((*i).peer);
            i = reconnects_.erase(i);
        }*/
    }

    // Garbage peers may not be needed any more
    if (garbage_.size() > 0) {
        _garbage();
    }
}

void Universe::_garbage() {
    UNIQUE_LOCK(net_mutex_, lk);
    // Only do garbage if processing is idle.
    if (ftl::pool.n_idle() == ftl::pool.size()) {
        if (garbage_.size() > 0) DLOG(1) << "Garbage collection";
        while (garbage_.size() > 0) {
            garbage_.front().reset();
            garbage_.pop_front();
        }
    }
}

void Universe::__start(Universe *u) {
#ifndef WIN32
    // TODO(Seb): move somewhere else (common initialization file?)
    signal(SIGPIPE, SIG_IGN);
#endif  // WIN32
    u->_run();
}

void Universe::_run() {
    auto start = std::chrono::high_resolution_clock::now();

    while (active_) {
        _setDescriptors();
        int selres = 1;

        _cleanupPeers();

        // Do periodics
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = now - start;
        if (elapsed.count() >= periodic_time_) {
            start = now;
            _periodic();
        }

        // It is an error to use "select" with no sockets ... so just sleep
        if (impl_->pollfds.size() == 0) {
            std::shared_lock lk(net_mutex_);
            socket_cv_.wait_for(
                lk,
                milliseconds(100),
                [this](){ return listeners_.size() > 0 || connection_count_ > 0; });
            continue;
        }

        #ifdef WIN32
        selres = WSAPoll(impl_->pollfds.data(), impl_->pollfds.size(), 100);
        #else
        selres = poll(impl_->pollfds.data(), impl_->pollfds.size(), 100);
        #endif

        // Some kind of error occured, it is usually possible to recover from this.
        if (selres < 0) {
            #ifdef WIN32
            int errNum = WSAGetLastError();
            switch (errNum) {
            case WSAENOTSOCK    : continue;  // Socket was closed
            default             : DLOG(WARNING) << "Unhandled poll error: " << errNum;
            }
            #else
            switch (errno) {
            case 9  : continue;  // Bad file descriptor = socket closed
            case 4  : continue;  // Interrupted system call ... no problem
            default : DLOG(WARNING) << "Unhandled poll error: " << strerror(errno) << "(" << errno << ")";
            }
            #endif
            continue;
        } else if (selres == 0) {
            // Timeout, nothing to do...
            continue;
        }

        SHARED_LOCK(net_mutex_, lk);

        // If connection request is waiting
        for (auto &l : listeners_) {
            if (l && l->is_listening() && (impl_->pollfds[impl_->idMap[l->fd()]].revents & POLLIN)) {
                std::unique_ptr<ftl::net::internal::SocketConnection> csock;
                try {
                    csock = l->accept();
                } catch (const std::exception &ex) {
                    _notifyError(nullptr, ftl::protocol::Error::kConnectionFailed, ex.what());
                }

                lk.unlock();

                if (csock) {
                    auto p = std::make_shared<Peer>(std::move(csock), this, &disp_);
                    _insertPeer(p);
                    p->start();
                }

                lk.lock();
            }
        }


        // Also check each clients socket to see if any messages or errors are waiting
        for (size_t p = 0; p < peers_.size(); ++p) {
            auto s = peers_[(p+phase_)%peers_.size()];

            if (s && s->isValid()) {
                // Note: It is possible that the socket becomes invalid after check but before
                // looking at the FD sets, therefore cache the original socket
                SOCKET sock = s->_socket();
                if (sock == INVALID_SOCKET) continue;

                if (impl_->idMap.count(sock) == 0) continue;

                const auto &fdstruct = impl_->pollfds[impl_->idMap[sock]];

                // This is needed on Windows to detect socket close.
                if (fdstruct.revents & POLLERR) {
                    if (s->socketError()) {
                        continue;  // No point in reading data...
                    }
                }
                // If message received from this client then deal with it
                if (fdstruct.revents & POLLIN) {
                    lk.unlock();
                    s->data();
                    lk.lock();
                }
            }
        }
        ++phase_;
    }

    // Garbage is a threadsafe container, moving there first allows the destructor to be called
    // without the lock.
    {
        UNIQUE_LOCK(net_mutex_, lk);
        garbage_.insert(garbage_.end(), peers_.begin(), peers_.end());
        reconnects_.clear();
        peers_.clear();
        peer_by_uri_.clear();
        peer_ids_.clear();
        listeners_.clear();
    }

    garbage_.clear();
}

ftl::Handle Universe::onConnect(const std::function<bool(const PeerPtr&)> &cb) {
    return on_connect_.on(cb);
}

ftl::Handle Universe::onDisconnect(const std::function<bool(const PeerPtr&)> &cb) {
    return on_disconnect_.on(cb);
}

ftl::Handle Universe::onError(
        const std::function<bool(const PeerPtr&, ftl::protocol::Error, const std::string &)> &cb) {
    return on_error_.on(cb);
}

PeerPtr Universe::injectFakePeer(std::unique_ptr<ftl::net::internal::SocketConnection> s) {
    auto p = std::make_shared<Peer>(std::move(s), this, &disp_);
    _insertPeer(p);
    _installBindings(p);
    return p;
}

PeerPtr Universe::_findPeer(const Peer *p) {
    SHARED_LOCK(net_mutex_, lk);
    for (const auto &pp : peers_) {
        if (pp.get() == p) return pp;
    }
    return nullptr;
}

void Universe::_notifyConnect(Peer *p) {
    const auto ptr = _findPeer(p);

    // The peer could have been removed from valid peers already.
    if (!ptr) return;

    {
        UNIQUE_LOCK(net_mutex_, lk);
        peer_ids_[ptr->id()] = ptr->local_id_;
    }

    on_connect_.triggerAsync(ptr);
}

void Universe::_notifyDisconnect(Peer *p) {
    const auto ptr = _findPeer(p);
    if (!ptr) return;

    on_disconnect_.triggerAsync(ptr);
}

void Universe::_notifyError(Peer *p, ftl::protocol::Error e, const std::string &errstr) {
    LOG(ERROR) << "Net Error (" << int(e) << "): " << errstr;
    const auto ptr = (p) ? _findPeer(p) : nullptr;

    on_error_.triggerAsync(ptr, e, errstr);
}
