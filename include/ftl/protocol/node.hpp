/**
 * @file node.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <ftl/uuid.hpp>
#include <ftl/protocol/frameid.hpp>
#include <ftl/lib/nlohmann/json_fwd.hpp>

namespace ftl {
namespace net {

class PeerBase;
using PeerPtr = std::shared_ptr<PeerBase>;

}

namespace protocol {

/**
 * @brief Type of node, web-service or regular.
 * 
 */
enum struct NodeType {
    kInvalid,
    kNode,
    kWebService,
};

/**
 * @brief Connection status.
 * 
 */
enum struct NodeStatus {
    kInvalid,       // no socket
    kConnecting,    // socket created, no handshake yet
    kConnected,     // connection fully established
    kDisconnected,  // socket closed, reconnect not possible
    kReconnecting   // socket closed, call reconnect() to try reconnecting
};

/**
 * @brief An API wrapper for a network connection. This object provides the
 * available RPC calls and connection status or control methods. Note that
 * releasing the shared pointer will not result in connection termination,
 * it must be closed and then released for the garbage collection to happen.
 * 
 */
class Node {
 public:
    /** PeerTcp for outgoing connection: resolve address and connect */
    explicit Node(const ftl::net::PeerPtr &impl);
    virtual ~Node();

    /**
     * Close the peer if open. Setting retry parameter to true will initiate
     * backoff retry attempts. This is used to deliberately close a connection
     * and not for error conditions where different close semantics apply.
     * 
     * @param retry Should reconnection be attempted?
     */
    virtual void close(bool retry = false);

    /**
     * @brief Check if the network connection is valid.
     * 
     * @return true 
     * @return false 
     */
    virtual bool isConnected() const;
    /**
     * Block until the connection and handshake has completed. You should use
     * onConnect callbacks instead of blocking, mostly this is intended for
     * the unit tests to keep them synchronous.
     * 
     * @return True if all connections were successful, false if timeout or error.
     */
    virtual bool waitConnection(int seconds = 1);

    /**
     * @internal
     * @brief Make a reconnect attempt. Called internally by Universe object.
     */
    virtual bool reconnect();

    virtual bool isOutgoing() const;

    /**
     * Test if the connection is valid. This returns true in all conditions
     * except where the socket has been disconnected permenantly, or was never
     * able to connect, perhaps due to an invalid address, or is in middle of a
     * reconnect attempt. (Valid states: kConnecting, kConnected)
     * 
     * Should return true only in cases when valid OS socket exists.
     */
    virtual bool isValid() const;

    /** node type */
    virtual NodeType getType() const { return NodeType::kNode; }

    /**
     * @brief Get current connection status.
     * 
     * @return NodeStatus 
     */
    virtual NodeStatus status() const;

    /**
     * @brief Get protocol version in use for this node.
     * 
     * @return uint32_t 
     */
    uint32_t getFTLVersion() const;
    uint8_t getFTLMajor() const { return getFTLVersion() >> 16; }
    uint8_t getFTLMinor() const { return (getFTLVersion() >> 8) & 0xFF; }
    uint8_t getFTLPatch() const { return getFTLVersion() & 0xFF; }

    /**
     * Get the sockets protocol, address and port as a url string. This will be
     * the same as the initial connection string on the client.
     */
    virtual std::string getURI() const;

    /**
     * Get the UUID for this peer.
     */
    virtual const ftl::UUID &id() const;

    /**
     * Get the peer id as a string.
     */
    virtual std::string to_string() const;

    /**
     * @brief Prevent this node auto-reconnecting.
     * 
     */
    virtual void noReconnect();

    /**
     * @brief Obtain a locally unique ID.
     * 
     * @return unsigned int 
     */
    virtual unsigned int localID();

    int connectionCount() const; // ???

    // === RPC Methods ===

    virtual void restart();

    virtual void shutdown();

    virtual bool hasStream(const std::string &uri);

    virtual void createStream(const std::string &uri, FrameID id);

    virtual nlohmann::json details();

    virtual int64_t ping();

    virtual nlohmann::json getConfig(const std::string &path);

    virtual void setConfig(const std::string &path, const nlohmann::json &value);

    virtual std::vector<std::string> listConfigs();

 protected:
    ftl::net::PeerPtr peer_; // move to NetPeer
};

using NodePtr = std::shared_ptr<Node>;

}  // namespace protocol
}  // namespace ftl
