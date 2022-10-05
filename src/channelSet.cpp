/**
 * @file channelSet.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/channelSet.hpp>

using ftl::protocol::ChannelSet;

ChannelSet operator&(const ChannelSet &a, const ChannelSet &b) {
    ChannelSet result;
    for (auto &i : a) {
        if (b.find(i) != b.end()) result.insert(i);
    }
    return result;
}

ChannelSet operator|(const ChannelSet &a, const ChannelSet &b) {
    ChannelSet result;
    result.insert(a.begin(), a.end());
    result.insert(b.begin(), b.end());
    return result;
}

ChannelSet operator-(const ChannelSet &a, const ChannelSet &b) {
    ChannelSet result;
    for (auto &i : a) {
        if (b.find(i) == b.end()) result.insert(i);
    }
    return result;
}

bool operator!=(const ChannelSet &a, const ChannelSet &b) {
    if (a.size() != b.size()) return true;
    for (auto &i : a) {
        if (b.count(i) == 0) return true;
    }
    return false;
}
