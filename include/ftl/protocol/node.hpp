/**
 * @file node.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <ftl/uuid.hpp>

#include <memory>

namespace ftl {
namespace net {
class Peer;
}

namespace protocol {

enum struct NodeType {
    kNode,
    kWebService,
};

enum struct NodeStatus {
    kInvalid,		// no socket
    kConnecting,	// socket created, no handshake yet
    kConnected,		// connection fully established
    kDisconnected,	// socket closed, reconnect not possible
    kReconnecting	// socket closed, call reconnect() to try reconnecting
};

/**
 * To be constructed using the Universe::connect() method and not to be
 * created directly.
 */
class Node {	
	public:
	/** Peer for outgoing connection: resolve address and connect */
	explicit Node(const std::shared_ptr<ftl::net::Peer> &impl);
	virtual ~Node();
	
	/**
	 * Close the peer if open. Setting retry parameter to true will initiate
	 * backoff retry attempts. This is used to deliberately close a connection
	 * and not for error conditions where different close semantics apply.
	 * 
	 * @param retry Should reconnection be attempted?
	 */
	void close(bool retry=false);

	bool isConnected() const;
	/**
	 * Block until the connection and handshake has completed. You should use
	 * onConnect callbacks instead of blocking, mostly this is intended for
	 * the unit tests to keep them synchronous.
	 * 
	 * @return True if all connections were successful, false if timeout or error.
	 */
	bool waitConnection(int seconds = 1);

	/**
	 * Make a reconnect attempt. Called internally by Universe object.
	 */
	bool reconnect();

	bool isOutgoing() const;
	
	/**
	 * Test if the connection is valid. This returns true in all conditions
	 * except where the socket has been disconnected permenantly, or was never
	 * able to connect, perhaps due to an invalid address, or is in middle of a
	 * reconnect attempt. (Valid states: kConnecting, kConnected)
	 * 
	 * Should return true only in cases when valid OS socket exists.
	 */
	bool isValid() const;
	
	/** node type */
	virtual NodeType getType() const { return NodeType::kNode; }

	NodeStatus status() const;
	
	uint32_t getFTLVersion() const;
	uint8_t getFTLMajor() const { return getFTLVersion() >> 16; }
	uint8_t getFTLMinor() const { return (getFTLVersion() >> 8) & 0xFF; }
	uint8_t getFTLPatch() const { return getFTLVersion() & 0xFF; }
	
	/**
	 * Get the sockets protocol, address and port as a url string. This will be
	 * the same as the initial connection string on the client.
	 */
	std::string getURI() const;
	
	/**
	 * Get the UUID for this peer.
	 */
	const ftl::UUID &id() const;
	
	/**
	 * Get the peer id as a string.
	 */
	std::string to_string() const;
			
	bool isWaiting() const;

	void noReconnect();

	unsigned int localID();

	protected:
	std::shared_ptr<ftl::net::Peer> peer_;
};

}
}
