#include "rpc.hpp"

#include "streams/netstream.hpp"

void ftl::rpc::install(ftl::net::Universe *net) {
    ftl::protocol::Net::installRPC(net);
}
