/**
 * @file universe.cpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include "universe.hpp"
#include "socketImpl.hpp"
#include <chrono>
#include <algorithm>

#define LOGURU_REPLACE_GLOG 1
#include <ftl/lib/loguru.hpp>

#include <nlohmann/json.hpp>

#include "protocol/connection.hpp"
#include "protocol/tcp.hpp"

#ifdef WIN32
#include <winsock.h>
#include <Ws2tcpip.h>
#endif

#ifndef WIN32
#include <signal.h>
#endif

using std::string;
using std::vector;
using std::thread;
using ftl::net::Peer;
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
	fd_set sfderror_;
	fd_set sfdread_;
};

}
}

// TODO: move to ServerSocket and ClientSocket
// Defaults, should be changed in config
#define TCP_SEND_BUFFER_SIZE	(1024*1024)
#define TCP_RECEIVE_BUFFER_SIZE	(1024*1024)  // Perhaps try 24K?
#define WS_SEND_BUFFER_SIZE	(1024*1024)
#define WS_RECEIVE_BUFFER_SIZE	(62*1024)

std::shared_ptr<Universe> Universe::instance_ = nullptr;

Universe::Universe() :
		active_(true),
		this_peer(ftl::protocol::id),
		impl_(new ftl::net::NetImplDetail()),
		peers_(10),
		phase_(0),
		periodic_time_(1.0),
		reconnect_attempts_(5),
		thread_(Universe::__start, this) {

	_installBindings();

	// Add an idle timer job to garbage collect peer objects
	// Note: Important to be a timer job to ensure no other timer jobs are
	// using the object.
	// FIXME: Replace use of timer.
	/*garbage_timer_ = ftl::timer::add(ftl::timer::kTimerIdle10, [this](int64_t ts) {
		if (garbage_.size() > 0) {
			UNIQUE_LOCK(net_mutex_,lk);
			if (ftl::pool.n_idle() == ftl::pool.size()) {
				if (garbage_.size() > 0) LOG(1) << "Garbage collection";
				while (garbage_.size() > 0) {
					delete garbage_.front();
					garbage_.pop_front();
				}
			}
		}
		return true;
	});*/
}

Universe::~Universe() {
	shutdown();
	CHECK(peer_instances_ == 0);
}

size_t Universe::getSendBufferSize(ftl::URI::scheme_t s) {
	// TODO: Allow these to be configured again.
	switch(s) {
		case ftl::URI::scheme_t::SCHEME_WS:
		case ftl::URI::scheme_t::SCHEME_WSS:
			return WS_SEND_BUFFER_SIZE;

		default:
			return TCP_SEND_BUFFER_SIZE;
	}		
}

