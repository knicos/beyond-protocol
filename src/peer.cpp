/**
 * @file peer.cpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/lib/loguru.hpp>
#include <ftl/lib/ctpl_stl.hpp>

#include "common.hpp"

#include <ftl/uri.hpp>
#include <ftl/time.hpp>
#include "peer.hpp"
//#include <ftl/config.h>

#include "protocol/connection.hpp"

using ftl::net::internal::SocketConnection;

#include "universe.hpp"

#include <iostream>
#include <memory>
#include <algorithm>
#include <tuple>
#include <chrono>
#include <vector>

using std::tuple;
using std::get;
using ftl::net::Peer;
using ftl::URI;
using ftl::net::Dispatcher;
using std::chrono::seconds;
using ftl::net::Universe;
using ftl::net::Callback;
using std::vector;
using ftl::protocol::NodeStatus;
using ftl::protocol::NodeType;

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
	return sock_ && ((status_ == NodeStatus::kConnected) || (status_ == NodeStatus::kConnecting));
}

void Peer::_set_socket_options() {
	CHECK(net_);
	CHECK(sock_);
	
	// error printed by set methods (return value ignored)
	sock_->set_send_buffer_size(net_->getSendBufferSize(sock_->scheme()));
	sock_->set_recv_buffer_size(net_->getRecvBufferSize(sock_->scheme()));

	LOG(1)	<< "send buffer size: " << (sock_->get_send_buffer_size() >> 10) << "KiB, "
			<< "recv buffer size: " << (sock_->get_recv_buffer_size() >> 10) << "KiB";
}

void Peer::_send_handshake() {
	LOG(1)	<< "(" << (outgoing_ ? "connecting" : "listening")
			<< " peer) handshake sent, status: "
			<< (isConnected() ? "connected" : "connecting");
	
	send("__handshake__", ftl::net::kMagic, ftl::net::kVersion, net_->id());
}

void Peer::_process_handshake(uint64_t magic, uint32_t version, UUID pid) {
	/** Handshake protocol:
	 * 	(1). Listening side accepts connection and sends handshake.
	 * 	(2). Connecting side acknowledges by replying with own handshake and
	 * 		 sets status to kConnected.
	 * 	(3). Listening side receives handshake and sets status to kConnected.
	 */
	if (magic != ftl::net::kMagic) {
		_close(reconnect_on_protocol_error_);
		LOG(ERROR) << "invalid magic during handshake";
		
	} else {
		if (version != ftl::net::kVersion) LOG(WARNING) << "net protocol using different versions!";

		LOG(INFO) << "(" << (outgoing_ ? "connecting" : "listening")
				  << " peer) handshake received from remote";

		status_ = NodeStatus::kConnected;
		version_ = version;
		peerid_ = pid;

		if (outgoing_) {
			// only outgoing connection replies with handshake, listening socket
			// sends initial handshake on connect
			_send_handshake();
		}

		net_->_notifyConnect(this);
	}
}

void Peer::_bind_rpc() {
	// Install return handshake handler.
	bind("__handshake__", [this](uint64_t magic, uint32_t version, UUID pid) {
		_process_handshake(magic, version, pid);
	});

	bind("__disconnect__", [this]() {
		close(reconnect_on_remote_disconnect_);
		LOG(INFO) << "peer elected to disconnect: " << id().to_string();
	});

	bind("__ping__", [this]() {
		return ftl::time::get_time();
	});

}

Peer::Peer(std::unique_ptr<internal::SocketConnection> s, Universe* u, Dispatcher* d) :
		is_waiting_(true), outgoing_(false), local_id_(0),
		uri_("0"), status_(NodeStatus::kConnecting), can_reconnect_(false),
		net_(u), sock_(std::move(s)) {
	
	/* Incoming connection constructor */

	CHECK(sock_) << "incoming SocketConnection pointer null";
	_set_socket_options();
	_updateURI();
	
	disp_ = std::make_unique<Dispatcher>(d);
	
	_bind_rpc();
	_send_handshake();
	++net_->peer_instances_;
}

Peer::Peer(const ftl::URI& uri, Universe *u, Dispatcher *d) : 
		outgoing_(true), local_id_(0), uri_(uri),
		status_(NodeStatus::kInvalid), can_reconnect_(true), net_(u) {
	
	/* Outgoing connection constructor */

	// Must do to prevent receiving message before handlers are installed
	UNIQUE_LOCK(recv_mtx_,lk);

	disp_ = std::make_unique<Dispatcher>(d);

	_bind_rpc();
	_connect();
	++net_->peer_instances_;
}

void Peer::_connect() {
	dbg_recv_begin_ctr_ = 0;
	dbg_recv_end_ctr_ = 0;

	sock_ = ftl::net::internal::createConnection(uri_); // throws on bad uri
	_set_socket_options();
	sock_->connect(uri_); // throws on error
	status_ = NodeStatus::kConnecting;
	is_waiting_ = true;
}

