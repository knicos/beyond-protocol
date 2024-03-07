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

#include <ftl/exception.hpp>
#include <ftl/protocol/node.hpp>

#include "dispatcher.hpp"
#include <ftl/uri.hpp>
#include <ftl/uuid.hpp>
#include <ftl/threads.hpp>

#include "rpc/time.hpp"

#include "uuidMSGPACK.hpp"

# define ENABLE_IF(...) \
  typename std::enable_if<(__VA_ARGS__), bool>::type = true

extern bool _run(bool blocking, bool nodelay);
extern int setDescriptors();

namespace ftl {
namespace net {

enum SendFlags
{
    NONE = 0,
    DELAY = 1
};


class Universe;

/** Peer Base */
class PeerBase
{
    friend class Universe;
    friend class Dispatcher;

public:
    using msgpack_buffer_t = msgpack::sbuffer;

public:
    friend class Dispatcher;

    explicit PeerBase(const ftl::URI& uri, ftl::net::Universe*, ftl::net::Dispatcher* d = nullptr);

    virtual ~PeerBase();

    /**
     * Test if the connection is valid. This returns true in all conditions
     * except where the socket has been disconnected permenantly, or was never
     * able to connect, perhaps due to an invalid address, or is in middle of a
     * reconnect attempt. (Valid states: kConnecting, kConnected)
     * 
     * Should return true only in cases when valid OS socket exists.
     */
    virtual bool isValid() const { return false; }

    /** peer type */
    virtual ftl::protocol::NodeType getType() const { return ftl::protocol::NodeType::kNode; }

    ftl::protocol::NodeStatus status() const { return status_; }

    virtual void start() {};

    /**
     * Close the peer if open. Setting retry parameter to true will initiate
     * backoff retry attempts. This is used to deliberately close a connection
     * and not for error conditions where different close semantics apply.
     * 
     * @param retry Should reconnection be attempted?
     */
    virtual void close(bool retry = false) {};

    virtual bool isConnected() const { return status_ == ftl::protocol::NodeStatus::kConnected; };
    /**
     * Block until the connection and handshake has completed. You should use
     * onConnect callbacks instead of blocking, mostly this is intended for
     * the unit tests to keep them synchronous.
     * 
     * @return True if all connections were successful, false if timeout or error.
     */
    virtual bool waitConnection(int seconds = 1);

    /** Return peer bandwidth estimation; 0 if not available */
    virtual int32_t AvailableBandwidth() { return 0; }

    virtual bool isOutgoing() const { return false; }

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

    inline int localID() const { return local_id_; }

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
    virtual void cancelCall(int id);

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
     * @return status code (TODO: specify)
     */
    template <typename... ARGS>
    int send(const std::string &name, ARGS&&... args);

    // NOTE: not used
    template <typename... ARGS>
    int try_send(const std::string &name, ARGS... args);

    // number of calls to send which are yet to complete
    virtual int pendingWriteCals() { return 0; } 

    // pending bytes in/out (not yet transmitted) (async)
    virtual int pendingOutgoing() { return 0; } 
    virtual int pendingIncoming() { return 0; }

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

    /** Close immediately without attempting to reconnect.
    *   Peer must be safe to release after shutdown() returns. Default implementation calls close(false)
    */
    virtual void shutdown() { close(false); }

    //int jobs() const { return job_count_; }

    static const int kMaxMessage = 4*1024*1024;      // 4Mb currently
    static const int kDefaultMessage = 512*1024;     // 0.5Mb currently

    /** send raw buffer directly (useful for pass-through without decoding) 
     *  should have some mechanism to track how many writes are in flight (output atomic int counter)
     */
    //virtual void sendBufferRaw(const char* buffer, size_t size) {}//{ std::promise<bool> promise; promise.set_value(false); return promise.get_future(); }

    void send_handshake();

    virtual int32_t getRtt() const;
    //virtual int64_t getJitter() const;
    //virtual int64_t getClockOffset() const;

protected:
    /** Called by ftl::Universe (no promises on any timing) */
    virtual void periodic_();

