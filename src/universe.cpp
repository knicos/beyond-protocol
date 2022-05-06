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

Callback ftl::net::Universe::cbid__ = 0;
Universe *Universe::instance_ = nullptr;

Universe::Universe() :
		active_(true),
		this_peer(ftl::protocol::id),
		impl_(new ftl::net::NetImplDetail()),
		phase_(0),
		periodic_time_(1.0),
		reconnect_attempts_(50),
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

	if (instance_ != nullptr) LOG(FATAL) << "Multiple net instances";
	instance_ = this;
}

Universe::~Universe() {
	shutdown();
}

size_t Universe::getSendBufferSize(ftl::URI::scheme_t s) {
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

	active_ = false;
	thread_.join();
	
	UNIQUE_LOCK(net_mutex_, lk);

	for (auto &s : peers_) {
		s->rawClose();
	}
	
	peers_.clear();
	
	for (auto &l : listeners_) {
		l->close();
	}
	
	listeners_.clear();
}

bool Universe::listen(const ftl::URI &addr) {
	try {
		auto l = create_listener(addr);
		l->bind();

		UNIQUE_LOCK(net_mutex_,lk);
		LOG(INFO) << "listening on " << l->uri().to_string();
		listeners_.push_back(std::move(l));
		return true;

	} catch (const std::exception &ex) {
		LOG(ERROR) << "Can't listen " << addr.to_string() << ", " << ex.what();
		return false;
	}
}

std::vector<ftl::URI> Universe::getListeningURIs() {
	UNIQUE_LOCK(net_mutex_, lk);
	std::vector<ftl::URI> uris;
	for (auto& l : listeners_) {
		uris.push_back(l->uri());
	}
	return uris;
}

bool Universe::isConnected(const ftl::URI &uri) {
	UNIQUE_LOCK(net_mutex_,lk);
	return (peer_by_uri_.find(uri.getBaseURI()) != peer_by_uri_.end());
}

bool Universe::isConnected(const std::string &s) {
	ftl::URI uri(s);
	return isConnected(uri);
}

Peer *Universe::connect(const ftl::URI &u) {

	// Check if already connected or if self (when could this happen?)
	{
		UNIQUE_LOCK(net_mutex_,lk);
		if (peer_by_uri_.find(u.getBaseURI()) != peer_by_uri_.end()) {
			return peer_by_uri_.at(u.getBaseURI());
		}

		if (u.getHost() == "localhost" || u.getHost() == "127.0.0.1") {
			for (const auto &l : listeners_) {
				if (l->port() == u.getPort()) {
					throw FTL_Error("Cannot connect to self");
				} // TODO extend api
			}
		}
	}

	Peer* p;
	
	p = new Peer(u, this, &disp_);
	
	if (p->status() != NodeStatus::kInvalid) {
		UNIQUE_LOCK(net_mutex_,lk);
		peers_.push_back(p);
		peer_by_uri_[u.getBaseURI()] = p;
	}
	else {
		LOG(ERROR) << "Peer in invalid state";
	}
	
	_installBindings(p);
	return p;
}

Peer* Universe::connect(const std::string& addr) {
	return connect(ftl::URI(addr));
}

void Universe::unbind(const std::string &name) {
	UNIQUE_LOCK(net_mutex_,lk);
	disp_.unbind(name);
}

int Universe::waitConnections() {
	SHARED_LOCK(net_mutex_, lk);
	int count = 0;
	for (auto p : peers_) {
		if (p->waitConnection()) count++;
	}
	return count;
}

socket_t Universe::_setDescriptors() {
	//Reset all file descriptors
	FD_ZERO(&impl_->sfdread_);
	FD_ZERO(&impl_->sfderror_);

	socket_t n = 0;

	// TODO: Shared lock for some of the time...
	UNIQUE_LOCK(net_mutex_, lk);

	//Set file descriptor for the listening sockets.
	for (auto &l : listeners_) {
		if (l) {
			FD_SET(l->fd(), &impl_->sfdread_);
			FD_SET(l->fd(), &impl_->sfderror_);
			n = std::max<socket_t>(n, l->fd());
		}
	}

	//Set the file descriptors for each client
	for (auto s : peers_) {
		// NOTE: s->isValid() should return true only and only if a valid OS
		//       socket exists.

		if (s && s->isValid()) {
			n = std::max<socket_t>(n, s->_socket());
			FD_SET(s->_socket(), &impl_->sfdread_);
			FD_SET(s->_socket(), &impl_->sfderror_);
		}
	}
	_cleanupPeers();

	return n;
}

void Universe::_installBindings(Peer *p) {
	
}

void Universe::_installBindings() {

}

