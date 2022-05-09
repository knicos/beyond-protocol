#include <ftl/protocol/muxer.hpp>
#include <ftl/lib/loguru.hpp>

using ftl::protocol::Muxer;
using ftl::protocol::Stream;
using ftl::protocol::StreamPacket;

Muxer::Muxer() : nid_{0} {
	//value("paused", false);
	//_forward("paused");
}

Muxer::~Muxer() {
	UNIQUE_LOCK(mutex_,lk);
	for (auto &se : streams_) {
		se.handle.cancel();
	}
}

void Muxer::_forward(const std::string &name) {
	/*on(name, [this,name]() {
		auto val = getConfig()[name];
		UNIQUE_LOCK(mutex_,lk);
		for (auto &se : streams_) {
			se.stream->set(name, val);
		}
	});*/
}


void Muxer::add(const std::shared_ptr<Stream> &s, size_t fsid, const std::function<int()> &cb) {
	UNIQUE_LOCK(mutex_,lk);
	if (fsid < 0u || fsid >= ftl::protocol::kMaxStreams) return;

	auto &se = streams_.emplace_back();
	//int i = streams_.size()-1;
	se.stream = s;
	se.ids.push_back(fsid);
	Muxer::StreamEntry *ptr = &se;

	se.handle = std::move(s->onPacket([this,s,ptr,cb](const StreamPacket &spkt, const Packet &pkt) {
		//TODO: Allow input streams to have other streamIDs
		// Same fsid means same streamIDs map together in the end

		/*ftl::stream::Muxer::StreamEntry *ptr = nullptr;
		{
			SHARED_LOCK(mutex_,lk);
			ptr = &streams_[i];
		}*/

		if (!cb && spkt.streamID > 0) {
			LOG(WARNING) << "Multiple framesets in stream";
			return true;
		}

		if (ptr->ids.size() <= spkt.streamID) {
			UNIQUE_LOCK(mutex_,lk);
			if (ptr->ids.size() <= spkt.streamID) {
				ptr->ids.resize(spkt.streamID + 1);
				ptr->ids[spkt.streamID] = cb();
			}
		}

		int fsid;
		{
			SHARED_LOCK(mutex_, lk);
			fsid = ptr->ids[spkt.streamID];
		}

		StreamPacket spkt2 = spkt;
		ptr->original_fsid = spkt.streamID;  // FIXME: Multiple originals needed
		spkt2.streamID = fsid;

		if (spkt2.frame_number < 255) {
			int id = _lookup(fsid, ptr, spkt.frame_number, pkt.frame_count);
			spkt2.frame_number = id;
		}

		_notify(spkt2, pkt);
		s->select(spkt.streamID, selected(fsid), true);
		return true;
	}));
}

void Muxer::remove(const std::shared_ptr<Stream> &s) {
	UNIQUE_LOCK(mutex_,lk);
	for (auto i = streams_.begin(); i != streams_.end(); ++i) {
		if (i->stream == s) {
			i->handle.cancel();
			auto *se = &(*i);

			for (size_t j=0; j<kMaxStreams; ++j) {
				for (auto &k : revmap_[j]) {
					if (k.first == se) {
						k.first = nullptr;
					}
				}
			}

			streams_.erase(i);
			return;
		}
	}
}

std::shared_ptr<Stream> Muxer::originStream(size_t fsid, int fid) {
	if (fsid < ftl::protocol::kMaxStreams && static_cast<uint32_t>(fid) < revmap_[fsid].size()) {
		return std::get<0>(revmap_[fsid][fid])->stream;
	}
	return nullptr;
}

bool Muxer::post(const StreamPacket &spkt, const Packet &pkt) {
	SHARED_LOCK(mutex_, lk);
	if (pkt.data.size() > 0 || !(spkt.flags & ftl::protocol::kFlagRequest)) available(spkt.frameSetID()) += spkt.channel;

	if (spkt.streamID < ftl::protocol::kMaxStreams && spkt.frame_number < revmap_[spkt.streamID].size()) {
		auto [se, ssid] = revmap_[spkt.streamID][spkt.frame_number];
		//auto &se = streams_[sid];

		if (!se) return false;

		//LOG(INFO) << "POST " << spkt.frame_number;

		StreamPacket spkt2 = spkt;
		spkt2.streamID = se->original_fsid;  // FIXME: Multiple possible originals
		spkt2.frame_number = ssid;
		se->stream->select(spkt2.streamID, selected(spkt.frameSetID()));
		return se->stream->post(spkt2, pkt);
	} else {
		return false;
	}
}

bool Muxer::begin() {
	bool r = true;
	for (auto &s : streams_) {
		r = r && s.stream->begin();
	}
	return r;
}

bool Muxer::end() {
	bool r = true;
	for (auto &s : streams_) {
		r = r && s.stream->end();
	}
	return r;
}

bool Muxer::active() {
	bool r = true;
	for (auto &s : streams_) {
		r = r && s.stream->active();
	}
	return r;
}

void Muxer::reset() {
	for (auto &s : streams_) {
		s.stream->reset();
	}
}

int Muxer::_lookup(size_t fsid, Muxer::StreamEntry *se, int ssid, int count) {
	SHARED_LOCK(mutex_, lk);
	
	auto i = se->maps.find(fsid);
	if (i == se->maps.end()) {
		lk.unlock();
		{
			UNIQUE_LOCK(mutex_, lk2);
			if (se->maps.count(fsid) == 0) {
				se->maps[fsid] = {};
			}
			i = se->maps.find(fsid);
		}
		lk.lock();
	}

	auto &map = i->second;

	if (static_cast<uint32_t>(ssid) >= map.size()) {
		lk.unlock();
		{
			UNIQUE_LOCK(mutex_, lk2);
			while (static_cast<uint32_t>(ssid) >= map.size()) {
				int nid = nid_[fsid]++;
				revmap_[fsid].push_back({se, static_cast<uint32_t>(map.size())});
				map.push_back(nid);
				for (int j=1; j<count; ++j) {
					int nid2 = nid_[fsid]++;
					revmap_[fsid].push_back({se, static_cast<uint32_t>(map.size())});
					map.push_back(nid2);
				}
			}
		}
		lk.lock();
	}
	return map[ssid];
}

void Muxer::_notify(const StreamPacket &spkt, const Packet &pkt) {
	SHARED_LOCK(mutex_, lk);
	available(spkt.frameSetID()) += spkt.channel;

	try {
		cb_.trigger(spkt, pkt);  // spkt.frame_number < 255 &&
	} catch (std::exception &e) {
		LOG(ERROR) << "Exception in packet handler (" << int(spkt.channel) << "): " << e.what();
		//reset();  // Force stream reset here to get new i-frames
	}
}
