/**
 * @file streams.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <any>
#include <unordered_map>
#include <memory>
#include <ftl/handle.hpp>
#include <ftl/threads.hpp>
#include <ftl/protocol/channels.hpp>
#include <ftl/protocol/channelSet.hpp>
#include <ftl/protocol/packet.hpp>
#include <ftl/protocol/frameid.hpp>
#include <ftl/protocol/error.hpp>

namespace ftl {
namespace protocol {
/** Represents a request for data through a stream */
struct Request {
    FrameID id;
    ftl::protocol::Channel channel;
    int bitrate;
    int count;
    ftl::protocol::Codec codec;
};

using RequestCallback = std::function<bool(const ftl::protocol::Request&)>;

using StreamCallback = std::function<bool(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &)>;

/**
 * @brief Enumeration of possible stream properties. Not all properties are supported
 * by all stream types, but they allow additional control and data access.
 * 
 */
enum struct StreamProperty {
    kInvalid = 0,
    kLooping,
    kSpeed,
    kBitrate,
    kMaxBitrate,
    kAdaptiveBitrate,
    kObservers,
    kURI,
    kPaused,
    kBytesSent,
    kBytesReceived,
    kLatency,
    kFrameRate,
    kName,
    kDescription,
    kTags,
    kUser
};

/**
 * @brief A hint about the streams capabilities.
 * 
 */
enum struct StreamType {
    kMixed,     // Multiple types of stream
    kUnknown,
    kLive,      // Net stream
    kRecorded   // File stream
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
    virtual ~Stream() {}

    /**
     * @brief If available, get a human readable name for the stream.
     * 
     * @return std::string 
     */
    virtual std::string name() const;

    /**
     * @brief Register a callback to receive packets. The callback is called at the available
     * frame rate, where each frame may consist of multiple packets and therefore multiple
     * callbacks. Each callback can occur in a different thread, therefore all packets for
     * a frame could be triggered in parallel.
     * 
     * @param cb 
     * @return ftl::Handle 
     */
    ftl::Handle onPacket(const StreamCallback &cb) { return cb_.on(cb); }

    /**
     * @brief Register a callback for frame and channel requests. Remote machines can send
     * requests, at which point the data should be generated and sent properly.
     * 
     * @param cb 
     * @return ftl::Handle 
     */
    ftl::Handle onRequest(const std::function<bool(const Request &)> &cb) { return request_cb_.on(cb); }