// Note: should be called inside a net lock
void Universe::_cleanupPeers() {
	auto i = peers_.begin();
	while (i != peers_.end()) {
		if (!(*i)->isValid() ||
			(*i)->status() == NodeStatus::kReconnecting ||
			(*i)->status() == NodeStatus::kDisconnected) {
			
			Peer *p = *i;
			LOG(INFO) << "Removing disconnected peer: " << p->id().to_string();
			_notifyDisconnect(p);

			auto ix = peer_ids_.find(p->id());
			if (ix != peer_ids_.end()) peer_ids_.erase(ix);

			for (auto i=peer_by_uri_.begin(); i != peer_by_uri_.end(); ++i) {
				if (i->second == p) {
					peer_by_uri_.erase(i);
					break;
				}
			}

			i = peers_.erase(i);

			if (p->status() == NodeStatus::kReconnecting) {
				reconnects_.push_back({reconnect_attempts_, 1.0f, p});
			} else {
				garbage_.push_back(p);
			}
		} else {
			i++;
		}
	}
}

Peer *Universe::getPeer(const UUID &id) const {
	SHARED_LOCK(net_mutex_,lk);
	auto ix = peer_ids_.find(id);
	if (ix == peer_ids_.end()) return nullptr;
	else return ix->second;
}

Peer *Universe::getWebService() const {
	SHARED_LOCK(net_mutex_,lk);
	for (auto* p : peers_) {
		if (p->getType() == NodeType::kWebService) {
			return p;
		}
	}
	return nullptr;
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
			UNIQUE_LOCK(net_mutex_,lk);
			peers_.push_back((*i).peer);
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

void Universe::__start(Universe * u) {
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

		// Do periodics
		auto now = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = now - start;
		if (elapsed.count() >= periodic_time_) {
			start = now;
			_periodic();
		}

		// It is an error to use "select" with no sockets ... so just sleep
		if (n == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
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

		{
			// TODO:(Nick) Shared lock unless connection is made
			UNIQUE_LOCK(net_mutex_,lk);

			//If connection request is waiting
			for (auto &l : listeners_) {
				if (l && l->is_listening()) {
					if (FD_ISSET(l->fd(), &(impl_->sfdread_))) {

						try {
							auto csock = l->accept();
							auto p = new Peer(std::move(csock), this, &disp_);
							peers_.push_back(p);

						} catch (const std::exception &ex) {
							LOG(ERROR) << "Connection failed: " << ex.what();
						}
					}
				}
			}
		}

		{
			SHARED_LOCK(net_mutex_, lk);

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
	}
}

Callback Universe::onConnect(const std::function<void(ftl::net::Peer*)> &cb) {
	UNIQUE_LOCK(handler_mutex_,lk);
	Callback id = cbid__++;
	on_connect_.push_back({id, cb});
	return id;
}

Callback Universe::onDisconnect(const std::function<void(ftl::net::Peer*)> &cb) {
	UNIQUE_LOCK(handler_mutex_,lk);
	Callback id = cbid__++;
	on_disconnect_.push_back({id, cb});
	return id;
}

Callback Universe::onError(const std::function<void(ftl::net::Peer*, const ftl::net::Error &)> &cb) {
	UNIQUE_LOCK(handler_mutex_,lk);
	Callback id = cbid__++;
	on_error_.push_back({id, cb});
	return id;
}

void Universe::removeCallback(Callback cbid) {
	UNIQUE_LOCK(handler_mutex_,lk);
	{
		auto i = on_connect_.begin();
		while (i != on_connect_.end()) {
			if ((*i).id == cbid) {
				i = on_connect_.erase(i);
			} else {
				i++;
			}
		}
	}

	{
		auto i = on_disconnect_.begin();
		while (i != on_disconnect_.end()) {
			if ((*i).id == cbid) {
				i = on_disconnect_.erase(i);
			} else {
				i++;
			}
		}
	}

	{
		auto i = on_error_.begin();
		while (i != on_error_.end()) {
			if ((*i).id == cbid) {
				i = on_error_.erase(i);
			} else {
				i++;
			}
		}
	}
}

void Universe::_notifyConnect(Peer *p) {
	UNIQUE_LOCK(handler_mutex_,lk);
	peer_ids_[p->id()] = p;

	for (auto &i : on_connect_) {
		try {
			i.h(p);
		} catch(...) {
			LOG(ERROR) << "Exception inside OnConnect hander: " << i.id;
		}
	}
}

void Universe::_notifyDisconnect(Peer *p) {
	// In all cases, should already be locked outside this function call
	//unique_lock<mutex> lk(net_mutex_);
	UNIQUE_LOCK(handler_mutex_,lk);
	for (auto &i : on_disconnect_) {
		try {
			i.h(p);
		} catch(...) {
			LOG(ERROR) << "Exception inside OnDisconnect hander: " << i.id;
		}
	}
}

void Universe::_notifyError(Peer *p, const ftl::net::Error &e) {
	// TODO(Nick)
}