/** Called from ftl::Universe::_periodic() */
bool Peer::reconnect() {

	if (status_ != NodeStatus::kReconnecting || !can_reconnect_) return false;

	URI uri(uri_);

	LOG(INFO) << "Reconnecting to " << uri_.to_string() << " ...";

	try {
		_connect();
		return true;
		
	} catch(const std::exception& ex) {
		LOG(ERROR) << "reconnect failed: " << ex.what();
	}

	return false;
}

void Peer::_updateURI() {
	// should be same as provided uri for connecting sockets, for connections
	// created by listening socket should generate some meaningful value
	uri_ = sock_->uri();
}

void Peer::rawClose() {	
	UNIQUE_LOCK(send_mtx_, lk_send);
	UNIQUE_LOCK(recv_mtx_, lk_recv);
	sock_->close();
	status_ = NodeStatus::kDisconnected;
}

void Peer::close(bool retry) {
	// Attempt to inform about disconnect
	if (sock_->is_valid()) { send("__disconnect__"); }

	UNIQUE_LOCK(send_mtx_, lk_send);
	UNIQUE_LOCK(recv_mtx_, lk_recv);

	_close(retry);
}

void Peer::_close(bool retry) {
	if (status_ != NodeStatus::kConnected && status_ != NodeStatus::kConnecting) return;
	status_ = NodeStatus::kDisconnected;
	
	if (sock_->is_valid()) {
		net_->_notifyDisconnect(this);
		sock_->close();
	}

	// Attempt auto reconnect?
	if (retry && can_reconnect_) {
		status_ = NodeStatus::kReconnecting;
	
	} else {
		status_ = NodeStatus::kDisconnected;
	}
}

bool Peer::socketError() {
	// TODO	implement in to SocketConnection and report if any
	// 		protocol errors as well
	
	// Must close before log since log may try to send over net causing
	// more socket errors...
	
	_close(reconnect_on_socket_error_);
	
	LOG(ERROR) << "Connection error: " << uri_.to_string() ; // << " - error " << err;
	return true;
}

void Peer::error(int e) {
	
}

NodeType Peer::getType() const {
	if ((uri_.getScheme() == URI::SCHEME_WS)
		|| (uri_.getScheme() == URI::SCHEME_WSS)) {
		
		return NodeType::kWebService;
	}
	return NodeType::kNode;
}

