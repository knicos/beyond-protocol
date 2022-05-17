/**
 * @file broadcaster.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <list>
#include <memory>
#include <ftl/protocol/streams.hpp>

namespace ftl {
namespace protocol {

/**
 * Forward all data to all child streams. Unlike the muxer which remaps the
 * stream identifiers in the stream packet, this does not alter the stream
 * packets.
 */
class Broadcast : public Stream {
 public:
    Broadcast();
    virtual ~Broadcast();

    void add(const std::shared_ptr<Stream> &);
    void remove(const std::shared_ptr<Stream> &);
    void clear();

    bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::DataPacket &) override;

    bool begin() override;
    bool end() override;
    bool active() override;

    void reset() override;

    void refresh() override;

    std::list<std::shared_ptr<Stream>> streams() const;

    void setProperty(ftl::protocol::StreamProperty opt, std::any value) override;

    std::any getProperty(ftl::protocol::StreamProperty opt) override;

    bool supportsProperty(ftl::protocol::StreamProperty opt) override;

    bool enable(FrameID id) override;

    bool enable(FrameID id, ftl::protocol::Channel channel) override;

    bool enable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

    void disable(FrameID id) override;

    void disable(FrameID id, ftl::protocol::Channel channel) override;

    void disable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

    StreamType type() const override;

 private:
    struct StreamEntry {
        std::shared_ptr<Stream> stream;
        ftl::Handle handle;
        ftl::Handle req_handle;
        ftl::Handle avail_handle;
    };

    std::list<StreamEntry> streams_;
};

}
}
