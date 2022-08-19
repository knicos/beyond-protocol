/**
 * @file rawmat.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <cstdint>

namespace ftl {
namespace codec {
namespace raw {

/**
 * @brief Uncompressed image data with an OpenCV type.
 * 
 */
template<typename T = uint8_t>
struct RawMat {
    uint16_t cols;
    uint16_t rows;
    uint32_t type;
    T data[];
};

}  // namespace raw
}  // namespace codec
}  // namespace ftl
