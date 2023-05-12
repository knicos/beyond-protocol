/**
 * @file peer.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <tuple>
#include <vector>
#include <future>
#include <type_traits>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <map>
#include <utility>
#include <string>

#include <msgpack.hpp>
#include "common_fwd.hpp"
#include "socket.hpp"
#include <ftl/exception.hpp>
#include <ftl/protocol/node.hpp>

#include "protocol.hpp"
#include "dispatcher.hpp"
#include <ftl/uri.hpp>
#include <ftl/uuid.hpp>
#include <ftl/threads.hpp>


# define ENABLE_IF(...) \
  typename std::enable_if<(__VA_ARGS__), bool>::type = true

extern bool _run(bool blocking, bool nodelay);
extern int setDescriptors();

namespace ftl {
namespace net {

class Universe;

/**
 * To be constructed using the Universe::connect() method and not to be
 * created directly.
 */
class Peer {
 public:
    friend class Universe;
    friend class Dispatcher;

    /** Peer for outgoing connection: resolve address and connect */
    explicit Peer(const ftl::URI& uri, ftl::net::Universe*, ftl::net::Dispatcher* d = nullptr);

    /** Peer for incoming connection: take ownership of given connection */
    explicit Peer(
        std::unique_ptr<internal::SocketConnection> s,
        ftl::net::Universe*,
        ftl::net::Dispatcher* d = nullptr);

    ~Peer();

    void start();

    /**
     * Close the peer if open. Setting retry parameter to true will initiate
     * backoff retry attempts. This is used to deliberately close a connection
     * and not for error conditions where different close semantics apply.
     * 
     * @param retry Should reconnection be attempted?
     */
    void close(bool retry = false);

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

    inline bool isOutgoing() const { return outgoing_; }

    /**
     * Test if the connection is valid. This returns true in all conditions
     * except where the socket has been disconnected permenantly, or was never
     * able to connect, perhaps due to an invalid address, or is in middle of a
     * reconnect attempt. (Valid states: kConnecting, kConnected)
     * 
     * Should return true only in cases when valid OS socket exists.
     */
    bool isValid() const;

    /** peer type */
    ftl::protocol::NodeType getType() const;

    ftl::protocol::NodeStatus status() const { return status_; }

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

    /**
     * Non-blocking Remote Procedure Call using a callback function.
     * 
     * @param name RPC Function name.
     * @param cb Completion callback.
     * @param args A variable number of arguments for RPC function.
     * 
     * @return A call id for use with cancelCall() if needed.
     */
    template <typename T, typename... ARGS>
    std::future<T> asyncCall(const std::string &name, ARGS... args);

    /**
     * Used to terminate an async call if the response is not required.
     * 
     * @param id The ID returned by the original asyncCall request.
     */
    void cancelCall(int id);

    /**
     * Blocking Remote Procedure Call using a string name.
     */
    template <typename R, typename... ARGS>
    R call(const std::string &name, ARGS... args);

    /**
     * Non-blocking send using RPC function, but with no return value.
     * 
     * @param name RPC Function name
     * @param args Variable number of arguments for function
     * 
     * @return Number of bytes sent or -1 if error
     */
    template <typename... ARGS>
    int send(const std::string &name, ARGS&&... args);

    template <typename... ARGS>
    int try_send(const std::string &name, ARGS... args);

    /**
     * Bind a function to an RPC call name. Note: if an overriding dispatcher
     * is used then these bindings will propagate to all peers sharing that
     * dispatcher.
     * 
     * @param name RPC name to bind to
     * @param func A function object to act as callback
     */
    template <typename F>
    void bind(const std::string &name, F func);

    void rawClose();

    inline void noReconnect() { can_reconnect_ = false; }

    inline unsigned int localID() const { return local_id_; }

    int connectionCount() const { return connection_count_; }

    /**
     * @brief Call recv to get data. Internal use, it is blocking so should only
     * be done if data is available.
     * 
     */
    void data();

    int jobs() const { return job_count_; }

 public:
    static const int kMaxMessage = 4*1024*1024;      // 4Mb currently
    static const int kDefaultMessage = 512*1024;     // 0.5Mb currently

 private:  // Functions
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

    void _dispatchResponse(uint32_t id, msgpack::object &err, msgpack::object &res);
    void _sendResponse(uint32_t id, const msgpack::object &obj);
    void _sendErrorResponse(uint32_t id, const msgpack::object &obj);

    /**
     * Get the internal OS dependent socket.
     * TODO(nick) Work out if this should be private. Used by select() in
     * Universe (universe.cpp)
     */
    int _socket() const;

