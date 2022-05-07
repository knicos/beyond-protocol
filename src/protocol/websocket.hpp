#ifndef _FTL_NET_WEBSOCKET_HPP_
#define _FTL_NET_WEBSOCKET_HPP_

#include "connection.hpp"
#include "tcp.hpp"
#include "tls.hpp"

#include <vector>
#include <random>

namespace ftl {
namespace net {
namespace internal {

template<typename SocketT>
class WebSocketBase : public SocketT {
public:
	WebSocketBase();
	ftl::URI::scheme_t scheme() const override;
	void connect(const ftl::URI& uri, int timeout=0) override;

	bool prepare_next(char* buffer, size_t len, size_t &offset) override;

	ssize_t writev(const struct iovec *iov, int iovcnt) override;

protected:

	// output io vectors (incl. header)
	std::vector<struct iovec> iovecs_;
};

using Connection_WS = WebSocketBase<Connection_TCP>;
#ifdef HAVE_GNUTLS
using Connection_WSS = WebSocketBase<Connection_TLS>;
#endif
}
}
}

#endif
