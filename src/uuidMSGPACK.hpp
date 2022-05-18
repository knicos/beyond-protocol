/**
 * @file uuidMSGPACK.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <ftl/uuid.hpp>
#include <msgpack.hpp>

namespace ftl {

class UUIDMSGPACK : public ftl::UUID {
 public:
    UUIDMSGPACK() : ftl::UUID() {}
    explicit UUIDMSGPACK(const ftl::UUID &u) : ftl::UUID(u) {}
    MSGPACK_DEFINE(uuid_);
};

}
