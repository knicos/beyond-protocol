/**
 * @file types.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Sebastian Hahta
 */

#pragma once

#if defined(WIN32)
// Windows
#include <BaseTsd.h>
#include <winsock2.h>
#include <ws2def.h>
#include <ws2tcpip.h>

typedef SSIZE_T ssize_t;

#include <msgpack.hpp>
// defined by msgpack, do not redefine here
/*
typedef struct iovec {
    void* iov_base;
    size_t iov_len;
};
*/

#else
// Linux
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace ftl {
namespace net {
namespace internal {

#if defined(WIN32)
// Windows
typedef SOCKET socket_t;
typedef sockaddr_in SocketAddress;

#else
// Linux

typedef int socket_t;

struct SocketAddress {
    socklen_t len = sizeof(struct sockaddr);
    struct sockaddr_in addr;
};
#endif

}  // namespace internal
}  // namespace net
}  // namespace ftl
