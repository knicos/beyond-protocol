/**
 * @file self.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include "universe.hpp"
#include <ftl/protocol/self.hpp>
#include "./streams/netstream.hpp"
#include "./streams/filestream.hpp"

using ftl::protocol::Self;

Self::Self(const std::shared_ptr<ftl::net::Universe> &impl): universe_(impl) {}

Self::~Self() {}

std::shared_ptr<ftl::protocol::Node> Self::connectNode(const std::string &uri) {
    return std::make_shared<ftl::protocol::Node>(universe_->connect(uri));
}

std::shared_ptr<ftl::protocol::Stream> Self::createStream(const std::string &uri) {
    ftl::URI u(uri);

    if (!u.isValid()) throw FTL_Error("Invalid Stream URI: " << uri);

    switch (u.getScheme()) {
    case ftl::URI::SCHEME_FTL   : return std::make_shared<ftl::protocol::Net>(uri, universe_.get(), true);
    case ftl::URI::SCHEME_FILE  :
    case ftl::URI::SCHEME_NONE  : return std::make_shared<ftl::protocol::File>(uri, true);
    default                     : throw FTL_Error("Invalid Stream URI: " << uri);
    }
}

std::shared_ptr<ftl::protocol::Stream> Self::getStream(const std::string &uri) {
    ftl::URI u(uri);

    if (!u.isValid()) throw FTL_Error("Invalid Stream URI");

    switch (u.getScheme()) {
    case ftl::URI::SCHEME_FTL   : return std::make_shared<ftl::protocol::Net>(uri, universe_.get(), false);
    case ftl::URI::SCHEME_FILE  :
    case ftl::URI::SCHEME_NONE  : return std::make_shared<ftl::protocol::File>(uri, false);
    default                     : throw FTL_Error("Invalid Stream URI: " << uri);
    }
}

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

int Self::waitConnections(int seconds) {
    return universe_->waitConnections(seconds);
}

std::shared_ptr<ftl::protocol::Node> Self::getNode(const ftl::UUID &pid) const {
    return std::make_shared<ftl::protocol::Node>(universe_->getPeer(pid));
}

std::shared_ptr<ftl::protocol::Node> Self::getWebService() const {
    return std::make_shared<ftl::protocol::Node>(universe_->getWebService());
}

std::list<std::shared_ptr<ftl::protocol::Node>> Self::getNodes() const {
    std::list<std::shared_ptr<ftl::protocol::Node>> result;
    auto peers = universe_->getPeers();
    std::transform(peers.begin(), peers.end(), std::back_inserter(result), [](const ftl::net::PeerPtr &ptr) {
        return std::make_shared<ftl::protocol::Node>(ptr);
    });
    return result;
}

ftl::Handle Self::onConnect(const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&)> &cb) {
    return universe_->onConnect([cb](const ftl::net::PeerPtr &p) {
        return cb(std::make_shared<ftl::protocol::Node>(p));
    });
}

ftl::Handle Self::onDisconnect(const std::function<bool(const std::shared_ptr<ftl::protocol::Node>&)> &cb) {
    return universe_->onDisconnect([cb](const ftl::net::PeerPtr &p) {
        return cb(std::make_shared<ftl::protocol::Node>(p));
    });
}

using ErrorCb = std::function<bool(
    const std::shared_ptr<ftl::protocol::Node>&,
    ftl::protocol::Error, const std::string &)>;

ftl::Handle Self::onError(const ErrorCb &cb) {
    return universe_->onError([cb](const ftl::net::PeerPtr &p, ftl::protocol::Error e, const std::string &estr) {
        return cb(std::make_shared<ftl::protocol::Node>(p), e, estr);
    });
}
