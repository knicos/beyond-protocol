/**
 * @file socket_windows.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Sebastian Hahta
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <string>

#include "../src/socket.hpp"

#include <ftl/exception.hpp>
#include <ftl/lib/loguru.hpp>

#pragma comment(lib, "Ws2_32.lib")

// winsock2 documentation
// https://docs.microsoft.com/en-us/windows/win32/api/winsock2/

using ftl::net::internal::Socket;
using ftl::net::internal::SocketAddress;

bool ftl::net::internal::resolve_inet_address(const std::string& hostname, int port, SocketAddress& address) {
    addrinfo hints = {}, *addrs;

    // TODO(Seb): use uri for hints. Fixed values used here
    // should work as long only TCP is used.
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto rc = getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &addrs);
    if (rc != 0 || addrs == nullptr) return false;
    address = *(reinterpret_cast<sockaddr_in*>(addrs->ai_addr));
    freeaddrinfo(addrs);
    return true;
}

class WinSock {
 public:

    WinSock() {
        CHECK(!winsock_initialized_);
        if (WSAStartup(MAKEWORD(1, 1), &wsaData_) != 0) {
            LOG(FATAL) << "WSAStartup() failed";
            // is it possible to retry/recover?
        }
        winsock_initialized_  = true;
        LOG(INFO) << "WSAStartup() done";
    }

    ~WinSock() {
        // is this safe in DLL? Documentation warns of deadlock if called from
        // DllMain() (not clear if static initialization ok or not). 
        CHECK(winsock_initialized_);
        if (WSACleanup() != 0) {
            LOG(FATAL) << "WSACleanup() failed: " << getErrorMsg(WSAGetLastError());
        }
        winsock_initialized_ = false;
        LOG(INFO) << "WSACleanup() done";
    }

    static bool isInitialized() { return winsock_initialized_ ; }

    static std::string getErrorMsg(int code) {
        wchar_t* s = NULL;

        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&s, 0, NULL);

        if (!s) {
            return "Unknown";
        }

        std::wstring ws(s);
        std::string msg(ws.begin(), ws.end());
        LocalFree(s);
        return msg;
    }

 private:
    static bool winsock_initialized_;
    WSAData wsaData_;
};

bool WinSock::winsock_initialized_ = false;

// Do WSAStartup as sttic initialisation so no threads active.
static WinSock winSock;

Socket::Socket(int domain, int type, int protocol) :
    status_(STATUS::INVALID), fd_(INVALID_SOCKET), family_(domain) {

    CHECK(WinSock::isInitialized());

    fd_ = ::socket(domain, type, protocol);
    if (fd_ == INVALID_SOCKET) {
        err_ = WSAGetLastError();
        throw FTL_Error("socket() failed" + get_error_string());
    }
    status_ = STATUS::UNCONNECTED;
}

bool Socket::is_valid() {
    return fd_ != INVALID_SOCKET;
}

ssize_t Socket::recv(char* buffer, size_t len, int flags) {
    CHECK(WinSock::isInitialized());
    auto err = ::recv(fd_, buffer, len, flags);
    if (err < 0) { err_ = WSAGetLastError(); }
    return err;
}

ssize_t Socket::send(const char* buffer, size_t len, int flags) {
    CHECK(WinSock::isInitialized());
    return ::send(fd_, buffer, len, flags);
}

ssize_t Socket::writev(const struct iovec* iov, int iovcnt) {
    CHECK(WinSock::isInitialized());
    std::vector<WSABUF> wsabuf(iovcnt);

    for (int i = 0; i < iovcnt; i++) {
        wsabuf[i].len = (ULONG)(iov[i].iov_len);
        wsabuf[i].buf = reinterpret_cast<char*>(iov[i].iov_base);
    }

    DWORD bytessent;
    auto err = WSASend(fd_, wsabuf.data(), static_cast<DWORD>(wsabuf.size()), (LPDWORD)&bytessent, 0, NULL, NULL);
    if (err < 0) { err_ = WSAGetLastError(); }
    return (err < 0) ? err : bytessent;
}

int Socket::bind(const SocketAddress& addr) {
    CHECK(WinSock::isInitialized());
    CHECK(status_ == STATUS::UNCONNECTED);

    int retval = ::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (retval == 0) {
        status_ = STATUS::OPEN;
        return 0;
    } else {
        err_ = WSAGetLastError();
        ::closesocket(fd_);
        status_ = STATUS::CLOSED;
        fd_ = INVALID_SOCKET;
    }
    return -1;
}

int Socket::listen(int backlog) {
    CHECK(WinSock::isInitialized());
    int retval = ::listen(fd_, backlog);
    if (retval == 0) {
        return 0;
    } else {
        err_ = WSAGetLastError();
        ::closesocket(fd_);
        status_ = STATUS::CLOSED;
        fd_ = INVALID_SOCKET;
        return retval;
    }
}

Socket Socket::accept(SocketAddress& addr) {
    CHECK(WinSock::isInitialized());
    CHECK(status_ == STATUS::OPEN);

    Socket socket;
    int addrlen = sizeof(addr);
    int retval = ::accept(fd_, reinterpret_cast<sockaddr*>(&addr), &addrlen);

    if (retval > 0) {
        socket.status_ = STATUS::OPEN;
        socket.fd_ = retval;
        socket.family_ = family_;
    } else {
        err_ = WSAGetLastError();
        DLOG(ERROR) << "accept returned error: " << get_error_string();
        socket.status_ = STATUS::INVALID;
    }
    return socket;
}

int Socket::connect(const SocketAddress& address) {
    CHECK(WinSock::isInitialized());
    int err = 0;
    if (status_ != STATUS::UNCONNECTED) {
        return -1;
    }

    err = ::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(SOCKADDR));
    if (err == 0) {
        status_ = STATUS::OPEN;
        return 0;

    } else {
        err_ = WSAGetLastError();
        if (err_ == EINPROGRESS) {
            status_ = STATUS::OPEN;
            return -1;
        } else {
            ::closesocket(fd_);
            status_ = STATUS::CLOSED;
            fd_ = INVALID_SOCKET;
        }
    }
    return -1;
}

int Socket::connect(const SocketAddress& address, int timeout) {
    CHECK(WinSock::isInitialized());
    // connect() blocks on Windows
    return connect(address);
}

bool Socket::close() {
    CHECK(status_ != STATUS::CLOSED) << "socket status_: " << status_;
    CHECK(fd_ != INVALID_SOCKET) << "not a valid socket";

    auto fd = fd_;
    status_ = STATUS::CLOSED;
    fd_ = INVALID_SOCKET;

    if (!WinSock::isInitialized()) {
        // Constructor would fail if WinSock was not started. It is possible
        // that ~WinSock() is called before all connections are closed at
        // program exit.
        LOG(ERROR) << "WinSock stopped before socket was closed";
        return false;
    }

    auto retval = closesocket(fd);

    if (retval != 0) {
        err_ = WSAGetLastError();
        LOG(ERROR) << "closesocket() returned " << retval << ": " << WinSock::getErrorMsg(err_);
    }

    return (retval == 0);
}


int Socket::setsockopt(int level, int optname, const void* optval, socklen_t optlen) {
    return ::setsockopt(fd_, level, optname, (const char*)optval, optlen);
}

int Socket::getsockopt(int level, int optname, void* optval, socklen_t* optlen) {
    return ::getsockopt(fd_, level, optname, reinterpret_cast<char*>(optval), optlen);
}

void Socket::set_blocking(bool val) {
    DLOG(ERROR) << "TODO: set blocking/non-blocking";
}

std::string Socket::get_error_string(int code) {
    return WinSock::getErrorMsg((code == 0) ? err_ : code);
}

bool Socket::is_fatal(int code) {
    if (code != 0) err_ = code;
    return !(err_ == 0 || err_ == WSAEINTR || err_ == WSAEMSGSIZE || err_ == WSAEINPROGRESS || err_ == WSAEWOULDBLOCK);
}

bool Socket::is_blocking() {
    return false;
}

// TCP socket

Socket ftl::net::internal::create_tcp_socket() {
    return Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

std::string ftl::net::internal::get_host(const SocketAddress& addr) {
    constexpr int kLenMax = 512;
    char hostname[kLenMax];
    char servinfo[kLenMax];
    auto retval = getnameinfo((struct sockaddr*)&addr,
        sizeof(struct sockaddr), hostname,
        kLenMax, servinfo, kLenMax, NI_NUMERICSERV);

    if (retval != 0) {
        return "N/A";
    } else {
        return std::string(hostname);
    }
}

SocketAddress Socket::getsockname() {
    SocketAddress addr;
    socklen_t len = sizeof(SocketAddress);
    ::getsockname(fd_, (struct sockaddr *)&addr, &len);
    return addr;
}

std::string ftl::net::internal::get_ip(const SocketAddress& addr) {
    char buf[64];
    inet_ntop(addr.sin_family, &(addr.sin_addr), buf, sizeof(buf));
    return std::string(buf);
}

int ftl::net::internal::get_port(const SocketAddress& addr) {
    return htons(addr.sin_port);
}
