/**
 * @file self.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include "universe.hpp"
#include <ftl/protocol/self.hpp>
#include "./streams/netstream.hpp"
#include "./streams/filestream.hpp"
#include <ftl/protocol/muxer.hpp>
#include <ftl/protocol/broadcaster.hpp>
#include <ftl/lib/nlohmann/json.hpp>
#include "uuidMSGPACK.hpp"

using ftl::protocol::Self;
using ftl::protocol::FrameID;

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
    case ftl::URI::SCHEME_CAST  : return std::make_shared<ftl::protocol::Broadcast>();
    case ftl::URI::SCHEME_MUX   : return std::make_shared<ftl::protocol::Muxer>();
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

size_t Self::getMaxConnections() const {
    return universe_->getMaxConnections();
}

void Self::setMaxConnections(size_t m) {
    universe_->setMaxConnections(m);
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

void Self::restartAll() {
    universe_->broadcast("restart");
}

void Self::shutdownAll() {
    universe_->broadcast("shutdown");
}

std::vector<nlohmann::json> Self::getAllNodeDetails() {
    auto response = universe_->findAll<std::string>("node_details");
    std::vector<nlohmann::json> result(response.size());
    for (auto &r : response) {
        result.push_back(nlohmann::json::parse(r));
    }
    return result;
}

std::vector<std::string> Self::getStreams() {
    return universe_->findAll<std::string>("list_streams");
}

std::shared_ptr<ftl::protocol::Node> Self::locateStream(const std::string &uri) {
    auto p = universe_->findOne<ftl::UUIDMSGPACK>("find_stream", uri);

    if (!p) return nullptr;
    auto peer = universe_->getPeer(*p);
    if (!peer) return nullptr;

    return std::make_shared<ftl::protocol::Node>(peer);
}

void Self::onRestart(const std::function<void()> &cb) {
    universe_->bind("restart", cb);
}

void Self::onShutdown(const std::function<void()> &cb) {
    universe_->bind("shutdown", cb);
}

void Self::onCreateStream(const std::function<void(const std::string &uri, FrameID id)> &cb) {
    universe_->bind("create_stream", [cb](const std::string &uri, int fsid, int fid) {
        cb(uri, FrameID(fsid, fid));
    });
}

void Self::onNodeDetails(const std::function<nlohmann::json()> &cb) {
    universe_->bind("node_details", [cb]() -> std::vector<std::string> {
        return {cb().dump()};
    });
}

void Self::onGetConfig(const std::function<nlohmann::json(const std::string &)> &cb) {
    universe_->bind("get_cfg", [cb](const std::string &path) {
        return cb(path).dump();
    });
}

void Self::onSetConfig(const std::function<void(const std::string &, const nlohmann::json &)> &cb) {
    universe_->bind("update_cfg", [cb](const std::string &path, const std::string &value) {
        cb(path, nlohmann::json::parse(value));
    });
}

void Self::onListConfig(const std::function<std::vector<std::string>()> &cb) {
    universe_->bind("list_configurables", cb);
}

size_t Self::getSendBufferSize(ftl::URI::scheme_t s) {
    return universe_->getSendBufferSize(s);
}

size_t Self::getRecvBufferSize(ftl::URI::scheme_t s) {
    return universe_->getRecvBufferSize(s);
}

void Self::setSendBufferSize(ftl::URI::scheme_t s, size_t size) {
    universe_->setSendBufferSize(s, size);
}

void Self::setRecvBufferSize(ftl::URI::scheme_t s, size_t size) {
    universe_->setRecvBufferSize(s, size);
}
