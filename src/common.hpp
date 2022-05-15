/**
 * @file common.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

// TODO(Nick): remove platform specific headers from here
#ifndef WIN32
#include <unistd.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define SOCKET int
#endif

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
typedef int socklen_t;
#endif
