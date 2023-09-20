/**
 * @file protocol.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <memory>
#include <string>
#include <ftl/uuid.hpp>

/**
 * @brief Future Tech Lab
 * 
 */
namespace ftl {
/**
 * @brief Protocol structures.
 * 
 */
namespace protocol {
class Node;
class Stream;
class Self;
class Service;

/** Reset network and streams. Used by tests. */
void reset();

extern ftl::UUID id;
}  // namespace protocol

/**
 * @brief Get the Self object. This may initialise the internal system when
 * first called. A Self object allows for the overall control of the network
 * and general RPC functionality between hosts. If this is called multiple
 * times then the same internal object is returned, it is a singleton.
 * 
 * @return std::shared_ptr<ftl::protocol::Self> 
 */
std::shared_ptr<ftl::protocol::Self> getSelf();

/**
 * @brief Create a secondary Self object. Mostly for testing purposes, this
 * allows additional instances to be created of the otherwise singleton class.
 * 
 * @return std::shared_ptr<ftl::protocol::Self> 
 */
std::shared_ptr<ftl::protocol::Self> createDummySelf();

/**
 * @brief Set the web service URI to use. There should be a single connection
 * to a web service that provides additional management functionality beyond
 * a typical node. By calling this function the system is informed about where
 * to ask about certain resources.
 * 
 * @param uri A websocket URI, either WS or WSS protocol.
 * @return A node instance for the service
 */
std::shared_ptr<ftl::protocol::Service> setServiceProvider(const std::string &uri);

/**
 * @brief Connect to another machine. This uses the singleton Self instance, however,
 * it is possible to also connect from another secondary Self instance by
 * using a member function.
 * 
 * @param uri A TCP URI with the address and port of another machine.
 * @return std::shared_ptr<ftl::protocol::Node> 
 */
std::shared_ptr<ftl::protocol::Node> connectNode(const std::string &uri);

/**
 * @brief Host a new stream. The URI must be either a file or an FTL protocol.
 * A file stream opened by this function will be write only, and a network
 * stream will broadcast itself as a newly available source.
 * 
 * @param uri Either file:// or ftl://
 * @return std::shared_ptr<ftl::protocol::Stream> 
 */
std::shared_ptr<ftl::protocol::Stream> createStream(const std::string &uri);

/**
 * @brief Open an existing stream. This can be a file or a network stream.
 * A file stream will be opened readonly, and a network stream will attempt
 * to find the stream on the local network or using the web service.
 * 
 * @param uri Either file:// or ftl://
 * @return std::shared_ptr<ftl::protocol::Stream> 
 */
std::shared_ptr<ftl::protocol::Stream> getStream(const std::string &uri);

/** Add certificate to whitelist. Used only if certificate validation is disabled */
void addCertificateToWhitelist(const std::string& signature);

/** Disable certificate validation. */
void disableCertificateValidation(bool enable=false);

}  // namespace ftl