    /**
     * @brief Send packets, either to file or over the network. Packets should follow
     * the overall protocol rules, detailed elsewhere.
     * 
     * @return true if sent
     * @return false if dropped
     */
    virtual bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &) = 0;

    // TODO(Nick): Add methods for: pause, paused, statistics

    /**
     * @brief Start the stream. Calls to the onPacket callback will only occur after
     * a call to this function (and before a call to end()).
     */
    virtual bool begin() = 0;

    /**
     * @brief Terminate the stream. This will stop callbacks and will close all resources.
     * 
     * @return true 
     * @return false 
     */
    virtual bool end() = 0;

    /**
     * Is the stream active? Generally true if begin() has been called, false
     * initially and after end(). However, it may go false for other reasons.
     * If false, no calls to onPacket will occur and posts will be ignored.
     */
    virtual bool active() = 0;

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

    /**
     * @brief Check if a channel for a frame is available.
     * 
     * @param id Frameset and frame number
     * @param channel 
     * @return true if there is channel data
     * @return false if the channel does not exist
     */
    bool available(FrameID id, ftl::protocol::Channel channel) const;

    /**
     * @brief Check if all channels in a set are available.
     * 
     * @param id Frameset and frame number
     * @param channels Set of channels to check
     * @return true if all channels exist
     * @return false if one or more do not exist
     */
    bool available(FrameID id, const ftl::protocol::ChannelSet channels) const;

    /**
     * @brief Register a callback for when new frames and channels become available.
     * 
     * @param cb 
     * @return ftl::Handle 
     */
    ftl::Handle onAvailable(const std::function<bool(FrameID, ftl::protocol::Channel)> &cb) { return avail_cb_.on(cb); }

    /**
     * @brief Get all channels seen for a frame. If the frame does not exist then
     * an empty set is returned.
     * 
     * @param id Frameset and frame number
     * @return Set of all seen channels, or empty. 
     */
    ftl::protocol::ChannelSet channels(FrameID id) const;

    /**
     * @brief Obtain a set of all active channels for a frame.
     * 
     * @param id Frameset and frame number
     * @return ftl::protocol::ChannelSet 
     */
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

    /**
     * @brief Check if a specific channel is enabled for a frame.
     * 
     * @param id Frameset and frame number
     * @param channel 
     * @return true if the channel is active
     * @return false if the channel is not active or does not exist
     */
    bool enabled(FrameID id, ftl::protocol::Channel channel) const;

    /**
     * Number of framesets in stream.
     */
    inline size_t size() const { return state_.size(); }

    /**
     * @brief Activate a frame. This allows availability information to be gathered
     * for the frame which might not otherwise be available. However, data is likely
     * missing unless a channel is enabled.
     * 
     * @param id Frameset and frame number
     * @return true if the frame could be enabled
     * @return false if the frame could not be found or enabled
     */
    virtual bool enable(FrameID id);

    /**
     * @brief Request a specific channel in a frame. Once the request is made data
     * should become available if it exists. This will also enable the frame if
     * not already enabled.
     * 
     * @param id Frameset and frame number
     * @param channel 
     * @return true if the channel is available and enabled
     * @return false if the channel does not exist
     */
    virtual bool enable(FrameID id, ftl::protocol::Channel channel);

    /**
     * @brief Activate a set of channels for a frame. Requests for data for each
     * given channel are sent and the data should then become available.
     * 
     * @param id Frameset and frame number
     * @param channels a set of all channels to activate
     * @return true if all channels could be enabled
     * @return false if some channel could not be enabled
     */
    virtual bool enable(FrameID id, const ftl::protocol::ChannelSet &channels);

    /**
     * @brief Disable an entire frame. If the frame is not available or is already
     * disabled then this method has no effect.
     * 
     * @param id 
     */
    virtual void disable(FrameID id);

    /**
     * @brief Disable a specific channel in a frame. If not available or already
     * disabled then this method has no effect.
     * 
     * @param id 
     * @param channel 
     */
    virtual void disable(FrameID id, ftl::protocol::Channel channel);

    /**
     * @brief Disable a set of channels in a frame. If not available or already
     * disabled then this method has no effect.
     * 
     * @param id 
     * @param channels 
     */
    virtual void disable(FrameID id, const ftl::protocol::ChannelSet &channels);

    /**
     * @brief Set a stream property to a new value. If the property is not supported,
     * is readonly or an invalid value type is given, then an exception is thrown.
     * Check if the property is supported first.
     * 
     * @param opt 
     * @param value 
     */
    virtual void setProperty(ftl::protocol::StreamProperty opt, std::any value) = 0;

    /**
     * @brief Get the value of a stream property. If the property is not supported then
     * an exception is thrown. The result is an `any` object that should be casted
     * correctly by the user.
     * 
     * @param opt 
     * @return std::any 
     */
    virtual std::any getProperty(ftl::protocol::StreamProperty opt) = 0;

    /**
     * @brief Check if a property is supported. No exceptions are thrown.
     * 
     * @param opt 
     * @return true if the property is at least readable
     * @return false if the property is unsupported
     */
    virtual bool supportsProperty(ftl::protocol::StreamProperty opt) = 0;

    /**
     * @brief Get the streams type hint.
     * 
     * @return StreamType 
     */
    virtual StreamType type() const { return StreamType::kUnknown; }

    /**
     * @brief Register a callback for asynchronous stream errors.
     * 
     * @param cb 
     * @return ftl::Handle 
     */
    ftl::Handle onError(const std::function<bool(ftl::protocol::Error, const std::string &)> &cb) {
        return error_cb_.on(cb);
    }

 protected:
    /** Dispatch packets to callbacks */
    void trigger(const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt);

    /** Mark the channel and frame as available */
    void seen(FrameID id, ftl::protocol::Channel channel);

    /** Dispatch a request */
    void request(const Request &req);

    /** Report a stream error */
    void error(ftl::protocol::Error, const std::string &str);

    mutable SHARED_MUTEX mtx_;

 private:
    struct FSState {
        bool enabled = false;
        ftl::protocol::ChannelSet selected;
        ftl::protocol::ChannelSet available;
        // TODO(Nick): Add a name and metadata
    };

    ftl::Handler<const ftl::protocol::StreamPacket&, const ftl::protocol::DataPacket&> cb_;
    ftl::Handler<const Request &> request_cb_;
    ftl::Handler<FrameID, ftl::protocol::Channel> avail_cb_;
    ftl::Handler<ftl::protocol::Error, const std::string&> error_cb_;
    std::unordered_map<int, FSState> state_;
};

using StreamPtr = std::shared_ptr<Stream>;

}  // namespace protocol
}  // namespace ftl