    // TODO: add name parameter that implementation may use to return different buffers depending on name

    // acquire msgpack buffer for send
    virtual msgpack_buffer_t get_buffer_() = 0;

    // send buffer to network (and return the buffer to peer instance)
    virtual int send_buffer_(const std::string& name, msgpack_buffer_t&& buffer, SendFlags flags = SendFlags::NONE) = 0;

    // call on received message (sync)
    void process_message_(msgpack::object_handle& object);

    // process handshke, returns true if valid handshake received
    bool process_handshake_(msgpack::object_handle& object);
    bool process_handshake_(uint64_t magic, uint32_t version, const ftl::UUIDMSGPACK &pid);

private:
    // close socket without sending disconnect message
    void _dispatchResponse(uint32_t id, msgpack::object &err, msgpack::object &res);
    void _sendResponse(uint32_t id, const msgpack::object &obj);
    void _sendErrorResponse(uint32_t id, const msgpack::object &obj);

    /*
    void _updateURI();
    void _set_socket_options();
    void _bind_rpc();
    */

protected:
    int local_id_ = -1;

    ftl::URI uri_;                                  // Original connection URI, or assumed URI
    ftl::UUID peerid_;                              // Received in handshake or allocated
    uint32_t version_;                              // Received protocol version in handshake

    std::atomic<ftl::protocol::NodeStatus> status_ = ftl::protocol::NodeStatus::kInvalid; // Connected, errored, reconnecting..

    ftl::net::Universe *net_;                       // Origin net universe

    // wait for all processing threads to exit
    virtual void waitForCallbacks();

private:
    std::unique_ptr<ftl::net::Dispatcher> disp_;    // For RPC call dispatch

    ClockInfo clock_info_;

    DECLARE_RECURSIVE_MUTEX(cb_mtx_);
    std::unordered_map<int, std::function<void(const msgpack::object&, const msgpack::object&)>> callbacks_;

    static std::atomic_int rpcid__;                 // Return ID for RPC calls

    bool handshake_sent_ = false;
};


// --- Inline Template Implementations -----------------------------------------

template <typename... ARGS>
int PeerBase::send(const std::string &name, ARGS&&... args) {
    auto args_obj = std::make_tuple(args...);
    auto call_obj = std::make_tuple(0, name, args_obj);
    auto buffer = get_buffer_();
    try {
        msgpack::pack(buffer, call_obj);
        return send_buffer_(name, std::move(buffer));

    } catch (...) {
        LOG(ERROR) << "Peer::send failed";
    }
    return -1;
}

template <typename F>
void PeerBase::bind(const std::string &name, F func) {
    // TODO: debug log all bindings (local and remote)
    disp_->bind(name, func,
        typename ftl::internal::func_kind_info<F>::result_kind(),
        typename ftl::internal::func_kind_info<F>::args_kind(),
        typename ftl::internal::func_kind_info<F>::has_peer());
}

template <typename R, typename... ARGS>
R PeerBase::call(const std::string &name, ARGS... args) {
    //LOG(INFO) << "RPC call (local): " << name;
    auto f = asyncCall<R>(name, std::forward<ARGS>(args)...);
    if (f.wait_for(std::chrono::milliseconds(1200)) != std::future_status::ready) {
        throw FTL_Error("Call timeout: " << name);
    }
    return f.get();
}

template <typename T, typename... ARGS>
std::future<T> PeerBase::asyncCall(const std::string &name, ARGS... args) {
    //LOG(INFO) << "RPC call (local): " << name;
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
    auto buffer = get_buffer_();

    try {
        msgpack::pack(buffer, call_obj);
        send_buffer_(name, std::move(buffer));

    } catch (...) {
        LOG(ERROR) << "Peer::asyncCall failed";
    }

    return future;
}

using PeerPtr = std::shared_ptr<PeerBase>;

}  // namespace net
}  // namespace ftl
