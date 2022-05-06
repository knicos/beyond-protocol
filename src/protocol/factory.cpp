#include <loguru.hpp>

#include <ftl/exception.hpp>

#include "connection.hpp"
#include "tcp.hpp"
#include "tls.hpp"
#include "websocket.hpp"

using ftl::net::internal::SocketConnection;
using ftl::net::internal::Connection_TCP;
using ftl::net::internal::Connection_WS;

#ifdef HAVE_GNUTLS
using ftl::net::internal::Connection_WSS;
#endif

using ftl::URI;

std::unique_ptr<SocketConnection> ftl::net::internal::createConnection(const URI &uri) {
	if (uri.getProtocol() == URI::SCHEME_TCP) {
		auto c = std::make_unique<Connection_TCP>();
		return c;

	} else if (uri.getProtocol() == URI::SCHEME_WS) {
		auto c = std::make_unique<Connection_WS>();
		return c;

	} else if (uri.getProtocol() == URI::SCHEME_WSS) {
#ifdef HAVE_GNUTLS
		auto c = std::make_unique<Connection_WSS>();
		return c;
#else
		throw FTL_Error("built without TLS support");
#endif

	} else {
		//LOG(ERROR) << "can't connect to: " << uri.to_string();
		throw FTL_Error("unrecognised connection protocol: " << uri.to_string());
	}

	return nullptr;
}