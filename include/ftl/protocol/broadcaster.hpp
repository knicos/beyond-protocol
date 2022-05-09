#pragma once

#include <ftl/protocol/streams.hpp>
#include <list>

namespace ftl {
namespace protocol {

/**
 * Forward all data to all child streams. Unlike the muxer which remaps the
 * stream identifiers in the stream packet, this does not alter the stream
 * packets.
 */
class Broadcast : public Stream {
	public:
	explicit Broadcast();
	virtual ~Broadcast();

	void add(const std::shared_ptr<Stream> &);
	void remove(const std::shared_ptr<Stream> &);
	void clear();

	bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &) override;

	bool begin() override;
	bool end() override;
	bool active() override;

	void reset() override;

	const std::list<std::shared_ptr<Stream>> &streams() const { return streams_; }

	private:
	std::list<std::shared_ptr<Stream>> streams_;
	std::list<ftl::Handle> handles_;
	//StreamCallback cb_;
	SHARED_MUTEX mutex_;
};

}
}
