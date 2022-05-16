/**
 * @file self.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <list>
#include <ftl/uuid.hpp>
#include <ftl/uri.hpp>
#include <ftl/handle.hpp>
#include <ftl/protocol/error.hpp>

namespace ftl {
namespace net {
class Universe;
}

namespace protocol {

class Node;
class Stream;

/**
 * @brief A wrapper providing RPC API and local node management. Internally the
 * Self instance is responsible for handling all network operations. Typically
 * there is just a single instance of this class, but more can be created for
 * testing purposes.
 * 
 */
class Self {
 public:
    /** Peer for outgoing connection: resolve address and connect */
    explicit Self(const std::shared_ptr<ftl::net::Universe> &impl);
    virtual ~Self();

    /**
     * @brief Connect to another host from this Self instance. Usually the
     * namespace method can be used instead.
     * 
     * @param uri A TCP URI.
     * @return std::shared_ptr<ftl::protocol::Node> 
     */
    std::shared_ptr<ftl::protocol::Node> connectNode(const std::string &uri);

    /**
     * @brief Create a new stream. Use the namespace method if possible.
     * 
     * @param uri A file:// or ftl:// URI.
     * @return std::shared_ptr<ftl::protocol::Stream> 
     */
    std::shared_ptr<ftl::protocol::Stream> createStream(const std::string &uri);

    /**
     * @brief Open an existing stream. Use the namespace method if possible.
     * 
     * @param uri A file:// or ftl:// URI
     * @return std::shared_ptr<ftl::protocol::Stream> 
     */
    std::shared_ptr<ftl::protocol::Stream> getStream(const std::string &uri);

    void start();

    /**
     * Open a new listening port on a given interface.
     *   eg. "tcp://localhost:9000"
     * @param addr URI giving protocol, interface and port
     */
    bool listen(const ftl::URI &addr);

    /**
     * @brief Open a new listening port on a given interface.
     * 
     * @param addr as a URI string
     * @return true if listening was started
     * @return false if the URI is invalid or could not open the port
     */
    bool listen(const std::string &addr) { return listen(ftl::URI(addr)); }

    /**
     * @brief Get the list of all listening addresses and ports.
     * 
     * @return std::vector<ftl::URI> 
     */
    std::vector<ftl::URI> getListeningURIs();

    /**
     * Essential to call this before destroying anything that registered
     * callbacks or binds for RPC. It will terminate all connections and
     * stop any network activity but without deleting the net object.
     */
    void shutdown();

    /**
     * @brief Query if a node is connected.
     * 
     * @param uri address and port of the node
     * @return true if connected
     * @return false if not found
     */
    bool isConnected(const ftl::URI &uri);
    bool isConnected(const std::string &s);

    /**
     * @brief Number of currently available nodes. Note that they may be
     * in a disconnected or errored state until garbage collected.
     * 
     * @return size_t 
     */
    size_t numberOfNodes() const;

    /**
     * @brief Will block until all currently registered connnections have completed.
     * You should not use this, but rather use onConnect.
     */
    int waitConnections(int seconds = 1);

    /** get peer pointer by peer UUID, returns nullptr if not found */
    std::shared_ptr<ftl::protocol::Node> getNode(const ftl::UUID &pid) const;
    /** get webservice peer pointer, returns nullptr if not connected to webservice */
    std::shared_ptr<ftl::protocol::Node>  getWebService() const;
    /**
     * @brief Get a list of all available nodes. Not all of these may actually be
     * connected currently.
     * 
     * @return std::list<std::shared_ptr<ftl::protocol::Node>> 
     */
    std::list<std::shared_ptr<ftl::protocol::Node>> getNodes() const;

    /**
     * @brief Register a callback for new node connections.
     * 
     * @return ftl::Handle 
     */
    ftl::Handle onConnect(const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&)>&);

    /**
     * @brief Register a callback for node disconnects.
     * 
     * @return ftl::Handle 
     */
    ftl::Handle onDisconnect(const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&)>&);

    /**
     * @brief Register a callback for any node or network errors. Note that the node pointer can
     * be null if the error was not associated with a specific node.
     * 
     * @return ftl::Handle 
     */
    ftl::Handle onError(
        const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&,
        ftl::protocol::Error,
        const std::string &)>&);

    // Used for testing
    ftl::net::Universe *getUniverse() const { return universe_.get(); }

 protected:
    std::shared_ptr<ftl::net::Universe> universe_;
};

}  // namespace protocol
}  // namespace ftl
