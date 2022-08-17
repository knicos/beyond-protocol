/**
 * @file websocket.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Sebastian Hahta
 */

#include <string>
#include <unordered_map>
#include <algorithm>
#include "websocket.hpp"
#include <ftl/lib/loguru.hpp>

#include <ftl/utility/base64.hpp>

using uchar = unsigned char;

#ifdef HAVE_GNUTLS
#include <gnutls/crypto.h>

inline uint32_t secure_rnd() {
    uint32_t rnd;
    gnutls_rnd(GNUTLS_RND_NONCE, &rnd, sizeof(uint32_t));
    return rnd;
}
#else
// TODO(Seb): Use cryptographically strong RNG. RFC 6455 (WebSicket protocol)
//            requires new masking key for every frame and the key must not be
//            predictable (use system random number source such as /dev/random
//            or another library if GnuTLS is not enabled).

#include <random>
static std::random_device rd_;
static std::uniform_int_distribution<uint32_t> dist_(0);

inline uint32_t secure_rnd() {
    return dist_(rd_);
}
#endif

using ftl::URI;
using ftl::net::internal::WebSocketBase;
using ftl::net::internal::Connection_TCP;
#ifdef HAVE_GNUTLS
using ftl::net::internal::Connection_TLS;
#endif

/* Taken from easywsclient */
struct wsheader_type {
    unsigned header_size;
    bool fin;
    int rsv;
    bool mask;
    enum opcode_type {
        CONTINUATION = 0x0,
        TEXT_FRAME = 0x1,
        BINARY_FRAME = 0x2,
        CLOSE = 8,
        PING = 9,
        PONG = 0xa,
    } opcode;
    int N0;
    uint64_t N;
    uint8_t masking_key[4];
};

struct ws_options {
    std::string userinfo = "";
};

// prepare ws header
int ws_prepare(wsheader_type::opcode_type op, bool useMask, uint32_t mask,
                size_t len, char *data, size_t maxlen) {
    uint8_t* masking_key = reinterpret_cast<uint8_t*>(&mask);

    char *header = data;
    size_t header_size = 2 + (len >= 126 ? 2 : 0) + (len >= 65536 ? 6 : 0) + (useMask ? 4 : 0);
    if (header_size > maxlen) return -1;

    memset(header, 0, header_size);
    header[0] = 0x80 | op;

    if (len < 126) {
        header[1] = (len & 0xff) | (useMask ? 0x80 : 0);
        if (useMask) {
            header[2] = masking_key[0];
            header[3] = masking_key[1];
            header[4] = masking_key[2];
            header[5] = masking_key[3];
        }
    } else if (len < 65536) {
        header[1] = 126 | (useMask ? 0x80 : 0);
        header[2] = (len >> 8) & 0xff;
        header[3] = (len >> 0) & 0xff;
        if (useMask) {
            header[4] = masking_key[0];
            header[5] = masking_key[1];
            header[6] = masking_key[2];
            header[7] = masking_key[3];
        }
    } else {
        header[1] = 127 | (useMask ? 0x80 : 0);
        header[2] = (len >> 56) & 0xff;
        header[3] = (len >> 48) & 0xff;
        header[4] = (len >> 40) & 0xff;
        header[5] = (len >> 32) & 0xff;
        header[6] = (len >> 24) & 0xff;
        header[7] = (len >> 16) & 0xff;
        header[8] = (len >>  8) & 0xff;
        header[9] = (len >>  0) & 0xff;
        if (useMask) {
            header[10] = masking_key[0];
            header[11] = masking_key[1];
            header[12] = masking_key[2];
            header[13] = masking_key[3];
        }
    }

    return static_cast<int>(header_size);
}

