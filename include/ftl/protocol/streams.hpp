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
#include <ftl/protocol/frameid.hpp>
#include <string>
#include <vector>
#include <unordered_set>

namespace ftl {
namespace protocol {

/* Represents a request for data through a stream */
struct Request {
	FrameID id;
	ftl::protocol::Channel channel;
	int bitrate;
	int count;
	ftl::protocol::Codec codec;
};

using RequestCallback = std::function<bool(const ftl::protocol::Request&)>;

using StreamCallback = std::function<bool(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &)>;

enum struct StreamProperty {
    kInvalid = 0,
    kLooping,
    kSpeed,
    kBitrate,
	kMaxBitrate,
    kAdaptiveBitrate,
	kObservers,
	kURI
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

	virtual std::string name() const;

	/**
	 * Obtain all packets for next frame. The provided callback function is
	 * called once for every packet. This function might continue to call the
	 * callback even after the read function returns, for example with a
	 * NetStream.
	 */
	ftl::Handle onPacket(const StreamCallback &cb) { return cb_.on(cb); }

	ftl::Handle onRequest(const std::function<bool(const Request &)> &cb) { return request_cb_.on(cb); }

	virtual bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &)=0;

    // TODO: Add methods for: pause, paused, statistics

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
	 * @brief Clear all state. This will remove all information about available
	 * and enabled frames or channels. You will then need to enable frames and
	 * channels again. If active the stream will remain active.
	 * 
	 */
	virtual void reset();

	/**
	 * @brief Re-request all channels and state. This will also cause video encoding
	 * to generate new I-frames as if a new connection is made. All persistent data
	 * channels would also become available. For file streams this would reset the
	 * stream to the beginning of the file.
	 * 
	 */
	virtual void refresh();

	/**
	 * @brief Check if a frame is available.
	 * 
	 * @param id Frameset and frame number
	 * @return true if data is available for the frame
	 * @return false if no data has been seen
	 */
	bool available(FrameID id) const;

	bool available(FrameID id, ftl::protocol::Channel channel) const;

	bool available(FrameID id, const ftl::protocol::ChannelSet channels) const;

	ftl::Handle onAvailable(const std::function<bool(FrameID, ftl::protocol::Channel)> &cb) { return avail_cb_.on(cb); }

	/**
	 * @brief Get all channels seen for a frame. If the frame does not exist then
	 * an empty set is returned.
	 * 
	 * @param id Frameset and frame number
	 * @return Set of all seen channels, or empty. 
	 */
	ftl::protocol::ChannelSet channels(FrameID id) const;

	ftl::protocol::ChannelSet enabledChannels(FrameID id) const;

	/**
	 * @brief Get all available frames in the stream.
	 * 
	 * @return Set of frame IDs
	 */
	std::unordered_set<FrameID> frames() const;

	/**
	 * @brief Get all enabled frames in the stream.
	 * 
	 * @return Set of frame IDs
	 */
	std::unordered_set<FrameID> enabled() const;

	/**
	 * @brief Check if a frame is enabled.
	 * 
	 * @param id Frameset and frame number
	 * @return true if data for this frame is enabled.
	 * @return false if data not enabled or frame does not exist.
	 */
	bool enabled(FrameID id) const;

	bool enabled(FrameID id, ftl::protocol::Channel channel) const;

	/**
	 * Number of framesets in stream.
	 */
	inline size_t size() const { return state_.size(); }

	virtual bool enable(FrameID id);

	virtual bool enable(FrameID id, ftl::protocol::Channel channel);

	virtual bool enable(FrameID id, const ftl::protocol::ChannelSet &channels);

	// TODO: Disable

	virtual void setProperty(ftl::protocol::StreamProperty opt, int value)=0;

	virtual int getProperty(ftl::protocol::StreamProperty opt)=0;

	virtual bool supportsProperty(ftl::protocol::StreamProperty opt)=0;

	virtual StreamType type() const { return StreamType::kUnknown; }

	protected:
	void trigger(const ftl::protocol::StreamPacket &spkt, const ftl::protocol::Packet &pkt);

	void seen(FrameID id, ftl::protocol::Channel channel);

	void request(const Request &req);

	mutable SHARED_MUTEX mtx_;

	private:
	struct FSState {
		bool enabled = false;
		ftl::protocol::ChannelSet selected;
		ftl::protocol::ChannelSet available;
		// TODO: Add a name and metadata
	};

	ftl::Handler<const ftl::protocol::StreamPacket&, const ftl::protocol::Packet&> cb_;
	ftl::Handler<const Request &> request_cb_;
	ftl::Handler<FrameID, ftl::protocol::Channel> avail_cb_;
	std::unordered_map<int, FSState> state_;
};

}
}
