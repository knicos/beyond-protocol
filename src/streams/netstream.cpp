#include "netstream.hpp"
//#include "adaptive.hpp"
#include <ftl/time.hpp>
#include "packetMsgpack.hpp"

#define LOGURU_REPLACE_GLOG 1
#include <ftl/lib/loguru.hpp>

#ifndef WIN32
#include <unistd.h>
#include <limits.h>
#endif

using ftl::protocol::Net;
using ftl::protocol::NetStats;
using ftl::protocol::StreamPacket;
using ftl::protocol::PacketMSGPACK;
using ftl::protocol::StreamPacketMSGPACK;
using ftl::protocol::Packet;
using ftl::protocol::Channel;
using ftl::protocol::Codec;
using ftl::protocol::kAllFrames;
using ftl::protocol::kAllFramesets;
using std::string;
using std::optional;

static constexpr int kFramesToRequest = 30;

std::atomic_size_t Net::req_bitrate__ = 0;
std::atomic_size_t Net::tx_bitrate__ = 0;
std::atomic_size_t Net::rx_sample_count__ = 0;
std::atomic_size_t Net::tx_sample_count__ = 0;
int64_t Net::last_msg__ = 0;
MUTEX Net::msg_mtx__;

static std::list<std::string> net_streams;
static std::atomic_flag has_bindings = ATOMIC_FLAG_INIT;
static SHARED_MUTEX stream_mutex;

Net::Net(const std::string &uri, ftl::net::Universe *net, bool host) :
		active_(false), net_(net), clock_adjust_(0), last_ping_(0), uri_(uri), host_(host) {
	
	// First net stream needs to register these RPC handlers
	//if (!has_bindings.test_and_set()) {
		if (net_->isBound("find_stream")) net_->unbind("find_stream");
		net_->bind("find_stream", [net = net_](const std::string &uri) -> optional<ftl::UUID> {
			LOG(INFO) << "Request for stream: " << uri;

			ftl::URI u1(uri);
			std::string base = u1.getBaseURI();

			SHARED_LOCK(stream_mutex, lk);
			for (const auto &s : net_streams) {
				ftl::URI u2(s);
				// Don't compare query string components.
				if (base == u2.getBaseURI()) {
					return net->id();
				}
			}
			return {};
		});

		if (net_->isBound("list_streams")) net_->unbind("list_streams");
		net_->bind("list_streams", [this]() {
			SHARED_LOCK(stream_mutex, lk);
			return net_streams;
		});
	//}

	last_frame_ = 0;
	time_peer_ = ftl::UUID(0);

	//abr_ = new ftl::stream::AdaptiveBitrate(std::max(0, std::min(255, value("bitrate", 64))));

	bitrate_ = 200; //abr_->current();
	//abr_->setMaxRate(static_cast<uint8_t>(std::max(0, std::min(255, value("max_bitrate", 200)))));
	//on("bitrate", [this]() {
	//	abr_->setMaxRate(static_cast<uint8_t>(std::max(0, std::min(255, value("max_bitrate", 200)))));
	//});

	/*abr_enabled_ = value("abr_enabled", false);
	on("abr_enabled", [this]() {
		abr_enabled_ = value("abr_enabled", false);
		bitrate_ = (abr_enabled_) ?
			abr_->current() :
			static_cast<uint8_t>(std::max(0, std::min(255, value("bitrate", 64))));
		tally_ = 0;
	});*/

	/*value("paused", false);
	on("paused", [this]() {
		paused_ = value("paused", false);
		if (!paused_) {
			reset();
		}
	});*/
}

Net::~Net() {
	end();

	// FIXME: Wait to ensure no net callbacks are active.
	// Do something better than this
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	//delete abr_;
}

