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

    void reset() override;

    bool enable(FrameID id) override;

    bool enable(FrameID id, ftl::protocol::Channel channel) override;

    bool enable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

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

    std::unordered_map<int, int> fsmap_;
    std::unordered_map<int, int> sourcecount_;
    std::unordered_map<int64_t, FrameID> imap_;
    std::unordered_map<FrameID, std::pair<FrameID, Muxer::StreamEntry*>> omap_;
    std::list<StreamEntry> streams_;
    mutable SHARED_MUTEX mutex_;
    std::atomic_int stream_ids_ = 0;
    std::atomic_int framesets_ = 0;

    /* On packet receive, map to local ID */
    FrameID _mapFromInput(StreamEntry *, FrameID id);

    /* On posting, map to output ID */
    std::pair<FrameID, StreamEntry*> _mapToOutput(FrameID id) const;
};

}  // namespace protocol
}  // namespace ftl
