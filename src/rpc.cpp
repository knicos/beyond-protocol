/**
 * @file rpc.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include "rpc.hpp"

#include "streams/netstream.hpp"

void ftl::rpc::install(ftl::net::Universe *net) {
    ftl::protocol::Net::installRPC(net);
}
