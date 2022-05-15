/**
 * @file rpc.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <memory>

namespace ftl {
namespace net {
class Universe;
}

namespace rpc {

void install(ftl::net::Universe *net);

}  // namespace rpc
}  // namespace ftl
