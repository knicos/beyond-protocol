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
#include <ftl/protocol/frameid.hpp>

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
     * @brief Get the maximum allowed number of connections. Any attempt to connect more
     * peers will result in them being rejected.
     * 
     * @return size_t 
     */
    size_t getMaxConnections() const;

    /**
     * @brief Set the maximum allowed connections. This should only be changed before
     * there are any active connections, resizing with active connections could cause
     * errors. The default number is 10.
     * 
     * @param m Number of allowed node connections 
     */
    void setMaxConnections(size_t m);

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

    // === The RPC methods ===

    /**
     * @brief Restart all locally connected nodes.
     * 
     */
    void restartAll();

    /**
     * @brief Shutdown all locally connected nodes.
     * 
     */
    void shutdownAll();

    /**
     * @brief Get JSON metadata for all connected nodes. This includes names, descriptions
     * and hardware resources.
     * 
     * @return std::vector<nlohmann::json> 
     */
    std::vector<nlohmann::json> getAllNodeDetails();

    /**
     * @brief Get a list of all streams available to this node. It will collate from all
     * connected nodes and the web service.
     * 
     * @return std::vector<std::string> 
     */
    std::vector<std::string> getStreams();

    /**
     * @brief Find which node provides a stream. The returned pointer is a nullptr if the
     * stream is not found.
     * 
     * @param uri The stream URI.
     * @return std::shared_ptr<ftl::protocol::Node> 
     */
    std::shared_ptr<ftl::protocol::Node> locateStream(const std::string &uri);

    /**
     * @brief Handle a restart request from other machines.
     * 
     * @param cb 
     * @return ftl::Handle 
     */
    void onRestart(const std::function<void()> &cb);

    /**
     * @brief Handle a shutdown request from other machines.
     * 
     * @param cb 
     * @return ftl::Handle 
     */
    void onShutdown(const std::function<void()> &cb);

    /**
     * @brief Handle a stream creation request. Most likely this is being sent by the web service.
     * 
     * @param cb 
     * @return ftl::Handle 
     */
    void onCreateStream(const std::function<void(const std::string &uri, FrameID id)> &cb);

    /**
     * @brief Handle a node details request.
     * 
     * The returned JSON object should have the following keys:
     * * id
     * * title
     * * devices
     * * gpus
     * 
     * It may also have:
     * * description
     * * tags
     * 
     * @param cb 
     */
    void onNodeDetails(const std::function<nlohmann::json()> &cb);

    /**
     * @brief Handle a get configuration request. A path to the configuration property is
     * provided and a JSON value, possibly an entire object, is returned. Null can also be
     * returned if not found.
     * 
     * @param cb 
     */
    void onGetConfig(const std::function<nlohmann::json(const std::string &)> &cb);

    /**
     * @brief Handle a change config request. The configuration property path and new JSON
     * value is given.
     * 
     * @param cb 
     */
    void onSetConfig(const std::function<void(const std::string &, const nlohmann::json &)> &cb);

    /**
     * @brief Handle a request for all config properties on this machine. This could return
     * just the root level objects, or every property.
     * 
     * @param cb 
     */
    void onListConfig(const std::function<std::vector<std::string>()> &cb);

    /**
     * @brief Get the Send Buffer Size in bytes.
     * 
     * @param s protocol
     * @return size_t
     */
    size_t getSendBufferSize(ftl::URI::scheme_t s);

    /**
     * @brief Get the Recv Buffer Size in bytes
     * 
     * @param s protocol
     * @return size_t 
     */
    size_t getRecvBufferSize(ftl::URI::scheme_t s);

    /**
     * @brief Set the Send Buffer size in bytes
     * 
     * @param s protocol
     * @param size new size
     */
    void setSendBufferSize(ftl::URI::scheme_t s, size_t size);

    /**
     * @brief Set the Recv Buffer size in bytes
     * 
     * @param s protocol
     * @param size new size
     */
    void setRecvBufferSize(ftl::URI::scheme_t s, size_t size);

 protected:
    std::shared_ptr<ftl::net::Universe> universe_;
};

}  // namespace protocol
}  // namespace ftl
