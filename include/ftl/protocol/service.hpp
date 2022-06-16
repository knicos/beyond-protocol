/**
 * @file service.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <ftl/protocol/node.hpp>

namespace ftl {
namespace protocol {

class Service: public ftl::protocol::Node {
 public:
    explicit Service(const ftl::net::PeerPtr &impl);
    virtual ~Service();
};

}  // namespace protocol
}  // namespace ftl
