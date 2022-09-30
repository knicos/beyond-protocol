/**
 * @file socket_linux.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Sebastian Hahta
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <loguru.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>

using ftl::net::internal::Socket;
using ftl::net::internal::SocketAddress;

/// resolve address for OS socket calls, return true on success
bool ftl::net::internal::resolve_inet_address(const std::string &hostname, int port, SocketAddress &address) {
    addrinfo hints = {}, *addrs;

    // TODO(Seb): use uri for hints. Fixed values used here
    // should work as long only TCP is used.
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto rc = getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &addrs);
    if (rc != 0 || addrs == nullptr) return false;

    address.len = (socklen_t) addrs->ai_addrlen;
    if (address.len <= sizeof(address.addr)) {
        memcpy(&address.addr, addrs->ai_addr, address.len);
    }
    freeaddrinfo(addrs);
    return true;
}

// Socket

Socket::Socket(int domain, int type, int protocol) :
        status_(STATUS::UNCONNECTED), fd_(-1), family_(domain), err_(0) {
    int retval = socket(domain, type, protocol);

    if (retval > 0) {
        fd_ = retval;
    } else {
        LOG(ERROR) << ("socket() failed");
        throw FTL_Error("socket: " + get_error_string());
    }
}

bool Socket::is_valid() { return status_ != STATUS::INVALID; }

ssize_t Socket::recv(char *buffer, size_t len, int flags) {
    return ::recv(fd_, buffer, len, flags);
}

ssize_t Socket::send(const char* buffer, size_t len, int flags) {
    return ::send(fd_, buffer, len, flags);
}

ssize_t Socket::writev(const struct iovec *iov, int iovcnt) {
    return ::writev(fd_, iov, iovcnt);
}

int Socket::bind(const SocketAddress &addr) {
    auto retval = ::bind(fd_, reinterpret_cast<const sockaddr*>(&addr.addr), addr.len);
    if (retval) {
        status_ = Socket::OPEN;
    }
    return retval;
}

int Socket::listen(int backlog) {
    return ::listen(fd_, backlog);
}

Socket Socket::accept(SocketAddress &addr) {
    addr.len = sizeof(SocketAddress);
    Socket socket;
    int retval = ::accept(fd_, reinterpret_cast<sockaddr*>(&(addr.addr)), &(addr.len));
    if (retval > 0) {
        socket.status_ = STATUS::OPEN;
        socket.fd_ = retval;
        socket.family_ = family_;
    } else {
        LOG(ERROR) << "accept returned error: " << strerror(errno);
        socket.status_ = STATUS::INVALID;
    }
    return socket;
}

int Socket::connect(const SocketAddress& address) {
    int err = 0;
    if (status_ == STATUS::UNCONNECTED) {
        err = ::connect(fd_, reinterpret_cast<const sockaddr*>(&address.addr), address.len);
        if (err == 0) {
            status_ = STATUS::OPEN;
            return 0;
        } else {
            if (errno == EINPROGRESS) {
                status_ = STATUS::OPEN;     // close() will be called by destructor
                                            // add better status code?
                return -1;
            } else {
                status_ = STATUS::CLOSED;
                ::close(fd_);
            }
        }
    }
    return err;
}

int Socket::connect(const SocketAddress &address, int timeout) {
    if (timeout <= 0) {
        return connect(address);
    }

    bool blocking = is_blocking();
    if (blocking) set_blocking(false);

    auto rc = connect(address);
    if (rc < 0) {
        if (errno == EINPROGRESS) {
            fd_set myset;
            fd_set errset;
            FD_ZERO(&myset);
            FD_SET(fd_, &myset);
            FD_ZERO(&errset);
            FD_SET(fd_, &errset);

            struct timeval tv;
            tv.tv_sec = timeout;
            tv.tv_usec = 0;

            rc = select(fd_+1u, NULL, &myset, &errset, &tv);
            if (FD_ISSET(fd_, &errset)) rc = -1;
        }
    }

    if (blocking)
        set_blocking(true);

    if (rc < 0) {
        ::close(fd_);
        status_ = STATUS::CLOSED;
        LOG(ERROR) << "socket error: " << strerror(errno);
        return rc;
    }

    return 0;
}

/// Close socket (if open). Multiple calls are safe.
bool Socket::close() {
    if (is_valid() && status_ != STATUS::CLOSED) {
        status_ = STATUS::CLOSED;
        return ::close(fd_) == 0;
    } else if (status_ != STATUS::CLOSED) {
        LOG(INFO) << "close() on non-valid socket";
    }
    return false;
}

int Socket::setsockopt(int level, int optname, const void *optval, socklen_t optlen) {
    return ::setsockopt(fd_, level, optname, optval, optlen);
}

int Socket::getsockopt(int level, int optname, void *optval, socklen_t *optlen) {
    return ::getsockopt(fd_, level, optname, optval, optlen);
}

void Socket::set_blocking(bool val) {
    auto arg = fcntl(fd_, F_GETFL, NULL);
    arg = val ? (arg | O_NONBLOCK) : (arg & ~O_NONBLOCK);
    fcntl(fd_, F_SETFL, arg);
}

bool Socket::is_blocking() {
    return fcntl(fd_, F_GETFL, NULL) & O_NONBLOCK;
}

bool Socket::is_fatal(int code) {
    int e = (code != 0) ? code : errno;
    return !(e == EINTR || e == EWOULDBLOCK || e == EINPROGRESS);
}

std::string Socket::get_error_string(int code) {
    return strerror((code != 0) ? code : errno);
}

// TCP socket

Socket ftl::net::internal::create_tcp_socket() {
    return Socket(AF_INET, SOCK_STREAM, 0);
}

std::string ftl::net::internal::get_host(const SocketAddress& addr) {
    char hbuf[1024];
    int err = getnameinfo(
        reinterpret_cast<const sockaddr*>(&(addr.addr)),
        addr.len,
        hbuf,
        sizeof(hbuf),
        NULL,
        0,
        NI_NAMEREQD);
    if (err == 0) { return std::string(hbuf); }
    else if (err == EAI_NONAME) return ftl::net::internal::get_ip(addr);
    else
        LOG(WARNING) << "getnameinfo(): " << gai_strerror(err) << " (" << err << ")";
    return "unknown";
}

SocketAddress Socket::getsockname() {
    SocketAddress addr;
    auto* a = reinterpret_cast<struct sockaddr*>(&(addr.addr));
    ::getsockname(fd_, a, &(addr.len));
    return addr;
}

std::string ftl::net::internal::get_ip(const SocketAddress& addr) {
    auto* addr_in = reinterpret_cast<const sockaddr_in*>(&(addr.addr));
    std::string address(inet_ntoa(addr_in->sin_addr));
    return address;
}

int ftl::net::internal::get_port(const SocketAddress& addr) {
    auto* addr_in = reinterpret_cast<const sockaddr_in*>(&(addr.addr));
    return htons(addr_in->sin_port);
}
