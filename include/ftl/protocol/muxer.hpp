#pragma once

#include <ftl/protocol/streams.hpp>

#include <map>

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

	void add(Stream *, size_t fsid=0, const std::function<int()> &cb=nullptr);
	void remove(Stream *);

	//bool onPacket(const StreamCallback &) override;

	bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &) override;

	bool begin() override;
	bool end() override;
	bool active() override;

	void reset() override;

	ftl::protocol::Stream *originStream(size_t fsid, int fid);

	private:
	struct StreamEntry {
		Stream *stream;
		std::unordered_map<int, std::vector<int>> maps;
		uint32_t original_fsid = 0;
		ftl::Handle handle;
		std::vector<int> ids;
	};

	std::list<StreamEntry> streams_;
	std::vector<std::pair<StreamEntry*,int>> revmap_[kMaxStreams];
	//std::list<ftl::Handle> handles_;
	int nid_[kMaxStreams];
	//StreamCallback cb_;
	SHARED_MUTEX mutex_;

	void _notify(const ftl::protocol::StreamPacket &spkt, const ftl::protocol::Packet &pkt);
	int _lookup(size_t fsid, StreamEntry *se, int ssid, int count);
	void _forward(const std::string &name);
};

}
}