bool Net::post(const StreamPacket &spkt, const Packet &pkt) {
	if (!active_) return false;
	bool hasStale = false;

	// Lock to prevent clients being added / removed
	{
		SHARED_LOCK(mutex_,lk);
		available(spkt.frameSetID()) += spkt.channel;

		// Map the frameset ID from a local one to a remote one
		StreamPacketMSGPACK spkt_net = *((StreamPacketMSGPACK*)&spkt);
		spkt_net.streamID = _localToRemoteFS(spkt.streamID);

		PacketMSGPACK pkt_strip;
		pkt_strip.codec = pkt.codec;
		pkt_strip.bitrate = pkt.bitrate;
		pkt_strip.frame_count = pkt.frame_count;
		pkt_strip.flags = pkt.flags;

		if (host_) {
            LOG(INFO) << "Send to " << clients_.size() << " clients";
			auto c = clients_.begin();
			while (c != clients_.end()) {
				auto &client = *c;

				// Strip packet data if channel is not wanted by client
				const bool strip = int(spkt.channel) < 32 && pkt.data.size() > 0 && ((1 << int(spkt.channel)) & client.channels) == 0;

				try {
					short pre_transmit_latency = short(ftl::time::get_time() - spkt.localTimestamp);

					if (!net_->send(client.peerid,
							base_uri_,
							pre_transmit_latency,  // Time since timestamp for tx
							spkt_net,
							(strip) ? pkt_strip : *((PacketMSGPACK*)&pkt))) {

						// Send failed so mark as client stream completed
						client.txcount = client.txmax;
					} else {
						if (!strip && pkt.data.size() > 0) _checkTXRate(pkt.data.size(), 0, spkt.timestamp);

						// Count frame as completed only if last block and channel is 0
						// FIXME: This is unreliable, colour might not exist etc.
						if (spkt_net.streamID == 0 && spkt.frame_number == 0 && spkt.channel == Channel::kColour) ++client.txcount;
					}
				} catch(...) {
					client.txcount = client.txmax;
				}

				if (client.txcount >= client.txmax) {
					hasStale = true;
				}
				++c;
			}
		} else {
			try {
				short pre_transmit_latency = short(ftl::time::get_time() - spkt.localTimestamp);
				if (!net_->send(*peer_,
						base_uri_,
						pre_transmit_latency,  // Time since timestamp for tx
						spkt_net,
						*((PacketMSGPACK*)&pkt))) {

				}
				if (pkt.data.size() > 0) _checkTXRate(pkt.data.size(), 0, spkt.timestamp);
			} catch(...) {
				// TODO: Some disconnect error
			}
		}
	}

	if (hasStale) _cleanUp();

	return true;
}

uint32_t Net::_localToRemoteFS(uint32_t fsid) {
	if (fsid == 255) return 255;
	local_fsid_ = fsid;
	return 0;
}

uint32_t Net::_remoteToLocalFS(uint32_t fsid) {
	return local_fsid_; //(fsid == 255) ? 255 : local_fsid_;
}

