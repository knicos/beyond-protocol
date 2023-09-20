#pragma once
#include "peer.hpp"

#include "common_fwd.hpp"
#include "socket.hpp"

namespace ftl { 
namespace net {

// ---------------------------------------------------------------------------------------------------------------------

/**
 * To be constructed using the Universe::connect() method and not to be
 * created directly.
 * 
 */
class PeerTcp : public PeerBase {
 public:
    //std::unique_ptr<PeerBase> rpc;

    friend class Universe;
    friend class Dispatcher;

    /** Peer for outgoing connection: resolve address and connect */
    explicit PeerTcp(const ftl::URI& uri, ftl::net::Universe*, ftl::net::Dispatcher* d = nullptr);

    /** Peer for incoming connection: take ownership of given connection */
    explicit PeerTcp(
        std::unique_ptr<internal::SocketConnection> s,
        ftl::net::Universe*,
        ftl::net::Dispatcher* d = nullptr);

    virtual ~PeerTcp();

    void start();

    /**
     * Close the peer if open. Setting retry parameter to true will initiate
     * backoff retry attempts. This is used to deliberately close a connection
     * and not for error conditions where different close semantics apply.
     * 
     * @param retry Should reconnection be attempted?
     */
    void close(bool retry) override;

    bool isConnected() const;
    /**
     * Make a reconnect attempt. Called internally by Universe object.
     */
    bool reconnect();

    inline bool isOutgoing() const { return outgoing_; }

    /**
     * Test if the connection is valid. This returns true in all conditions
     * except where the socket has been disconnected permenantly, or was never
     * able to connect, perhaps due to an invalid address, or is in middle of a
     * reconnect attempt. (Valid states: kConnecting, kConnected)
     * 
     * Should return true only in cases when valid OS socket exists.
     */
    bool isValid() const override;

    /** peer type */
    ftl::protocol::NodeType getType() const override;

    uint32_t getFTLVersion() const { return version_; }
    uint8_t getFTLMajor() const { return version_ >> 16; }
    uint8_t getFTLMinor() const { return (version_ >> 8) & 0xFF; }
    uint8_t getFTLPatch() const { return version_ & 0xFF; }

    /**
     * Get the sockets protocol, address and port as a url string. This will be
     * the same as the initial connection string on the client.
     */
    std::string getURI() const { return uri_.to_string(); }

    const ftl::URI &getURIObject() const { return uri_; }

    /**
     * Get the UUID for this peer.
     */
    const ftl::UUID &id() const { return peerid_; }

    /**
     * Get the peer id as a string.
     */
    std::string to_string() const { return peerid_.to_string(); }

    void rawClose();

    inline void noReconnect() { can_reconnect_ = false; }

    int connectionCount() const { return connection_count_; }

    /**
     * @brief Call recv to get data. Internal use, it is blocking so should only
     * be done if data is available. (used by ftl::net::Universe)
     */
    void recv();

    int jobs() const { return job_count_; }

    void shutdown() override;

public:
    static const int kMaxMessage = 4*1024*1024;      // 4Mb currently
    static const int kDefaultMessage = 512*1024;     // 0.5Mb currently

protected:
    msgpack_buffer_t get_buffer_() override;

    // send buffer to network
    int send_buffer_(const std::string&, msgpack_buffer_t&&, SendFlags) override;

private:  // Functions
    // opposite of get_buffer
    void set_buffer_(msgpack_buffer_t&&);

    bool socketError();  // Process one error from socket
    void error(int e);

    // check if buffer has enough decoded data from lower layer and advance
    // buffer if necessary (skip headers etc).
    bool _has_next();

    // After data is read from network, _data() is called on new thread.
    // Received data is kept valid until _data() returns
    // (by msgpack::object_handle in local scope).
    bool _data();

    // close socket without sending disconnect message
    void _close(bool retry = true);

    /**
     * Get the internal OS dependent socket.
     * TODO(nick) Work out if this should be private. Used by select() in
     * Universe (universe.cpp)
     */
    int _socket() const;

    void _updateURI();
    void _set_socket_options();
    void _bind_rpc();

    void _connect();

    void _createJob();

    void _waitCall(int id, std::condition_variable &cv, bool &hasreturned, const std::string &name);

    std::atomic_flag already_processing_ = ATOMIC_FLAG_INIT;
    std::atomic_flag recv_checked_ = ATOMIC_FLAG_INIT;

    msgpack::unpacker recv_buf_;
    size_t recv_buf_max_ = kDefaultMessage;
    MUTEX recv_mtx_;

    // Send buffers
    msgpack::sbuffer send_buf_;
    DECLARE_RECURSIVE_MUTEX(send_mtx_);

    const bool outgoing_;

    uint32_t version_;                              // Received protocol version in handshake

    bool can_reconnect_;                            // Client connections can retry

    std::unique_ptr<internal::SocketConnection> sock_;

    std::atomic_int job_count_ = 0;                 // Ensure threads are done before destructing
    std::atomic_int connection_count_ = 0;          // Number of successful connections total ?
    std::atomic_int retry_count_ = 0;               // Current number of reconnection attempts

    // reconnect when clean disconnect received from remote
    bool reconnect_on_remote_disconnect_ = true;
    // reconnect on socket error/disconnect without message (remote crash ...)
    bool reconnect_on_socket_error_ = true;
    // reconnect on protocol error (msgpack decode, bad handshake, ...)
    bool reconnect_on_protocol_error_ = false;
};

using PeerTcpPtr = std::shared_ptr<PeerTcp>;

}
}
