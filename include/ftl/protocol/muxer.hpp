/**
 * @file muxer.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <map>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <string>
#include <ftl/protocol/streams.hpp>

namespace ftl {
namespace protocol {

static constexpr size_t kMaxStreams = 5;

/**
 * Combine multiple streams into a single stream. StreamPackets are modified
 * by mapping the stream identifiers consistently to new values. Both reading
 * and writing are supported but a write must be preceeded by a read for the
 * stream mapping to be registered.
 */
class Muxer : public Stream {
 public:
    Muxer();
    virtual ~Muxer();

    /**
     * @brief Add a child stream. If a frameset ID is given then all input framesets
     * are merged together into that single frameset as different frames. If no fsid
     * is given then all seen framesets from this stream are allocated to a new locally
     * unique frameset number.
     * 
     * @param stream the child stream object
     * @param fsid an optional fixed frameset for this stream
     */
    void add(const std::shared_ptr<Stream> &stream, int fsid = -1);

    /**
     * @brief Remove a child stream.
     * 
     * @param stream 
     */
    void remove(const std::shared_ptr<Stream> &stream);

    /**
     * @brief Send packets to the corresponding child stream. The packets streamID and frame
     * number are used by the muxer to work out which child stream should receive the data.
     * The numbers are mapped to the correct output values before being sent to the child.
     * 
     * @return true 
     * @return false if the packets could not be forwarded.
     */
    bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &) override;

    bool begin() override;
    bool end() override;
    bool active() override;
    bool active(FrameID id) override;

    void reset() override;

    /** enable(): frameset id 255 to apply for all available framesets, source id 255 for all available frames.
     *  If both 255: enable all frames on all framesets.
     */
    bool enable(FrameID id) override;
    bool enable(FrameID id, ftl::protocol::Channel channel) override;
    bool enable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

    /** fsid 255/sid 255 trick does not apply for disable() (TODO?) */
    void disable(FrameID id) override;
    void disable(FrameID id, ftl::protocol::Channel channel) override;
    void disable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

    void setProperty(ftl::protocol::StreamProperty opt, std::any value) override;

    std::any getProperty(ftl::protocol::StreamProperty opt) override;

    bool supportsProperty(ftl::protocol::StreamProperty opt) override;

    StreamType type() const override;

    /**
     * @brief Get the stream instance associated with an ID.
     * 
     * @return std::shared_ptr<Stream> 
     */
    std::shared_ptr<Stream> originStream(FrameID) const;

    /**
     * @brief Get a stream by URI.
     * 
     * If the stream does not exist in the muxer, then returns nullptr.
     * 
     * @param uri 
     * @return std::shared_ptr<Stream> 
     */
    std::shared_ptr<Stream> findStream(const std::string &uri) const;

    /**
     * @brief Find the local frame ID using only the URI.
     * 
     * If the URI does not contain a "set" and "frame" query component,
     * then 0 and 0 are assumed.
     * 
     * @param uri 
     * @return FrameID 
     */
    FrameID findLocal(const std::string &uri) const;

    /**
     * @brief Find the local frame ID given a URI and stream specific ID.
     * 
     * This function first attempts to find a stream entity using the URI.
     * 
     * @param uri 
     * @param remote 
     * @return FrameID 
     */
    FrameID findLocal(const std::string &uri, FrameID remote) const;

    /**
     * @brief Find the system local ID for a stream specific ID.
     * 
     * @param stream 
     * @param remote 
     * @return FrameID 
     */
    FrameID findLocal(const std::shared_ptr<Stream> &stream, FrameID remote) const;

    /**
     * @brief Find the system local ID for a stream specific ID, pre-allocate one if doesn't exist.
     * 
     * Remote framesets for given remote FrameID will have the returned local FrameID.
     * 
     * @param stream 
     * @param remote 
     * @return FrameID 
     */
    FrameID findOrCreateLocal(const std::shared_ptr<Stream>& stream, FrameID remote);

    /**
     * @brief Given a local frame ID, get the stream specific ID.
     * 
     * @see originStream
     * 
     * @param local 
     * @return FrameID 
     */
    FrameID findRemote(FrameID local) const;

    /**
     * @brief Obtain a list of all streams in the muxer.
     * 
     * @return std::list<std::shared_ptr<Stream>> 
     */
    std::list<std::shared_ptr<Stream>> streams() const;

 private:
    struct StreamEntry {
        std::shared_ptr<Stream> stream;
        ftl::Handle handle;
        ftl::Handle req_handle;
        ftl::Handle avail_handle;
        ftl::Handle err_handle;
        int id = 0;
        int fixed_fs = -1;
    };

    /** map between local and remote framsets, first 16 bits for stream id, second 16 bits for frameset */
    std::unordered_map<int, int> fsmap_;
    std::unordered_map<int, int> sourcecount_;

    /* map between stream specific ids to local ids
    *  packs stream id and frame id in uint64_t (first 32 bits for stream id, last for frameid) */
    std::unordered_map<int64_t, FrameID> imap_;

    /** map between local FrameID and remote (FrameID, StreamEntry) pair */
    std::unordered_map<FrameID, std::pair<FrameID, Muxer::StreamEntry*>> omap_;

    std::list<StreamEntry> streams_;
    mutable SHARED_MUTEX mutex_;
    std::atomic_int stream_ids_ = 0;
    std::atomic_int framesets_ = 0;

    /* On packet receive, map to local ID */
    FrameID _mapFromInput(StreamEntry *, FrameID id);

    FrameID _mapFromInput(const StreamEntry *, FrameID id) const;

    /* On posting, map to output ID */
    std::pair<FrameID, StreamEntry*> _mapToOutput(FrameID id) const;
};

}  // namespace protocol
}  // namespace ftl
