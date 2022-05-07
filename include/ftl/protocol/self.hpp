/**
 * @file node.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <ftl/uuid.hpp>
#include <ftl/uri.hpp>
#include <ftl/handle.hpp>

#include <memory>

namespace ftl {
namespace net {
class Universe;
}

namespace protocol {

struct Error {
	int errno;
};

class Self {	
	public:
	/** Peer for outgoing connection: resolve address and connect */
	explicit Self(const std::shared_ptr<ftl::net::Universe> &impl);
	virtual ~Self();
	
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

	bool isConnected(const ftl::URI &uri);
	bool isConnected(const std::string &s);
	
	size_t numberOfNodes() const;

	/**
	 * Will block until all currently registered connnections have completed.
	 * You should not use this, but rather use onConnect.
	 */
	int waitConnections();
	
	/** get peer pointer by peer UUID, returns nullptr if not found */
	std::shared_ptr<ftl::protocol::Node> getNode(const ftl::UUID &pid) const;
	/** get webservice peer pointer, returns nullptr if not connected to webservice */
	std::shared_ptr<ftl::protocol::Node>  getWebService() const;

	ftl::Handle onConnect(const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&)>&);
	ftl::Handle onDisconnect(const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&)>&);
	ftl::Handle onError(const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&, const ftl::protocol::Error &)>&);

	protected:
	std::shared_ptr<ftl::net::Universe> universe_;
};

}
}