// parse ws header, returns true on success
void ws_parse(uchar *data, size_t len, wsheader_type *ws) {
    if (len < 2) throw FTL_Error("Websock header too small");

    ws->fin = (data[0] & 0x80) == 0x80;
    ws->rsv = (data[0] & 0x70);
    ws->opcode = (wsheader_type::opcode_type) (data[0] & 0x0f);
    ws->mask = (data[1] & 0x80) == 0x80;
    ws->N0 = (data[1] & 0x7f);
    ws->header_size = 2 + (ws->N0 == 126? 2 : 0) + (ws->N0 == 127? 8 : 0) + (ws->mask? 4 : 0);

    if (len < ws->header_size) throw FTL_Error("Websock header too small");
    if (ws->rsv != 0) throw FTL_Error("WS header reserved not zero");

    // invalid opcode, corrupted header?
    if ((ws->opcode > 10) || ((ws->opcode > 2) && (ws->opcode < 8))) throw FTL_Error("Websock opcode invalid");

    int i = 0;
    if (ws->N0 < 126) {
        ws->N = ws->N0;
        i = 2;
    } else if (ws->N0 == 126) {
        ws->N = 0;
        ws->N |= ((uint64_t) data[2]) << 8;
        ws->N |= ((uint64_t) data[3]) << 0;
        i = 4;
    } else if (ws->N0 == 127) {
        ws->N = 0;
        ws->N |= ((uint64_t) data[2]) << 56;
        ws->N |= ((uint64_t) data[3]) << 48;
        ws->N |= ((uint64_t) data[4]) << 40;
        ws->N |= ((uint64_t) data[5]) << 32;
        ws->N |= ((uint64_t) data[6]) << 24;
        ws->N |= ((uint64_t) data[7]) << 16;
        ws->N |= ((uint64_t) data[8]) << 8;
        ws->N |= ((uint64_t) data[9]) << 0;
        i = 10;
    }

    if (ws->mask) {
        ws->masking_key[0] = ((uint8_t) data[i+0]) << 0;
        ws->masking_key[1] = ((uint8_t) data[i+1]) << 0;
        ws->masking_key[2] = ((uint8_t) data[i+2]) << 0;
        ws->masking_key[3] = ((uint8_t) data[i+3]) << 0;
    } else {
        ws->masking_key[0] = 0;
        ws->masking_key[1] = 0;
        ws->masking_key[2] = 0;
        ws->masking_key[3] = 0;
    }
}

// same as above, pointer type casted to unsigned
void ws_parse(char *data, size_t len, wsheader_type *ws) {
    ws_parse(reinterpret_cast<unsigned char*>(data), len, ws);
}


int getPort(const ftl::URI &uri) {
    auto port = uri.getPort();

    if (port == 0) {
        if (uri.getScheme() == URI::scheme_t::SCHEME_WS) {
            port = 80;
        } else if (uri.getScheme() == URI::scheme_t::SCHEME_WSS) {
            port = 443;
        } else {
            throw FTL_Error("Bad WS uri:" + uri.to_string());
        }
    }

    return port;
}

////////////////////////////////////////////////////////////////////////////////

template<typename SocketT>
WebSocketBase<SocketT>::WebSocketBase() {}

template<typename SocketT>
void WebSocketBase<SocketT>::connect(const ftl::URI& uri, int timeout) {
    int port = getPort(uri);

    // connect via TCP/TLS
    if (!SocketT::connect(uri.getHost(), port, timeout)) {
        throw FTL_Error("WS: connect() failed");
    }

    std::string http = "";
    int status;
    int i;
    char line[256];

    http += "GET " + uri.getPath() + " HTTP/1.1\r\n";
    if (port == 80) {
        http += "Host: " + uri.getHost() + "\r\n";
    } else {
        // TODO(Seb): is this correct when connecting over TLS
        http += "Host: " + uri.getHost() + ":"
             + std::to_string(port) + "\r\n";
    }

    if (uri.hasUserInfo()) {
        http += "Authorization: Basic ";
        http += base64_encode(uri.getUserInfo()) + "\r\n";
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Authorization
        if (uri.getProtocol() != URI::scheme_t::SCHEME_WSS) {
            LOG(WARNING) << "HTTP Basic Auth is being sent without TLS";
        }
    }

    http += "Upgrade: websocket\r\n";
    http += "Connection: Upgrade\r\n";
    http += "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n";
    http += "Sec-WebSocket-Version: 13\r\n";
    http += "\r\n";

    int rc = SocketT::send(http.c_str(), static_cast<int>(http.length()));
    if (rc != static_cast<int>(http.length())) {
        throw FTL_Error("Could not send Websocket http request... ("
                        + std::to_string(rc) + ", "
                         + std::to_string(errno) + ")\n" + http);
    }

    for (i = 0; i < 2 || (i < 255 && line[i-2] != '\r' && line[i-1] != '\n'); ++i) {
        if (SocketT::recv(line + i, 1) == 0) {
            throw FTL_Error("Connection closed by remote");
        }
    }

    line[i] = 0;
    if (i == 255) {
        throw FTL_Error("Got invalid status line connecting to: " + uri.getHost());
    }
    if (sscanf(line, "HTTP/1.1 %d", &status) != 1 || status != 101) {
        throw FTL_Error("ERROR: Got bad status connecting to: "
                        + uri.getHost() + ": " + line);
    }

    std::unordered_map<std::string, std::string> headers;

    while (true) {
        for (i = 0; i < 2 || (i < 255 && line[i-2] != '\r' && line[i-1] != '\n'); ++i) {
            if (SocketT::recv(line+i, 1) == 0) {
                throw FTL_Error("Connection closed by remote");
            }
        }
        if (line[0] == '\r' && line[1] == '\n') { break; }

        // Split the headers into a map for checking
        line[i] = 0;
        const std::string cppline(line);
        const auto ix = cppline.find(":");
        const auto label = cppline.substr(0, ix);
        const auto value = cppline.substr(ix + 2, cppline.size() - ix - 4);
        headers[label] = value;
    }

    // Validate some of the headers
    if (headers.count("Connection") == 0 || headers.at("Connection") != "upgrade")
        throw FTL_Error("Missing WS connection header");
    if (headers.count("Upgrade") == 0 || headers.at("Upgrade") != "websocket")
        throw FTL_Error("Missing WS Upgrade");
    if (headers.count("Sec-WebSocket-Accept") == 0)
        throw FTL_Error("Missing WS accept header");
}

