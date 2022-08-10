#include <ftl/codec/msgpack.hpp>

using ftl::codec::pack;
using ftl::codec::unpack;

struct CameraMSGPACK : public ftl::data::Camera {
    MSGPACK_DEFINE(fx, fy, cx, cy, width, height, minDepth, maxDepth, baseline, doffs);
};

// Instantiations supported without the msgpack.hpp header
template void pack<int>(const int &v, std::vector<uint8_t> &out);
template void pack<float>(const float &v, std::vector<uint8_t> &out);
template void pack<std::string>(const std::string &v, std::vector<uint8_t> &out);
template void pack<double>(const double &v, std::vector<uint8_t> &out);
template void pack<std::vector<float>>(const std::vector<float> &v, std::vector<uint8_t> &out);
template void pack<ftl::data::Pose>(const ftl::data::Pose &v, std::vector<uint8_t> &out);
template void pack<std::vector<int>>(const std::vector<int> &v, std::vector<uint8_t> &out);
template void pack<std::vector<std::string>>(const std::vector<std::string> &v, std::vector<uint8_t> &out);
template void pack<ftl::data::StereoPose>(const ftl::data::StereoPose &v, std::vector<uint8_t> &out);
template <> void ftl::codec::pack(const ftl::data::Intrinsics &v, std::vector<uint8_t> &out) {
    std::tuple<CameraMSGPACK, int, int> data;
    reinterpret_cast<ftl::data::Camera&>(std::get<0>(data)) = std::get<0>(v);
    std::get<1>(data) = std::get<1>(v);
    std::get<2>(data) = std::get<2>(v);
    pack(data, out);
}
template void pack<ftl::data::Intrinsics>(const ftl::data::Intrinsics &v, std::vector<uint8_t> &out);

template int unpack<int>(const std::vector<uint8_t> &in);
template float unpack<float>(const std::vector<uint8_t> &in);
template std::string unpack<std::string>(const std::vector<uint8_t> &in);
template double unpack<double>(const std::vector<uint8_t> &in);
template std::vector<float> unpack<std::vector<float>>(const std::vector<uint8_t> &in);
template ftl::data::Pose unpack<ftl::data::Pose>(const std::vector<uint8_t> &in);
template std::vector<int> unpack<std::vector<int>>(const std::vector<uint8_t> &in);
template std::vector<std::string> unpack<std::vector<std::string>>(const std::vector<uint8_t> &in);
template ftl::data::StereoPose unpack<ftl::data::StereoPose>(const std::vector<uint8_t> &in);
template <> ftl::data::Intrinsics ftl::codec::unpack(const std::vector<uint8_t> &in) {
    auto data = unpack<std::tuple<CameraMSGPACK, int, int>>(in);
    return data;
}
template ftl::data::Intrinsics unpack<ftl::data::Intrinsics>(const std::vector<uint8_t> &in);
