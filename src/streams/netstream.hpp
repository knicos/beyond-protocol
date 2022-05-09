#pragma once

#include "../universe.hpp"
#include <ftl/threads.hpp>
#include <ftl/protocol/packet.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/handle.hpp>
#include <string>

namespace ftl {
namespace protocol {

class AdaptiveBitrate;

namespace detail {
struct StreamClient {
	ftl::UUID peerid;
	std::atomic<int> txcount;			// Frames sent since last request
	int txmax;							// Frames to send in request
	std::atomic<uint32_t> channels;		// A channel mask, those that have been requested
	uint8_t quality;
};
}

/**
 * The maximum number of frames a client can request in a single request.
 */
static const int kMaxFrames = 100;

struct NetStats {
	float rxRate;
	float txRate;
};

/**
 * Send and receive packets over a network. This class manages the connection
 * of clients or the discovery of a stream and deals with bitrate adaptations.
 * Each packet post is forwarded to each connected client that is still active.
 */
class Net : public Stream {
	public:
	Net(const std::string &uri, ftl::net::Universe *net, bool host=false);
	~Net();

	bool post(const ftl::protocol::StreamPacket &, const ftl::protocol::Packet &) override;

	bool begin() override;
	bool end() override;
	bool active() override;

	bool enable(uint8_t fs, uint8_t f) override;

	void reset() override;

	inline const ftl::UUID &getPeer() const {
		if (host_) { throw FTL_Error("Net::getPeer() not possible, hosting stream"); }
		if (!peer_){ throw FTL_Error("steram::Net has no valid Peer. Not found earlier?"); }
		return *peer_;
	}

	inline ftl::Handle onClientConnect(const std::function<bool(ftl::net::Peer*)> &cb) { return connect_cb_.on(cb); }

	/**
	 * Return the average bitrate of all streams since the last call to this
	 * function. Units are Mbps.
	 */
	static NetStats getStatistics();

private:
	SHARED_MUTEX mutex_;
	bool active_;
	ftl::net::Universe *net_;
	int64_t clock_adjust_;
	ftl::UUID time_peer_;
	std::optional<ftl::UUID> peer_;
	int64_t last_frame_;
	int64_t last_ping_;
	std::string uri_;
	std::string base_uri_;
	const bool host_;
	int tally_;
	std::array<std::atomic<int>,32> reqtally_ = {0};
	ftl::protocol::ChannelSet last_selected_;
	uint8_t bitrate_=255;
	std::atomic_int64_t bytes_received_ = 0;
	int64_t last_completion_ = 0;
	int64_t time_at_last_ = 0;
	float required_bps_;
	float actual_bps_;
	bool abr_enabled_;
	bool paused_ = false;

	AdaptiveBitrate *abr_;

	ftl::Handler<ftl::net::Peer*> connect_cb_;

	uint32_t local_fsid_ = 0;

	static std::atomic_size_t req_bitrate__;
	static std::atomic_size_t tx_bitrate__;
	static std::atomic_size_t rx_sample_count__;
	static std::atomic_size_t tx_sample_count__;
	static int64_t last_msg__;
	static MUTEX msg_mtx__;

	std::list<detail::StreamClient> clients_;

	bool _processRequest(ftl::net::Peer &p, ftl::protocol::StreamPacket &spkt, const ftl::protocol::Packet &pkt);
	void _checkRXRate(size_t rx_size, int64_t rx_latency, int64_t ts);
	void _checkTXRate(size_t tx_size, int64_t tx_latency, int64_t ts);
	bool _sendRequest(ftl::protocol::Channel c, uint8_t frameset, uint8_t frames, uint8_t count, uint8_t bitrate, bool doreset=false);
	void _cleanUp();
	uint32_t _localToRemoteFS(uint32_t fsid);
	uint32_t _remoteToLocalFS(uint32_t fsid);
};

}
}