    void _send_handshake();
    void _process_handshake(uint64_t magic, uint32_t version, const UUID &pid);

    void _updateURI();
    void _set_socket_options();
    void _bind_rpc();

    void _connect();
    int _send();

    void _createJob();

    void _waitCall(int id, std::condition_variable &cv, bool &hasreturned, const std::string &name);

    template<typename... ARGS>
    void _trigger(const std::vector<std::function<void(Peer &, ARGS...)>> &hs, ARGS... args) {
        for (auto h : hs) {
            h(*this, args...);
        }
    }

    std::atomic_flag already_processing_ = ATOMIC_FLAG_INIT;
    std::atomic_flag recv_checked_ = ATOMIC_FLAG_INIT;

    msgpack::unpacker recv_buf_;
    size_t recv_buf_max_ = kDefaultMessage;
    MUTEX recv_mtx_;

    // Send buffers
    msgpack::vrefbuffer send_buf_;
    DECLARE_RECURSIVE_MUTEX(send_mtx_);
    DECLARE_RECURSIVE_MUTEX(cb_mtx_);

    const bool outgoing_;
    unsigned int local_id_;
    ftl::URI uri_;                                  // Original connection URI, or assumed URI
    ftl::UUID peerid_;                              // Received in handshake or allocated
    ftl::protocol::NodeStatus status_;              // Connected, errored, reconnecting..
    uint32_t version_;                              // Received protocol version in handshake
    bool can_reconnect_;                            // Client connections can retry
    ftl::net::Universe *net_;                       // Origin net universe

    std::unique_ptr<internal::SocketConnection> sock_;
    std::unique_ptr<ftl::net::Dispatcher> disp_;    // For RPC call dispatch
    std::map<int, std::function<void(const msgpack::object&, const msgpack::object&)>> callbacks_;

    std::atomic_int job_count_ = 0;                 // Ensure threads are done before destructing
    std::atomic_int connection_count_ = 0;          // Number of successful connections total
    std::atomic_int retry_count_ = 0;               // Current number of reconnection attempts

    // reconnect when clean disconnect received from remote
    bool reconnect_on_remote_disconnect_ = true;
    // reconnect on socket error/disconnect without message (remote crash ...)
    bool reconnect_on_socket_error_ = true;
    // reconnect on protocol error (msgpack decode, bad handshake, ...)
    bool reconnect_on_protocol_error_ = false;

    static std::atomic_int rpcid__;                 // Return ID for RPC calls
};

// --- Inline Template Implementations -----------------------------------------

template <typename... ARGS>
int Peer::send(const std::string &s, ARGS&&... args) {
    UNIQUE_LOCK(send_mtx_, lk);
    auto args_obj = std::make_tuple(args...);
    auto call_obj = std::make_tuple(0, s, args_obj);
    msgpack::pack(send_buf_, call_obj);
    int rc = _send();
    return rc;
}

template <typename F>
void Peer::bind(const std::string &name, F func) {
    disp_->bind(name, func,
        typename ftl::internal::func_kind_info<F>::result_kind(),
        typename ftl::internal::func_kind_info<F>::args_kind(),
        typename ftl::internal::func_kind_info<F>::has_peer());
}

template <typename R, typename... ARGS>
R Peer::call(const std::string &name, ARGS... args) {
    auto f = asyncCall<R>(name, std::forward<ARGS>(args)...);
    if (f.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
        throw FTL_Error("Call timeout: " << name);
    }
    return f.get();
}

template <typename T, typename... ARGS>
std::future<T> Peer::asyncCall(const std::string &name, ARGS... args) {
    auto args_obj = std::make_tuple(args...);
    uint32_t rpcid = 0;

    std::shared_ptr<std::promise<T>> promise = std::make_shared<std::promise<T>>();
    std::future<T> future = promise->get_future();

    {
        UNIQUE_LOCK(cb_mtx_, lk);
        rpcid = rpcid__++;
        callbacks_[rpcid] = [promise](const msgpack::object &res, const msgpack::object &err) {
            if (err.is_nil()) {
                T value;
                res.convert<T>(value);
                promise->set_value(value);
            } else {
                std::string errmsg;
                err.convert<std::string>(errmsg);
                promise->set_exception(std::make_exception_ptr(ftl::exception(ftl::Formatter() << errmsg)));
            }
        };
    }

    auto call_obj = std::make_tuple(0, rpcid, name, args_obj);

    UNIQUE_LOCK(send_mtx_, lk);
    msgpack::pack(send_buf_, call_obj);
    _send();
    return future;
}

using PeerPtr = std::shared_ptr<ftl::net::Peer>;

}  // namespace net
}  // namespace ftl
