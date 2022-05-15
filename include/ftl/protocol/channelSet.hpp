/**
 * @file channelSet.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <unordered_set>
#include <ftl/protocol/channels.hpp>

namespace ftl {
namespace protocol {
using ChannelSet = std::unordered_set<ftl::protocol::Channel>;
}
}

ftl::protocol::ChannelSet operator&(const ftl::protocol::ChannelSet &a, const ftl::protocol::ChannelSet &b);

ftl::protocol::ChannelSet operator-(const ftl::protocol::ChannelSet &a, const ftl::protocol::ChannelSet &b);

inline ftl::protocol::ChannelSet &operator+=(ftl::protocol::ChannelSet &t, ftl::protocol::Channel c) {
    t.insert(c);
    return t;
}

inline ftl::protocol::ChannelSet &operator-=(ftl::protocol::ChannelSet &t, ftl::protocol::Channel c) {
    t.erase(c);
    return t;
}

inline ftl::protocol::ChannelSet operator+(const ftl::protocol::ChannelSet &t, ftl::protocol::Channel c) {
    auto r = t;
    r.insert(c);
    return r;
}

inline ftl::protocol::ChannelSet operator+(ftl::protocol::Channel a, ftl::protocol::Channel b) {
    std::unordered_set<ftl::protocol::Channel> r;
    r.insert(a);
    r.insert(b);
    return r;
}

bool operator!=(const ftl::protocol::ChannelSet &a, const ftl::protocol::ChannelSet &b);
