#ifndef _FTL_NET_SOCKET_CONNECTION_TCP_HPP_
#define _FTL_NET_SOCKET_CONNECTION_TCP_HPP_

#include <ftl/exception.hpp>
#include <ftl/uri.hpp>

#include "connection.hpp"

namespace ftl {
namespace net {
namespace internal {

////////////////////////////////////////////////////////////////////////////////
// TCP client/server (directly uses socket without additional processing)

class Server_TCP : public SocketServer {
private:
	std::string host_;

protected:
	using SocketServer::SocketServer;

public:
	Server_TCP(const std::string& hostname, int port);
	std::unique_ptr<SocketConnection> accept() override;

	ftl::URI uri() override;
};

class Connection_TCP : public SocketConnection {
private:
	friend class Server_TCP;

protected:
	Connection_TCP(Socket sock, SocketAddress addr);

public:
	Connection_TCP();
	
	ftl::URI::scheme_t scheme() const override { return ftl::URI::SCHEME_TCP; }
	bool connect(const std::string &hostname, int port, int timeout=0);
	void connect(const ftl::URI& uri, int timeout=0) override;
};

} // namespace internal
} // namespace net
} // namespace ftl

#endif