bool Net::begin() {
	if (active_) return true;
	//if (!get<string>("uri")) return false;

	//uri_ = *get<string>("uri");

	ftl::URI u(uri_);
	if (!u.isValid() || !(u.getScheme() == ftl::URI::SCHEME_FTL)) return false;
	base_uri_ = u.getBaseURI();

	if (net_->isBound(base_uri_)) {
		LOG(ERROR) << "Stream already exists! - " << uri_;
		active_ = false;
		return false;
	}

	// Add the RPC handler for the URI
	net_->bind(base_uri_, [this](ftl::net::Peer &p, short ttimeoff, const StreamPacketMSGPACK &spkt_raw, const PacketMSGPACK &pkt) {
		int64_t now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()).time_since_epoch().count();

		if (!active_) return;
		if (paused_) return;

		StreamPacket spkt = spkt_raw;
		spkt.localTimestamp = now - int64_t(ttimeoff);
		spkt.hint_capability = 0;
		spkt.hint_source_total = 0;
		spkt.version = 4;
		spkt.hint_peerid = p.localID();
		// Map remote frameset ID to a local one.
		spkt.streamID = _remoteToLocalFS(spkt.streamID);

		// Manage recuring requests
		if (!host_ && last_frame_ != spkt.timestamp) {
			UNIQUE_LOCK(mutex_, lk);
			if (last_frame_ != spkt.timestamp) {
				int tf = spkt.timestamp - last_frame_;  // Milliseconds per frame
				int tc = now - last_completion_;		// Milliseconds since last frame completed
				last_completion_ = now;
				bytes_received_ = 0;
				last_frame_ = spkt.timestamp;

				lk.unlock();

				// Apply adaptive bitrate adjustment if needed
				/*if (abr_enabled_) {
					int new_bitrate = abr_->adjustment(tf, tc, pkt.bitrate);
					if (new_bitrate != bitrate_) {
						bitrate_ = new_bitrate;
						tally_ = 0;  // Force request send
					}
				}*/

				if (size() > spkt.frameSetID()) {
					auto sel = selected(spkt.frameSetID());

					// A change in channel selections, so send those requests now
					if (sel != last_selected_) {
						auto changed = sel - last_selected_;
						last_selected_ = sel;

						for (auto c : changed) {
							_sendRequest(c, spkt.frameSetID(), kAllFrames, kFramesToRequest, 255);
						}
					}
				}

				// Are we close to reaching the end of our frames request?
				if (tally_ <= 5) {
					// Yes, so send new requests
					for (size_t i = 0; i < size(); ++i) {
						const auto &sel = selected(i);
						
						for (auto c : sel) {
							_sendRequest(c, i, kAllFrames, kFramesToRequest, 255);
						}
					}
					tally_ = kFramesToRequest;
				} else {
					--tally_;
				}
			}
		}

		bytes_received_ += pkt.data.size();
		//time_at_last_ = now;

		// If hosting and no data then it is a request for data
		// Note: a non host can receive empty data, meaning data is available
		// but that you did not request it
		if (host_ && pkt.data.size() == 0 && (spkt.flags & ftl::protocol::kFlagRequest)) {
			_processRequest(p, spkt, pkt);
		} else {
			// FIXME: Allow availability to change...
			available(spkt.frameSetID()) += spkt.channel;
		}

		cb_.trigger(spkt, pkt);
		if (pkt.data.size() > 0) _checkRXRate(pkt.data.size(), now-(spkt.timestamp+ttimeoff), spkt.timestamp);
	});

	if (host_) {
		LOG(INFO) << "Hosting stream: " << uri_;

		// Alias the URI to the configurable if not already
		// Allows the URI to be used to get config data.
		/*if (ftl::config::find(uri_) == nullptr) {
			ftl::config::alias(uri_, this);
		}*/

		{
			// Add to list of available streams
			UNIQUE_LOCK(stream_mutex, lk);
			net_streams.push_back(uri_);
		}

		// Automatically set name if missing
		//if (!get<std::string>("name")) {
			char hostname[1024] = {0};
			#ifdef WIN32
			DWORD size = 1024;
			GetComputerName(hostname, &size);
			#else
			gethostname(hostname, 1024);
			#endif

			//set("name", std::string(hostname));
		//}

		net_->broadcast("add_stream", uri_);
		active_ = true;
		
	} else {
		
		tally_ = kFramesToRequest;
		active_ = true;
	}

	return true;
}

void Net::reset() {
	UNIQUE_LOCK(mutex_, lk);

	for (size_t i = 0; i < size(); ++i) {
		auto sel = selected(i);
		
		for (auto c : sel) {
			_sendRequest(c, i, kAllFrames, kFramesToRequest, 255, true);
		}
	}
	tally_ = kFramesToRequest;
}

bool Net::enable(uint8_t fs, uint8_t f) {
	if (host_) { return false; }

	// not hosting, try to find peer now
	// First find non-proxy version, then check for proxy version if no match
	auto p = net_->findOne<ftl::UUID>("find_stream", uri_);

	if (p) {
		peer_ = *p;
	} else {
		// use webservice (if connected)
		auto ws = net_->getWebService();
		if (ws) {
			peer_ = ws->id();
		} else {
			LOG(ERROR) << "Stream Peer not found";
			return false;
		}
	}
	
	// TODO: check return value
	net_->send(*peer_, "enable_stream", uri_, fs, 0);
	_sendRequest(Channel::kColour, fs, kAllFrames, kFramesToRequest, 255, true);

	return true;
}

