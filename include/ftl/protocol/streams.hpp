/**
 * @file streams.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <ftl/handle.hpp>
#include <ftl/threads.hpp>
#include <ftl/protocol/channels.hpp>
#include <ftl/protocol/channelSet.hpp>
#include <ftl/protocol/packet.hpp>
#include <string>
#include <vector>
#include <unordered_set>

namespace ftl {
namespace protocol {

/* Represents a request for data through a stream */
struct Request {
	uint32_t frameset;
	uint32_t frame;
	ftl::protocol::Channel channel;
	int bitrate;
	int count;
	ftl::protocol::Codec codec;
};

using RequestCallback = std::function<bool(const ftl::protocol::Request&)>;

using StreamCallback = std::function<bool(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &)>;

enum struct StreamOption {
    kInvalid = 0,
    kLooping,
    kSpeed,
    kBitrate,
    kAdaptiveBitrate
};

enum struct StreamType {
    kMixed,
    kUnknown,
    kLive,
    kRecorded
};

/**
 * Base stream class to be implemented. Provides encode and decode functionality
 * around a generic packet read and write mechanism. Some specialisations will
 * provide and automatically handle control signals.
 *
 * Streams are bidirectional, frames can be both received and written.
 */
class Stream {
	public:
	virtual ~Stream() {};

	/**
	 * Obtain all packets for next frame. The provided callback function is
	 * called once for every packet. This function might continue to call the
	 * callback even after the read function returns, for example with a
	 * NetStream.
	 */
	ftl::Handle onPacket(const StreamCallback &cb) { return cb_.on(cb); };

	virtual bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &)=0;

    // TODO: Add methods for: pause, paused, statistics, stream type, options

	/**
	 * Start the stream. Calls to the onPacket callback will only occur after
	 * a call to this function (and before a call to end()).
	 */
	virtual bool begin()=0;

	virtual bool end()=0;

	/**
	 * Is the stream active? Generally true if begin() has been called, false
	 * initially and after end(). However, it may go false for other reasons.
	 * If false, no calls to onPacket will occur and posts will be ignored.
	 */
	virtual bool active()=0;

	/**
	 * Perform a forced reset of the stream. This means something different
	 * depending on stream type, for example with a network stream it involves
	 * resending all stream requests as if a reconnection had occured.
	 */
	virtual void reset();

	/**
	 * Query available video channels for a frameset.
	 */
	const ftl::protocol::ChannelSet &available(int fs) const;

	/**
	 * Query selected channels for a frameset. Channels not selected may not
	 * be transmitted, received or decoded.
	 */
	ftl::protocol::ChannelSet selected(int fs) const;

	ftl::protocol::ChannelSet selectedNoExcept(int fs) const;

	/**
	 * Change the video channel selection for a frameset.
	 */
	void select(int fs, const ftl::protocol::ChannelSet &, bool make=false);

	/**
	 * Number of framesets in stream.
	 */
	inline size_t size() const { return state_.size(); }

	virtual bool enable(uint8_t fs, uint8_t f) { return true; }

	protected:
	ftl::Handler<const ftl::protocol::StreamPacket&, const ftl::protocol::Packet&> cb_;

	/**
	 * Allow modification of available channels. Calling this with an invalid
	 * fs number will create that frameset and increase the size.
	 */
	ftl::protocol::ChannelSet &available(int fs);

	private:
	struct FSState {
		ftl::protocol::ChannelSet selected;
		ftl::protocol::ChannelSet available;
	};

	std::vector<FSState> state_;
	mutable SHARED_MUTEX mtx_;
};

}
}
