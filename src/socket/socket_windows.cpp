#include "../src/socket.hpp"

#include <ftl/exception.hpp>
#include <ftl/lib/loguru.hpp>

#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#include <string>

// winsock2 documentation
// https://docs.microsoft.com/en-us/windows/win32/api/winsock2/

using ftl::net::internal::Socket;
using ftl::net::internal::SocketAddress;

bool ftl::net::internal::resolve_inet_address(const std::string& hostname, int port, SocketAddress& address) {
	addrinfo hints = {}, *addrs;

	// TODO: use uri for hints. Fixed values used here
	// should work as long only TCP is used.
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	auto rc = getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &addrs);
	if (rc != 0 || addrs == nullptr) return false;
	address = *((sockaddr_in*) (addrs->ai_addr));
	freeaddrinfo(addrs);
	return true;
}

static std::atomic_bool is_initialized_;
static WSAData wsaData_;

Socket::Socket(int domain, int type, int protocol) :
	status_(STATUS::UNCONNECTED), fd_(-1), family_(domain) {

	if (!is_initialized_.exchange(true)) {
		if (WSAStartup(MAKEWORD(1, 1), &wsaData_) != 0) {
			LOG(FATAL) << "could not initialize sockets";
			// is it possible to retry/recover?
		}
	}

	// TODO: initialization might not be complete if called from another thread
	fd_ = ::socket(domain, type, protocol);
	if (fd_ == INVALID_SOCKET) {
		err_ = WSAGetLastError();
		throw FTL_Error("socket() failed" + get_error_string());
	}
}

bool Socket::is_valid() {
	return fd_ != INVALID_SOCKET;
}

ssize_t Socket::recv(char* buffer, size_t len, int flags) {
	auto err = ::recv(fd_, buffer, len, flags);
	if (err < 0) { err_ = WSAGetLastError(); }
	return err;
}

ssize_t Socket::send(const char* buffer, size_t len, int flags) {
	return ::send(fd_, buffer, len, flags);
}

ssize_t Socket::writev(const struct iovec* iov, int iovcnt) {

	std::vector<WSABUF> wsabuf(iovcnt);

	for (int i = 0; i < iovcnt; i++) {
		wsabuf[i].len = (ULONG)(iov[i].iov_len);
		wsabuf[i].buf = (char*)(iov[i].iov_base);
	}

	DWORD bytessent;
	auto err = WSASend(fd_, wsabuf.data(), static_cast<DWORD>(wsabuf.size()), (LPDWORD)&bytessent, 0, NULL, NULL);
	if (err < 0) { err_ = WSAGetLastError(); }
	return (err < 0) ? err : bytessent;

}

int Socket::bind(const SocketAddress& addr) {
	int retval = ::bind(fd_, (sockaddr*)(&addr), sizeof(addr));
	if (retval == 0) {
		status_ = STATUS::OPEN;
		return 0;
	}
	else {
		err_ = WSAGetLastError();
		::closesocket(fd_);
		status_ = STATUS::CLOSED;
		fd_ = INVALID_SOCKET;
	}
	return -1;
}

int Socket::listen(int backlog) {
	int retval = ::listen(fd_, backlog);
	if (retval == 0) {
		return 0;
	}
	else {
		err_ = WSAGetLastError();
		::closesocket(fd_);
		status_ = STATUS::CLOSED;
		fd_ = INVALID_SOCKET;
		return retval;
	}
}

Socket Socket::accept(SocketAddress& addr) {
	Socket socket;
	int addrlen = sizeof(addr);
	int retval = ::accept(fd_, (sockaddr*)(&addr), &addrlen);

	if (retval > 0) {
		socket.status_ = STATUS::OPEN;
		socket.fd_ = retval;
		socket.family_ = family_;
	} else {
		err_ = WSAGetLastError();
		LOG(ERROR) << "accept returned error: " << get_error_string();
		socket.status_ = STATUS::INVALID;
	}
	return socket;
}

int Socket::connect(const SocketAddress& address) {
	int err = 0;
	if (status_ != STATUS::UNCONNECTED) {
		return -1;
	}
	
	err = ::connect(fd_, (sockaddr*)(&address), sizeof(SOCKADDR));
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
	// connect() blocks on Windows
	return connect(address);
}

bool Socket::close() {
	bool retval = true;
	if (is_valid() && status_ != STATUS::CLOSED) {
		status_ = STATUS::CLOSED;
		retval = closesocket(fd_) == 0;
		err_ = errno;
	}
	fd_ = INVALID_SOCKET;
	return retval;
}


int Socket::setsockopt(int level, int optname, const void* optval, socklen_t optlen) {
	return ::setsockopt(fd_, level, optname, (const char*)optval, optlen);
}

int Socket::getsockopt(int level, int optname, void* optval, socklen_t* optlen) {
	return ::getsockopt(fd_, level, optname, (char*)optval, optlen);
}

void Socket::set_blocking(bool val) {
	LOG(ERROR) << "TODO: set blocking/non-blocking";
}

std::string Socket::get_error_string(int code) {
	wchar_t* s = NULL;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, (code != 0) ? code : err_, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&s, 0, NULL);
	if (!s) {
		return "Unknown";
	}
	std::wstring ws(s);
	std::string msg(ws.begin(), ws.end());
	LocalFree(s);
	return msg;
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

std::string ftl::net::internal::get_host(SocketAddress& addr) {
	constexpr int len_max = 512;
	char hostname[len_max];
	char servinfo[len_max];
	auto retval = getnameinfo((struct sockaddr*)&addr,
		sizeof(struct sockaddr), hostname,
		len_max, servinfo, len_max, NI_NUMERICSERV);

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

std::string ftl::net::internal::get_ip(SocketAddress& addr) {
	char buf[64];
	inet_ntop(addr.sin_family, &(addr.sin_addr), buf, sizeof(buf));
	return std::string(buf);
}

int ftl::net::internal::get_port(SocketAddress& addr) {
	return htons(addr.sin_port);
}