template<typename SocketT>
bool WebSocketBase<SocketT>::prepare_next(char* data, size_t data_len, size_t& offset) {
    offset = 0;

    // Header may be smaller than 14 bytes. If there isn't enough data,
    // do not process before receiving more data.
    if (data_len < 14) { return false; }

    wsheader_type header;
    ws_parse(data, data_len, &header);

    if ((header.N + header.header_size) > data_len) {
        /*LOG(WARNING) << "buffered: " << data_len
                     << ", ws frame size: " << (header.N + header.header_size)
                     << " (not enough data in buffer)"; */
        return false;
    }

    if (header.mask) {
        throw FTL_Error("masked WebSocket data not supported");  // TODO(Seb):
    }

    // payload/application data/extension of control frames should be ignored?
    // fragments are OK (data is be in order and frames are not interleaved)

    offset = header.header_size;
    return true;
}

template<typename SocketT>
ssize_t WebSocketBase<SocketT>::writev(const struct iovec *iov, int iovcnt) {

    if ((iovcnt + 1) >= ssize_t(iovecs_.size())) { iovecs_.resize(iovcnt + 1); }
    // copy iovecs to local buffer, first iovec entry reserved for header
    std::copy(iov, iov + iovcnt, iovecs_.data() + 1);

    // masking
    size_t msglen = 0;
    uint32_t mask = secure_rnd();
    uint8_t* masking_key = reinterpret_cast<uint8_t*>(&mask);

    // calculate total size of message and mask it.
    for (int i = 1; i < iovcnt + 1; i++) {
        const size_t mlen = iovecs_[i].iov_len;
        char *buf = reinterpret_cast<char*>(iovecs_[i].iov_base);

        // TODO(Seb): Make this more efficient.
        for (size_t j = 0; j != mlen; ++j) {
            buf[j] ^= masking_key[(msglen + j)&0x3];
        }
        msglen += mlen;
    }

    // create header
    constexpr size_t kHSize = 20;
    char h_buffer[kHSize];

    auto rc = ws_prepare(wsheader_type::BINARY_FRAME, true, mask, msglen, h_buffer, kHSize);
    if (rc < 0) { return -1; }

    // send header + data
    iovecs_[0].iov_base = h_buffer;
    iovecs_[0].iov_len = rc;

    auto sent = SocketT::writev(iovecs_.data(), iovcnt + 1);
    if (sent > 0) {
        // do not report sent header size
        return sent - rc;
    }
    return sent;
}

template<typename SocketT>
ftl::URI::scheme_t WebSocketBase<SocketT>::scheme() const {return ftl::URI::SCHEME_TCP; }

// explicit instantiation
template class WebSocketBase<Connection_TCP>;  // Connection_WS
#ifdef HAVE_GNUTLS
template class WebSocketBase<Connection_TLS>;  // Connection_WSS
#endif
