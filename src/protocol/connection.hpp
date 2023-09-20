/**
 * @file connection.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Sebastian Hahta
 */

#pragma once

#include <memory>
#include <string>
#include <ftl/exception.hpp>
#include <ftl/uri.hpp>
#include "../socketImpl.hpp"

namespace ftl {
namespace net {
namespace internal {

/** Socket client, wraps around socket connection. Transport protocols
 *  can be implemented by subclassing SocketConnection.
 *  
 *  Assumes IP socket.
 */
class SocketConnection {

 protected:
    Socket sock_;
    SocketAddress addr_;  // move to socket? save uri here


    void connect(const SocketAddress &address, int timeout = 0);

    SocketConnection() {}

 private:
    bool can_increase_sock_buffer_;

 public:
    SocketConnection(const SocketConnection&) = delete;

    SocketConnection(Socket socket, SocketAddress addr) :
        sock_(socket), addr_(addr), can_increase_sock_buffer_(true) {}

    virtual ~SocketConnection();

    // connection accepts reads/writes
    virtual bool is_valid();

    // OS socket file descriptor
    virtual socket_t fd();

    virtual ftl::URI uri();
    virtual ftl::URI::scheme_t scheme() const;

    virtual void connect(const ftl::URI& uri, int timeout = 0) = 0;

    // virtual void connect(int timeout=0); // TODO: set uri in constructor

    // close connection, return true if operation successful. never throws.
    virtual bool close();

    // process next item in buffer, returns true if available and sets offset
    // to number of bytes to skip in buffer (variable length headers etc.)
    virtual bool prepare_next(char* buffer, size_t len, size_t &offset) {
        offset = 0;
        return true;
    }

    // send, returns number of bytes sent
    virtual ssize_t send(const char* buffer, size_t len);
    // receive, returns number of bytes received
    virtual ssize_t recv(char *buffer, size_t len);
    // scatter write, return number of bytes sent. always sends all data in iov.
    virtual ssize_t writev(const struct iovec *iov, int iovcnt);

    virtual bool set_recv_buffer_size(size_t sz);
    virtual bool set_send_buffer_size(size_t sz);
    virtual size_t get_recv_buffer_size();
    virtual size_t get_send_buffer_size();

    int getSocketError();

    int is_fatal(int code = 0);

    virtual std::string host();
    virtual int port();
};

/** Socket server, wraps listening sockets. Transport protocols can
 *  be implemented by subclassing SocketServer. Is object slicing a problem 
 *  when subclass only implements additional methods (and does not define custom
 *  destructor or members)
 * 
 *  Assumes IP socket.
 */
class SocketServer {
 public:
    virtual ~SocketServer() = default;
 protected:
    Socket sock_;
    SocketAddress addr_;
    bool is_listening_;

    SocketServer() = default;

 public:
    SocketServer(Socket sock, SocketAddress addr) :
        sock_(sock), addr_(addr), is_listening_(false) {}

    // return OS file descriptor (for select()/poll()/etc.)
    socket_t fd();

    virtual bool is_listening();

    // bind and listen socket, throws exception on error
    virtual bool bind(const SocketAddress &address, int backlog = 0);
    virtual bool bind(int backlog = 0);

    // accept connection, throws exception on error
    virtual std::unique_ptr<SocketConnection> accept() = 0;

    /// stop accepting new connection and close underlying socket
    virtual bool close();

    virtual ftl::URI uri() = 0;

    /// avoid use, use URI etc. instead
    virtual std::string host();
    /// avoid use, use URI etc. instead
    virtual int port();
};

// SocketConnection factory
std::unique_ptr<SocketConnection> createConnection(const ftl::URI &uri);

}  // namespace internal
}  // namespace net
}  // namespace ftl