void Peer::data() {
	UNIQUE_LOCK(recv_mtx_,lk);

	if (!sock_->is_valid()) { return; }

	int rc = 0;

	recv_buf_.reserve_buffer(kMaxMessage);

	if (recv_buf_.buffer_capacity() < (kMaxMessage / 10)) {
		LOG(WARNING) << "Net buffer at capacity";
		return;
	}

	int cap = static_cast<int>(recv_buf_.buffer_capacity());
	// Buffer acquired, recv can be called outside the lock.

	// TODO: Check if this is actually correct. If two threads call recv()
	//       outside the lock and the second thread to call recv() re-acquires 
	//       the lock first, buffer_consumed() will be called first with second
	//       thread's number of bytes (rc).
	auto ctr = dbg_recv_begin_ctr_++;
	lk.unlock();

	try {
		rc = sock_->recv(recv_buf_.buffer(), recv_buf_.buffer_capacity());
		
		if (rc >= cap - 1) {
			LOG(WARNING) << "More than buffers worth of data received"; 
		}
		if (cap < (kMaxMessage / 10)) {
			LOG(WARNING) << "NO BUFFER";
		}

	} catch (ftl::exception& ex) {
		LOG(ERROR) << "connection error: " << ex.what() << ", disconnected";
		close(reconnect_on_protocol_error_);
		return;

	} catch (...) {
		LOG(FATAL) << "unknown exception from SocketConnection::recv()";
	}

	if (rc == 0) { // retry later
		CHECK(sock_->is_valid() == false);
		//close(reconnect_on_socket_error_);
		return;
	}
	if (rc < 0) { // error so close peer
		sock_->close();
		close(reconnect_on_socket_error_);
		return;
	}

	// Re-acquire lock before processing buffer further
	lk.lock();

	// buffer_consumed() will not be updated with correct value, race condition
	// described above has occurred
	CHECK(ctr == dbg_recv_end_ctr_++) << "race in Peer::data()";

	recv_buf_.buffer_consumed(rc);
	
	if (is_waiting_) {
		is_waiting_ = false;
		lk.unlock();

		++job_count_;

		ftl::pool.push([this](int id) {
			try {
				_data();
			} catch (const std::exception &e) {
				LOG(ERROR) << "Error processing packet: " << e.what();	
			}
			--job_count_;
		});
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
	UNIQUE_LOCK(recv_mtx_, lk);

	// msgpack::object is valid as long as handle is
	msgpack::object_handle msg_handle;

	try {
		bool has_next = _has_next() && recv_buf_.next(msg_handle);
		if (!has_next) {
			is_waiting_ = true;
			return false;
		}
	} catch (const std::exception& ex) {
		LOG(ERROR) << "decoding error: " << ex.what() << ", disconnected";
		_close(reconnect_on_protocol_error_);
		return false;
	}

	lk.unlock();

	msgpack::object obj = msg_handle.get();
	
	// more data: repeat (loop)
	++job_count_;
	ftl::pool.push([this](int id) {
		try {
			_data();
		} catch (const std::exception &e) {
			LOG(ERROR) << "Error processing packet: " << e.what();	
		}
		--job_count_;
	});

	if (status_ == NodeStatus::kConnecting) {
		// If not connected, must lock to make sure no other thread performs this step
		lk.lock();

		// Verify still not connected after lock
		if (status_ == NodeStatus::kConnecting) {
			// First message must be a handshake
			try {
				tuple<uint32_t, std::string, msgpack::object> hs;
				obj.convert(hs);
				
				if (get<1>(hs) != "__handshake__") {
					LOG(WARNING) << "Missing handshake - got '" << get<1>(hs) << "'";

					// Allow a small delay in case another thread is doing the handshake
					lk.unlock();
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					if (status_ == NodeStatus::kConnecting) {
						LOG(ERROR) << "failed to get handshake";
						close(reconnect_on_protocol_error_);
						lk.lock();
						return false;
					}
				} else {
					// Must handle immediately with no other thread able
					// to read next message before completion.
					// The handshake handler must not block.
					disp_->dispatch(*this, obj);
					return true;
				}
			} catch(...) {
				LOG(WARNING) << "Bad first message format... waiting";
				// Allow a small delay in case another thread is doing the handshake

				lk.unlock();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				if (status_ == NodeStatus::kConnecting) {
					LOG(ERROR) << "failed to get handshake";
					close(reconnect_on_protocol_error_);
					return false;
				}
			}
		} else {
			lk.unlock();
		}
	}
	
	disp_->dispatch(*this, obj);

	// Lock again before freeing msg_handle (destruction order).
	// msgpack::object_handle destructor modifies recv_buffer_
	lk.lock();
	return true;
}

void Peer::_dispatchResponse(uint32_t id, const std::string &name, msgpack::object &res) {
	// TODO: Handle error reporting...
	UNIQUE_LOCK(cb_mtx_,lk);
	if (callbacks_.count(id) > 0) {
		
		// Allow for unlock before callback
		auto cb = std::move(callbacks_[id]);
		callbacks_.erase(id);
		lk.unlock();

		// Call the callback with unpacked return value
		try {
			(*cb)(res);
		} catch(std::exception &e) {
			LOG(ERROR) << "Exception in RPC response: " << e.what();
		}
	} else {
		LOG(WARNING) << "Missing RPC callback for result - discarding: " << name;
	}
}

void Peer::cancelCall(int id) {
	UNIQUE_LOCK(cb_mtx_,lk);
	if (callbacks_.count(id) > 0) {
		callbacks_.erase(id);
	}
}

void Peer::_sendResponse(uint32_t id, const std::string &name, const msgpack::object &res) {
	Dispatcher::response_t res_obj = std::make_tuple(1,id,name,res);
	UNIQUE_LOCK(send_mtx_,lk);
	msgpack::pack(send_buf_, res_obj);
	_send();
}

void Peer::_waitCall(int id, std::condition_variable &cv, bool &hasreturned, const std::string &name) {
	std::mutex m;

	int64_t beginat = ftl::time::get_time();
	std::function<void(int)> j;
	while (!hasreturned) {
		// Attempt to do a thread pool job if available
		if ((bool)(j=ftl::pool.pop())) {
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
	else if (status_ != NodeStatus::kConnecting) return false;
	
	std::mutex m;
	std::unique_lock<std::mutex> lk(m);
	std::condition_variable cv;

	auto h = net_->onConnect([this, &cv](const std::shared_ptr<Peer> &p) {
		if (p.get() == this) {
			cv.notify_one();
		}
		return true;
	});

	cv.wait_for(lk, seconds(s), [this]() { return status_ == NodeStatus::kConnected;});
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
			LOG(ERROR) << "writev() failed";
			return c;
		}
	
		ssize_t sz = 0; for (size_t i = 0; i < send_buf_.vector_size(); i++) {
			sz += send_buf_.vector()[i].iov_len;
		} 
		if (c != sz) {
			LOG(ERROR) << "writev(): incomplete send";
			_close(reconnect_on_socket_error_);
		}

		send_buf_.clear();

	} catch (std::exception& ex) {
		LOG(ERROR) << "exception while sending data, closing connection";
		_close(reconnect_on_protocol_error_);
	}
	
	return c;
}

Peer::~Peer() {
	--net_->peer_instances_;
	{
		UNIQUE_LOCK(send_mtx_,lk1);
		UNIQUE_LOCK(recv_mtx_,lk2);
		_close(false);
	}

	// Prevent deletion if there are any jobs remaining
	while (job_count_ > 0 && ftl::pool.size() > 0) {
		LOG(INFO) << "waiting for peer jobs...";
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}
