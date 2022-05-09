#include <ftl/protocol/node.hpp>
#include "peer.hpp"

using ftl::protocol::Node;
using ftl::net::Peer;

Node::Node(const std::shared_ptr<Peer> &impl): peer_(impl) {}

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
