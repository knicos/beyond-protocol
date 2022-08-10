#pragma once

#include <vector>
#include <string>
#include <tuple>
#include <cstdint>

#include <ftl/data/camera.hpp>
#include <ftl/protocol/channels.hpp>

namespace ftl {
namespace data {

using Pose = std::vector<double>;
using StereoPose = std::tuple<Pose, Pose>;
using Intrinsics = std::tuple<ftl::data::Camera, int, int>;

}
namespace codec {

template <typename T>
void pack(const T &v, std::vector<uint8_t> &out);

template <typename T>
T unpack(const std::vector<uint8_t> &in);

}
}
