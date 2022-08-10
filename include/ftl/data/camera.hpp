#pragma once

namespace ftl {
namespace data {

/**
 * All properties associated with cameras. This structure is designed to
 * operate on CPU and GPU.
 */
struct Camera {
    float fx;               // Focal length X
    float fy;               // Focal length Y (usually same as fx)
    float cx;               // Principle point Y
    float cy;               // Principle point Y
    unsigned int width;     // Pixel width
    unsigned int height;    // Pixel height
    float minDepth;         // Near clip in meters
    float maxDepth;         // Far clip in meters
    float baseline;         // For stereo pair
    float doffs;            // Disparity offset
};

}  // namespace data
}  // namespace ftl