size_t Universe::getRecvBufferSize(ftl::URI::scheme_t s) {
	switch(s) {
		case ftl::URI::scheme_t::SCHEME_WS:
		case ftl::URI::scheme_t::SCHEME_WSS:
			return WS_RECEIVE_BUFFER_SIZE;
		
		default:
			return TCP_RECEIVE_BUFFER_SIZE;
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
	LOG(INFO) << "Cleanup Network ...";

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
}

bool Universe::listen(const ftl::URI &addr) {
	try {
		auto l = create_listener(addr);
		l->bind();

		{
			UNIQUE_LOCK(net_mutex_,lk);
			LOG(INFO) << "listening on " << l->uri().to_string();
			listeners_.push_back(std::move(l));
		}
		socket_cv_.notify_one();
		return true;

	} catch (const std::exception &ex) {
		LOG(ERROR) << "Can't listen " << addr.to_string() << ", " << ex.what();
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
	SHARED_LOCK(net_mutex_,lk);
	return (peer_by_uri_.find(uri.getBaseURI()) != peer_by_uri_.end());
}

bool Universe::isConnected(const std::string &s) {
	ftl::URI uri(s);
	return isConnected(uri);
}

void Universe::_insertPeer(const std::shared_ptr<Peer> &ptr) {
	UNIQUE_LOCK(net_mutex_,lk);
	for (size_t i=0; i<peers_.size(); ++i) {
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

std::shared_ptr<Peer> Universe::connect(const ftl::URI &u) {

	// Check if already connected or if self (when could this happen?)
	{
		SHARED_LOCK(net_mutex_,lk);
		if (peer_by_uri_.find(u.getBaseURI()) != peer_by_uri_.end()) {
			return peers_[peer_by_uri_.at(u.getBaseURI())];
		}

		if (u.getHost() == "localhost" || u.getHost() == "127.0.0.1") {
			if (std::any_of(listeners_.begin(), listeners_.end(), [u](const auto &l) { return l->port() == u.getPort(); })) {
				throw FTL_Error("Cannot connect to self");
			}
		}
	}
	
	auto p = std::make_shared<Peer>(u, this, &disp_);
	
	if (p->status() != NodeStatus::kInvalid) {
		_insertPeer(p);
	}
	else {
		LOG(ERROR) << "Peer in invalid state";
	}
	
	_installBindings(p);
	return p;
}

std::shared_ptr<Peer> Universe::connect(const std::string& addr) {
	return connect(ftl::URI(addr));
}

void Universe::unbind(const std::string &name) {
	UNIQUE_LOCK(net_mutex_,lk);
	disp_.unbind(name);
}

int Universe::waitConnections() {
	SHARED_LOCK(net_mutex_, lk);
	return std::count_if(peers_.begin(), peers_.end(), [](const auto &p) {
		return p && p->waitConnection();
	});
}

socket_t Universe::_setDescriptors() {
	//Reset all file descriptors
	FD_ZERO(&impl_->sfdread_);
	FD_ZERO(&impl_->sfderror_);

	socket_t n = 0;

	SHARED_LOCK(net_mutex_, lk);

	//Set file descriptor for the listening sockets.
	for (auto &l : listeners_) {
		if (l) {
			FD_SET(l->fd(), &impl_->sfdread_);
			FD_SET(l->fd(), &impl_->sfderror_);
			n = std::max<socket_t>(n, l->fd());
		}
	}

	// FIXME: Bug, it crashes here sometimes, segfault on reading the shared_ptr

	//Set the file descriptors for each client
	for (const auto &s : peers_) {
		// NOTE: s->isValid() should return true only and only if a valid OS
		//       socket exists.

		if (s && s->isValid()) {
			n = std::max<socket_t>(n, s->_socket());
			FD_SET(s->_socket(), &impl_->sfdread_);
			FD_SET(s->_socket(), &impl_->sfderror_);
		}
	}

	return n;
}

void Universe::_installBindings(const std::shared_ptr<Peer> &p) {
	
}

void Universe::_installBindings() {

}

void Universe::_removePeer(std::shared_ptr<Peer> &p) {
	UNIQUE_LOCK(net_mutex_, ulk);
			
	if (p && (!p->isValid() ||
		p->status() == NodeStatus::kReconnecting ||
		p->status() == NodeStatus::kDisconnected)) {

		LOG(INFO) << "Removing disconnected peer: " << p->id().to_string();
		on_disconnect_.triggerAsync(p);
	
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
		} else {
			i++;
		}
	}
}

std::shared_ptr<Peer> Universe::getPeer(const UUID &id) const {
	SHARED_LOCK(net_mutex_,lk);
	auto ix = peer_ids_.find(id);
	if (ix == peer_ids_.end()) return nullptr;
	else return peers_[ix->second];
}

std::shared_ptr<Peer> Universe::getWebService() const {
	SHARED_LOCK(net_mutex_,lk);
	auto it = std::find_if(peers_.begin(), peers_.end(), [](const auto &p) {
		return p && p->getType() == NodeType::kWebService;
	});
	return (it != peers_.end()) ? *it : nullptr;
}

void Universe::_periodic() {
	auto i = reconnects_.begin();
	while (i != reconnects_.end()) {

		std::string addr = i->peer->getURI();

		{
			UNIQUE_LOCK(net_mutex_,lk);
			ftl::URI u(addr);
			bool removed = false;

			if (u.getHost() == "localhost" || u.getHost() == "127.0.0.1") {
				for (const auto &l : listeners_) {
					if (l->port() == u.getPort()) {
						// TODO: use UUID?
						LOG(ERROR) << "Cannot connect to self";
						garbage_.push_back((*i).peer);
						i = reconnects_.erase(i);
						removed = true;
						break;
					}
				}
			}

			if (removed) continue;
		}

		if ((*i).peer->reconnect()) {
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
			LOG(WARNING) << "Reconnection to peer failed";
		}
	}
}

void Universe::__start(Universe *u) {
#ifndef WIN32
	// TODO: move somewhere else (common initialization file?)
	signal(SIGPIPE,SIG_IGN);
#endif  // WIN32
	u->_run();
}

void Universe::_run() {
	timeval block;

	auto start = std::chrono::high_resolution_clock::now();

	while (active_) {
		SOCKET n = _setDescriptors();
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
		if (n == 0) {
			std::shared_lock lk(net_mutex_);
			socket_cv_.wait_for(lk, std::chrono::milliseconds(300), [this](){ return listeners_.size() > 0 || connection_count_ > 0; });
			//std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		//Wait for a network event or timeout in 3 seconds
		block.tv_sec = 0;
		block.tv_usec = 100000;
		selres = select(n+1u, &(impl_->sfdread_), 0, &(impl_->sfderror_), &block);

		// NOTE Nick: Is it possible that not all the recvs have been called before I
		// again reach a select call!? What are the consequences of this? A double recv attempt?

		//Some kind of error occured, it is usually possible to recover from this.
		if (selres < 0) {
			switch (errno) {
			case 9	: continue;  // Bad file descriptor = socket closed
			case 4	: continue;  // Interrupted system call ... no problem
			default	: LOG(WARNING) << "Unhandled select error: " << strerror(errno) << "(" << errno << ")";
			}
			continue;
		} else if (selres == 0) {
			// Timeout, nothing to do...
			continue;
		}

		SHARED_LOCK(net_mutex_,lk);

		//If connection request is waiting
		for (auto &l : listeners_) {
			if (l && l->is_listening() && FD_ISSET(l->fd(), &(impl_->sfdread_))) {
				std::unique_ptr<ftl::net::internal::SocketConnection> csock;
				try {
					csock = l->accept();
				} catch (const std::exception &ex) {
					LOG(ERROR) << "Connection failed: " << ex.what();
				}

				lk.unlock();

				if (csock) {
					auto p = std::make_shared<Peer>(std::move(csock), this, &disp_);
					_insertPeer(p);
				}

				lk.lock();
			}
		}


		// Also check each clients socket to see if any messages or errors are waiting
		for (size_t p=0; p<peers_.size(); ++p) {
			auto s = peers_[(p+phase_)%peers_.size()];

			if (s != NULL && s->isValid()) {
				// Note: It is possible that the socket becomes invalid after check but before
				// looking at the FD sets, therefore cache the original socket
				SOCKET sock = s->_socket();
				if (sock == INVALID_SOCKET) continue;

				if (FD_ISSET(sock, &impl_->sfderror_)) {
					if (s->socketError()) {
						s->close();
						continue;  // No point in reading data...
					}
				}
				//If message received from this client then deal with it
				if (FD_ISSET(sock, &impl_->sfdread_)) {
					s->data();
				}
			}
		}
		++phase_;
	}

	// Garbage is a threadsafe container, moving there first allows the destructor to be called
	// without the lock.
	{
		UNIQUE_LOCK(net_mutex_,lk);
		garbage_.insert(garbage_.end(), peers_.begin(), peers_.end());
		reconnects_.clear();
		peers_.clear();
		peer_by_uri_.clear();
		peer_ids_.clear();
		listeners_.clear();
	}

	garbage_.clear();
}

ftl::Handle Universe::onConnect(const std::function<bool(const std::shared_ptr<Peer>&)> &cb) {
	return on_connect_.on(cb);
}

ftl::Handle Universe::onDisconnect(const std::function<bool(const std::shared_ptr<Peer>&)> &cb) {
	return on_disconnect_.on(cb);
}

ftl::Handle Universe::onError(const std::function<bool(const std::shared_ptr<Peer>&, const ftl::net::Error &)> &cb) {
	return on_error_.on(cb);
}

std::shared_ptr<Peer> Universe::_findPeer(const Peer *p) {
	SHARED_LOCK(net_mutex_,lk);
	for (const auto &pp : peers_) {
		if (pp.get() == p) return pp;
	}
	return nullptr;
}

void Universe::_notifyConnect(Peer *p) {
	const auto ptr = _findPeer(p);

	// The peer could have been removed from valid peers already.
	if (!ptr) return;

	on_connect_.triggerAsync(ptr);
}

void Universe::_notifyDisconnect(Peer *p) {
	const auto ptr = _findPeer(p);
	if (!ptr) return;

	on_disconnect_.triggerAsync(ptr);
}

void Universe::_notifyError(Peer *p, const ftl::net::Error &e) {
	// TODO(Nick)
}
