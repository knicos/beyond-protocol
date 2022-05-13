/** no-op socket for unit tests. Simulated data transfer implemented in
 * Connection_Mock */

#include "../src/socketImpl.hpp"

#include <string>

using ftl::net::internal::Socket;
using ftl::net::internal::SocketAddress;

bool ftl::net::internal::resolve_inet_address(const std::string &hostname, int port, SocketAddress &address) {
	return true;
}

Socket::Socket(int domain, int type, int protocol) :
		status_(STATUS::UNCONNECTED), fd_(-1), family_(domain), err_(0) {
}

bool Socket::is_valid() { return true; }

bool Socket::is_open() { return true; }

ssize_t Socket::recv(char *buffer, size_t len, int flags) {
	return 0;
}

ssize_t Socket::send(const char* buffer, size_t len, int flags) {
	return 0;
}

ssize_t Socket::writev(const struct iovec *iov, int iovcnt) {
	return 0;
}

int Socket::bind(const SocketAddress &addr) {
	return 0;
}

int Socket::listen(int backlog) {
	return 0;
}

Socket Socket::accept(SocketAddress &addr) {
	return Socket();
}

int Socket::connect(const SocketAddress& address) {
	return 0;
}

int Socket::connect(const SocketAddress &address, int timeout) {
	return 0;
}

/// Close socket (if open). Multiple calls are safe.
bool Socket::close() {
	return true;
}

int Socket::setsockopt(int level, int optname, const void *optval, socklen_t optlen) {
	return 0;
}

int Socket::getsockopt(int level, int optname, void *optval, socklen_t *optlen) {
	return 0;
}

bool Socket::set_recv_buffer_size(size_t sz) {
	return true;
}

bool Socket::set_send_buffer_size(size_t sz) {
	return true;
}

size_t Socket::get_recv_buffer_size() {
	return 0;
}

size_t Socket::get_send_buffer_size() {
	return 0;
}


void Socket::set_blocking(bool val) {	
}


bool Socket::is_blocking() {
	return true;
}

std::string Socket::get_error_string(int code) {
	return "not real socket";
}

bool Socket::set_nodelay(bool val) {
	return true;
}

SocketAddress Socket::getsockname() {
	SocketAddress addr;
	return addr;
}

// TCP socket

Socket ftl::net::internal::create_tcp_socket() {
	return Socket();
}

std::string ftl::net::internal::get_ip(SocketAddress& address) { return ""; }
std::string ftl::net::internal::get_host(SocketAddress& address) { return ""; }
int ftl::net::internal::get_port(SocketAddress& address) { return 0; } 