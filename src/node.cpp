/**
 * @file node.cpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/node.hpp>
#include "peer.hpp"
#include <ftl/lib/nlohmann/json.hpp>
#include <ftl/time.hpp>

using ftl::protocol::Node;
using ftl::net::PeerPtr;
using ftl::protocol::FrameID;

Node::Node(const PeerPtr &impl): peer_(impl) {}

Node::~Node() {}

void Node::close(bool retry) {
    peer_->close(retry);
}

bool Node::isConnected() const {
    return peer_->isConnected();
}

bool Node::waitConnection(int s) {
    return peer_->waitConnection(s);
}

bool Node::reconnect() {
    return peer_->reconnect();
}

bool Node::isOutgoing() const {
    return peer_->isOutgoing();
}

bool Node::isValid() const {
    return peer_->isValid();
}

ftl::protocol::NodeStatus Node::status() const {
    return peer_->status();
}

uint32_t Node::getFTLVersion() const {
    return peer_->getFTLVersion();
}

std::string Node::getURI() const {
    return peer_->getURI();
}

const ftl::UUID &Node::id() const {
    return peer_->id();
}

std::string Node::to_string() const {
    return peer_->to_string();
}

void Node::noReconnect() {
    peer_->noReconnect();
}

unsigned int Node::localID() {
    return peer_->localID();
}

int Node::connectionCount() const {
    return peer_->connectionCount();
}

void Node::restart() {
    peer_->send("restart");
}

void Node::shutdown() {
    peer_->send("shutdown");
}

bool Node::hasStream(const std::string &uri) {
    return !!peer_->call<std::optional<std::string>>("find_stream", uri);
}

void Node::createStream(const std::string &uri, FrameID id) {
    peer_->send("create_stream", uri, id.frameset(), id.source());
}

nlohmann::json Node::details() {
    const std::string res = peer_->call<std::string>("node_details");
    return nlohmann::json::parse(res);
}

int64_t Node::ping() {
    return peer_->call<int64_t>("__ping__");
}

nlohmann::json Node::getConfig(const std::string &path) {
    return nlohmann::json::parse(peer_->call<std::string>("get_cfg", path));
}

void Node::setConfig(const std::string &path, const nlohmann::json &value) {
    peer_->send("update_cfg", path, value.dump());
}

std::vector<std::string> Node::listConfigs() {
    return peer_->call<std::vector<std::string>>("list_configurables");
}
