/**
 * @file protocol.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <memory>
#include <ftl/uuid.hpp>

namespace ftl {
namespace protocol {
class Node;
class Stream;
class Listener;
class Service;

/** Reset network and streams. Used by tests. */
void reset();

extern ftl::UUID id;
}

std::shared_ptr<ftl::protocol::Listener> createListener(const std::string &uri);
std::shared_ptr<ftl::protocol::Service> setServiceProvider(const std::string &uri);
std::shared_ptr<ftl::protocol::Node> createPeer(const std::string &uri);
std::shared_ptr<ftl::protocol::Stream> createStream(const std::string &uri);

}
