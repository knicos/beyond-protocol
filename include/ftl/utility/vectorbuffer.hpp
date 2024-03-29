/**
 * @file vectorbuffer.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <vector>

namespace ftl {
namespace util {

/**
 * Used for msgpack encoding into an existing std::vector object.
 */
class FTLVectorBuffer {
 public:
    inline explicit FTLVectorBuffer(std::vector<unsigned char> &v) : vector_(v) {}

    inline void write(const char *data, std::size_t size) {
        vector_.insert(vector_.end(), (const unsigned char*)data, (const unsigned char*)data+size);
    }

 private:
    std::vector<unsigned char> &vector_;
};
}  // namespace util
}  // namespace ftl
