/**
 * @file socket.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Sebastian Hahta
 */

#pragma once

#include <string>

#include "socket/types.hpp"

namespace ftl {
namespace net {

namespace internal {

/** OS socket wrapper.
 * All methods which return int, values > 0 indicate success.
 */

class Socket {
 private:
    enum STATUS { INVALID, UNCONNECTED, OPEN, CLOSED };
    STATUS status_;
    socket_t fd_;
    SocketAddress addr_;
    int family_;
    int err_;

 public:
    Socket(int domain, int type, int protocol);
    ~Socket();

    bool is_valid();
    bool is_open();
    bool is_closed();
    bool is_fatal(int code = 0);

    ssize_t recv(char *buffer, size_t len, int flags);
    ssize_t send(const char* buffer, size_t len, int flags);
    ssize_t writev(const struct iovec *iov, int iovcnt);

    int bind(const SocketAddress&);

    int listen(int backlog);

    Socket accept(SocketAddress&);

    int connect(const SocketAddress&);

    /** Connect with timeout. Timeout implemented by changing socket temporarily
     * to non-blocking mode and using select(). Uses connect().
     */
    int connect(const SocketAddress& addr, int timeout);

    /// Close socket (if open). Multiple calls are safe.
    bool close();

    Socket() : status_(STATUS::INVALID), fd_(-1), family_(-1), err_(0) {}

    /// Get the socket file descriptor.
    socket_t fd() const { return fd_; }

    bool set_recv_buffer_size(size_t sz);

    bool set_send_buffer_size(size_t sz);

    size_t get_recv_buffer_size();

    size_t get_send_buffer_size();


    void set_blocking(bool val);

    bool is_blocking();

    std::string get_error_string(int code = 0);

    // only valid for TCP sockets
    bool set_nodelay(bool val);
    bool get_nodelay();

    SocketAddress getsockname();

    // TODO(Seb): perhaps remove and implement in custom methods instead
    int setsockopt(int level, int optname, const void *optval, socklen_t optlen);
    int getsockopt(int level, int optname, void *optval, socklen_t *optlen);
};

Socket create_tcp_socket();

/// resolve address: get SocketAddress from hostname port
bool resolve_inet_address(const std::string &hostname, int port, SocketAddress& address);
// add new functions for other socket types

// TODO(Seb): assumes ipv4, add protocol info to SocketAddress structure?
std::string get_ip(const SocketAddress& address);
std::string get_host(const SocketAddress& address);
int get_port(const SocketAddress& address);

}  // namespace internal
}  // namespace net
}  // namespace ftl
