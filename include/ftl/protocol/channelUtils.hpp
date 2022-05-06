#pragma once

#include <ftl/protocol/channels.hpp>

namespace ftl {
namespace protocol {

inline bool isVideo(Channel c) { return (int)c < 32; };
inline bool isAudio(Channel c) { return (int)c >= 32 && (int)c < 64; };
inline bool isData(Channel c) { return (int)c >= 64; };

/** Obtain a string name for channel. */
std::string name(Channel c);

/** Obtain OpenCV type for channel. */
int type(Channel c);

/** @deprecated */
inline bool isFloatChannel(ftl::codecs::Channel chan) {
	switch (chan) {
	case Channel::GroundTruth:
	case Channel::Depth		:
	//case Channel::Normals   :
	case Channel::Confidence:
	case Channel::Flow      :
	case Channel::Density:
	case Channel::Energy	: return true;
	default					: return false;
	}
}

}
}
