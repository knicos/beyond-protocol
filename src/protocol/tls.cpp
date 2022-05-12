#include "tls.hpp"

#ifdef HAVE_GNUTLS

#include <sstream>
#include <iomanip>

#include <ftl/exception.hpp>
#include <ftl/lib/loguru.hpp>

using ftl::net::internal::Connection_TLS;
using uchar = unsigned char;

/** get basic certificate info: Distinguished Name (DN), issuer DN,
 *  certificate fingerprint */
std::string get_cert_info(gnutls_session_t session) {
	const gnutls_datum_t *cert_list;
	unsigned int cert_list_size = 0;

	cert_list = gnutls_certificate_get_peers(session, &cert_list_size);

	std::string str = "";

	if (cert_list_size > 0) {
		gnutls_x509_crt_t cert;
		char data[256];
		size_t size;

		gnutls_x509_crt_init(&cert);

		gnutls_x509_crt_import(cert, &cert_list[0],
								GNUTLS_X509_FMT_DER);

		size = sizeof(data);
		gnutls_x509_crt_get_dn(cert, data, &size);
		str += "DN: " + std::string(data);

		size = sizeof(data);
		gnutls_x509_crt_get_issuer_dn(cert, data, &size);
		str += "; Issuer DN: " + std::string(data);

		size = sizeof(data);
		gnutls_x509_crt_get_fingerprint(cert, GNUTLS_DIG_SHA1, data, &size);
		std::stringstream ss;
		ss << std::hex << std::setfill('0');
		for (size_t i = 0; i < size; i++) {
			ss << std::setw(2) << int(((uchar*) data)[i]);
			if (i != (size - 1)) ss << ":";
		}
		str += "; certificate fingerprint (SHA1): " + ss.str();

		gnutls_x509_crt_deinit(cert);
	}
	
	return str;
}

int Connection_TLS::check_gnutls_error_(int errcode) {
	if (errcode < 0 && gnutls_error_is_fatal(errcode) == 0) {
		Connection_TCP::close();
	}
	
	if (errcode < 0) {
		auto msg = gnutls_strerror(errcode);
		throw FTL_Error(msg);
	}

	return errcode;
};

bool Connection_TLS::connect(const std::string& hostname, int port, int timeout) {
	// TODO: throw if already connected 

	check_gnutls_error_(gnutls_certificate_allocate_credentials(&xcred_));
	check_gnutls_error_(gnutls_certificate_set_x509_system_trust(xcred_));
	check_gnutls_error_(gnutls_init(&session_, GNUTLS_CLIENT));
	check_gnutls_error_(gnutls_server_name_set(session_, GNUTLS_NAME_DNS, hostname.c_str(), hostname.length()));

	gnutls_session_set_verify_cert(session_, hostname.c_str(), 0);
	check_gnutls_error_(gnutls_set_default_priority(session_));

	if (!Connection_TCP::connect(hostname, port, timeout)) {
		throw FTL_Error("TLS connection failed");
	}

	check_gnutls_error_(gnutls_credentials_set(session_, GNUTLS_CRD_CERTIFICATE, xcred_));

	gnutls_transport_set_int(session_, sock_.fd());
	gnutls_handshake_set_timeout(session_, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);

	check_gnutls_error_(gnutls_handshake(session_));

	LOG(INFO) << "TLS connection established: "
			  << gnutls_session_get_desc(session_) << "; "
			  << get_cert_info(session_);

	// try a few times? (included in gnutls example)
	// do { ... } while (retval < 0 && gnutls_error_is_fatal(retval) == 0);

	return true;
}

bool Connection_TLS::close() {
	if (sock_.is_open()) {
		gnutls_bye(session_, GNUTLS_SHUT_RDWR);
	}
	return Connection_TCP::close();
}

ssize_t Connection_TLS::recv(char *buffer, size_t len) {
	auto recvd = gnutls_record_recv(session_, buffer, len);
	if (recvd == 0) {
		LOG(1) << "recv returned 0 (buffer size " << len << "), closing connection";
		close();
	}

	return check_gnutls_error_(recvd);
}

ssize_t Connection_TLS::send(const char* buffer, size_t len) {
	return check_gnutls_error_(gnutls_record_send(session_, buffer, len));
}

ssize_t Connection_TLS::writev(const struct iovec *iov, int iovcnt) {
	gnutls_record_cork(session_);

	for (int i = 0; i < iovcnt; i++) {
		size_t sent = 0;
		do {
			// should always succeed and cache whole buffer (no error checking)
			sent += send((const char*) iov[i].iov_base, iov[i].iov_len);
		}
		while(sent < iov[i].iov_len);
	}
	
	return check_gnutls_error_(gnutls_record_uncork(session_, GNUTLS_RECORD_WAIT));
}

#endif