bool Net::_sendRequest(Channel c, uint8_t frameset, uint8_t frames, uint8_t count, uint8_t bitrate, bool doreset) {
	if (!active_ || host_) return false;

	PacketMSGPACK pkt = {
		Codec::kAny,			// TODO: Allow specific codec requests
		0,
		count,
		bitrate_,
		0
	};

	uint8_t sflags = ftl::protocol::kFlagRequest;
	if (doreset) sflags |= ftl::protocol::kFlagReset;

	StreamPacketMSGPACK spkt = {
		5,
		ftl::time::get_time(),
		frameset,
		frames,
		c,
		sflags,
		0,
		0,
		0
	};

	net_->send(*peer_, base_uri_, (short)0, spkt, pkt);
	return true;
}

void Net::_cleanUp() {
	UNIQUE_LOCK(mutex_,lk);
	for (auto i=clients_.begin(); i!=clients_.end(); ++i) {
		auto &client = *i;
		if (client.txcount >= client.txmax) {
			if (client.peerid == time_peer_) {
				time_peer_ = ftl::UUID(0);
			}
			LOG(INFO) << "Remove peer: " << client.peerid.to_string();
			i = clients_.erase(i);
		}
	}
}

/* Packets for specific framesets, frames and channels are requested in
 * batches (max 255 unique frames by timestamp). Requests are in the form
 * of packets that match the request except the data component is empty.
 */
bool Net::_processRequest(ftl::net::Peer &p, StreamPacket &spkt, const Packet &pkt) {
	bool found = false;
    LOG(INFO) << "processing request";

	{
		SHARED_LOCK(mutex_,lk);
		// Does the client already exist
		for (auto &c : clients_) {
			if (c.peerid == p.id()) {
				// Yes, so reset internal request counters
				c.txcount = 0;
				c.txmax = static_cast<int>(pkt.frame_count);
				if (int(spkt.channel) < 32) c.channels |= 1 << int(spkt.channel);
				found = true;
				// break;
			}
		}
	}

	// No existing client, so add a new one.
	if (!found) {
		{
			UNIQUE_LOCK(mutex_,lk);

			auto &client = clients_.emplace_back();
			client.peerid = p.id();
			client.quality = 255;  // TODO: Use quality given in packet
			client.txcount = 0;
			client.txmax = static_cast<int>(pkt.frame_count);
			if (int(spkt.channel) < 32) client.channels |= 1 << int(spkt.channel);
		}

		spkt.hint_capability |= ftl::protocol::kStreamCap_NewConnection;

		try {
			connect_cb_.trigger(&p);
		} catch (const ftl::exception &e) {
			LOG(ERROR) << "Exception in stream connect callback: " << e.what();
		}
	}

    LOG(INFO) << "Request processed";

	return false;
}

void Net::_checkRXRate(size_t rx_size, int64_t rx_latency, int64_t ts) {
	req_bitrate__ += rx_size * 8;
	rx_sample_count__ += 1;
}

void Net::_checkTXRate(size_t tx_size, int64_t tx_latency, int64_t ts) {
	tx_bitrate__ += tx_size * 8;
	tx_sample_count__ += 1;
}

NetStats Net::getStatistics() {
	int64_t ts = ftl::time::get_time();
	UNIQUE_LOCK(msg_mtx__,lk);
	const float r = (float(req_bitrate__) / float(ts - last_msg__) * 1000.0f / 1048576.0f);
	const float t = (float(tx_bitrate__) / float(ts - last_msg__) * 1000.0f / 1048576.0f);
	last_msg__ = ts;
	req_bitrate__ = 0;
	tx_bitrate__ = 0;
	rx_sample_count__ = 0;
	tx_sample_count__ = 0;
	return {r, t};
}

bool Net::end() {
	if (!active_) return false;

	{
		UNIQUE_LOCK(stream_mutex, lk);
		auto i = std::find(net_streams.begin(), net_streams.end(), uri_);
		if (i != net_streams.end()) net_streams.erase(i);
	}

	active_ = false;
	net_->unbind(base_uri_);
	return true;
}

bool Net::active() {
	return active_;
}
