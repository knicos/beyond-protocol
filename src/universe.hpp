/**
 * @file universe.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <msgpack.hpp>

#include <ftl/protocol.hpp>
#include "peer.hpp"
#include "dispatcher.hpp"
#include <ftl/uuid.hpp>
#include <ftl/uri.hpp>
#include <ftl/threads.hpp>
#include <ftl/handle.hpp>

#include <nlohmann/json_fwd.hpp>

#include <vector>
#include <list>
#include <string>
#include <thread>
#include <map>

namespace ftl {
namespace net {

struct Error {
	int errno;
};

struct ReconnectInfo {
	int tries;
	float delay;
	std::shared_ptr<Peer> peer;
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
	friend class Peer;

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
	std::shared_ptr<Peer> connect(const std::string &addr);
	std::shared_ptr<Peer> connect(const ftl::URI &addr);

	bool isConnected(const ftl::URI &uri);
	bool isConnected(const std::string &s);
	
	size_t numberOfPeers() const { return peers_.size(); }

	/**
	 * Will block until all currently registered connnections have completed.
	 * You should not use this, but rather use onConnect.
	 */
	int waitConnections();
	
	/** get peer pointer by peer UUID, returns nullptr if not found */
	std::shared_ptr<Peer> getPeer(const ftl::UUID &pid) const;
	/** get webservice peer pointer, returns nullptr if not connected to webservice */
	std::shared_ptr<Peer> getWebService() const;

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
	int asyncCall(const UUID &pid, const std::string &name,
			std::function<void(const R&)> cb,
			ARGS... args);
	
	template <typename... ARGS>
	bool send(const UUID &pid, const std::string &name, ARGS... args);

	template <typename... ARGS>
	int try_send(const UUID &pid, const std::string &name, ARGS... args);

	template <typename R, typename... ARGS>
	std::optional<R> findOne(const std::string &name, ARGS... args);

	template <typename R, typename... ARGS>
	std::vector<R> findAll(const std::string &name, ARGS... args);

	void setLocalID(const ftl::UUID &u) { this_peer = u; };
	const ftl::UUID &id() const { return this_peer; }

	// --- Event Handlers ------------------------------------------------------

	Callback onConnect(const std::function<void(const std::shared_ptr<ftl::net::Peer>&)>&);
	Callback onDisconnect(const std::function<void(const std::shared_ptr<ftl::net::Peer>&)>&);
	Callback onError(const std::function<void(const std::shared_ptr<Peer>&, const ftl::net::Error &)>&);

	void removeCallback(Callback cbid);

	size_t getSendBufferSize(ftl::URI::scheme_t s);
	size_t getRecvBufferSize(ftl::URI::scheme_t s);

	static inline std::shared_ptr<Universe> getInstance() { return instance_; }
	
private:
	void _run();
	SOCKET _setDescriptors(); // TODO: move to implementation
	void _installBindings();
	void _installBindings(const std::shared_ptr<ftl::net::Peer>&);
	//bool _subscribe(const std::string &res);
	void _cleanupPeers();
	void _notifyConnect(ftl::net::Peer *);
	void _notifyDisconnect(ftl::net::Peer *);
	void _notifyError(ftl::net::Peer *, const ftl::net::Error &);
	void _periodic();
	
	static void __start(Universe *u);
	
	bool active_;
	ftl::UUID this_peer;
	mutable SHARED_MUTEX net_mutex_;
	RECURSIVE_MUTEX handler_mutex_;
	
	std::unique_ptr<NetImplDetail> impl_;
	
	std::vector<std::unique_ptr<ftl::net::internal::SocketServer>> listeners_;
	std::vector<std::shared_ptr<ftl::net::Peer>> peers_;
	std::unordered_map<std::string, size_t> peer_by_uri_;
	std::map<ftl::UUID, size_t> peer_ids_;

	ftl::net::Dispatcher disp_;
	std::list<ReconnectInfo> reconnects_;
	size_t phase_;
	std::list<std::shared_ptr<ftl::net::Peer>> garbage_;
	ftl::Handle garbage_timer_;

	size_t send_size_;
	size_t recv_size_;
	double periodic_time_;
	int reconnect_attempts_;

	struct ConnHandler {
		Callback id;
		std::function<void(const std::shared_ptr<ftl::net::Peer>&)> h;
	};

	struct ErrHandler {
		Callback id;
		std::function<void(const std::shared_ptr<ftl::net::Peer>&, const ftl::net::Error &)> h;
	};

	// Handlers
	std::list<ConnHandler> on_connect_;
	std::list<ConnHandler> on_disconnect_;
	std::list<ErrHandler> on_error_;

	static Callback cbid__;
	static std::shared_ptr<Universe> instance_;

	// NOTE: Must always be last member
	std::thread thread_;
};

