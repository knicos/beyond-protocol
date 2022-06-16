/**
 * @file service.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/service.hpp>

using ftl::protocol::Service;
using ftl::protocol::Node;

Service::Service(const ftl::net::PeerPtr &impl): Node(impl) {}

Service::~Service() {}
