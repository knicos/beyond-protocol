#pragma once

#include <msgpack.hpp>
#include <ftl/codec/data.hpp>
#include <ftl/utility/vectorbuffer.hpp>

template <typename T>
void ftl::codec::pack(const T &v, std::vector<uint8_t> &out) {
    out.resize(0);
    ftl::util::FTLVectorBuffer buf(out);
    msgpack::pack(buf, v);
}

template <typename T>
T ftl::codec::unpack(const std::vector<uint8_t> &in) {
    auto unpacked = msgpack::unpack((const char*)in.data(), in.size());
    T t;
    unpacked.get().convert<T>(t);
    return t;
}
