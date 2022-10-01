/**
 * @file connection.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Sebastian Hahta
 */

#include <vector>
#include <algorithm>
#include <string>
#include <loguru.hpp>
#include <ftl/exception.hpp>
#include "connection.hpp"

using ftl::net::internal::SocketConnection;
using ftl::net::internal::socket_t;
using ftl::net::internal::SocketServer;
using ftl::URI;

// SocketConnection ////////////////////////////////////////////////////////////

SocketConnection::~SocketConnection() {
    sock_.close();
}

socket_t SocketConnection::fd() { return sock_.fd(); }

ftl::URI SocketConnection::uri() {
    std::string str = host() + ":" + std::to_string(get_port(addr_));
    return ftl::URI(str);
}

ftl::URI::scheme_t SocketConnection::scheme() const {
    return ftl::URI::scheme_t::SCHEME_NONE;
}

bool SocketConnection::is_valid() {
    return sock_.is_open();
}

int SocketConnection::is_fatal(int code) {
    return sock_.is_fatal(code);
}

void SocketConnection::connect(const SocketAddress &address, int timeout) {
    addr_ = address;
    int rv = 0;
    if (timeout <= 0) rv = sock_.connect(addr_ );
    else
        rv = sock_.connect(addr_, timeout);

    if (rv != 0) { throw FTL_Error("connect(): " + sock_.get_error_string()); }
}

ssize_t SocketConnection::recv(char *buffer, size_t len) {
    auto recvd = sock_.recv(buffer, len, 0);
    if (recvd == 0) {
        DLOG(3) << "recv(): read size 0";
        return -1;  // -1 means close, 0 means retry
    }
    if (recvd < 0) {
        if (!sock_.is_fatal()) return 0;  // Retry
        throw FTL_Error(sock_.get_error_string());
    }
    return recvd;
}

ssize_t SocketConnection::send(const char* buffer, size_t len) {
    return sock_.send(buffer, len, 0);
}

ssize_t SocketConnection::writev(const struct iovec *iov, int iovcnt) {
    ssize_t sent = sock_.writev(iov, iovcnt);

    ssize_t requested = 0;
    for (int i = 0; i < iovcnt; i++) { requested += iov[i].iov_len; }

    if (sent < 0) {
        DLOG(ERROR) << "writev(): " << sock_.get_error_string();
        if (sock_.is_fatal()) {
            return sent;
        }
        sent = 0;
    }
    if (sent == requested) { return sent; }

    std::vector<struct iovec> iov_local(iovcnt);
    auto* iov_ptr = iov_local.data();
    std::copy(iov, iov + iovcnt, iov_ptr);

    ssize_t sent_total = sent;
    int writev_calls = 1;
    while (sent_total < requested) {
        // ssize_t unsigned on Windows? Define as signed and use isntead of long
        int64_t iov_len = int64_t(iov_ptr[0].iov_len) - int64_t(sent);

        if (iov_len < 0) {
            // buffer was sent, update sent with remaining bytes and
            // repeat with next item in iov
            sent = -iov_len;
            iov_ptr++;
            iovcnt--;
            continue;
        }

        iov_ptr[0].iov_base = static_cast<char*>(iov_ptr[0].iov_base) + sent;
        iov_ptr[0].iov_len = iov_ptr[0].iov_len - sent;

        sent = sock_.writev(iov_ptr, iovcnt);
        writev_calls++;

        if (sent < 0) {
            DLOG(ERROR) << "writev(): " << sock_.get_error_string();
            if (sock_.is_fatal()) {
                return sent;
            }
            sent = 0;
        }

        sent_total += sent;
    }

    DLOG(2) << "message required " << writev_calls << " writev() calls";

    if (can_increase_sock_buffer_) {
        auto send_buf_size = sock_.get_send_buffer_size();
        auto send_buf_size_new = size_t(sock_.get_send_buffer_size() * 1.5);

        DLOG(WARNING) << "Send buffer size "
                     << (send_buf_size >> 10) << " KiB. "
                     << "Increasing socket buffer size to "
                     << (send_buf_size_new >> 10) << "KiB.";

        if (!sock_.set_send_buffer_size(send_buf_size_new)) {
            DLOG(ERROR) << "could not increase send buffer size, "
                    << "set_send_buffer_size() failed";
            can_increase_sock_buffer_ = false;
        } else {
            send_buf_size = sock_.get_send_buffer_size();
            bool error = send_buf_size < send_buf_size_new;
            DLOG_IF(WARNING, error)
                << "could not increase send buffer size "
                << "(buffer size: " << send_buf_size << ")";
            can_increase_sock_buffer_ &= !error;
        }
    }

    return requested;
}

bool SocketConnection::close() {
    return sock_.close();
}

std::string SocketConnection::host() {
    return get_host(addr_);
}

int SocketConnection::port() {
    DLOG(ERROR) << "port() not implemented";
    return -1;
}

bool SocketConnection::set_recv_buffer_size(size_t sz) {
    auto old = get_recv_buffer_size();
    auto ok = sock_.set_recv_buffer_size(sz);
    if (!ok) {
        DLOG(ERROR) << "setting socket send buffer size failed:"
                   << sock_.get_error_string();
    }
    if (get_recv_buffer_size() == old) {
        DLOG(ERROR) << "recv buffer size was not changed";
    }
    return ok;
}

bool SocketConnection::set_send_buffer_size(size_t sz) {
    auto old = get_send_buffer_size();
    auto ok = sock_.set_send_buffer_size(sz);
    if (!ok) {
        DLOG(ERROR) << "setting socket send buffer size failed:"
                   << sock_.get_error_string();
    }
    if (get_send_buffer_size() == old) {
        DLOG(ERROR) << "send buffer size was not changed";
    }

    return ok;
}

size_t SocketConnection::get_recv_buffer_size() {
    return sock_.get_recv_buffer_size();
}

size_t SocketConnection::get_send_buffer_size() {
    return sock_.get_send_buffer_size();
}

int SocketConnection::getSocketError() {
    int val = 0;
    socklen_t optlen = sizeof(val);
    if (sock_.getsockopt(SOL_SOCKET, SO_ERROR, &val, &optlen) == 0) {
        return val;
    }
    return errno;  // TODO(Seb): Windows.
}

// SocketServer ////////////////////////////////////////////////////////////////

socket_t SocketServer::fd() {
    return sock_.fd();
}

bool SocketServer::is_listening() {
    return is_listening_;
}

bool SocketServer::bind(const SocketAddress &address, int backlog) {
    bool retval = true;

    retval &= sock_.bind(address) == 0;
    if (!retval) {
        auto msg = sock_.get_error_string();
        throw FTL_Error("socket error:" + msg);
    }

    retval &= sock_.listen(backlog) == 0;
    if (!retval) {
        auto msg = sock_.get_error_string();
        throw FTL_Error("socket error:" + msg);
    } else { is_listening_ = true; }

    addr_ = sock_.getsockname();
    return retval;
}

bool SocketServer::bind(int backlog) {
    return bind(addr_, backlog);
}

bool SocketServer::close() {
    is_listening_ = false;
    return sock_.close();
}

std::string SocketServer::host() {
    return get_host(addr_);
}

int SocketServer::port() {
    return get_port(addr_);
}