//------------------------------------------------------------------------------

template <typename F>
void Universe::bind(const std::string &name, F func) {
	UNIQUE_LOCK(net_mutex_,lk);
	disp_.bind(name, func,
		typename ftl::internal::func_kind_info<F>::result_kind(),
		typename ftl::internal::func_kind_info<F>::args_kind(),
		typename ftl::internal::func_kind_info<F>::has_peer());
}

template <typename... ARGS>
void Universe::broadcast(const std::string &name, ARGS... args) {
	SHARED_LOCK(net_mutex_,lk);
	for (auto p : peers_) {
		if (!p->waitConnection()) continue;
		p->send(name, args...);
	}
}

template <typename R, typename... ARGS>
std::optional<R> Universe::findOne(const std::string &name, ARGS... args) {
	struct SharedData {
		std::atomic_bool hasreturned = false;
		std::mutex m;
		std::condition_variable cv;
		std::optional<R> result;
	};

	auto sdata = std::make_shared<SharedData>();

	auto handler = [sdata](const std::optional<R> &r) {
		std::unique_lock<std::mutex> lk(sdata->m);
		if (r && !sdata->hasreturned) {
			sdata->hasreturned = true;
			sdata->result = r;
		}
		lk.unlock();
		sdata->cv.notify_one();
	};

	{
		SHARED_LOCK(net_mutex_,lk);
		for (auto p : peers_) {
			if (!p->waitConnection()) continue;
			p->asyncCall<std::optional<R>>(name, handler, args...);
		}
	}
	
	// Block thread until async callback notifies us
	std::unique_lock<std::mutex> llk(sdata->m);
	sdata->cv.wait_for(llk, std::chrono::seconds(1), [sdata] {
		return (bool)sdata->hasreturned;
	});

	return sdata->result;
}

template <typename R, typename... ARGS>
std::vector<R> Universe::findAll(const std::string &name, ARGS... args) {
	struct SharedData {
		std::atomic_int returncount = 0;
		std::atomic_int sentcount = 0;
		std::mutex m;
		std::condition_variable cv;
		std::vector<R> results;
	};

	auto sdata = std::make_shared<SharedData>();

	auto handler = [sdata](const std::vector<R> &r) {
		std::unique_lock<std::mutex> lk(sdata->m);
		++sdata->returncount;
		sdata->results.insert(sdata->results.end(), r.begin(), r.end());
		lk.unlock();
		sdata->cv.notify_one();
	};

	{
		SHARED_LOCK(net_mutex_,lk);
		for (auto p : peers_) {
			if (!p->waitConnection()) continue;
			++sdata->sentcount;
			p->asyncCall<std::vector<R>>(name, handler, args...);
		}
	}
	
	std::unique_lock<std::mutex> llk(sdata->m);
	sdata->cv.wait_for(llk, std::chrono::seconds(1), [sdata]{return sdata->returncount == sdata->sentcount; });
	return sdata->results;
}

template <typename R, typename... ARGS>
R Universe::call(const ftl::UUID &pid, const std::string &name, ARGS... args) {
	Peer *p = getPeer(pid);
	if (p == nullptr || !p->isConnected()) {
		if (p == nullptr) throw FTL_Error("Attempting to call an unknown peer : " << pid.to_string());
		else throw FTL_Error("Attempting to call an disconnected peer : " << pid.to_string());
	}
	return p->call<R>(name, args...);
}

template <typename R, typename... ARGS>
int Universe::asyncCall(const ftl::UUID &pid, const std::string &name, std::function<void(const R&)> cb, ARGS... args) {
	Peer *p = getPeer(pid);
	if (p == nullptr || !p->isConnected()) {
		if (p == nullptr) throw FTL_Error("Attempting to call an unknown peer : " << pid.to_string());
		else throw FTL_Error("Attempting to call an disconnected peer : " << pid.to_string());
	}
	return p->asyncCall(name, cb, args...);
}

template <typename... ARGS>
bool Universe::send(const ftl::UUID &pid, const std::string &name, ARGS... args) {
	Peer *p = getPeer(pid);
	if (p == nullptr) {
		LOG(WARNING) << "Attempting to call an unknown peer : " << pid.to_string();
		return false;
	}

	return p->isConnected() && p->send(name, args...) > 0;
}

template <typename... ARGS>
int Universe::try_send(const ftl::UUID &pid, const std::string &name, ARGS... args) {
	Peer *p = getPeer(pid);
	if (p == nullptr) {
		//DLOG(WARNING) << "Attempting to call an unknown peer : " << pid.to_string();
		return false;
	}

	return (p->isConnected()) ? p->try_send(name, args...) : -1;
}

};  // namespace net
};  // namespace ftl
