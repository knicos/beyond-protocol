#include "universe.hpp"
#include <ftl/protocol/self.hpp>

using ftl::protocol::Self;

Self::Self(const std::shared_ptr<ftl::net::Universe> &impl): universe_(impl) {}

Self::~Self() {}
	
void Self::start() {
    universe_->start();
}

bool Self::listen(const ftl::URI &addr) {
    return universe_->listen(addr);
}

std::vector<ftl::URI> Self::getListeningURIs() {
    return universe_->getListeningURIs();
}

void Self::shutdown() {
    universe_->shutdown();
}

bool Self::isConnected(const ftl::URI &uri) {
    return universe_->isConnected(uri);
}

bool Self::isConnected(const std::string &s) {
    return universe_->isConnected(s);
}

size_t Self::numberOfNodes() const {
    return universe_->numberOfPeers();
}

int Self::waitConnections() {
    return universe_->waitConnections();
}

std::shared_ptr<ftl::protocol::Node> Self::getNode(const ftl::UUID &pid) const {
    return std::make_shared<ftl::protocol::Node>(universe_->getPeer(pid));
}

std::shared_ptr<ftl::protocol::Node> Self::getWebService() const {
    return std::make_shared<ftl::protocol::Node>(universe_->getWebService());
}
