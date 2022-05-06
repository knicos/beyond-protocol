/**
 * @file handlers.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <functional>
#include <memory>

namespace ftl {
namespace net {

class Socket;

using SockDataHandler = std::function<void(int, std::string&)>;
using SockErrorHandler = std::function<void(int)>;
using SockConnectHandler = std::function<void()>;
using SockDisconnectHandler = std::function<void(int)>;

using DataHandler = std::function<void(std::shared_ptr<Socket>, int, std::string&)>;
using ErrorHandler = std::function<void(std::shared_ptr<Socket>, int)>;
using ConnectHandler = std::function<void(std::shared_ptr<Socket> &)>;
using DisconnectHandler = std::function<void(std::shared_ptr<Socket>)>;

};
};

