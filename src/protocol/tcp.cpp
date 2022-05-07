

#include <ftl/exception.hpp>
#include "tcp.hpp"
#include <ftl/lib/loguru.hpp>

using namespace ftl::net::internal;

// Connection_TCP //////////////////////////////////////////////////////////////

Connection_TCP::Connection_TCP(Socket sock, SocketAddress addr) : SocketConnection(sock, addr) {
	if (!sock_.set_nodelay(true) || !sock_.get_nodelay()) {
		LOG(ERROR) << "Could not set TCP_NODELAY";
	}
}

Connection_TCP::Connection_TCP() : SocketConnection(create_tcp_socket(), {}) {
	if (!sock_.set_nodelay(true) || !sock_.get_nodelay()) {
		LOG(ERROR) << "Could not set TCP_NODELAY";
	}
}


bool Connection_TCP::connect(const std::string &hostname, int port, int timeout) {
	if (!resolve_inet_address(hostname, port, addr_)) {
		throw FTL_Error("could not resolve hostname: " + hostname);
	}
	auto err = sock_.connect(addr_);
	if (err < 0) {
		throw FTL_Error("connect() error: " + sock_.get_error_string());
	}

	return true;
}

void Connection_TCP::connect(const ftl::URI& uri, int timeout) {
	if (!connect(uri.getHost(), uri.getPort(), timeout)) {
		throw FTL_Error("Could not open TCP connection");
	}
}

// Server_TCP //////////////////////////////////////////////////////////////////

Server_TCP::Server_TCP(const std::string &hostname, int port) :
		SocketServer(create_tcp_socket(), {}), host_(hostname) {
	
	if (!resolve_inet_address(hostname, port, addr_)) {
		throw FTL_Error("could not resolve " + hostname);
	}

	int enable = 1;
	if (sock_.setsockopt(SOL_SOCKET, SO_REUSEADDR, (char*)(&enable), sizeof(int)) < 0) {
		LOG(ERROR) << "Setting SO_REUSEADDR failed";
	}
}

std::unique_ptr<SocketConnection> Server_TCP::accept() {
	SocketAddress addr;
	auto sock = sock_.accept(addr);
	auto connection = std::unique_ptr<Connection_TCP>(
		new Connection_TCP(sock, addr)); // throws on error
	return connection;
}

ftl::URI Server_TCP::uri() {
	return ftl::URI("tcp://" + host() + ":" + std::to_string(port()));
}