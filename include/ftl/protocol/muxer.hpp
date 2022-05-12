#pragma once

#include <ftl/protocol/streams.hpp>

#include <map>
#include <list>

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
	explicit Muxer();
	virtual ~Muxer();

	void add(const std::shared_ptr<Stream> &, int fsid=-1);
	void remove(const std::shared_ptr<Stream> &);

	//bool onPacket(const StreamCallback &) override;

	bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &) override;

	bool begin() override;
	bool end() override;
	bool active() override;

	void reset() override;

	bool enable(FrameID id) override;

	bool enable(FrameID id, ftl::protocol::Channel channel) override;

	bool enable(FrameID id, const ftl::protocol::ChannelSet &channels) override;

	void setProperty(ftl::protocol::StreamProperty opt, int value) override;

	int getProperty(ftl::protocol::StreamProperty opt) override;

	bool supportsProperty(ftl::protocol::StreamProperty opt) override;

	StreamType type() const override;

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

}
}
