#ifndef _FTL_NET_TLS_HPP_
#define _FTL_NET_TLS_HPP_

#ifdef HAVE_GNUTLS

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "tcp.hpp"

namespace ftl {
namespace net {
namespace internal {

class Connection_TLS : public Connection_TCP {
public:
	Connection_TLS() {};
	Connection_TLS(const std::string &hostname, int port, int timeout=0);

	bool connect(const std::string &hostname, int port, int timeout=0);

	bool close() override;
	
protected:
	ssize_t recv(char *buffer, size_t len) override;
	ssize_t send(const char* buffer, size_t len) override;
	ssize_t writev(const struct iovec *iov, int iovcnt) override;
	
	int check_gnutls_error_(int errcode); // check for fatal error and throw

private:
	gnutls_session_t session_;
	gnutls_certificate_credentials_t xcred_;
};


}
}
}

#endif // HAVE_GNUTLS
#endif // _FTL_NET_TLS_HPP_
