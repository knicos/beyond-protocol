// OS specific implementations for TCP sockets

#include "../socket.hpp"

#ifdef WIN32
#include "socket_windows.cpp"
#else
#include "socket_linux.cpp"
#endif

bool Socket::is_open() { return status_ == STATUS::OPEN; }

bool Socket::is_closed() { return status_ == STATUS::CLOSED; }

bool Socket::set_recv_buffer_size(size_t sz) {
	int a = static_cast<int>(sz);
	return setsockopt(SOL_SOCKET, SO_RCVBUF, (char *) &a, sizeof(int)) != -1;
}

bool Socket::set_send_buffer_size(size_t sz) {
	int a = static_cast<int>(sz);
	return setsockopt(SOL_SOCKET, SO_SNDBUF, (char *) &a, sizeof(int)) != -1;
}

size_t Socket::get_recv_buffer_size() {
	int a = 0;
	socklen_t optlen = sizeof(int);
	getsockopt(SOL_SOCKET, SO_RCVBUF, (char *) &a, &optlen);
	return a;
}

size_t Socket::get_send_buffer_size() {
	int a = 0;
	socklen_t optlen = sizeof(int);
	getsockopt(SOL_SOCKET, SO_SNDBUF, (char *) &a, &optlen);
	return a;
}

bool Socket::set_nodelay(bool val) {
	int flags = val ? 1 : 0;
	return setsockopt(IPPROTO_TCP, TCP_NODELAY,
				(char *) &flags, sizeof(flags)) == 0;
}

bool Socket::get_nodelay() {
	int flags = 0;
	socklen_t len = sizeof(int);
	getsockopt(IPPROTO_TCP, TCP_NODELAY, (char *) &flags, &len);
	return flags;
}