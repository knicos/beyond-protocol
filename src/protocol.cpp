#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include "universe.hpp"

static std::shared_ptr<ftl::net::Universe> universe;

//ctpl::thread_pool ftl::pool(std::thread::hardware_concurrency()*2);
ctpl::thread_pool ftl::pool(4);

void ftl::protocol::reset() {
    universe.reset();
}

ftl::UUID ftl::protocol::id;

std::shared_ptr<ftl::protocol::Self> ftl::getSelf() {
    if (!universe) universe = std::make_shared<ftl::net::Universe>();
    return std::make_shared<ftl::protocol::Self>(universe);
}

std::shared_ptr<ftl::protocol::Self> ftl::createDummySelf() {
    ftl::UUID uuid;
    auto u = std::make_shared<ftl::net::Universe>();
    u->setLocalID(uuid);
    return std::make_shared<ftl::protocol::Self>(u);
}

/*std::shared_ptr<ftl::protocol::Service> ftl::setServiceProvider(const std::string &uri) {

}*/

std::shared_ptr<ftl::protocol::Node> ftl::connectNode(const std::string &uri) {
    return getSelf()->connectNode(uri);
}

std::shared_ptr<ftl::protocol::Stream> ftl::createStream(const std::string &uri) {
    return getSelf()->createStream(uri);
}

std::shared_ptr<ftl::protocol::Stream> ftl::getStream(const std::string &uri) {
    return getSelf()->getStream(uri);
}
